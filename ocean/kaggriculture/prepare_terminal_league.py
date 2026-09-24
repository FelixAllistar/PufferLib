"""Remote PSRO selection + bot panel; prepare BC-only response, never start training."""
import configparser
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from run_expanded_bc import frozen_args, run
from run_critic_matrix import training_args

OUT = Path('qualification/critic_matrix_20260923/league_v2')
LEAGUE = Path('saved/kaggriculture_terminal_bc_league_v2')
OLD = Path('checkpoints/kaggriculture/1790058597151/0000000299335680.bin')
NEW = Path('checkpoints/kaggriculture/terminal_continue_300m_v1/0000000299335680.bin')
BC = Path('qualification/bc_expansion_20260923/remote_run.CPV5Ma/comparison/actor_uniform.bin')
PROFILE = Path('qualification/bc_expansion_20260923/profile.ini')


def main():
    pane = os.environ.get('TMUX_PANE')
    if not pane:
        raise RuntimeError('Run in remote tmux')
    for name in ('puffer', 'kag_bc'):
        if subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError(f'{name} already running')
    OUT.mkdir(exist_ok=False)
    LEAGUE.mkdir(exist_ok=False)
    active = Path('config/kaggriculture.ini')
    original = active.read_bytes()
    (OUT / 'active-config-before.ini').write_bytes(original)
    work = OUT / 'psro-config.ini'
    work.write_bytes(PROFILE.read_bytes())
    os.environ['KAG_PYTHON'] = sys.executable
    def phase(label):
        print('\n' + label, flush=True)
        subprocess.run(['tmux', 'select-pane', '-t', pane, '-T', label], check=True)
        (OUT / 'status.json').write_text(json.dumps(dict(phase=label, time=time.time()), indent=2))
    try:
        phase('LEAGUE | preserve and pin OLD 300M + NEW terminal-money final')
        records = []
        for name, source in [('old_300m', OLD), ('terminal_final', NEW)]:
            dest = LEAGUE / f'{name}.bin'
            run([sys.executable, 'ocean/kaggriculture/eval_observation_versions.py',
                 'copy', source, dest], OUT / f'copy_{name}.log')
            assert hashlib.sha256(source.read_bytes()).digest() == hashlib.sha256(dest.read_bytes()).digest()
            records.append((name, 'champion', '0.5', str(dest)))
        with (LEAGUE / 'manifest.tsv').open('x') as stream:
            writer = csv.writer(stream, delimiter='\t')
            writer.writerow(['policy', 'role', 'base_weight', 'source'])
            writer.writerows(records)
        phase('LEAGUE | PSRO screen 6 stages, shortlist 3, confirm support; no training')
        run(['bash', 'ocean/kaggriculture/psro.sh', 'iterate', '--run', NEW.parent,
             '--league', LEAGUE, '--config', work, '--output', 'logs/kaggriculture/terminal_bc_psro_v1',
             '--range', '0:100:6', '--shortlist', '3', '--prescreen-games', '32',
             '--games', '32', '--confirm-games', '64', '--gpu-agents', '64', '--jobs', '1',
             '--hidden-size', '256', '--num-layers', '2', '--max-admit', '2',
             '--max-league', '6', '--fixed', 'pass,rules,top', '--stochastic'], OUT / 'psro.log')
        rows = list(csv.DictReader((LEAGUE / 'learner.tsv').open(), delimiter='\t'))
        champion = Path(rows[-1]['active_checkpoint'])
        models = [('old_300m', LEAGUE / 'old_300m.bin'), ('selected_champion', champion)]
        if champion.read_bytes() != (LEAGUE / 'terminal_final.bin').read_bytes():
            models.append(('terminal_final', LEAGUE / 'terminal_final.bin'))
        summary = []
        for name, model in models:
            for bot in ('pass', 'rules', 'top', 'script', 'adaptive'):
                for deterministic in (0, 1):
                    for seat in (0, 1):
                        phase(f'BOT PANEL | {name} vs {bot} | deterministic={deterministic} seat={seat}')
                        logfile = OUT / f'{name}_{bot}_det{deterministic}_seat{seat}.log'
                        run(['./puffer', 'eval_bot', 'kaggriculture', *frozen_args(PROFILE),
                             f'base.load_model_path={model}', 'base.result_fd=0',
                             f'base.run_id=panel_v1_{name}_{bot}_{deterministic}_{seat}',
                             'base.num_games=32', 'base.eval_agents=32',
                             f'base.eval_deterministic={deterministic}', 'base.seed=5', 'env.seed=707',
                             f'env.bot_first={seat}', 'env.reset_state_prob=0', 'env.reset_opening_prob=0',
                             'env.bot_opponent_fraction=1', *[f'env.bot_{b}_fraction={int(b==bot)}'
                             for b in ('pass', 'rules', 'top', 'script', 'adaptive')]], logfile)
                        lines = [line for line in logfile.read_text().splitlines() if line.startswith('{"env/perf"')]
                        metrics = json.loads(lines[-1])
                        summary.append(dict(model=name, checkpoint=str(model), bot=bot,
                            deterministic=deterministic, seat=seat, money=metrics['env/money'],
                            opponent_money=metrics['env/opponent_money'], win_rate=metrics['env/win_rate'],
                            games=metrics['env/n']))
                        (OUT / 'bot_panel.json').write_text(json.dumps(summary, indent=2))
        phase('CONFIG | restore pre-terminal rewards; BC-only + league; NO training launch')
        config = configparser.ConfigParser(); config.read(PROFILE)
        overrides = dict(x.split('=', 1) for x in training_args(PROFILE, BC, BC, OLD,
                            'bc_only_shaped_league_v1', False, 300000000)[3:])
        overrides.update({'selfplay.opponent_pool': 'None', 'selfplay.opponent_league': str(LEAGUE / 'league.ini')})
        for field, value in overrides.items():
            section, key = field.split('.', 1)
            if not config.has_section(section): config.add_section(section)
            config.set(section, key, value)
        prepared = OUT / 'bc_only_shaped_league.ini'
        with prepared.open('x') as stream: config.write(stream)
        if active.read_bytes() != original:
            raise RuntimeError('Active config changed during evaluation; prepared config saved but not installed')
        active.write_bytes(prepared.read_bytes())
        (OUT / 'COMPLETED.json').write_text(json.dumps(dict(champion=str(champion),
            league=str(LEAGUE), config=str(active), training_started=False), indent=2))
        phase('DONE | PSRO + bot panel complete; BC-only shaped-reward league config ready')
    except BaseException as error:
        (OUT / 'FAILED.json').write_text(json.dumps(dict(error=repr(error)), indent=2))
        phase('STOPPED | inspect league_v2/FAILED.json; no training started')
        raise


if __name__ == '__main__':
    main()
