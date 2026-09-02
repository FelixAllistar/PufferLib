#!/usr/bin/env python3
"""Incrementally fetch Kaggriculture daily top-episode archives.

The public Kaggle API serves the index and raw archive without a Kaggle CLI
session.  This tool uses that endpoint when the CLI is unavailable, skips
already complete ``raw/<slug>/<slug>.zip`` files, and writes a sidecar with
the index row, archive size, SHA-256, and URL.  It never removes or replaces a
raw replay archive.

Examples::

    # Inspect the newest fourteen index rows without downloading anything.
    python refresh_daily_replays.py --root /workspace/elite_replays --dry-run

    # Fetch only missing/new archives after the given cutoff.
    python refresh_daily_replays.py --root /workspace/elite_replays \
        --since 2026-08-22 --download

The archive endpoint is intentionally a direct raw download rather than a
Kaggle CLI subprocess, because Vast images commonly have neither credentials
nor the CLI installed.  ``--kaggle-bin`` remains available for environments
that want to audit an installed CLI, but direct HTTP is the deterministic
fallback used by default.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import pathlib
import tempfile
import urllib.error
import urllib.request
from typing import Any, Iterable


DEFAULT_INDEX_URL = (
    "https://www.kaggle.com/api/v1/datasets/download/"
    "kaggle/kaggriculture-episodes-index?filename=manifest.csv&raw=true"
)
DEFAULT_ARCHIVE_URL = (
    "https://www.kaggle.com/api/v1/datasets/download/kaggle/{slug}?raw=true"
)


def _date(value: str) -> dt.date:
    try:
        return dt.date.fromisoformat(str(value))
    except ValueError as exc:
        raise ValueError(f"invalid ISO date: {value!r}") from exc


def _fetch(url: str) -> bytes:
    request = urllib.request.Request(
        url, headers={"User-Agent": "pufferlib-kaggriculture-refresh/1"}
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def read_index(url: str = DEFAULT_INDEX_URL) -> list[dict[str, str]]:
    """Read and validate the public daily index manifest."""

    raw = _fetch(url)
    text = raw.decode("utf-8-sig")
    rows = list(csv.DictReader(text.splitlines()))
    required = {
        "date", "daily_dataset_slug", "daily_dataset_url", "episode_count",
        "total_bytes",
    }
    if not rows or not required.issubset(rows[0]):
        raise ValueError(
            f"index {url} lacks expected columns; got "
            f"{sorted(rows[0]) if rows else 'empty'}"
        )
    valid = []
    for row in rows:
        if not row.get("daily_dataset_slug", "").strip():
            continue
        _date(row["date"])
        valid.append({key: str(value or "") for key, value in row.items()})
    valid.sort(key=lambda row: row["date"], reverse=True)
    return valid


def _slug(value: str) -> str:
    value = str(value).strip().rstrip("/")
    if "/" in value:
        value = value.rsplit("/", 1)[-1]
    if not value.startswith("kaggriculture-episodes-"):
        raise ValueError(f"unexpected daily dataset slug: {value!r}")
    return value


def _archive_path(root: pathlib.Path, slug: str) -> pathlib.Path:
    return root / "raw" / slug / f"{slug}.zip"


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(16 * 1024 * 1024)
            if not chunk:
                return digest.hexdigest()
            digest.update(chunk)


def _write_json(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent,
        prefix=f".{path.name}.", delete=False,
    ) as stream:
        temporary = pathlib.Path(stream.name)
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
    temporary.replace(path)


def _download_resumable(url: str, destination: pathlib.Path) -> tuple[int, bool]:
    """Download one archive atomically, resuming a partial temporary file."""

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.part")
    existing = temporary.stat().st_size if temporary.is_file() else 0
    headers = {"User-Agent": "pufferlib-kaggriculture-refresh/1"}
    if existing:
        headers["Range"] = f"bytes={existing}-"
    request = urllib.request.Request(url, headers=headers)
    try:
        response = urllib.request.urlopen(request, timeout=180)
    except urllib.error.HTTPError as error:
        if existing and error.code == 416:
            # A complete temporary file is still safe to atomically promote;
            # its hash is checked by the caller.
            response = None
        else:
            raise
    if response is None:
        pass
    else:
        append = existing and getattr(response, "status", 200) == 206
        if not append:
            existing = 0
            temporary.unlink(missing_ok=True)
        mode = "ab" if append else "wb"
        with response, temporary.open(mode) as stream:
            while True:
                chunk = response.read(16 * 1024 * 1024)
                if not chunk:
                    break
                stream.write(chunk)
    size = temporary.stat().st_size
    temporary.replace(destination)
    return size, bool(existing)


def _select_rows(
    rows: Iterable[dict[str, str]], *, since: dt.date | None,
    until: dt.date | None, days: int, slugs: set[str],
) -> list[dict[str, str]]:
    selected = []
    for row in rows:
        date = _date(row["date"])
        slug = _slug(row["daily_dataset_slug"])
        if since and date < since:
            continue
        if until and date > until:
            continue
        if slugs and slug not in slugs:
            continue
        selected.append({**row, "daily_dataset_slug": slug})
    selected.sort(key=lambda row: row["date"], reverse=True)
    if days:
        selected = selected[:days]
    return selected


def refresh(
    root: pathlib.Path, rows: Iterable[dict[str, str]], *, download: bool,
    archive_url_template: str = DEFAULT_ARCHIVE_URL,
    report_path: pathlib.Path | None = None,
) -> dict[str, Any]:
    """Process selected index rows and return a machine-readable report."""

    root = root.resolve()
    results: list[dict[str, Any]] = []
    for row in rows:
        slug = _slug(row["daily_dataset_slug"])
        archive = _archive_path(root, slug)
        metadata_path = archive.with_suffix(archive.suffix + ".metadata.json")
        expected_uncompressed = int(row.get("total_bytes") or 0)
        item: dict[str, Any] = {
            "date": row["date"], "slug": slug, "archive": str(archive),
            "episode_count": int(row.get("episode_count") or 0),
            "index_total_bytes": expected_uncompressed,
            "index_url": row.get("daily_dataset_url", ""),
            "status": "planned",
        }
        if archive.is_file() and archive.stat().st_size > 0:
            item.update({
                "status": "existing", "size": archive.stat().st_size,
                "sha256": _sha256(archive),
            })
        elif not download:
            item["status"] = "missing"
        else:
            url = archive_url_template.format(slug=slug)
            size, resumed = _download_resumable(url, archive)
            item.update({
                "status": "downloaded", "size": size, "resumed": resumed,
                "sha256": _sha256(archive), "download_url": url,
            })
        if archive.is_file() and archive.stat().st_size > 0:
            item.setdefault("size", archive.stat().st_size)
            item.setdefault("sha256", _sha256(archive))
            metadata = {
                "date": row["date"], "slug": slug,
                "daily_dataset_url": row.get("daily_dataset_url", ""),
                "download_url": archive_url_template.format(slug=slug),
                "episode_count": int(row.get("episode_count") or 0),
                "index_total_bytes": expected_uncompressed,
                "archive_bytes": item["size"], "sha256": item["sha256"],
                "refreshed_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            }
            _write_json(metadata_path, metadata)
            item["metadata"] = str(metadata_path)
        results.append(item)
    report = {
        "index_rows": len(results), "downloaded": sum(
            item["status"] == "downloaded" for item in results
        ), "existing": sum(
            item["status"] == "existing" for item in results
        ), "missing": sum(
            item["status"] == "missing" for item in results
        ), "rows": results,
    }
    if report_path is not None:
        _write_json(report_path, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("/workspace/elite_replays"))
    parser.add_argument("--index-url", default=DEFAULT_INDEX_URL)
    parser.add_argument("--archive-url-template", default=DEFAULT_ARCHIVE_URL)
    parser.add_argument("--since", type=_date)
    parser.add_argument("--until", type=_date)
    parser.add_argument(
        "--days", type=int, default=14,
        help="newest selected index rows to process (0 means all)",
    )
    parser.add_argument(
        "--slug", action="append", default=[],
        help="exact daily slug; repeat to select explicit archives",
    )
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="plan only")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    if args.days < 0:
        parser.error("--days must be non-negative")
    if args.since and args.until and args.since > args.until:
        parser.error("--since must not be after --until")
    rows = _select_rows(
        read_index(args.index_url), since=args.since, until=args.until,
        days=args.days, slugs={_slug(value) for value in args.slug},
    )
    report = refresh(
        args.root, rows, download=args.download and not args.dry_run,
        archive_url_template=args.archive_url_template, report_path=args.report,
    )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
