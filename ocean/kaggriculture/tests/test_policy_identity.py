import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from policy_identity import deduplicate, aggregated_weights, identity
from eval_observation_versions import FRESH_TAGS


class IdentityTests(unittest.TestCase):
    def test_all_fresh_controller_metadata_is_part_of_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [root / f'{i}.bin' for i in range(4)]
            for path in paths:
                path.write_bytes(b'same weights')
                for tag, value in zip(FRESH_TAGS, (3, 3, 0, 32, 3, 8, 1, 1, 0)):
                    Path(f'{path}.{tag}').write_text(str(value))
            Path(f'{paths[1]}.macro_mode').write_text('2')
            Path(f'{paths[2]}.macro_decision_interval').write_text('4')
            Path(f'{paths[3]}.macro_score_features').write_text('1')
            self.assertEqual(len({identity(path) for path in paths}), 4)
            self.assertEqual(len(deduplicate(paths)[0]), 4)

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
