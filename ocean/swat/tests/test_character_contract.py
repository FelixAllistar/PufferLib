"""Headless GLB consumer checks using only public synthetic data and stdlib."""
import argparse
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from character_fixtures import synthetic


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True, type=Path)
    args = parser.parse_args()
    probe = args.probe.resolve()
    with tempfile.TemporaryDirectory(prefix='swat-character-contract-') as tmp:
        root = Path(tmp)
        asset, times, output = (root / n for n in ['asset.glb', 'times.txt', 'poses.bin'])
        times.write_text('-1\n0\n1\n0\n')
        asset.write_bytes(synthetic())
        result = subprocess.run([str(probe), str(asset), str(times), str(output), '32'], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        assert 'max_influences=7 vertices_over_four=3' in result.stdout
        raw = output.read_bytes()
        values = struct.unpack('<' + 'f' * (len(raw) // 4), raw)
        stride = 10 * 16 + 3 * (1 + 3 * 6)
        assert len(values) == stride * 4 and all(map(math.isfinite, values))
        for sample, visible in enumerate([1, 0, 1, 0]):
            start = sample * stride + 10 * 16
            # Seven equal influences, nonidentity binds, shuffled joint list:
            # averaged translations = .1 * mean(0..6) = .3; x scale = 26/7.
            if sample == 0:
                expected = [0, .3, 0, 26/7, .3, 0, 0, 1.3, 0]
                assert max(abs(a-b) for a,b in zip(values[start+1:start+10], expected)) < 1e-6
            assert values[start+19] == visible  # zero-scale STEP prop
            rigid = start + 38
            assert values[rigid] == 1
            assert values[rigid+1:rigid+10] == (4,0,0,5,0,0,4,2,0)
        assert values[stride:stride*2] == values[stride*3:stride*4]  # seek back
        cases = ['empty', 'bounds', 'cycle', 'joints', 'pair', 'duplicate', 'matrix_channel', 'external', 'weights', 'affine', 'material', 'parents', 'duplicate_child', 'emissive', 'four', 'truncated']
        for case in cases:
            asset.write_bytes(synthetic(malformed=case) if case not in ['four', 'truncated'] else (synthetic()[:-1] if case == 'truncated' else synthetic()))
            result = subprocess.run([str(probe), str(asset), str(times), str(output), '4' if case == 'four' else '32'], capture_output=True, text=True, timeout=30)
            assert result.returncode == 1 and 'Character rejected:' in result.stderr, (case, result)
        asset.write_bytes(synthetic(malformed='zero_cubic')); times.write_text('.5\n')
        result = subprocess.run([str(probe), str(asset), str(times), str(output), '32'], capture_output=True, text=True, timeout=30)
        assert result.returncode == 1  # undefined zero quaternion between valid cubic keys
        print('PASS full influences, remap/binds, rigid transforms, STEP visibility, seek and malformed inputs')


if __name__ == '__main__':
    main()
