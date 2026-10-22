import importlib.util
import json
import os
from pathlib import Path
import tempfile
import textwrap
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('retro_sweep_worker', Path(__file__).parents[1]/'sweep_worker.py')
worker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(worker)


class WorkerTests(unittest.TestCase):
    def test_continuation_rejects_incompatible_architecture_before_training(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'parent.bin').write_bytes(b'parent')
            (root/'parent.bin.ini').write_text('[policy]\narchitecture = puffernet\nhidden_size = 256\nnum_layers = 2\n')
            settings = {'base.load_model_path': 'parent.bin', 'policy.architecture': 'puffernet',
                        'policy.hidden_size': '256', 'policy.num_layers': '2'}
            with patch.object(worker, 'ROOT', root):
                worker.validate_parent(settings)
                for key, value in [('policy.hidden_size', '128'), ('policy.num_layers', '1'),
                                   ('policy.architecture', 'transformer')]:
                    with self.assertRaisesRegex(RuntimeError, 'parent architecture mismatch'):
                        worker.train_worker({**settings, key: value})
                (root/'parent.bin.ini').unlink()
                with self.assertRaisesRegex(RuntimeError, 'architecture metadata'):
                    worker.validate_parent(settings)

    def test_full_game_score_requires_clean_complete_panel(self):
        settings = {'panel.repeats': '8', 'panel.game_repeats': '16', 'sweep.metric': 'curriculum'}
        panel = {'attempts': 256, 'score': 77.0}
        game = {'attempts': 16, 'game': 1, 'archive_enabled': 0, 'clears': 2, 'mean_levels': 5.0, 'score': 17.5}
        self.assertEqual(worker.validated_panel_score(settings, panel, game), 94.5)
        for key, value in [('attempts', 15), ('archive_enabled', 1), ('score', 18.5), ('mean_levels', 33)]:
            with self.assertRaises(RuntimeError):
                worker.validated_panel_score(settings, panel, {**game, key: value})

    def test_game_attempt_rows_do_not_override_summary(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)/'game.tsv'
            path.write_text('# score=7 clears=1 attempts=16 frames=10000\n# game=1 mean_levels=0.75 archive_enabled=0\n# game_attempt=15 wins=0 frames=200\n')
            summary = worker.panel_summary(path)
            self.assertEqual(summary['frames'], 10000)
            self.assertNotIn('game_attempt', summary)

    def test_archive_retention_preserves_best_latest_and_weights(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); logs = root/'logs'; logs.mkdir()
            checkpoints = []
            for i, score in enumerate([5, 3, 4]):
                path = root/'checkpoints'/str(i)/'0000000000000001.bin'
                path.parent.mkdir(parents=True);path.write_bytes(b'weights')
                Path(str(path)+'.archive.gz').write_bytes(b'archive')
                trial = logs/(str(i)+'.eval');trial.mkdir()
                (trial/'trial.json').write_text(json.dumps({'checkpoint': str(path), 'score': score}))
                checkpoints.append(path)
            worker.retain_archives(logs, checkpoints[-1])
            self.assertTrue(all(p.exists() for p in checkpoints))
            self.assertEqual([Path(str(p)+'.archive.gz').exists() for p in checkpoints], [True, False, True])

    def test_speed_uses_completed_decisions_after_warmup(self):
        rows = [{'seconds': '1', 'steps': '0'}, {'seconds': '12', 'steps': '1000'},
                {'seconds': '42', 'steps': '301000'}]
        self.assertEqual(worker.measured_speed(rows, 30), 10000)
        self.assertIsNone(worker.measured_speed(rows, 31))

    def run_fixture(self, *, attempts=32, panel_score=3.25, curve_score=3.25, slow=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root/'native'
            native.write_text(textwrap.dedent('''\
                #!/usr/bin/env python3
                import json,os,signal,struct,sys,time
                from pathlib import Path
                s=dict(a[2:].split('=',1) for a in sys.argv[2:])
                assert s['base.checkpoint_interval']=='0'
                logs=Path(s['base.log_dir'])/'retro'
                run=s['base.run_id']
                progress=logs/(run+'.progress.csv')
                progress.write_text('steps,seconds,sps,entropy,kl,value,clipfrac,vram_used_gb\\n'
                    '1000,12,1000,1,0,0,0,2\\n'
                    '2000,42,1000,1,0,0,0,2\\n')
                stopped=False
                def stop(signum,frame):
                    global stopped
                    stopped=True
                signal.signal(signal.SIGUSR1,stop)
                if s['fixture.slow']=='1':
                    while not stopped: time.sleep(.02)
                checkpoint=Path(s['base.checkpoint_dir'])/'retro'/run/'0000000001048576.bin'
                checkpoint.parent.mkdir(parents=True)
                checkpoint.write_bytes(b'fixture')
                Path(str(checkpoint)+'.panel.tsv').write_text('# score='+s['fixture.panel_score']
                    +' clears=5 attempts='+s['fixture.attempts']+' progress=0.5 frames=1000\\n'
                    '# checkpoints=3.25\\n')
                curves=[0.]*192
                curves[0]=float(s['fixture.curve_score']);curves[64]=1.;curves[128]=1048576.
                data=struct.pack('=fff i 192f',3.25,1.,1048576.,1,*curves)
                os.write(int(s['base.result_fd']),data)
            '''))
            native.chmod(0o755)
            read_fd, write_fd = os.pipe()
            settings = {'base.result_fd': str(write_fd), 'base.run_id': 'fixture',
                'base.log_dir': str(root/'logs'), 'base.checkpoint_dir': str(root/'checkpoints'),
                'base.checkpoint_interval': '192',
                'sweep.trainer_path': str(native), 'sweep.startup_timeout': '5',
                'sweep.max_trial_seconds': '10', 'sweep.eval_timeout': '5',
                'sweep.memory_limit_mb': '15360', 'sweep.speed_check_seconds': '30',
                'sweep.min_steps_per_second': '5000', 'sweep.train.total_timesteps.min': '1000000',
                'train.total_timesteps': '1048576', 'panel.repeats': '1',
                'fixture.attempts': str(attempts), 'fixture.panel_score': str(panel_score),
                'fixture.curve_score': str(curve_score),
                'fixture.slow': str(int(slow))}
            try:
                with patch.object(worker, 'ROOT', root), patch.object(worker, 'STOP', False):
                    worker.train_worker(settings)
                os.close(write_fd);write_fd=None
                data = os.read(read_fd, worker.RESULT.size)
                result = worker.RESULT.unpack(data)
                trial = json.loads((root/'logs/retro/fixture.eval/trial.json').read_text())
                self.assertEqual((result[0], result[2], result[3]), (3.25, 1048576., 1))
                self.assertGreaterEqual(result[1], 1.)
                self.assertAlmostEqual(result[1], trial['cost'], places=5)
                self.assertEqual(result[68], result[1])
                self.assertEqual(trial['panel']['attempts'], 32)
                self.assertEqual(trial['reason'], 'completed')
                self.assertEqual(len(list((root/'checkpoints/retro/fixture').glob('*.bin'))), 1)
            finally:
                os.close(read_fd)
                if write_fd is not None:
                    os.close(write_fd)

    def test_native_panel_score_and_result_pipe_are_preserved(self):
        self.run_fixture()

    def test_incomplete_level_coverage_cannot_enter_gp(self):
        with self.assertRaisesRegex(RuntimeError, 'all-32-level'):
            self.run_fixture(attempts=31)

    def test_disagreement_with_native_score_cannot_enter_gp(self):
        with self.assertRaisesRegex(RuntimeError, 'all-32-level'):
            self.run_fixture(panel_score=9)

    def test_curve_score_cannot_disagree_with_fixed_panel_score(self):
        with self.assertRaisesRegex(RuntimeError, 'one matching fixed-panel score'):
            self.run_fixture(curve_score=0)

    def test_slow_trial_saves_then_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'rejected trial: slow'):
            self.run_fixture(slow=True)


if __name__ == '__main__':
    unittest.main()
