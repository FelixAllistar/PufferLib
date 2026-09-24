#!/usr/bin/env python3
"""Remote tmux: critic qualification and five matched ~20M PPO experiments.

PPO inherits the terminal directly: its normal interactive dashboard is kept.
tmux pipe-pane records output without turning the trainer's stdout into a pipe.
"""
import argparse
import configparser
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys
import time

from run_expanded_bc import frozen_args, run
from verify_critic_checkpoint import verify


def critic_gate(log):
    rows = re.findall(r'CRITIC_GATE val_rmse=(\S+) constant_train_mean_rmse=(\S+) val_ev=(\S+)',
                      pathlib.Path(log).read_text())
    if not rows:
        raise ValueError('missing held-out critic diagnostics')
    rmse, constant, ev = map(float, rows[-1])
    if not (0 <= rmse < constant and ev > 0):
        raise ValueError(f'critic failed held-out constant-baseline gate: {rows[-1]}')
    return dict(val_rmse=rmse, constant_rmse=constant, explained_variance=ev)


def training_args(profile, model, reference, opponent, run_id, emag, steps, agents=2048):
    return ['./puffer', 'train', 'kaggriculture', *frozen_args(profile),
            f'base.load_model_path={model}', 'base.load_enemy_model_path=None',
            f'base.run_id={run_id}', 'base.result_fd=0', 'base.checkpoint_interval=2',
            'base.eval_epoch_mult=0', 'base.perf_log=1', 'base.async=0',
            'base.seed=5', 'train.seed=42', f'vec.total_agents={agents}',
            'train.horizon=720', 'train.minibatch_size=1440',
            f'train.total_timesteps={steps}', 'train.learning_rate=0.0003',
            'train.anneal_lr=0', 'train.anneal_ent_coef=0',
            'env.reset_state_prob=0.8', 'selfplay.enabled=1',
            f'selfplay.opponent_pool={opponent}', 'selfplay.opponent_pool_prob=1',
            'selfplay.opponent_league=None', 'selfplay.opponent_pool_weights=None',
            'selfplay.eval_pool_size=0', 'selfplay.snapshot_interval=0',
            f'train.emag_kl_coef={0.01 if emag else 0}', 'train.emag_tau=0',
            'train.emag_cutoff=0.134', f'selfplay.magnet_path={reference if emag else "None"}']


