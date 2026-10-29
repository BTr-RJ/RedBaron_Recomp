"""Real WebRTC processes and native TCP/UDP sockets; no copyrighted game data."""
import asyncio
import json
from pathlib import Path
import socket
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "server"))
from rooms import Rooms
from websockets.asyncio.client import connect
from websockets.asyncio.server import serve


class Client:
    def __init__(self, process, index):
        self.process, self.index = process, index
        self.events, self.errors = [], []
        self.reader = asyncio.create_task(self.read())

    async def read(self):
        async for line in self.process.stdout:
            self.events.append(json.loads(line))

    async def wait(self, op, count=1, timeout=30):
        async with asyncio.timeout(timeout):
            while True:
                matches = [m for m in self.events if m["op"] == op]
                if len(matches) >= count:
                    return matches[-1]
                if self.process.returncode is not None:
                    raise RuntimeError(f"Client {self.index} exited: {self.events}")
                await asyncio.sleep(.02)

    async def stop(self):
        if self.process.returncode is None:
            try:
                self.process.stdin.write(b'{"op":"stop"}\n')
                await self.process.stdin.drain()
                await asyncio.wait_for(self.process.wait(), 15)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except asyncio.TimeoutError:
                self.process.kill()
                await self.process.wait()
                raise AssertionError(f"Client {self.index} did not shut down")
        await self.reader


