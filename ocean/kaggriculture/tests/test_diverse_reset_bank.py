import csv
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
import zipfile
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
import build_diverse_reset_bank as diverse
from replay_native import first_difference


class DiverseResetTests(unittest.TestCase):
    def test_strict_parity_contract(self):
        values = [None, True, False, 1, 1.0, '1', [], {}, [1], [1.0],
                  {'a': [1, {'b': True}]}, {'a': [1, {'b': 1}]}]
        for left in values:
            for right in values:
                self.assertEqual(diverse.typed_equal(left, right), not first_difference(left, right))

    def test_seed_split_stable_across_episode_and_date(self):
        for seed in range(100):
            self.assertEqual(diverse.split_for('a', seed, '2026-08-16', '2026-09-12'),
                             diverse.split_for('b', seed, '2026-09-11', '2026-09-12'))
            self.assertEqual(diverse.split_for('a', seed, '2026-09-12', '2026-09-12'), 'future')

    def test_selection_balanced_bounded_and_reproducible(self):
        rows = [dict(turn=turn, scenarios='sell_now,recovery,early_expansion,harvest_ready')
                for turn in range(719) for _ in range(2)]
        episode = {'steps': [None] * 720, 'id': 7}
        with patch.object(diverse, 'episode_rows', return_value=iter(rows)):
            selected = diverse.select_rows('source', episode)
        self.assertLessEqual(len(selected), 12)
        for band in range(8):
            self.assertTrue(any(band * 719 // 8 <= turn < (band + 1) * 719 // 8 for turn in selected))
        with patch.object(diverse, 'episode_rows', return_value=iter(rows)):
            self.assertEqual(selected, diverse.select_rows('source', episode))

    def test_atomic_writer_checks_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'bank.kgb'
            writer = diverse.Writer(path, 1, 4)
            row = dict(sha256=hashlib.sha256(b'abcd').hexdigest())
            self.assertFalse(path.exists())
            with self.assertRaises(ValueError):
                writer.append(b'abce', row)
            writer.append(b'abcd', row)
            writer.close()
            self.assertEqual(path.stat().st_size, diverse.old.BANK_HEADER.size + 4)
            self.assertFalse(writer.temporary.exists())

    def test_reuse_requires_audit_matching_contract_and_disjoint_interval(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            signature = dict(lib_sha256='abc', holdout_date='2026-09-12', episodes_per_day=300)
            (root / 'summary.json').write_text(json.dumps({'shards': [dict(date='2026-08-16', signature=signature)]}))
            (root / 'audit.json').write_text(json.dumps({'passed': True}))
            options = dict(output=str(root / 'new'), lib_sha256='abc',
                           holdout_date='2026-09-12', episode_start=300)
            rows = diverse.reusable_shards(root, options)
            self.assertEqual(rows[0]['shard_directory'], str(root / 'shards' / '2026-08-16'))
            for replacement in ({'lib_sha256': 'different'}, {'episode_start': 299},
                                {'holdout_date': '2026-09-13'}, {'output': str(root)}):
                with self.assertRaises(ValueError):
                    diverse.reusable_shards(root, dict(options, **replacement))
            (root / 'audit.json').write_text(json.dumps({'passed': False}))
            with self.assertRaises(ValueError):
                diverse.reusable_shards(root, options)

    def test_incremental_merge_preserves_base_and_reserves_future_seeds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            destination = root / 'expanded'
            destination.mkdir()
            summaries = []
            def shard(date, folder, data):
                folder.mkdir(parents=True)
                for split in ('train', 'holdout', 'future'):
                    writer = diverse.Writer(folder / f'{split}.kgb', 1, 4)
                    for eid, seed in data.get(split, []):
                        payload = int(eid).to_bytes(4, 'little')
                        row = dict(sha256=hashlib.sha256(payload).hexdigest(), episode_id=str(eid),
                                   seed=str(seed), turn='90', byte_size=4,
                                   index_rows=json.dumps([dict(scenarios='recovery', agent='test', plants=1, animals=1)]))
                        writer.append(payload, row)
                    writer.close()
                summaries.append(dict(date=date, shard_directory=str(folder),
                                      native_state_size=4, native_state_version=1))
            shard('2026-08-16', root / 'base', {'train': [(1, 1)], 'holdout': [(2, 2)]})
            shard('2026-09-12', root / 'future', {'future': [(3, 3)]})
            shard('2026-08-16', root / 'delta', {'train': [(1, 1), (4, 4), (5, 3)]})
            before = (root / 'base' / 'train.kgb').read_bytes()
            report = diverse.merge(dict(output=str(destination), reserved_future_seeds={'3'}), summaries)
            self.assertEqual(report['banks']['full']['states'], 2)
            self.assertEqual(report['banks']['holdout']['states'], 1)
            self.assertEqual(report['banks']['future']['states'], 1)
            self.assertEqual(before, (root / 'base' / 'train.kgb').read_bytes())

    def test_late_mismatch_discards_earlier_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            archive = root / 'kaggriculture-episodes-2026-09-11.zip'
            episode = {'id': 17, 'module_version': '1.32.0', 'steps': ['first', 'last']}
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('episode.json', json.dumps(episode))
            lib = SimpleNamespace(kg_state_serialized_size=lambda: 4,
                                  kg_state_serialization_version=lambda: 1,
                                  kg_create=lambda cfg: 123, kg_destroy=lambda state: None,
                                  kg_step=lambda state, actions: None)
            row = {'state_key': '17:0:0'}
            options = dict(output=str(root / 'out'), episodes_per_day=1,
                           holdout_date='2026-09-12', lib_sha256='test', reserve_gib=0, lib='unused')
            def parity(lib, state, frame, label):
                if frame == 'last':
                    raise AssertionError('intentional late mismatch')
            import ctypes
            from replay_native import CConfig
            cfg = CConfig(seed=7)
            with patch.object(diverse, 'load_core', return_value=lib), \
                    patch.object(diverse, 'validate_episode', return_value=None), \
                    patch.object(diverse, 'replay_config', return_value=cfg), \
                    patch.object(diverse, 'select_rows', return_value={0: [row]}), \
                    patch.object(diverse, 'assert_parity', side_effect=parity), \
                    patch.object(diverse.old, 'assert_parity'), \
                    patch.object(diverse.old, 'verify_resume'), \
                    patch.object(diverse.old, 'serialize_state', return_value=b'abcd'), \
                    patch.object(diverse.old, 'action_pair', return_value=([{}, {}], None)):
                result = diverse.process_archive((str(archive), options))
            self.assertEqual(result['skipped']['parity_incompatible_episode'], 1)
            self.assertEqual(result['counts'], {})
            for split in ('train', 'holdout', 'future'):
                bank = root / 'out' / 'shards' / '2026-09-11' / f'{split}.kgb'
                self.assertEqual(bank.stat().st_size, diverse.old.BANK_HEADER.size)


if __name__ == '__main__':
    unittest.main()
