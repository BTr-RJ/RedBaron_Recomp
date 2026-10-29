"""RedBaron room admission and WebRTC signaling; never receives game packets."""
from __future__ import annotations

import asyncio
import base64
from dataclasses import dataclass, field
import hashlib
import hmac
import json
import logging
import os
import re
import secrets
import time

from websockets.asyncio.server import serve, ServerConnection
from websockets.exceptions import ConnectionClosed

PROTOCOL = 1
BUILD = "redbaron-wire-1"
LOG = logging.getLogger("redbaron.rooms")


class Rejected(Exception):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise Rejected(message)


def integer(value, low, high):
    return type(value) is int and low <= value <= high


@dataclass(eq=False)
class Member:
    socket: ServerConnection
    name: str
    players: int
    id: int
    outgoing: asyncio.Queue = field(default_factory=lambda: asyncio.Queue(128))

    def send(self, data):
        # A slow client cannot stall every other member of its room.
        try:
            self.outgoing.put_nowait(json.dumps(data, separators=(",", ":")))
        except asyncio.QueueFull:
            raise Rejected("Client is not receiving messages fast enough")

    def public(self):
        return {"id": self.id, "name": self.name, "players": self.players}


@dataclass
class Room:
    code: str
    mode: str
    public: bool = False
    members: dict[int, Member] = field(default_factory=dict)
    locked: bool = False
    # IDs aren't reused: delayed ICE messages cannot target a replacement member.
    next_id: int = 1

    @property
    def capacity(self):
        return 2 if self.mode == "coop" else 8


