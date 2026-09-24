#!/usr/bin/env python3
"""One remote, additive terminal-money-only BC+critic -> PPO experiment."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time

from run_expanded_bc import frozen_args, run
from run_critic_matrix import critic_gate, training_args
from verify_critic_checkpoint import verify

ROOT = Path('qualification/critic_matrix_20260923/terminal_only_v1')
SOURCE = Path('qualification/bc_expansion_20260923/profile.ini')
COMPARISON = Path('qualification/bc_expansion_20260923/remote_run.CPV5Ma/comparison')
BC = COMPARISON / 'actor_uniform.bin'
TRAINER = Path('ocean/kaggriculture/build/critic_matrix_20260923/kag_bc')
BRIDGE = Path('ocean/kaggriculture/build/bc_expansion_20260923/libbc_replay.so')
OPPONENT = Path('checkpoints/kaggriculture/1790058597151/0000000299335680.bin')
STEPS = 20643840
RUN_ID = 'terminal_only_v1_BC_CRITIC_PPO'


def terminal_profile(source):
    config = configparser.ConfigParser()
    config.read(source)
    for key in ('reward_growth_land', 'reward_growth_crop', 'reward_growth_animal',
                'reward_alive_daily', 'reward_quality_scale', 'reward_pbrs_scale',
                'reward_money_timing', 'curriculum_enabled'):
        config.set('env', key, '0')
    config.set('train', 'reward_clip', '0')
    return config


def main():
    pane = os.environ.get('TMUX_PANE')
    if not pane or not os.isatty(1):
        raise RuntimeError('run directly in remote tmux, without stdout piping')
    for name in ('puffer', 'kag_bc'):
        if subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError(f'{name} already running; refusing GPU contention')
    ROOT.mkdir(parents=True, exist_ok=False)
    profile = ROOT / 'profile.ini'
    with profile.open('x') as stream:
        terminal_profile(SOURCE).write(stream)
    (ROOT / 'active-config-before.ini').write_bytes(Path('config/kaggriculture.ini').read_bytes())
    paths = [SOURCE, BC, TRAINER, BRIDGE, Path('puffer'), COMPARISON / 'manifest.json']
    (ROOT / 'inputs.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                               for p in paths}, indent=2))
    subprocess.run(['tmux', 'set-option', '-w', '-t', pane, 'pane-border-status', 'top'], check=True)
    subprocess.run(['tmux', 'set-option', '-w', '-t', pane, 'pane-border-format', '#{pane_title}'], check=True)

    def phase(label):
        subprocess.run(['tmux', 'select-pane', '-t', pane, '-T', label], check=True)
        (ROOT / 'status.json').write_text(json.dumps(dict(phase=label, updated=time.time()), indent=2))
        print('\n' + label + '\n', flush=True)

    def interactive(command, log):
        with (ROOT / 'commands.jsonl').open('a') as stream:
            stream.write(json.dumps(list(map(str, command))) + '\n')
        subprocess.run(['tmux', 'pipe-pane', '-t', pane,
                        'tee -a ' + shlex.quote(str(log.resolve())) + ' >/dev/null'], check=True)
        try:
            subprocess.run(list(map(str, command)), check=True)
        finally:
            subprocess.run(['tmux', 'pipe-pane', '-t', pane], check=True)

    try:
        phase('TERMINAL ONLY | PREP 1/2: rebuild expert returns; same 518 games/split')
        data = ROOT / 'terminal.bc'
        run([sys.executable, 'ocean/kaggriculture/build_entity_bc_dataset.py',
             '--manifest', COMPARISON / 'manifest.json', '--profile', profile,
             '--lib', BRIDGE, '--teacher', 'Majkel1337', '--output', data], ROOT / 'dataset.log')
        phase('TERMINAL ONLY | PREP 2/2: fit value head; BC actor/encoder frozen')
        model = ROOT / 'bc_terminal_critic.bin'
        log = ROOT / 'critic.log'
        run([TRAINER, f'bc.profile={profile}', f'bc.data={data}', 'bc.mode=critic',
             'bc.verify_only=0', 'bc.batch=1', 'bc.seed=42', 'bc.report_interval=2',
             'bc.detailed_stats=0', 'bc.anchor_l2=0', 'bc.value_coef=1',
             'bc.learning_rate=0.003', 'bc.epochs=20', f'bc.load_model_path={BC}',
             f'bc.output={model}'], log)
        checks = verify(BC, model, log, data)
        checks.update(critic_gate(log))
        (ROOT / 'critic_checks.json').write_text(json.dumps(checks, indent=2))
        phase('TERMINAL ONLY | BC + critic -> PPO | 20.64M | resets=.8 | eMAG OFF')
        interactive(training_args(profile, model, BC, OPPONENT, RUN_ID, False, STEPS),
                    ROOT / 'train.console.log')
        final = Path('checkpoints/kaggriculture') / RUN_ID / f'{STEPS:016d}.bin'
        if not final.exists():
            raise RuntimeError(f'missing final checkpoint: {final}')
        for seat in (0, 1):
            phase(f'TERMINAL ONLY | EVAL: 32 reset-free rules games | seat={seat}')
            interactive(['./puffer', 'eval_bot', 'kaggriculture', *frozen_args(profile),
                f'base.load_model_path={final}', f'base.run_id={RUN_ID}_eval{seat}',
                'base.result_fd=0', 'base.num_games=32', 'base.eval_agents=32',
                'base.eval_deterministic=1', 'base.seed=5', 'env.seed=707',
                f'env.bot_first={seat}', 'env.reset_state_prob=0', 'env.reset_opening_prob=0',
                'env.bot_opponent_fraction=1', 'env.bot_pass_fraction=0',
                'env.bot_top_fraction=0', 'env.bot_rules_fraction=1',
                'env.bot_script_fraction=0', 'env.bot_adaptive_fraction=0'], ROOT / f'eval_seat{seat}.log')
        (ROOT / 'COMPLETED.json').write_text(json.dumps(dict(checkpoint=str(final), steps=STEPS), indent=2))
        phase('DONE | TERMINAL ONLY completed; nothing promoted; defaults untouched')
    except BaseException as error:
        (ROOT / 'FAILED.json').write_text(json.dumps(dict(error=repr(error)), indent=2))
        phase('STOPPED | TERMINAL ONLY: inspect FAILED.json')
        raise


if __name__ == '__main__':
    main()
