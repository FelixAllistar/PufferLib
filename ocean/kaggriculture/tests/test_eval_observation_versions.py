import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1] / "eval_observation_versions.py"
spec = importlib.util.spec_from_file_location("kag_version_eval", MODULE)
evaluation = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = evaluation
spec.loader.exec_module(evaluation)


class ObservationEvaluationTests(unittest.TestCase):
    def test_executor_sidecar_overrides_reused_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / 'checkpoints/kaggriculture/example/0000000000001000.bin'
            model.parent.mkdir(parents=True)
            model.touch()
            self.assertEqual(evaluation.executor_version(model, root), 0)
            log = root / 'logs/kaggriculture/example.ini'
            log.parent.mkdir(parents=True)
            log.write_text('[env]\nmacro_executor_version=1\n')
            self.assertEqual(evaluation.executor_version(model, root), 1)
            Path(str(model)+'.executor_version').write_text('0\n')
            self.assertEqual(evaluation.executor_version(model, root), 0)

    def test_pair_coverage_and_layout_compatibility(self):
        # Exercise interleaved generations, cached tails, and >8 opponents.
        for count in (2, 4, 9, 12, 32):
            for versions in ([0] * count, [1] * count,
                             [i % 2 for i in range(count)],
                             [int(i < 4) for i in range(count)]):
                policies = [evaluation.Policy(i, str(i), str(i), version, (i // 2) % 2)
                            for i, version in enumerate(versions)]
                for focal in (0, 1, count - 1):
                    seen = []
                    for mode, left, right, waves in evaluation.matrix_jobs(policies, focal):
                        self.assertEqual(len({p.version for p in left}), 1)
                        self.assertEqual(len({p.executor for p in left}), 1)
                        if mode == "matrix":
                            self.assertLessEqual(len(left), 9)
                            seen.extend((left[i].index, left[j].index)
                                        for i in range(waves)
                                        for j in range(i + 1, len(left)))
                        else:
                            self.assertLessEqual(len(right), 8)
                            self.assertEqual(len({p.version for p in right}), 1)
                            self.assertEqual(len({p.executor for p in right}), 1)
                            seen.extend(tuple(sorted((a.index, b.index)))
                                        for a in left for b in right)
                    limit = focal or count - 1
                    expected = {(i, j) for i in range(limit) for j in range(i + 1, count)}
                    self.assertEqual(set(seen), expected)
                    self.assertEqual(len(seen), len(expected))

    def test_reverse_mapping_preserves_cash_and_draws(self):
        old = [evaluation.Policy(5, "old", "old.bin", 0)]
        new = [evaluation.Policy(1, "new", "new.bin", 1)]
        raw = ["0", "0", "0.8", "0.1", "75000", "55000", "100"]
        result = evaluation.remap_row(raw, old, new, True)
        self.assertEqual(result[:2], (1, 5))
        self.assertAlmostEqual(result[2], 0.2)
        self.assertEqual(result[3:], (0.1, 55000, 75000, 100))
        # Screen indices belong to separate manifests and must not be swapped.
        self.assertEqual(evaluation.remap_row(raw, old, new, False)[:3], (5, 1, 0.8))

    def test_metadata_does_not_inherit_current_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "config").mkdir()
            (root / "config/kaggriculture.ini").write_text("[env]\nobservation_version=1\n")
            model = root / "checkpoints/kaggriculture/experiment/0000000000001000.bin"
            model.parent.mkdir(parents=True)
            model.touch()
            self.assertEqual(evaluation.observation_version(model, root), 0)
            log = root / "logs/kaggriculture/experiment.ini"
            log.parent.mkdir(parents=True)
            log.write_text("[env]\nobservation_version=1\n")
            self.assertEqual(evaluation.observation_version(model, root), 1)
            copied = root / "saved/run_experiment_0000000000001000.bin"
            copied.parent.mkdir()
            copied.touch()
            self.assertEqual(evaluation.observation_version(copied, root), 1)
            sidecar = Path(str(copied) + ".obs_version")
            sidecar.write_text("0\n")
            self.assertEqual(evaluation.observation_version(copied, root), 0)
            sidecar.write_text("2\n")
            with self.assertRaises(ValueError):
                evaluation.observation_version(copied, root)

    def test_new_four_old_eight_use_three_native_batches(self):
        policies = [evaluation.Policy(i, str(i), str(i), int(i < 4)) for i in range(12)]
        self.assertEqual(len(list(evaluation.matrix_jobs(policies))), 3)
        self.assertEqual(len(list(evaluation.matrix_jobs(policies, 4))), 2)

    def test_recovered_run_metadata_without_training_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / 'checkpoints/kaggriculture/response/0000000000001000.bin'
            model.parent.mkdir(parents=True)
            model.touch()
            metadata = model.parent / 'run_metadata.json'
            metadata.write_text('{"run_id":"response","observation_version":1}')
            self.assertEqual(evaluation.observation_version(model, root), 1)
            copied = root / 'saved/run_response_0000000000001000.bin'
            copied.parent.mkdir()
            copied.touch()
            self.assertEqual(evaluation.observation_version(copied, root), 1)
            Path(str(model) + '.obs_version').write_text('0\n')
            self.assertEqual(evaluation.observation_version(model, root), 0)
            metadata.write_text('{"run_id":"wrong_run","observation_version":1}')
            with self.assertRaises(ValueError):
                evaluation.observation_version(copied, root)


if __name__ == "__main__":
    unittest.main()
