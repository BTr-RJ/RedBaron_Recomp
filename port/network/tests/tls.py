"""Verify Windows trust rejects an untrusted WSS certificate, without changing trust stores."""
import asyncio
import json
import logging
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile

from websockets.asyncio.server import serve


async def main(executable, openssl):
    logging.getLogger("websockets.server").setLevel(logging.CRITICAL)
    with tempfile.TemporaryDirectory(prefix="redbaron-tls-") as folder:
        root = Path(folder)
        subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-keyout", str(root / "key.pem"), "-out", str(root / "cert.pem"),
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
                       check=True, capture_output=True)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(root / "cert.pem", root / "key.pem")
        connections = []
        async def connected(socket):
            connections.append(socket)
            await socket.close()
        async with serve(connected, "127.0.0.1", 0, ssl=tls) as server:
            config = root / "session.json"
            config.write_text(json.dumps(dict(name="TLSCheck", namespace=139,
                server=f"wss://127.0.0.1:{server.sockets[0].getsockname()[1]}")))
            process = await asyncio.create_subprocess_exec(executable, "--config", str(config),
                stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            try:
                stdout, stderr = await asyncio.wait_for(process.communicate(), 20)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
                raise AssertionError("TLS validation did not finish")
            events = [json.loads(line) for line in stdout.splitlines()]
            assert not connections and not any(e["op"] == "welcome" for e in events), events
            assert any("12175" in e.get("reason", "") for e in events), (events, stderr)
            print("PASS: untrusted WSS certificate rejected by Windows (ERROR_WINHTTP_SECURE_FAILURE)")


if __name__ == "__main__":
    asyncio.run(main(str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve())))
