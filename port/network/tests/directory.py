"""Exercise the native directory client against malformed server responses."""
import asyncio
import json
from pathlib import Path
import struct
import sys
import tempfile

from websockets.asyncio.server import serve


async def main(executable):
    session = (b'RBLAN01\x02' + bytes(8) + struct.pack('>I', 0x584109B5) + bytes(60) +
               struct.pack('>6I', 7, 0, 1, 0, 0, 0)).hex()
    room = dict(room='0123456789ABCDEF', host='Host', mode='multiplayer', players=1,
                capacity=8, session=session)
    cases = [
        (dict(op='rooms', rooms=[room], next_offset=None), True),
        (dict(op='rooms', rooms=[dict(room, session='00')], next_offset=None), False),
        (dict(op='rooms', rooms=[dict(room, session=session[:32]+'00000000'+session[40:])], next_offset=None), False),
        (dict(op='rooms', rooms=[dict(room, host='<script>')], next_offset=None), False),
        (dict(op='rooms', rooms=[dict(room, capacity=2)], next_offset=None), False),
        (dict(op='rooms', rooms=[room]*51, next_offset=None), False),
        (dict(op='rooms', rooms=[room], next_offset=0), False),
    ]
    with tempfile.TemporaryDirectory(prefix='redbaron-directory-') as folder:
        for index, (response, accepted) in enumerate(cases):
            async def handler(socket):
                await socket.recv()
                await socket.send(json.dumps(response))
            async with serve(handler, '127.0.0.1', 0, compression=None) as server:
                port = server.sockets[0].getsockname()[1]
                config = Path(folder) / f'{index}.json'
                config.write_text(json.dumps(dict(server=f'ws://127.0.0.1:{port}')))
                process = await asyncio.create_subprocess_exec(str(executable), '--list', str(config),
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
                stdout, stderr = await asyncio.wait_for(process.communicate(), 25)
                event = json.loads(stdout)
                assert (process.returncode == 0) == accepted, (index, event, stderr)
                assert event['op'] == ('rooms' if accepted else 'error'), (index, event)
    print('PASS: directory client rejects malformed game records and room metadata')


if __name__ == '__main__':
    asyncio.run(main(Path(sys.argv[1]).resolve()))
