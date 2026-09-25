#!/usr/bin/env python3
"""Space-bounded daily refresh. Keep ZIPs compressed; never extract episodes."""
import argparse
import json
import pathlib
import shutil
import urllib.request
import zipfile

import refresh_daily_replays as refresh


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=pathlib.Path, required=True)
    parser.add_argument('--since', required=True)
    parser.add_argument('--until', required=True)
    parser.add_argument('--report', type=pathlib.Path, required=True)
    parser.add_argument('--reserve-gib', type=float, default=35)
    parser.add_argument('--max-download-gib', type=float, default=9)
    args = parser.parse_args()
    args.root.mkdir(parents=True, exist_ok=True)
    rows = [row for row in refresh.read_index() if args.since <= row['date'] <= args.until]
    results = []
    budget = 0
    for row in rows:
        slug = refresh._slug(row['daily_dataset_slug'])
        destination = refresh._archive_path(args.root, slug)
        if not destination.exists():
            url = refresh.DEFAULT_ARCHIVE_URL.format(slug=slug)
            request = urllib.request.Request(url, headers={'Range': 'bytes=0-0'})
            with urllib.request.urlopen(request, timeout=120) as response:
                # GET Range works on Kaggle's signed redirect; HEAD does not.
                size = int(response.headers.get('Content-Range', '').split('/')[-1])
            budget += size
            if budget > args.max_download_gib * 2**30:
                raise RuntimeError('compressed download budget exceeded')
            if shutil.disk_usage(args.root).free - size < args.reserve_gib * 2**30:
                raise RuntimeError('disk reserve would be breached')
        report = refresh.refresh(args.root, [row], download=True)
        item = report['rows'][0]
        with zipfile.ZipFile(destination) as archive:
            episodes = [entry for entry in archive.infolist() if entry.filename.endswith('.json')]
            if len(episodes) != int(row['episode_count']):
                raise RuntimeError(f'archive episode count mismatch: {destination}')
            item['zip_episode_count'] = len(episodes)
            item['zip_uncompressed_bytes'] = sum(entry.file_size for entry in episodes)
        results.append(item)
        refresh._write_json(args.report, {'rows': results, 'complete': len(results) == len(rows)})
        print(json.dumps(item), flush=True)


if __name__ == '__main__':
    main()
