"""Offline evidence-corruption gates; no native bot or live service is invoked."""
import copy
import math
from pathlib import Path
import statistics
import tempfile
import unittest

from dual_bench import verify_blocks


class BlockEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.prefix = Path(self.temp.name) / "comparison"
        values = [[-2., 0., 3.], [-1., 3., 2.], [1., 3., -1.]]
        self.paths = [Path(str(self.prefix) + f'.{name}.csv')
                      for name in ('control', 'candidate', 'difference')]
        self.result = {}
        for path, name, returns in zip(self.paths, ('control', 'candidate', 'paired_gain'), values):
            path.write_text('block,mean_bb\n' + ''.join(f'{i},{value}\n' for i, value in enumerate(returns)))
            self.result[name] = {'bb100': 100 * statistics.mean(returns),
                                 'ci95_half_width_bb100': 196 * statistics.stdev(returns) / math.sqrt(3)}

    def tearDown(self):
        self.temp.cleanup()

    def test_valid_blocks_match_independent_means_and_intervals(self):
        evidence = verify_blocks(self.prefix, 3, self.result)
        self.assertEqual({item['rows'] for item in evidence.values()}, {3})
        self.assertTrue(all(len(item['sha256']) == 64 for item in evidence.values()))

    def test_all_csvs_truncated_to_same_plausible_prefix(self):
        for path in self.paths:
            path.write_text('\n'.join(path.read_text().splitlines()[:-1]) + '\n')
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            verify_blocks(self.prefix, 3, self.result)

    def test_missing_duplicate_or_reordered_row(self):
        original = self.paths[1].read_text()
        for replacement in ('block,mean_bb\n0,-1\n1,3\n',
                            'block,mean_bb\n0,-1\n1,3\n1,2\n',
                            'block,mean_bb\n0,-1\n2,2\n1,3\n'):
            with self.subTest(replacement=replacement):
                self.paths[1].write_text(replacement)
                with self.assertRaises(ValueError):
                    verify_blocks(self.prefix, 3, self.result)
        self.paths[1].write_text(original)

    def test_inconsistent_difference_and_nonfinite_csv(self):
        original = self.paths[2].read_text()
        for replacement in ('0,2.0', '0,nan', '0,inf'):
            with self.subTest(replacement=replacement):
                self.paths[2].write_text(original.replace('0,1.0', replacement))
                with self.assertRaises(ValueError):
                    verify_blocks(self.prefix, 3, self.result)

    def test_incorrect_or_nonfinite_native_summary(self):
        for arm in ('control', 'candidate', 'paired_gain'):
            for field in ('bb100', 'ci95_half_width_bb100'):
                for value in (float('nan'), float('inf'), -9999.):
                    with self.subTest(arm=arm, field=field, value=value):
                        result = copy.deepcopy(self.result)
                        result[arm][field] = value
                        with self.assertRaises(ValueError):
                            verify_blocks(self.prefix, 3, result)


if __name__ == '__main__':
    unittest.main()
