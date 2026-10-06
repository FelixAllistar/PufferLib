#!/usr/bin/env python3
"""Fetch the pinned optional Steam Audio runtime; no system installation."""
import hashlib
from pathlib import Path
import shutil
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "build/swat/deps/steam-audio"
VERSION = "4.8.1"
SHA256 = "4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449"
URL = f"https://github.com/ValveSoftware/steam-audio/releases/download/v{VERSION}/steamaudio_{VERSION}.zip"


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    archive = DEST / "sdk.zip"
    if not archive.exists():
        print(f"Downloading Steam Audio {VERSION} (about 181 MB)", flush=True)
        pending = DEST / "sdk.zip.part"
        with urllib.request.urlopen(URL, timeout=60) as source, pending.open("wb") as target:
            shutil.copyfileobj(source, target)
        pending.replace(archive)
    with archive.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != SHA256:
        raise SystemExit(f"Steam Audio checksum mismatch: {archive}; expected {SHA256}, received {digest}")
    names = ["steamaudio/lib/linux-x64/libphonon.so", "steamaudio/lib/windows-x64/phonon.dll"]
    with zipfile.ZipFile(archive) as sdk:
        for name in names:
            target = DEST / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with sdk.open(name) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
    for platform, library in [("", names[0]), ("windows", names[1])]:
        output = ROOT / "build/swat" / platform
        output.mkdir(parents=True, exist_ok=True)
        shutil.copy2(DEST / library, output / Path(library).name)
        for license_name in ["LICENSE.md", "THIRDPARTY.md"]:
            shutil.copy2(ROOT / "vendor/steam_audio" / license_name, output / f"STEAM_AUDIO_{license_name}")
    print("Steam Audio ready for Linux and Windows. Rebuild/start SWAT to use HRTF audio.")


if __name__ == "__main__":
    main()
