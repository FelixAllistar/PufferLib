"""Compare a pilot shard against the unchanged old parity builder, on the same CPU."""
import argparse
import csv
import json
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
import build_replay_state_bank as baseline
from index_replay_states import FIELDS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pilot', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with pathlib.Path(f'{args.pilot}.manifest.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    index = args.output / 'index.tsv'
    with index.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter='\t')
        writer.writeheader()
        for row in rows:
            writer.writerows(json.loads(row['index_rows']))
    archives = sorted({row['source'].split(':', 1)[0] for row in rows})
    bank = args.output / 'baseline.kgb'
    started = time.monotonic()
    result = baseline.build_bank(baseline.parse_args([*archives, '--index', str(index), '--output', str(bank)]))
    seconds = time.monotonic() - started
    with pathlib.Path(f'{bank}.manifest.tsv').open() as stream:
        baseline_rows = list(csv.DictReader(stream, delimiter='\t'))
    def hashes(records):
        return {(row['episode_id'], row['turn']): row['sha256'] for row in records}
    assert hashes(rows) == hashes(baseline_rows), 'snapshot payloads differ'
    report = dict(baseline_seconds=seconds, matched_snapshots=len(rows),
                  matched_episodes=result['counts']['episodes'], parity_frames=result['counts']['parity_frames'])
    (args.output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
