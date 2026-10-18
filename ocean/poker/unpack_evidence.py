"""Verify and unpack a poker report's raw evidence and frozen sources.

Only evidence/archives.json is read; validation_packages.json is separate.
All archives, members and existing outputs are checked before any file is written.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate manifest key: {key}")
        result[key] = value
    return result


def relative_path(name):
    if (not isinstance(name, str) or not name or "\\" in name or "\0" in name
            or ":" in name.split("/")[0]
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError(f"Unsafe relative path: {name!r}")
    return Path(*name.split("/"))


def reject_symlinks(path):
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ValueError(f"Symlink path is not allowed: {item}")


def verify_bytes(data, expected, label):
    if len(data) != expected["bytes"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
        raise ValueError(f"Size or SHA256 mismatch: {label}")


def unpack(report, destination=None):
    report = Path(report).absolute()
    destination = Path(destination).absolute() if destination is not None else report
    manifest_path = report / "evidence/archives.json"
    reject_symlinks(manifest_path)
    reject_symlinks(destination)
    manifest = json.loads(manifest_path.read_text(), object_pairs_hook=unique_object)
    payloads = {}
    for name, expected in manifest.items():
        archive = manifest_path.parent / relative_path(name)
        reject_symlinks(archive)
        if not archive.is_file():
            raise ValueError(f"Archive is not a regular file: {archive}")
        data = archive.read_bytes()
        verify_bytes(data, expected, name)
        members = expected["members"]
        for member_name in members:
            relative_path(member_name)
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as handle:
            entries = handle.getmembers()
            names = [entry.name for entry in entries]
            if len(names) != len(set(names)) or set(names) != set(members):
                raise ValueError(f"Tar member names do not match manifest: {name}")
            for entry in entries:
                target = relative_path(entry.name)
                if entry.type not in (tarfile.REGTYPE, tarfile.AREGTYPE) or entry.sparse:
                    raise ValueError(f"Only regular file members are allowed: {entry.name}")
                if entry.size != members[entry.name]["bytes"]:
                    raise ValueError(f"Tar member size mismatch: {entry.name}")
                with handle.extractfile(entry) as source:
                    content = source.read()
                verify_bytes(content, members[entry.name], entry.name)
                if target in payloads and payloads[target] != content:
                    raise ValueError(f"Conflicting members across archives: {entry.name}")
                payloads[target] = content
    # Preflight the complete output set, including parent paths, before writing.
    unchanged = set()
    for relative, content in payloads.items():
        if any(parent in payloads for parent in relative.parents):
            raise ValueError(f"A member is also an output directory: {relative}")
        target = destination / relative
        reject_symlinks(target)
        for parent in target.parents:
            if parent.exists() and not parent.is_dir():
                raise ValueError(f"Output parent is not a directory: {parent}")
        if target.exists():
            if not target.is_file() or target.read_bytes() != content:
                raise ValueError(f"Existing output differs: {target}")
            unchanged.add(relative)
    for relative, content in payloads.items():
        if relative in unchanged:
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        reject_symlinks(target)
        with target.open("xb") as output:
            output.write(content)
    return {"archives_verified": len(manifest), "files_verified": len(payloads),
            "files_written": len(payloads) - len(unchanged),
            "identical_files_preserved": len(unchanged), "destination": str(destination)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, metavar="REPORT")
    parser.add_argument("--destination", type=Path, metavar="DIR",
                        help="Output directory; defaults to REPORT")
    args = parser.parse_args()
    try:
        result = unpack(args.report, args.destination)
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
