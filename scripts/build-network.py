"""Build the authored online companion and pinned dependencies, without game data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "port/network"


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def checksum(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(entry, cache):
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / entry["sha256"]
    if path.is_file() and checksum(path) == entry["sha256"]:
        return path
    partial = path.with_suffix(".part")
    print("Downloading", entry["url"], flush=True)
    request = urllib.request.Request(entry["url"], headers={"User-Agent": "RedBaron-build/1"})
    with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
        shutil.copyfileobj(response, output)
    if checksum(partial) != entry["sha256"]:
        raise RuntimeError("Dependency checksum mismatch; downloaded file will not be used")
    partial.replace(path)
    return path


def unpack(archive, target, digest):
    stamp = target / ".redbaron-source"
    if stamp.is_file() and stamp.read_text() == digest:
        return
    if target.exists() and any(target.iterdir()):
        raise RuntimeError(f"Refusing to replace an existing source folder: {target}")
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            parts = Path(member.name).parts
            if len(parts) < 2:
                continue
            member.name = str(Path(*parts[1:]))
            source.extract(member, target, filter="data")
    stamp.write_text(digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=ROOT / "network-work")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--stage", type=Path, help="Copy the companion and its licenses beside the game")
    args = parser.parse_args()
    if not 1 <= args.parallel <= 64:
        parser.error("--parallel must be 1-64")
    work = args.work.resolve()
    dependencies = json.loads((SOURCE / "dependencies.json").read_text())
    archives = {name: fetch(entry, work / "cache") for name, entry in dependencies.items()}
    datachannel, mbedtls = work / "source/datachannel", work / "source/mbedtls"
    unpack(archives["datachannel"], datachannel, dependencies["datachannel"]["sha256"])
    unpack(archives["mbedtls"], mbedtls, dependencies["mbedtls"]["sha256"])
    for name in ("libjuice", "plog", "usrsctp"):
        unpack(archives[name], datachannel / "deps" / name, dependencies[name]["sha256"])
    includes = work / "source/json/nlohmann"
    includes.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archives["json"], includes / "json.hpp")
    shutil.copy2(SOURCE / "threading_alt.h", mbedtls / "include/threading_alt.h")
    for option in ("MBEDTLS_SSL_DTLS_SRTP", "MBEDTLS_THREADING_C", "MBEDTLS_THREADING_ALT"):
        run(sys.executable, mbedtls / "scripts/config.py", "-f",
            mbedtls / "include/mbedtls/mbedtls_config.h", "set", option)
    install = work / "sdk"
    run("cmake", "-S", mbedtls, "-B", work / "mbedtls-build", "-G", "Ninja",
        "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_BUILD_TYPE=Release",
        "-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF", "-DMBEDTLS_FATAL_WARNINGS=OFF",
        f"-DCMAKE_INSTALL_PREFIX={install.as_posix()}")
    run("cmake", "--build", work / "mbedtls-build", "--parallel", args.parallel)
    run("cmake", "--install", work / "mbedtls-build")
    shutil.copy2(SOURCE / "threading_alt.h", install / "include/threading_alt.h")
    run("cmake", "-S", SOURCE, "-B", work / "build", "-G", "Ninja",
        "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl", "-DCMAKE_BUILD_TYPE=Release",
        f"-DREDBARON_DATACHANNEL_SOURCE={datachannel.as_posix()}",
        f"-DREDBARON_JSON_INCLUDE={includes.parent.as_posix()}", f"-DCMAKE_PREFIX_PATH={install.as_posix()}")
    run("cmake", "--build", work / "build", "--target", "redbaron_net", "--parallel", args.parallel)
    licenses = work / "build/licenses"
    licenses.mkdir(exist_ok=True)
    for label, directory in (("libdatachannel", datachannel), ("MbedTLS", mbedtls),
                             *((name, datachannel / "deps" / name) for name in ("libjuice", "plog", "usrsctp"))):
        for source in directory.iterdir():
            if source.is_file() and source.name.upper().startswith(("LICENSE", "COPYING")):
                shutil.copy2(source, licenses / (label + "-" + source.name))
    shutil.copy2(archives["json_license"], licenses / "nlohmann-json-LICENSE.MIT")
    shutil.copy2(SOURCE / "dependencies.json", licenses / "dependencies.json")
    if args.stage:
        destination = args.stage.resolve()
        destination.mkdir(parents=True, exist_ok=True)
        for name in ("redbaron_net.exe", "datachannel.dll"):
            shutil.copy2(work / "build" / name, destination / name)
        shutil.copytree(licenses, destination / "network-licenses", dirs_exist_ok=True)
    print("Online companion ready:", work / "build/redbaron_net.exe")


if __name__ == "__main__":
    main()
