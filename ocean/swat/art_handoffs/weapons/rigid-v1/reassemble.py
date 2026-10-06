#!/usr/bin/env python3
"""Reconstruct the original lossless weapon handoff ZIP from two checked parts."""
from pathlib import Path
import hashlib, json, zipfile

root = Path(__file__).resolve().parent
manifest = json.loads((root / "parts_manifest.json").read_text())
chunks = []
for item in manifest["parts_in_order"]:
    data = (root / item["file"]).read_bytes()
    if len(data) != item["size_bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
        raise SystemExit("Part verification failed: " + item["file"])
    chunks.append(data)
data = b"".join(chunks)
original = manifest["original_archive"]
if len(data) != original["size_bytes"] or hashlib.sha256(data).hexdigest() != original["sha256"]:
    raise SystemExit("Reassembled archive hash mismatch")
output = root / original["file"]
if output.exists() and output.read_bytes() != data:
    raise SystemExit("Refusing to overwrite a different file: " + str(output))
output.write_bytes(data)
with zipfile.ZipFile(output) as archive:
    bad = archive.testzip()
    if bad:
        raise SystemExit("ZIP CRC failed: " + bad)
print("Verified ZIP ready to extract: " + str(output))
print("SHA256: " + original["sha256"])
