"""Socket-level admission, isolation, signaling and cleanup tests; no game data."""
import asyncio
import base64
import hashlib
import hmac
import json
import struct
import unittest

from websockets.asyncio.client import connect
from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

from rooms import Rooms, BUILD, PROTOCOL


class RoomTests(unittest.IsolatedAsyncioTestCase):
    SESSION = (b"RBLAN01\x02" + bytes(8) + struct.pack(">I", 0x584109B5) + bytes(60) +
               struct.pack(">6I", 8, 0, 0, 0, 0, 0)).hex()
    async def asyncSetUp(self):
        self.rooms = Rooms(turn_urls=["turn:relay.example:3478"], turn_secret="test-only-secret")
        self.server = await serve(self.rooms.handler, "127.0.0.1", 0,
                                  max_size=49152, close_timeout=.2, compression=None)
        self.url = f"ws://127.0.0.1:{self.server.sockets[0].getsockname()[1]}"
        self.clients = []

    async def asyncTearDown(self):
        await asyncio.gather(*(s.close() for s in self.clients))
        self.server.close()
        await self.server.wait_closed()
        self.assertEqual(self.rooms.connections, 0)
        self.assertEqual(self.rooms.rooms, {})

    async def message(self, socket, op):
        async with asyncio.timeout(3):
            while True:
                result = json.loads(await socket.recv())
                if result["op"] == op:
                    return result

    async def open(self, **options):
        socket = await connect(self.url, proxy=None)
        self.clients.append(socket)
        data = dict(op="create", protocol=PROTOCOL, build=BUILD, name="Test", players=1)
        data.update(options)
        await socket.send(json.dumps(data))
        async with asyncio.timeout(3):
            result = json.loads(await socket.recv())
        return socket, result

    async def test_eight_players_capacity_and_turn_credentials(self):
        host, welcome = await self.open()
        self.assertEqual(welcome["id"], 0)
        members = [host]
        for i in range(1, 8):
            client, joined = await self.open(op="join", room=welcome["room"], name=f"Player{i}")
            self.assertEqual(joined["id"], i)
            self.assertEqual(len(joined["members"]), i + 1)
            members.append(client)
        _, rejected = await self.open(op="join", room=welcome["room"])
        self.assertEqual(rejected, {"op": "error", "message": "Room is full"})
        credentials = welcome["ice"][0]
        expected = base64.b64encode(hmac.new(b"test-only-secret", credentials["username"].encode(),
                                           hashlib.sha1).digest()).decode()
        self.assertEqual(credentials["credential"], expected)
        self.assertNotIn("test-only-secret", json.dumps(welcome))

    async def test_coop_and_local_slots(self):
        _, welcome = await self.open(mode="coop", players=2)
        self.assertEqual(welcome["capacity"], 2)
        _, result = await self.open(op="join", room=welcome["room"])
        self.assertEqual(result["message"], "Room is full")
        _, rejected = await self.open(mode="coop", players=3)
        self.assertEqual(rejected["op"], "error")
        _, other = await self.open(players=4)
        _, joined = await self.open(op="join", room=other["room"], players=4)
        self.assertEqual(joined["op"], "welcome")
        _, rejected = await self.open(op="join", room=other["room"])
        self.assertEqual(rejected["op"], "error")

    async def test_signaling_sender_is_authenticated_by_membership(self):
        host, welcome = await self.open()
        client, joined = await self.open(op="join", room=welcome["room"])
        await host.send(json.dumps({"op": "signal", "to": joined["id"], "from": 500,
                                   "kind": "offer", "data": "SDP"}))
        self.assertEqual(await self.message(client, "signal"),
                         {"op": "signal", "from": 0, "kind": "offer", "data": "SDP"})
        await client.send(json.dumps({"op": "signal", "to": 0, "from": 0,
                                     "kind": "answer", "data": "ANSWER"}))
        self.assertEqual((await self.message(host, "signal"))["from"], joined["id"])
        await client.send(json.dumps({"op": "lock", "locked": True}))
        self.assertEqual((await self.message(client, "error"))["message"],
                         "Only the host can close room admission")

    async def test_lock_and_disconnect_cleanup(self):
        host, welcome = await self.open()
        client, joined = await self.open(op="join", room=welcome["room"])
        await host.send(json.dumps({"op": "lock", "locked": True}))
        await self.message(host, "locked")
        _, rejected = await self.open(op="join", room=welcome["room"])
        self.assertIn("closed", rejected["message"])
        await host.close()
        async with asyncio.timeout(3):
            try:
                while True:
                    await client.recv()
            except ConnectionClosed:
                pass
        self.assertNotIn(welcome["room"], self.rooms.rooms)

    async def test_no_peer_id_reuse_or_cross_room_signaling(self):
        host, welcome = await self.open()
        client, joined = await self.open(op="join", room=welcome["room"])
        await client.close()
        self.assertEqual((await self.message(host, "left"))["id"], joined["id"])
        _, next_joined = await self.open(op="join", room=welcome["room"])
        self.assertGreater(next_joined["id"], joined["id"])
        isolated, _ = await self.open()
        await isolated.send(json.dumps({"op": "signal", "to": next_joined["id"],
                                        "kind": "offer", "data": "SDP"}))
        self.assertEqual((await self.message(isolated, "error"))["message"], "Unknown peer")

    async def test_protocol_name_and_shape_validation(self):
        for kwargs in ({"protocol": 5}, {"protocol": True}, {"build": "unknown"}, {"name": "A" * 16},
                       {"name": "<script>"}, {"players": True}, {"mode": "bad"}):
            _, rejected = await self.open(**kwargs)
            self.assertEqual(rejected["op"], "error")
        socket = await connect(self.url, proxy=None)
        self.clients.append(socket)
        await socket.send("[]")
        self.assertEqual((await self.message(socket, "error"))["message"], "Expected a JSON object")

    async def test_host_removes_failed_peer_without_closing_room(self):
        host, welcome = await self.open()
        client, joined = await self.open(op="join", room=welcome["room"])
        await host.send(json.dumps({"op": "kick", "id": joined["id"]}))
        self.assertEqual((await self.message(host, "left"))["id"], joined["id"])
        with self.assertRaises(ConnectionClosed):
            await asyncio.wait_for(client.recv(), 3)
        _, replacement = await self.open(op="join", room=welcome["room"])
        self.assertEqual(replacement["op"], "welcome")
        self.assertGreater(replacement["id"], joined["id"])

    async def directory(self, **options):
        socket = await connect(self.url, proxy=None)
        self.clients.append(socket)
        await socket.send(json.dumps(dict(op="list", protocol=PROTOCOL, build=BUILD, **options)))
        async with asyncio.timeout(3):
            return json.loads(await socket.recv())

    async def publish(self, socket, session=SESSION):
        await socket.send(json.dumps({"op": "publish_session", "session": session}))
        await socket.send(json.dumps({"op": "ping"}))
        await self.message(socket, "pong")

    async def test_directory_public_only_capacity_and_lock(self):
        private_host, private = await self.open(name="Private")
        public_host, public = await self.open(name="Public", public=True)
        coop_host, coop = await self.open(name="Coop", mode="coop", public=True)
        self.assertEqual((await self.directory())["rooms"], [])
        await self.publish(public_host)
        await self.publish(coop_host)
        listed = await self.directory()
        self.assertEqual({item["room"] for item in listed["rooms"]}, {public["room"], coop["room"]})
        self.assertNotIn(private["room"], json.dumps(listed))
        self.assertNotIn("credential", json.dumps(listed))
        self.assertEqual((await self.directory(mode="coop"))["rooms"][0]["host"], "Coop")
        client, joined = await self.open(op="join", room=coop["room"])
        self.assertEqual(joined["op"], "welcome")
        self.assertEqual((await self.directory(mode="coop"))["rooms"], [])
        await public_host.send(json.dumps({"op": "lock", "locked": True}))
        await self.message(public_host, "locked")
        self.assertEqual((await self.directory())["rooms"], [])
        await public_host.send(json.dumps({"op": "lock", "locked": False}))
        await self.message(public_host, "locked")
        await public_host.close()
        async with asyncio.timeout(3):
            while public["room"] in self.rooms.rooms:
                await asyncio.sleep(.01)
        self.assertEqual((await self.directory())["rooms"], [])

    async def test_directory_rejects_invalid_requests_and_pages(self):
        for options in ({"mode": "invalid"}, {"offset": True}, {"offset": -1}):
            self.assertEqual((await self.directory(**options))["op"], "error")
        for index in range(51):
            host, welcome = await self.open(name=f"Host{index}", public=True)
            self.assertEqual(welcome["op"], "welcome")
            await self.publish(host)
        first = await self.directory()
        self.assertEqual(len(first["rooms"]), 50)
        second = await self.directory(offset=first["next_offset"])
        self.assertEqual(len(second["rooms"]), 1)
        self.assertIsNone(second["next_offset"])
        self.assertFalse(set(item["room"] for item in first["rooms"]) &
                         set(item["room"] for item in second["rooms"]))

    async def test_visibility_must_be_boolean_and_defaults_private(self):
        _, invalid = await self.open(public="yes")
        self.assertEqual(invalid["op"], "error")
        _, private = await self.open()
        self.assertNotIn(private["room"], json.dumps(await self.directory()))

    async def test_session_publication_is_host_only_and_validated(self):
        host, welcome = await self.open(public=True)
        client, _ = await self.open(op="join", room=welcome["room"])
        await client.send(json.dumps({"op": "publish_session", "session": self.SESSION}))
        self.assertIn("Only a public room host", (await self.message(client, "error"))["message"])
        await self.publish(host)
        self.assertEqual((await self.directory())["rooms"][0]["session"], self.SESSION)
        await self.publish(host, None)
        self.assertEqual((await self.directory())["rooms"], [])
        await host.send(json.dumps({"op": "publish_session", "session": "00"}))
        self.assertEqual((await self.message(host, "error"))["message"], "Invalid session record")


if __name__ == "__main__":
    unittest.main()
