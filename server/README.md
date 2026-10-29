# RedBaron room service

WebSocket admission and WebRTC signaling for a host and up to seven
remote peers. Slots count players, including local guests; co-op has two slots.
Only the host creates offers and changes room admission. A host disconnect ends
the room. This service does not simulate the game or supply Xbox Live accounts.
The original game's simulation authority is still under investigation.

The service is prepared for future hosting. No public endpoint is operated by the
project yet. Local signaling tests are not evidence of Internet gameplay.

## Local development

Use Python 3.11 or newer in a virtual environment:

```sh
python -m pip install -r server/requirements.txt
python -m unittest discover -s server -v
python server/rooms.py
```

The development address is `ws://127.0.0.1:8787`. The launcher must use `wss://`
for public servers. Each room code is a private invitation; do not publish it while
a game is running. No passwords or game files are sent to the service.

## Hosting later

Use a Linux VPS with a public IPv4 address, a DNS hostname pointing to that address,
Docker Engine and Compose. In this directory:

```sh
python prepare_deploy.py --domain play.example.org --public-ip YOUR_PUBLIC_IPV4
docker compose up -d --build
```

The generator creates `state/` once and refuses to overwrite it. This directory
contains the TURN secret and is excluded from Git and the Docker build context.
Keep it private. Caddy obtains HTTPS certificates. The room service listens only
on the VPS loopback interface. Set the launcher's server address to the printed
`wss://` URL.

Allow inbound TCP 80/443 for HTTPS, UDP 3478 for STUN/TURN, and UDP 49160-49359
for relay allocations in both the provider and OS firewall. Coturn's authentication
uses time-limited HMAC credentials from the room service. The relay shared secret
is never distributed to clients. Restrict SSH separately. Servers behind provider
NAT also need Coturn's external/private address mapping configured for their host.

Direct ICE connections are preferred; TURN is the fallback for restrictive NAT or
CGNAT. UDP-blocking networks require further transport work; do not promise
connectivity on every network or zero latency. Relay traffic consumes VPS bandwidth.

Before opening service to the public, validate two independent Internet connections,
forced relay, eight game clients, host loss and reconnect. Container deployment and
public connectivity must be checked on the chosen VPS; no VPS is available yet.

References: [libdatachannel](https://github.com/paullouisageneau/libdatachannel),
[Coturn deployment](https://github.com/coturn/coturn/tree/master/docker/coturn),
[websockets server](https://websockets.readthedocs.io/en/15.0.1/reference/asyncio/server.html).
