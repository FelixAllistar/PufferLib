import hashlib
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('worker', Path(__file__).parents[1]/'benchmarks/sweep_metamon.py')
worker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(worker)

class WorkerTest(unittest.TestCase):
    def test_ten_percent_schedule(self):
        for budget in (500_000_000, 556_136_064, 1_000_000_000, 2_000_000_000):
            for agents in (2048, 4096):
                for horizon in (16, 32, 64):
                    interval, final = worker.checkpoint_schedule(dict(
                        **{'train.total_timesteps':str(budget), 'vec.total_agents':str(agents),
                           'train.horizon':str(horizon)}))
                    rollout = agents*horizon
                    self.assertLess(budget-final, rollout)
                    marks = list(range(interval*rollout, final, interval*rollout))
                    self.assertEqual(len(marks), 9)
                    for i, step in enumerate(marks, 1):
                        self.assertAlmostEqual(step/final, i/10, delta=.005)

    def test_binary_result(self):
        data = worker.packet(.5, 400, 1_000_000_000)
        self.assertEqual(len(data), 784)
        values = worker.RESULT.unpack(data)
        self.assertEqual(values[:4], (.5, 400, 1_000_000_000, 1))
        self.assertEqual((values[4], values[68], values[132]), (.5, 400, 1_000_000_000))

    def test_invalid_scores(self):
        for score in (float('nan'), float('inf'), -.1, 1.1):
            with self.assertRaises(ValueError):
                worker.packet(score, 4, 100)

    def test_evaluation_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory)/'weights.bin'
            checkpoint.write_bytes(b'test')
            report = dict(sha256=hashlib.sha256(b'test').hexdigest(), games=24,
                          games_per_team=8, seed=2, opponent='TaurosV0', mode='argmax',
                          protocol='native-private-v1', wins=11, draws=2, losses=11, score=.5)
            self.assertEqual(worker.validate(report, checkpoint, 8, 2), .5)
            for key, value in [('games', 23), ('sha256', 'bad'), ('seed', 3),
                               ('score', .8), ('wins', 12), ('opponent', 'heuristic')]:
                with self.subTest(key=key), self.assertRaises(ValueError):
                    worker.validate({**report, key:value}, checkpoint, 8, 2)

    def test_all_layouts_are_divisible(self):
        for agents in (2048, 4096):
            for horizon in (16, 32, 64):
                for minibatch in (1024, 2048, 4096):
                    for fraction in (.25, .5, .75):
                        learner_rows = agents-int(agents*fraction/2)
                        self.assertEqual(learner_rows % (minibatch//horizon), 0)

    def run_fake_trial(self, fail_snapshot=False, trainer_active=False):
        """Exercise the real worker/pipe/files with no training or GPU subprocesses."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trainer = root/'trainer'
            trainer.write_bytes(b'fake trainer')
            checkpoints = root/'checkpoints/pokemon/test_run'
            result_dir = root/'logs/pokemon/test_run.metamon'
            requested = 655360  # Exactly ten rollout epochs.
            destination, writer = os.pipe()
            settings = {'base.result_fd':str(writer), 'base.run_id':'test_run',
                        'base.log_dir':str(root/'logs'), 'base.checkpoint_dir':str(root/'checkpoints'),
                        'base.checkpoint_interval':'0', 'sweep.trainer_path':str(trainer),
                        'sweep.eval_python':'fake-python', 'sweep.eval_games':'8', 'sweep.eval_seed':'3',
                        'sweep.eval_timeout':'100', 'train.total_timesteps':str(requested),
                        'vec.total_agents':'2048', 'train.horizon':'32'}
            evaluator_commands = []
            training = {'polls':0, 'active':False, 'snapshots_during_training':0, 'evaluators':0}

            def report(command):
                checkpoint = Path(command[2])
                output = Path(command[command.index('--output')+1])
                output.mkdir(parents=True)
                summary = dict(sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(), games=24,
                               games_per_team=8, seed=3, opponent='TaurosV0', mode='argmax',
                               protocol='native-private-v1', wins=12, draws=0, losses=12, score=.5)
                (output/'summary.json').write_text(json.dumps(summary))

            def popen(command, **kwargs):
                process = mock.Mock()
                process.poll.return_value = 0
                process.wait.return_value = 0
                if command[1] == 'train':
                    args = dict(arg[2:].split('=', 1) for arg in command[2:])
                    self.assertEqual(args['base.checkpoint_interval'], '1')
                    self.assertEqual(args['train.total_timesteps'], str(requested))
                    checkpoints.mkdir(parents=True)
                    for step in range(0, requested+1, 65536):
                        checkpoint = checkpoints/f'{step:016d}.bin'
                        checkpoint.write_bytes(f'weights{step}'.encode())
                        sidecar = Path(str(checkpoint)+'.ini')
                        sidecar.write_text('[policy]\nhidden_size=1024\nnum_layers=2\n'
                                           '[env]\nlearner_team=421,366,389,395,512,445\nlearner_lead=121\n')
                        os.utime(sidecar, (time.time()-5, time.time()-5))
                    os.write(int(args['base.result_fd']), worker.packet(.1, 1, requested))
                    if trainer_active:
                        def training_poll():
                            training['polls'] += 1
                            training['active'] = training['polls'] <= 30
                            return None if training['active'] else 0
                        process.poll.side_effect = training_poll
                else:
                    self.assertEqual(training['evaluators'], 0)
                    training['evaluators'] += 1
                    training['snapshots_during_training'] += training['active']
                    evaluator_commands.append(command)
                    report(command)
                    if fail_snapshot and len(evaluator_commands) == 1:
                        process.poll.return_value = 1
                        process.wait.return_value = 1
                    code = process.wait.return_value
                    def evaluated():
                        training['evaluators'] -= 1
                        return code
                    process.wait.side_effect = evaluated
                return process

            def final_run(command, **kwargs):
                report(command)

            try:
                argv = ['sweep_metamon.py', 'train']+[f'--{key}={value}' for key,value in settings.items()]
                with mock.patch.object(worker.sys, 'argv', argv), \
                        mock.patch.object(worker.subprocess, 'Popen', side_effect=popen), \
                        mock.patch.object(worker.subprocess, 'run', side_effect=final_run), \
                        mock.patch.object(worker.time, 'sleep'), \
                        mock.patch.object(worker.os, 'kill') as kill, \
                        mock.patch.object(worker.os, 'killpg') as killpg, \
                        contextlib.redirect_stdout(io.StringIO()):
                    worker.main()
                kill.assert_not_called()
                killpg.assert_not_called()
                packet = worker.RESULT.unpack(os.read(destination, worker.RESULT.size))
                self.assertEqual(packet[0], .5)  # Only final evaluation feeds PROTEIN.
                self.assertEqual(packet[3], 1)
                self.assertEqual(len(evaluator_commands), 9)
                if trainer_active:
                    self.assertEqual(training['snapshots_during_training'], 9)
                status = json.loads((result_dir/'snapshot_evaluations.json').read_text())
                self.assertEqual(len(status['results']), 9)
                self.assertEqual(sum(row['status']=='failed' for row in status['results']), int(fail_snapshot))
                self.assertIsNone(status['active'])
                frozen = result_dir/'snapshots/0000000000065536/input/weights.bin'
                self.assertEqual(frozen.stat().st_ino, (checkpoints/'0000000000065536.bin').stat().st_ino)
                self.assertTrue((frozen.parent/'config.ini').exists())
                self.assertTrue((result_dir/'trial.json').exists())
            finally:
                os.close(destination)
                os.close(writer)

    def test_complete_periodic_worker(self):
        self.run_fake_trial()

    def test_periodic_failure_does_not_fail_trial(self):
        self.run_fake_trial(fail_snapshot=True)

    def test_snapshots_run_alongside_training_one_at_a_time(self):
        self.run_fake_trial(trainer_active=True)

if __name__ == '__main__':
    unittest.main()
