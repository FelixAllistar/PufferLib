#!/usr/bin/env python3
"""Fetch and verify the project-local stock compiler and pinned oracle source."""
import hashlib
from pathlib import Path
import platform
import shutil
import stat
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = "2.0.35"
SHA256 = "63039d1a119f716767ac5a7d8fe0717cfacf219c6c253c35192148e0dade722f"
REVISION = "9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e"
LEAN_VERSION = "4.34.0"
LEAN_SHA256 = "5f14e0f3762058a529b1598f4355e6787c5f155be1bcce72ae288b9c35e3adb4"


def download_checked(archive, url, expected):
    if not archive.exists():
        partial = archive.with_suffix(archive.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as out:
            while chunk := response.read(1024 * 1024):
                out.write(chunk)
        partial.replace(archive)
    actual = subprocess.check_output(["sha256sum", str(archive)], text=True).split()[0]
    if actual != expected:
        raise SystemExit(f"Checksum mismatch for {archive}: {actual}")


def extract_lean(archive, base):
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            dest = base / entry.filename
            if not dest.resolve().is_relative_to(base.resolve()):
                raise SystemExit(f"Unsafe archive path: {entry.filename}")
            mode = entry.external_attr >> 16
            if entry.is_dir():
                dest.mkdir(parents=True, exist_ok=True)
            elif stat.S_ISLNK(mode):
                dest.parent.mkdir(parents=True, exist_ok=True)
                target = package.read(entry).decode()
                if not (dest.parent / target).resolve().is_relative_to(base.resolve()):
                    raise SystemExit(f"Unsafe archive link: {entry.filename}")
                dest.symlink_to(target)
            else:
                dest.parent.mkdir(parents=True, exist_ok=True)
                with package.open(entry) as src, dest.open("wb") as out:
                    shutil.copyfileobj(src, out, 1024 * 1024)
                if mode:
                    dest.chmod(stat.S_IMODE(mode))


def main():
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise SystemExit("The bootstrap currently pins Linux x86_64 toolchains.")
    base = ROOT / "build/pokemon_gen9/toolchain"
    base.mkdir(parents=True, exist_ok=True)
    archive = base / f"bend-{VERSION}-linux-x64.tar.gz"
    if not (base / "bend/bin/bend").is_file():
        download_checked(archive,
            f"https://github.com/bendlang/bend/releases/download/v{VERSION}/{archive.name}", SHA256)
        with tarfile.open(archive) as package:
            package.extractall(base, filter="data")
    installed = subprocess.check_output([str(base / "bend/bin/bend"), "version"], text=True)
    if installed.strip() != f"bend {VERSION}":
        raise SystemExit(f"Unexpected installed compiler: {installed.strip()}")
    lean = base / f"lean-{LEAN_VERSION}-linux"
    lean_archive = base / f"{lean.name}.zip"
    if not (lean / "bin/lean").is_file():
        download_checked(lean_archive,
            f"https://github.com/leanprover/lean4/releases/download/v{LEAN_VERSION}/{lean_archive.name}",
            LEAN_SHA256)
        extract_lean(lean_archive, base)
    lean_version = subprocess.check_output([str(lean / "bin/lean"), "--version"], text=True)
    if f"version {LEAN_VERSION}" not in lean_version:
        raise SystemExit(f"Unexpected installed Lean: {lean_version.strip()}")
    tools = ROOT / "build/pokemon_gen9/tools"
    tools.mkdir(parents=True, exist_ok=True)
    for name in ("package.json", "package-lock.json"):
        shutil.copyfile(ROOT / "ocean/pokemon_gen9/tools" / name, tools / name)
    lock_hash = hashlib.sha256((tools / "package-lock.json").read_bytes()).hexdigest()
    stamp = tools / ".installed-lock"
    ready = (stamp.is_file() and stamp.read_text().strip() == lock_hash and
        all((tools / "node_modules" / name / "package.json").is_file()
            for name in ("esbuild", "ts-chacha20")))
    if not ready:
        subprocess.run(["npm", "ci", "--no-audit", "--no-fund"], cwd=tools, check=True)
        stamp.write_text(lock_hash + "\n")
    source = ROOT / "build/pokemon_gen9/showdown"
    if not source.exists():
        subprocess.run(["git", "clone", "https://github.com/smogon/pokemon-showdown.git", str(source)], check=True)
        subprocess.run(["git", "-C", str(source), "checkout", "--detach", REVISION], check=True)
    subprocess.run(["python3", "ocean/pokemon_gen9/audit_source.py", "--check",
                    "ocean/pokemon_gen9/source-map.json"], cwd=ROOT, check=True)
    subprocess.run(["node", "ocean/pokemon_gen9/prepare_reference.cjs"], cwd=ROOT, check=True)
    subprocess.run(["node", "ocean/pokemon_gen9/export_catalog.cjs"], cwd=ROOT, check=True)
    subprocess.run(["python3", "ocean/pokemon_gen9/emit_item_rules.py"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
