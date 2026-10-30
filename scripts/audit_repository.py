"""Read-only audit of tracked files and every reachable historical tree."""
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
PRIVATE_EXACT = {"agents.md", "product.md", "design.md", "docs/estado_atual.md"}


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    public_check = "--public" in sys.argv[1:]
    commits = git("rev-list", "--all").decode("ascii").splitlines()
    if not commits:
        print("ERROR: no commits to audit.")
        return 1
    violations = {}
    inspected = set()
    trees = [(commit[:12], git("ls-tree", "-r", "--name-only", "-z", commit)) for commit in commits]
    trees.append(("index", git("ls-files", "-z")))
    for label, listing in trees:
        for raw in listing.split(b"\0"):
            if not raw:
                continue
            path = raw.decode("utf-8", errors="surrogateescape")
            inspected.add(path)
            normalized = path.lower()
            if ((public_check and (normalized in PRIVATE_EXACT or normalized.startswith(".impeccable/"))) or normalized.startswith(PREFIXES) or normalized in EXACT or
                    PurePosixPath(path).suffix.lower() in SUFFIXES):
                violations.setdefault(path, label)
    if violations:
        for path, commit in sorted(violations.items()):
            print(f"BLOCKED {commit}: {path}")
        print(f"FAILED: {len(violations)} forbidden paths in history/index.")
        return 1
    print(f"OK: {len(commits)} commits, {len(inspected)} paths; no forbidden files.")
    print("Also review the provenance and content of new files before publishing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
