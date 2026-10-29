"""Generate private VPS configuration; never overwrite an existing deployment."""
import argparse
import ipaddress
from pathlib import Path
import re
import secrets


def prepare(domain, public_ip, output):
    if not re.fullmatch(r"(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]{2,63}", domain):
        raise ValueError("Provide a DNS hostname such as play.example.org")
    address = ipaddress.IPv4Address(public_ip)
    if not address.is_global:
        raise ValueError("The relay needs the VPS public IPv4 address")
    output = Path(output)
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    secret = secrets.token_hex(32)
    files = {
        "rooms.env": f"BIND_HOST=127.0.0.1\nPORT=8787\nSTUN_URLS=stun:{domain}:3478\n"
                     f"TURN_URLS=turn:{domain}:3478\nTURN_SECRET={secret}\n",
        "Caddyfile": f"{domain} {{\n  reverse_proxy 127.0.0.1:8787\n}}\n",
        "turnserver.conf": f"""listening-port=3478
fingerprint
use-auth-secret
static-auth-secret={secret}
realm={domain}
external-ip={address}
min-port=49160
max-port=49359
total-quota=256
user-quota=8
max-bps=2500000
no-cli
no-tls
no-dtls
no-multicast-peers
denied-peer-ip=0.0.0.0-0.255.255.255
denied-peer-ip=10.0.0.0-10.255.255.255
denied-peer-ip=100.64.0.0-100.127.255.255
denied-peer-ip=127.0.0.0-127.255.255.255
denied-peer-ip=169.254.0.0-169.254.255.255
denied-peer-ip=172.16.0.0-172.31.255.255
denied-peer-ip=192.168.0.0-192.168.255.255
denied-peer-ip=::1
denied-peer-ip=fc00::-fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff
denied-peer-ip=fe80::-febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff
log-file=stdout
simple-log
no-stdout-log
"""}
    for name, content in files.items():
        path = output / name
        path.write_text(content, encoding="utf-8")
        # Coturn's unprivileged container must be able to read its bind-mounted file.
        # The enclosing directory stays private to the deployment operator.
        path.chmod(0o644 if name != "rooms.env" else 0o600)
    return f"wss://{domain}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--domain", required=True)
    parser.add_argument("--public-ip", required=True)
    parser.add_argument("--output", default=str(Path(__file__).parent / "state"))
    args = parser.parse_args()
    print("Launcher server URL:", prepare(args.domain, args.public_ip, args.output))
