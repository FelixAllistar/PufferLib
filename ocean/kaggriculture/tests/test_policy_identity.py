import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from policy_identity import deduplicate, aggregated_weights


class IdentityTests(unittest.TestCase):
    def model(self, root, name, content, version=1):
        path = root / name
        path.write_bytes(content)
        Path(str(path) + '.obs_version').write_text(str(version))
        return path

    def test_dedup_keeps_candidate_and_remaps_focal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a = self.model(root, 'a.bin', b'a')
            alias = self.model(root, 'alias.bin', b'a')
            b = self.model(root, 'b.bin', b'b')
            kept, focal, aliases = deduplicate([a, alias, b, alias], 2)
            self.assertEqual(kept, [str(a), str(b)])
            self.assertEqual(focal, 1)
            self.assertEqual(aliases[-1][1], str(a))

    def test_layout_is_part_of_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a = self.model(root, 'a.bin', b'a', 0)
            b = self.model(root, 'b.bin', b'a', 1)
            self.assertEqual(len(deduplicate([a, b])[0]), 2)

    def test_alias_cannot_erase_larger_weight(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a = self.model(root, 'a.bin', b'a')
            alias = self.model(root, 'alias.bin', b'a')
            manifest, meta = root / 'manifest.tsv', root / 'meta.tsv'
            manifest.write_text(f'id\tpolicy\tcheckpoint\n0\ta\t{a}\n1\talias\t{alias}\n')
            meta.write_text('policy\tweight\tresponse\na\t0.423\t0\nalias\t0.003\t0\n#exploitability\t0\t0\n')
            weights = aggregated_weights(manifest, meta)
            self.assertAlmostEqual(weights['a'], .426)
            self.assertAlmostEqual(weights['alias'], .426)


if __name__ == '__main__':
    unittest.main()