def prepare_critics(args, common, phase):
    phase('PREP 1/3 | Fit BC critic ONLY | actor + encoder frozen')
    bc_critic = args.output / 'bc_critic.bin'
    log = args.output / 'bc_critic.log'
    run([*common, 'bc.mode=critic', 'bc.epochs=20', f'bc.load_model_path={args.bc}',
         f'bc.output={bc_critic}'], log)
    check = verify(args.bc, bc_critic, log, args.data)
    check.update(critic_gate(log))
    (args.output / 'bc_critic_checks.json').write_text(json.dumps(check, indent=2))
    phase('PREP 2/3 | Fit RAW actor critic ONLY | no behavioral cloning')
    raw = args.output / 'raw_seed42.bin'
    run([*common, 'bc.mode=train', 'bc.epochs=0', 'bc.load_model_path=None',
         f'bc.output={raw}'], args.output / 'raw_initialization.log')
    raw_critic = args.output / 'raw_critic.bin'
    log = args.output / 'raw_critic.log'
    run([*common, 'bc.mode=critic', 'bc.epochs=20', f'bc.load_model_path={raw}',
         f'bc.output={raw_critic}'], log)
    check = verify(raw, raw_critic, log, args.data)
    check.update(critic_gate(log))
    (args.output / 'raw_critic_checks.json').write_text(json.dumps(check, indent=2))
    return bc_critic, raw_critic


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--profile', type=pathlib.Path, required=True)
    parser.add_argument('--data', type=pathlib.Path, required=True)
    parser.add_argument('--bc', type=pathlib.Path, required=True)
    parser.add_argument('--opponent', type=pathlib.Path, required=True)
    parser.add_argument('--trainer', type=pathlib.Path, required=True)
    parser.add_argument('--prepared', type=pathlib.Path,
                        help='JSON with bc_critic, bc_log, raw, raw_critic, raw_log; all revalidated')
    args = parser.parse_args()
    pane = os.environ.get('TMUX_PANE')
    if not pane or not os.isatty(1):
        parser.error('run directly inside remote tmux, WITHOUT piping stdout to tee')
    for name in ('puffer', 'kag_bc'):
        if subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 0:
            parser.error(f'{name} is already running')
    args.output.mkdir(parents=True, exist_ok=False)
    profile = args.output / 'profile.ini'
    profile.write_bytes(args.profile.read_bytes())
    config = configparser.ConfigParser(); config.read(profile)
    assert config.getint('policy', 'hidden_size') == 256 and config.getint('policy', 'num_layers') == 2
    tag = args.output.name
    window = subprocess.check_output(['tmux', 'display-message', '-p', '-t', pane,
                                      '#{session_name}:#{window_index}'], text=True).strip()
    subprocess.run(['tmux', 'set-option', '-w', '-t', window, 'pane-border-status', 'top'], check=True)
    subprocess.run(['tmux', 'set-option', '-w', '-t', window, 'pane-border-format', '#{pane_title}'], check=True)
    subprocess.run(['tmux', 'set-option', '-w', '-t', window, 'automatic-rename', 'off'], check=True)

    def phase(label):
        subprocess.run(['tmux', 'select-pane', '-t', pane, '-T', label], check=True)
        status = dict(phase=label, updated=time.time(), output=str(args.output))
        (args.output / 'status.json').write_text(json.dumps(status, indent=2))
        print('\n' + label + '\n', flush=True)

    def interactive(command, logfile):
        with (args.output / 'commands.jsonl').open('a') as stream:
            stream.write(json.dumps(command) + '\n')
        subprocess.run(['tmux', 'pipe-pane', '-t', pane,
                        'tee -a ' + shlex.quote(str(logfile.resolve())) + ' >/dev/null'], check=True)
        try:
            subprocess.run(list(map(str, command)), check=True)
        finally:
            subprocess.run(['tmux', 'pipe-pane', '-t', pane], check=True)

    common = [str(args.trainer), f'bc.profile={profile}', f'bc.data={args.data}',
              'bc.verify_only=0', 'bc.batch=1', 'bc.seed=42', 'bc.report_interval=2',
              'bc.detailed_stats=0', 'bc.anchor_l2=0', 'bc.value_coef=1',
              'bc.learning_rate=0.003']
    try:
        if args.prepared:
            phase('PREP | Revalidate saved critic fits and actor parity')
            saved = json.loads(args.prepared.read_text())
            bc_critic, raw_critic = map(pathlib.Path, (saved['bc_critic'], saved['raw_critic']))
            for name, before, after, log in (
                ('bc', args.bc, bc_critic, saved['bc_log']),
                ('raw', saved['raw'], raw_critic, saved['raw_log'])):
                check = verify(before, after, log, args.data)
                check.update(critic_gate(log))
                (args.output / f'{name}_critic_checks.json').write_text(json.dumps(check, indent=2))
            (args.output / 'prepared.json').write_text(json.dumps(saved, indent=2))
        else:
            bc_critic, raw_critic = prepare_critics(args, common, phase)

        phase('PREP 3/3 | Full-size eMAG memory/load check | 2 updates; not a candidate')
        interactive(training_args(profile, args.bc, args.bc, args.opponent,
                    tag + '_emag_smoke', True, 2949120), args.output / 'emag_smoke.console.log')

        branches = [('A_BC_PPO', args.bc, False), ('B_BC_EMAG_PPO', args.bc, True),
                    ('C_BC_CRITIC_PPO', bc_critic, False), ('D_BC_CRITIC_EMAG_PPO', bc_critic, True),
                    ('E_RAW_CRITIC_PPO', raw_critic, False)]
        steps = 20643840  # exactly 14 complete 2048-agent x 720-step updates
        manifest = dict(steps_per_run=steps, training_resets=0.8, learning_rate=0.0003,
                        annealing=False, seed=42, critic_only=True, emag_reference=str(args.bc),
                        fixed_opponent=str(args.opponent), branches=[x[0] for x in branches])
        (args.output / 'experiment.json').write_text(json.dumps(manifest, indent=2))
        scores = []
        for index, (name, initial, emag) in enumerate(branches, 1):
            phase(f'{index}/5 {name} | 20.64M steps | resets=.8 | eMAG={"ON" if emag else "OFF"}')
            run_id = tag + '_' + name
            interactive(training_args(profile, initial, args.bc, args.opponent, run_id, emag, steps),
                        args.output / f'{name}.console.log')
            model = pathlib.Path('checkpoints/kaggriculture') / run_id / f'{steps:016d}.bin'
            if not model.exists():
                raise RuntimeError(f'expected final checkpoint missing: {model}')
            for seat in (0, 1):
                phase(f'EVAL {index}/5 {name} | 32 reset-free games | seat={seat}')
                log = args.output / f'{name}_eval_seat{seat}.log'
                # Evaluation also retains a real terminal; metrics are read from the tmux capture.
                interactive(['./puffer', 'eval_bot', 'kaggriculture', *frozen_args(profile),
                    f'base.load_model_path={model}', f'base.run_id={run_id}_eval{seat}',
                    'base.result_fd=0', 'base.num_games=32', 'base.eval_agents=32',
                    'base.eval_deterministic=1', 'base.seed=5', 'env.seed=707',
                    f'env.bot_first={seat}', 'env.reset_state_prob=0', 'env.reset_opening_prob=0',
                    'env.bot_opponent_fraction=1', 'env.bot_pass_fraction=0',
                    'env.bot_top_fraction=0', 'env.bot_rules_fraction=1',
                    'env.bot_script_fraction=0', 'env.bot_adaptive_fraction=0'], log)
            scores.append(dict(branch=name, checkpoint=str(model)))
            (args.output / 'finished_runs.json').write_text(json.dumps(scores, indent=2))
        phase('DONE | A B C D E completed | nothing promoted; defaults unchanged')
        (args.output / 'COMPLETED.json').write_text(json.dumps(scores, indent=2))
    except BaseException as error:
        phase('STOPPED | inspect FAILED.json; no automatic retries or config changes')
        (args.output / 'FAILED.json').write_text(json.dumps(dict(error=repr(error)), indent=2))
        raise


if __name__ == '__main__':
    main()
