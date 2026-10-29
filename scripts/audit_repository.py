"""Reject known game-derived paths in all reachable Git history (read-only)."""
from pathlib import Path, PurePosixPath
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = (
    "game/", "dlc/", "server/state/", "network-work/", "sdk_experimental/", "build_cpp_release/",
    "port/generated/default/", "port/metadata/",
)
EXACT = {"port/src/snoopyflyingace.ico", "port/src/snoopyflyingace.rc"}
SUFFIXES = {
    ".xex", ".xexp", ".xpr", ".pkg", ".bnk", ".wem", ".wmv",
    ".xwb", ".xsb", ".stfs", ".exe", ".dll", ".pdb", ".bak", ".bundle",
}


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    commits = git("rev-list", "--all").decode("ascii").splitlines()
    if not commits:
        print("ERRO: nenhum commit para auditar.")
        return 1
    violations = {}
    inspected = set()
    for commit in commits:
        for raw in git("ls-tree", "-r", "--name-only", "-z", commit).split(b"\0"):
            if not raw:
                continue
            path = raw.decode("utf-8", errors="surrogateescape")
            inspected.add(path)
            normalized = path.lower()
            if (normalized.startswith(PREFIXES) or normalized in EXACT or
                    PurePosixPath(path).suffix.lower() in SUFFIXES):
                violations.setdefault(path, commit[:12])
    if violations:
        for path, commit in sorted(violations.items()):
            print(f"BLOQUEADO {commit}: {path}")
        print(f"FALHOU: {len(violations)} caminhos proibidos no historico.")
        return 1
    print(f"OK: {len(commits)} commits, {len(inspected)} caminhos; nenhum arquivo proibido.")
    print("Revisar tambem a origem e o conteudo de novos arquivos antes de publicar.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