async def main(executable):
    clients, listeners = [], []
    rooms = Rooms()
    with tempfile.TemporaryDirectory(prefix="redbaron-net-") as folder:
        async with serve(rooms.handler, "127.0.0.1", 0, compression=None, max_size=49152) as server:
            url = f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}"
            async def launch(index, room="", public=False):
                config = Path(folder) / f"client-{index}.json"
                config.write_text(json.dumps(dict(server=url, name=f"Test{index}",
                                                   namespace=140+index, room=room, public=public)))
                log = open(Path(folder) / f"client-{index}.log", "wb")
                process = await asyncio.create_subprocess_exec(
                    str(executable), "--config", str(config), stdin=asyncio.subprocess.PIPE,
                    stdout=asyncio.subprocess.PIPE, stderr=log)
                log.close()
                client = Client(process, index)
                clients.append(client)
                return client

            try:
                host = await launch(0, public=True)
                welcome = await host.wait("welcome")
                async with connect(url, proxy=None) as directory:
                    await directory.send(json.dumps(dict(op="list", protocol=1, build="redbaron-wire-1")))
                    listing = json.loads(await directory.recv())
                    assert [item["room"] for item in listing["rooms"]] == [welcome["room"]], listing
                print("PASS: public native host appears in room directory", flush=True)
                for i in range(1, 8):
                    client = await launch(i, welcome["room"])
                    await client.wait("welcome")
                    await client.wait("peer_ready")
                await host.wait("peer_ready", 7)
                print("PASS: eight native WebRTC processes admitted and encrypted channels open", flush=True)
                ninth = await launch(8, welcome["room"])
                rejected = await ninth.wait("error")
                assert rejected["message"] == "Room is full", rejected

                async def echo(reader, writer):
                    try:
                        while data := await reader.read(16384):
                            writer.write(data)
                            await writer.drain()
                        writer.write_eof()
                        await writer.drain()
                    finally:
                        writer.close()
                        await writer.wait_closed()

                listeners.append(await asyncio.start_server(echo, "127.140.0.0", 21000))
                listeners.append(await asyncio.start_server(echo, "127.144.0.4", 21000))
                async def tcp(source, destination, size):
                    reader, writer = await asyncio.open_connection(
                        f"127.{140+source}.0.{destination}", 21000,
                        local_addr=(f"127.{140+source}.0.{source}", 0))
                    payload = bytes((n * 17 + source) % 256 for n in range(size))
                    async def send():
                        for offset in range(0, size, 8192):
                            writer.write(payload[offset:offset+8192])
                            await writer.drain()
                        writer.write_eof()
                    sender = asyncio.create_task(send())
                    received = await asyncio.wait_for(reader.read(), 30)
                    await sender
                    assert received == payload, (source, destination, len(received), size)
                    writer.close()
                    await writer.wait_closed()
                await asyncio.gather(*(tcp(i, 0, 200000+i*1000) for i in range(1, 8)))
                await tcp(3, 4, 1024*1024)
                print("PASS: TCP streams, simultaneous clients, 1 MiB host relay and half-close", flush=True)

                loop = asyncio.get_running_loop()
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
                    receiver.bind(("127.140.0.0", 43821))
                    receiver.setblocking(False)
                    times = []
                    for i in range(1, 8):
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                            sender.bind((f"127.{140+i}.0.{i}", 0))
                            sender.setblocking(False)
                            payload = f"discovery-qos-{i}".encode()
                            started = time.perf_counter()
                            await loop.sock_sendto(sender, payload, (f"127.{140+i}.0.0", 43821))
                            data, remote = await asyncio.wait_for(loop.sock_recvfrom(receiver, 8192), 3)
                            assert data == payload and remote[0] == f"127.140.0.{i}", (data, remote)
                            await loop.sock_sendto(receiver, data, remote)
                            data, remote = await asyncio.wait_for(loop.sock_recvfrom(sender, 8192), 3)
                            assert data == payload and remote == (f"127.{140+i}.0.0", 43821)
                            times.append((time.perf_counter() - started) * 1000)
                print(f"PASS: unordered UDP and ephemeral-port replies; local RTT {min(times):.1f}-{max(times):.1f} ms", flush=True)
                await clients[7].stop()
                await host.wait("left")
                async with connect(url, proxy=None) as stalled:
                    await stalled.send(json.dumps(dict(op="join", protocol=1, build="redbaron-wire-1",
                                                       name="Stalled", room=welcome["room"])))
                    admission = json.loads(await stalled.recv())
                    assert admission["op"] == "welcome", admission
                    rejected = await host.wait("peer_error", timeout=40)
                    assert rejected["id"] == admission["id"], rejected
                    await host.wait("left", 2)
                    assert len(rooms.rooms[welcome["room"]].members) == 7
                    await tcp(1, 0, 10000)
                print("PASS: stalled ICE admission is removed without disturbing the match", flush=True)
                async with connect(url, proxy=None) as invalid:
                    await invalid.send(json.dumps(dict(op="join", protocol=1, build="redbaron-wire-1",
                                                       name="BadSignal", room=welcome["room"])))
                    admission = json.loads(await invalid.recv())
                    assert admission["op"] == "welcome", admission
                    await invalid.send(json.dumps(dict(op="signal", to=0, kind="answer", data="invalid SDP")))
                    rejected = await host.wait("peer_error", 2)
                    assert rejected["id"] == admission["id"], rejected
                    await host.wait("left", 3)
                await tcp(1, 0, 10000)
                print("PASS: malformed peer signaling is isolated; host and other clients keep playing", flush=True)
                await host.stop()
                for client in clients[1:8]:
                    await asyncio.wait_for(client.process.wait(), 15)
                print("PASS: host departure closes all seven client sessions", flush=True)
            finally:
                for listener in listeners:
                    listener.close()
                    await listener.wait_closed()
                outcomes = await asyncio.gather(*(c.stop() for c in clients), return_exceptions=True)
                for result in outcomes:
                    if isinstance(result, BaseException):
                        print("Cleanup error:", result, file=sys.stderr)
                for c in clients:
                    errors = [e for e in c.events if e["op"] == "error"]
                    if errors and c.index != 8:
                        print(f"Client {c.index} errors: {errors}", file=sys.stderr)
                if sys.exc_info()[0]:
                    for path in Path(folder).glob("*.log"):
                        print(path.name, path.read_text(errors="replace")[-3000:], file=sys.stderr)


if __name__ == "__main__":
    asyncio.run(main(Path(sys.argv[1]).resolve()))