class Rooms:
    def __init__(self, *, turn_urls=(), turn_secret="", stun_urls=(), max_rooms=1000):
        self.rooms: dict[str, Room] = {}
        self.connections = 0
        self.max_rooms = max_rooms
        self.turn_urls, self.turn_secret, self.stun_urls = turn_urls, turn_secret, stun_urls
        if bool(turn_urls) != bool(turn_secret):
            raise ValueError("TURN_URLS and TURN_SECRET must be configured together")

    def ice(self, room, member):
        result = [{"urls": url} for url in self.stun_urls]
        if self.turn_urls:
            # Coturn REST credentials. The shared secret stays exclusively server-side.
            username = f"{int(time.time()) + 43200}:{room.code}:{member.id}"
            credential = base64.b64encode(hmac.new(
                self.turn_secret.encode(), username.encode(), hashlib.sha1).digest()).decode()
            result.extend({"urls": url, "username": username, "credential": credential}
                          for url in self.turn_urls)
        return result

    def directory(self, request):
        require(isinstance(request, dict), "Expected a JSON object")
        require(integer(request.get("protocol"), PROTOCOL, PROTOCOL) and request.get("build") == BUILD,
                "Incompatible client version. Update the game")
        mode = request.get("mode", "all")
        require(mode in ("all", "multiplayer", "coop"), "Invalid room mode")
        offset = request.get("offset", 0)
        require(integer(offset, 0, self.max_rooms), "Invalid directory offset")
        entries = []
        for room in sorted(self.rooms.values(), key=lambda item: item.code):
            players = sum(member.players for member in room.members.values())
            if (not room.public or room.locked or not room.members.get(0) or players >= room.capacity
                    or (mode != "all" and room.mode != mode)):
                continue
            entries.append({"room": room.code, "host": room.members[0].name,
                            "mode": room.mode, "players": players, "capacity": room.capacity})
        page = entries[offset:offset + 50]
        return {"op": "rooms", "rooms": page, "next_offset": offset + len(page)
                if offset + len(page) < len(entries) else None}

    def admit(self, socket, request):
        require(isinstance(request, dict), "Expected a JSON object")
        require(integer(request.get("protocol"), PROTOCOL, PROTOCOL) and request.get("build") == BUILD,
                "Incompatible client version. Update the launcher")
        name = request.get("name")
        require(isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9 _-]{1,15}", name)
                and name.strip() == name, "Player name must be 1-15 letters, digits, spaces, _ or -")
        players = request.get("players", 1)
        require(integer(players, 1, 4), "Invalid local player count")
        if request.get("op") == "create":
            require(len(self.rooms) < self.max_rooms, "Server is full. Try again later")
            mode = request.get("mode", "multiplayer")
            require(mode in ("multiplayer", "coop"), "Invalid room mode")
            require(players <= (2 if mode == "coop" else 8), "Co-op supports two players")
            public = request.get("public", False)
            require(type(public) is bool, "Invalid room visibility")
            code = secrets.token_hex(8).upper()
            while code in self.rooms:
                code = secrets.token_hex(8).upper()
            room = Room(code, mode, public=public)
            self.rooms[code] = room
            member_id = 0
        elif request.get("op") == "join":
            code = request.get("room", "")
            require(isinstance(code, str) and re.fullmatch(r"[0-9A-F]{16}", code), "Invalid room code")
            room = self.rooms.get(code)
            require(room is not None, "Room was not found or the host disconnected")
            require(not room.locked, "The host has closed this room to new players")
            require(sum(m.players for m in room.members.values()) + players <= room.capacity,
                    "Room is full")
            require(room.next_id <= 65535, "Create a new room to accept more connections")
            member_id = room.next_id
            room.next_id += 1
        else:
            raise Rejected("First message must create or join a room")
        member = Member(socket, name, players, member_id)
        room.members[member_id] = member
        member.send({"op": "welcome", "protocol": PROTOCOL, "build": BUILD,
                     "room": room.code, "id": member.id, "host": 0, "mode": room.mode,
                     "capacity": room.capacity, "members": [m.public() for m in room.members.values()],
                     "ice": self.ice(room, member)})
        for peer in room.members.values():
            if peer is not member:
                self.deliver(peer, {"op": "joined", "member": member.public()})
        return room, member

    def deliver(self, member, data):
        try:
            member.send(data)
        except Rejected:
            # Close asynchronously, without blocking the room's receive loop.
            asyncio.create_task(member.socket.close(1013, "Receive queue full"))

    def dispatch(self, room, member, data):
        require(room.members.get(member.id) is member, "Room membership ended")
        require(isinstance(data, dict), "Expected a JSON object")
        op = data.get("op")
        if op == "signal":
            target = data.get("to")
            require(integer(target, 0, 65535) and target != member.id,
                    "Unknown peer")
            if target not in room.members:
                # A candidate already in flight can arrive after the peer left.
                # IDs never repeat, so dropping it cannot affect a later joiner.
                require(target < room.next_id, "Unknown peer")
                return
            require(member.id == 0 or target == 0, "Only host-to-client signaling is allowed")
            kind, body = data.get("kind"), data.get("data")
            require(kind in ("offer", "answer", "candidate") and isinstance(body, str)
                    and 0 < len(body) <= 32768, "Invalid WebRTC signal")
            require(kind != "offer" or member.id == 0, "Only the host creates offers")
            require(kind != "answer" or target == 0, "Answers must target the host")
            # Never relay a client-supplied sender ID or arbitrary JSON fields.
            self.deliver(room.members[target], {"op": "signal", "from": member.id,
                         "kind": kind, "data": body})
        elif op == "kick":
            require(member.id == 0, "Only the host can remove a peer")
            target = data.get("id")
            require(integer(target, 1, 65535), "Invalid peer")
            peer = room.members.get(target)
            if peer:
                self.leave(room, peer)
                asyncio.create_task(peer.socket.close(1008, "Host ended this connection"))
        elif op == "lock":
            require(member.id == 0, "Only the host can close room admission")
            require(type(data.get("locked")) is bool, "Invalid admission state")
            room.locked = data["locked"]
            for peer in room.members.values():
                self.deliver(peer, {"op": "locked", "locked": room.locked})
        elif op == "ice":
            member.send({"op": "ice", "ice": self.ice(room, member)})
        elif op == "ping":
            member.send({"op": "pong"})
        else:
            raise Rejected("Unknown operation")

    def leave(self, room, member):
        if room.members.pop(member.id, None) is None:
            return
        if member.id == 0:
            self.rooms.pop(room.code, None)
            # No implicit host migration: the title's authoritative state is on the host.
            for peer in list(room.members.values()):
                self.deliver(peer, {"op": "closed", "reason": "Host disconnected"})
                asyncio.create_task(peer.socket.close(1001, "Host disconnected"))
        else:
            for peer in room.members.values():
                self.deliver(peer, {"op": "left", "id": member.id})

    async def handler(self, socket):
        room = member = writer = None
        self.connections += 1
        try:
            require(self.connections <= self.max_rooms * 8, "Server is full")
            raw = await asyncio.wait_for(socket.recv(), timeout=10)
            require(isinstance(raw, str), "Expected text JSON")
            request = json.loads(raw)
            if isinstance(request, dict) and request.get("op") == "list":
                await socket.send(json.dumps(self.directory(request), separators=(",", ":")))
                await socket.close(1000, "Directory complete")
                return
            room, member = self.admit(socket, request)

            async def send_messages():
                while True:
                    await socket.send(await member.outgoing.get())

            writer = asyncio.create_task(send_messages())
            # Each connection can burst ICE candidates, but cannot flood indefinitely.
            tokens, last = 120.0, time.monotonic()
            async for raw in socket:
                now = time.monotonic()
                tokens = min(120, tokens + (now - last) * 20) - 1
                last = now
                require(tokens >= 0, "Too many messages")
                require(isinstance(raw, str), "Expected text JSON")
                self.dispatch(room, member, json.loads(raw))
        except (Rejected, json.JSONDecodeError, UnicodeError, asyncio.TimeoutError) as error:
            try:
                await socket.send(json.dumps({"op": "error", "message": str(error) or "Handshake timed out"}))
                await socket.close(1008, "Request rejected")
            except ConnectionClosed:
                pass
        except ConnectionClosed:
            pass
        finally:
            self.connections -= 1
            if room and member:
                self.leave(room, member)
            if writer:
                writer.cancel()
                await asyncio.gather(writer, return_exceptions=True)


async def main():
    rooms = Rooms(turn_urls=os.getenv("TURN_URLS", "").split(),
                  turn_secret=os.getenv("TURN_SECRET", ""),
                  stun_urls=os.getenv("STUN_URLS", "").split())
    host, port = os.getenv("BIND_HOST", "127.0.0.1"), int(os.getenv("PORT", "8787"))
    async with serve(rooms.handler, host, port, max_size=49152, max_queue=16,
                     compression=None, ping_interval=20, ping_timeout=20,
                     close_timeout=3, server_header=None) as server:
        LOG.info("Room service listening on %s:%d", host, port)
        await server.serve_forever()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main())
