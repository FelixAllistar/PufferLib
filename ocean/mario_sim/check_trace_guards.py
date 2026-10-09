#!/usr/bin/env python3
"""Negative controls for the CUDA replay gate, derived from one passing frame."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('verifier', type=Path)
    parser.add_argument('trace', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.trace.open('rb') as stream:
        h = list(struct.unpack('<8IQ', stream.read(40)))
        if h[0] != 0x534d4231 or h[1] != 2:
            raise ValueError('negative controls require a qualified source trace')
        data = stream.read(32768)
        case = bytearray(stream.read(h[3]))
        count = struct.unpack_from('<I', case, h[2])[0]
        if count != 1:
            raise ValueError('source must begin with a one-frame branch')
        frame = bytearray(stream.read(h[4]))
        if len(data) != 32768 or len(frame) != h[4]:
            raise ValueError('truncated source')
    args.output.mkdir(parents=True, exist_ok=True)
    h[5] = h[6] = 1
    header = struct.pack('<8IQ', *h)
    invalid = h.copy()
    invalid[0] = 0
    fixtures = [('invalid_header', struct.pack('<8IQ', *invalid), 2)]
    fixtures.append(('truncated', header + data + case, 2))
    # Reach the graph path in the second batch. Only its last frame is corrupt.
    altered = frame.copy()
    altered[1] ^= 1
    repeated = h.copy()
    repeated[5] = repeated[6] = 257
    fixtures.append(('ram_mismatch_graph', struct.pack('<8IQ', *repeated) + data
                     + (case + frame) * 256 + case + altered, 1))
    # Version 2's explicit override fields occupy 84 bytes after SmbLogic.
    # Row zero is an invalid resident tile edit; the reset fault must propagate.
    invalid_scene = case.copy()
    struct.pack_into('<I', invalid_scene, h[2] - 84, 16)
    struct.pack_into('<i', invalid_scene, h[2] - 8, 0)
    fixtures.append(('invalid_scene_reset', header + data + invalid_scene + frame, 1))
    results = []
    for name, content, expected in fixtures:
        path = args.output / (name + '.bin')
        path.write_bytes(content)
        run = subprocess.run([str(args.verifier.resolve()), str(path.resolve())],
                             capture_output=True, text=True, timeout=120)
        (args.output / (name + '.log')).write_text(run.stdout + run.stderr)
        if run.returncode != expected:
            raise RuntimeError(f'{name}: expected exit {expected}, got {run.returncode}: {run.stderr}')
        if expected == 1:
            result = json.loads(run.stdout)
            if result['failures'] != 1:
                raise RuntimeError(f'{name}: expected exactly one detected failure')
        results.append({'test': name, 'exit_code': run.returncode, 'passed': True})
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results))


if __name__ == '__main__':
    main()
