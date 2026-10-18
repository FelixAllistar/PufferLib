"""Strict source/build metadata helpers; no native, model or HTTP imports."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath


PACKAGE = Path(__file__).resolve().parent
BASE_COMMIT = "a4bfd971ae4bdc2c46b8bffef2617f368b9be017"
EVALUATION_COMMIT = "8ae0b7ed8742a25c89b29a382eaad23442c57120"
MODEL_FIELDS = {"model_version", "size_domain", "size_encoding", "model_domain",
                "model_npz_sha256", "model_numeric_sha256", "module_source_closure_sha256",
                "module_binary_sha256", "numeric_bytes", "nodes"}
MAX_METADATA_BYTES = 16 << 20


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest_string(value):
    require(type(value) is str and len(value) == 64 and value != "0" * 64
            and all(character in "0123456789abcdef" for character in value),
            "expected a nonzero lowercase SHA256")
    return value


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def descriptor(path, expected=None):
    path = Path(path).resolve(strict=True)
    require(path.is_file(), "input is not a regular file: " + str(path))
    actual = sha256(path)
    if expected is not None:
        require(actual == digest_string(expected), "file digest differs: " + str(path))
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": actual}


def verify_descriptor(value):
    require(type(value) is dict and set(value) == {"path", "bytes", "sha256"},
            "file descriptor must have exactly path, bytes and sha256")
    require(type(value["path"]) is str and Path(value["path"]).is_absolute(),
            "descriptor path must be absolute")
    require(type(value["bytes"]) is int and value["bytes"] >= 0,
            "descriptor byte count must be a nonnegative integer")
    actual = descriptor(value["path"], value["sha256"])
    require(actual == value, "descriptor is aliased or its byte count changed")
    return actual


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("ascii")


def read_object(path, expected=None):
    row = descriptor(path, expected)
    require(row["bytes"] <= MAX_METADATA_BYTES, "metadata document is too large")
    raw = Path(row["path"]).read_bytes()
    require(hashlib.sha256(raw).hexdigest() == row["sha256"], "metadata changed during read")

    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "duplicate JSON key")
            result[key] = value
        return result

    def nonfinite(_):
        raise ValueError("nonfinite JSON metadata")

    value = json.loads(raw, object_pairs_hook=unique, parse_constant=nonfinite)
    require(type(value) is dict, "metadata root must be an object")
    return value


def model_binding(value):
    require(type(value) is dict and set(value) == MODEL_FIELDS,
            "exact ten-field external model binding required")
    require(type(value["model_version"]) is int and value["model_version"] == 2
            and type(value["size_domain"]) is int and value["size_domain"] == 2
            and value["model_domain"] == "behavior-clone-full-model-v2"
            and value["size_encoding"] == "legal_interior_fraction_f64_v2",
            "incompatible model version, domain or sizing encoding")
    for key in ("model_npz_sha256", "model_numeric_sha256", "module_source_closure_sha256",
                "module_binary_sha256"):
        digest_string(value[key])
    require(type(value["nodes"]) is int and 1 <= value["nodes"] <= 4064,
            "model node count is out of range")
    require(type(value["numeric_bytes"]) is int and 1 <= value["numeric_bytes"] <= 524288,
            "model numeric byte count is out of range")
    return dict(value)


def relative_name(value):
    require(type(value) is str and value, "empty or non-string source name")
    path = PurePosixPath(value)
    require(not path.is_absolute() and value == path.as_posix()
            and all(part not in ("", ".", "..") for part in path.parts),
            "source name is not a normalized relative path")
    return path


def package_inventory():
    path = PACKAGE / "package_sources.json"
    value = read_object(path)
    require(set(value) == {"schema", "files"}
            and value["schema"] == "poker-clone-package-sources-v1",
            "unexpected source inventory schema")
    require(type(value["files"]) is list and 1 <= len(value["files"]) <= 128,
            "invalid package source inventory")
    rows, seen = [], set()
    for entry in value["files"]:
        require(type(entry) is dict and set(entry) == {"path", "bytes", "sha256"},
                "invalid package source inventory entry")
        name = relative_name(entry["path"])
        require(str(name) not in seen and str(name) != "package_sources.json",
                "duplicate or self-referencing inventory entry")
        seen.add(str(name))
        row = descriptor(PACKAGE / name, entry["sha256"])
        require(type(entry["bytes"]) is int and row["bytes"] == entry["bytes"],
                "package source byte count differs")
        rows.append(row)
    rows.append(descriptor(path))
    return rows


def save_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write("\n")


def fresh_output(value):
    path = Path(value).resolve()
    require(not path.is_relative_to(PACKAGE) and not PACKAGE.is_relative_to(path),
            "generated output must be separate from the package source tree")
    path.mkdir(parents=True, exist_ok=False)
    return path
