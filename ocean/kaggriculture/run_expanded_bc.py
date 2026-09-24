#!/usr/bin/env python3
"""Bounded, additive 2/2 BC comparison; never edits the active PPO config.

Run with uv from the repository root. Inputs are a frozen profile and portable
parity-checked tape manifest. Standalone evaluations are diagnostics, not a
selection gate for BC -> PPO. No automatic model promotion or long PPO run.
"""
import argparse
import configparser
import hashlib
import json
import pathlib
import subprocess
import sys
import time


def frozen_args(profile):
    config = configparser.ConfigParser()
    config.read(profile)
    sections = ('base', 'policy', 'vec', 'selfplay', 'env', 'train')
    return [f'{section}.{key}={value}' for section in sections if section in config
            for key, value in config[section].items()]


def run(command, logfile):
    print('RUN', ' '.join(map(str, command)), flush=True)
    with logfile.open('x') as log:
        process = subprocess.Popen(list(map(str, command)), stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
        try:
            for line in process.stdout:
                log.write(line)
                log.flush()
                print(line, end='', flush=True)
            if process.wait():
                raise RuntimeError(f'command failed; see {logfile}')
        except BaseException:
            if process.poll() is None:
                process.terminate()
                process.wait()
            raise
        finally:
            process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=pathlib.Path, required=True)
    parser.add_argument('--profile', type=pathlib.Path, required=True)
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--ppo-checkpoint', type=pathlib.Path, required=True)
    parser.add_argument('--epochs', type=int, default=40)
    args = parser.parse_args()
    if not 1 <= args.epochs <= 100:
        parser.error('bounded experiment requires 1..100 epochs')
    for name in ('puffer', 'kag_bc'):
        if subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 0:
            parser.error(f'{name} is already running; refusing GPU contention')
    manifest = json.loads(args.manifest.read_text())
    entries = manifest['cache']
    if len(entries) < 400 or {e['split'] for e in entries} != {'train', 'holdout'}:
        parser.error('expanded comparison needs >=400 verified games and an episode holdout')
    config = configparser.ConfigParser(); config.read(args.profile)
    if (config.getint('policy', 'hidden_size'), config.getint('policy', 'num_layers'),
        config.getint('env', 'macro_mode'), config.getint('env', 'macro_executor_version')) != (256, 2, 2, 2):
        parser.error('this experiment is H256/L2, macro2/executor2 only')
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'profile.ini').write_bytes(args.profile.read_bytes())
    (args.output / 'manifest.json').write_bytes(args.manifest.read_bytes())
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
              (args.profile, args.manifest, args.ppo_checkpoint,
               args.build / 'kag_bc', args.build / 'libbc_replay.so', pathlib.Path('puffer'))}
    (args.output / 'input_hashes.json').write_text(json.dumps(hashes, indent=2))
    data = args.output / 'expanded.bc'
    run([sys.executable, 'ocean/kaggriculture/build_entity_bc_dataset.py',
         '--manifest', args.manifest, '--profile', args.profile,
         '--lib', args.build / 'libbc_replay.so', '--teacher', 'Majkel1337',
         '--output', data], args.output / 'dataset.log')
    trainer = str(args.build / 'kag_bc')
    common = [f'bc.profile={args.profile}', f'bc.data={data}', 'bc.mode=train',
              'bc.batch=1', 'bc.seed=7', 'bc.value_coef=0', 'bc.anchor_l2=0',
              'bc.report_interval=5', 'bc.opening_steps=24', 'bc.detailed_stats=1',
              'bc.macro_class_balance=0', 'bc.opening_argmax_coef=0']
    run([trainer, *common, 'bc.verify_only=1'], args.output / 'preflight.log')
    variants = [('actor_uniform', 'None', '0.003', '1'),
                ('actor_opening', 'None', '0.003', '16'),
                ('ppo_opening', str(args.ppo_checkpoint), '0.00005', '16')]
    for name, initial, lr, weight in variants:
        model = args.output / f'{name}.bin'
        run([trainer, *common, 'bc.verify_only=0', f'bc.epochs={args.epochs}',
             f'bc.learning_rate={lr}', f'bc.opening_weight={weight}',
             f'bc.root_weight={weight}', f'bc.load_model_path={initial}',
             f'bc.output={model}'], args.output / f'{name}.log')
        # Common reset-free rules opponents and both seats, not the changing
        # training self-play mixture. Preserve weak clones for downstream PPO.
        for seat in (0, 1):
            run(['./puffer', 'eval_bot', 'kaggriculture', *frozen_args(args.profile),
                 f'base.load_model_path={model}', f'base.run_id=bc_expanded_{name}_seat{seat}',
                 'base.result_fd=0', 'base.num_games=32', 'base.eval_agents=32',
                 'base.eval_deterministic=1', 'base.seed=5', 'env.seed=707',
                 f'env.bot_first={seat}', 'env.reset_state_prob=0', 'env.reset_opening_prob=0',
                 'env.bot_opponent_fraction=1', 'env.bot_pass_fraction=0',
                 'env.bot_top_fraction=0', 'env.bot_rules_fraction=1',
                 'env.bot_script_fraction=0', 'env.bot_adaptive_fraction=0'],
                args.output / f'{name}_eval_seat{seat}.log')
    (args.output / 'COMPLETED.json').write_text(json.dumps(dict(
        finished_at=time.time(), games=len(entries), variants=[v[0] for v in variants],
        note='BC candidates only. Downstream BC -> eMAG/PPO comparison still required; no promotion.'), indent=2))


if __name__ == '__main__':
    main()
