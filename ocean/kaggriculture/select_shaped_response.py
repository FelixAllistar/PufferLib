"""PSRO the shaped BC response and evaluate selected candidate; no PPO launch."""
import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from run_expanded_bc import run, frozen_args

OUT = Path('qualification/critic_matrix_20260923/shaped_psro_v1')
LEAGUE = Path('saved/kaggriculture_terminal_bc_league_v2')
PROFILE = Path('logs/kaggriculture/bc_only_shaped_league_v1.start.ini')


def main():
    pane = os.environ.get('TMUX_PANE')
    if not pane: raise RuntimeError('Run in tmux')
    for name in ('puffer', 'kag_bc'):
        if subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError(f'{name} already running')
    resume = sys.argv[1:] == ['--resume-panel']
    if sys.argv[1:] and not resume: raise ValueError('Only --resume-panel is supported')
    work = OUT / 'psro-config.ini'
    if resume:
        selection = json.loads((OUT / 'selection.json').read_text())
        before = (OUT / 'active-config-before.ini').read_bytes()
        if (OUT / 'FAILED.json').exists():
            (OUT / 'FAILED.json').rename(OUT / f'FAILED.{time.time_ns()}.json')
    else:
        OUT.mkdir(exist_ok=False)
        work.write_bytes(PROFILE.read_bytes())
        before = Path('config/kaggriculture.ini').read_bytes()
        (OUT / 'active-config-before.ini').write_bytes(before)
    os.environ['KAG_PYTHON'] = sys.executable
    def phase(label):
        print(label, flush=True)
        subprocess.run(['tmux', 'select-pane', '-t', pane, '-T', label], check=True)
        (OUT / 'status.json').write_text(json.dumps(dict(phase=label, time=time.time()), indent=2))
    try:
        phase('SHAPED RESPONSE | PSRO selection + confirmation | no training')
        if not resume:
            run(['bash', 'ocean/kaggriculture/psro.sh', 'iterate', '--run',
             'checkpoints/kaggriculture/bc_only_shaped_league_v1', '--league', LEAGUE,
             '--config', work, '--output', 'logs/kaggriculture/shaped_response_psro_v1',
             '--range', '0:100:6', '--shortlist', '3', '--prescreen-games', '32',
             '--games', '32', '--confirm-games', '64', '--gpu-agents', '64', '--jobs', '1',
             '--hidden-size', '256', '--num-layers', '2', '--max-admit', '2',
             '--max-league', '6', '--fixed', 'pass,rules,top', '--stochastic'], OUT / 'psro.log')
            with (LEAGUE / 'learner.tsv').open() as stream:
                selection = list(csv.DictReader(stream, delimiter='\t'))[-1]
        model = Path(selection['active_checkpoint'])
        (OUT / 'selection.json').write_text(json.dumps(selection, indent=2))
        scores = []
        for bot in ('pass', 'rules', 'top', 'script', 'adaptive'):
            for seat in (0, 1):
                phase(f'SHAPED RESPONSE | selected candidate vs {bot} | stochastic seat={seat}')
                log = OUT / f'{bot}_seat{seat}.log'
                if resume and log.exists():
                    lines = [x for x in log.read_text().splitlines() if x.startswith('{"env/perf"')]
                    if lines:
                        metrics = json.loads(lines[-1])
                        scores.append(dict(bot=bot, seat=seat, money=metrics['env/money'],
                                           win_rate=metrics['env/win_rate'], games=metrics['env/n']))
                        continue
                    log.rename(log.with_suffix(f'.failed.{time.time_ns()}.log'))
                # Native start.ini contains injected internal fields, not CLI options.
                options = [x for x in frozen_args(PROFILE) if not x.split('=', 1)[0].split('.', 1)[1].startswith('_')]
                run(['./puffer', 'eval_bot', 'kaggriculture', *options,
                     f'base.load_model_path={model}', 'base.result_fd=0',
                     f'base.run_id=shaped_psro_v1_{bot}_{seat}', 'base.num_games=32',
                     'base.eval_agents=32', 'base.eval_deterministic=0', 'base.seed=5',
                     'env.seed=707', f'env.bot_first={seat}', 'env.reset_state_prob=0',
                     'env.reset_opening_prob=0', 'env.bot_opponent_fraction=1',
                     *[f'env.bot_{b}_fraction={int(b==bot)}' for b in
                       ('pass', 'rules', 'top', 'script', 'adaptive')]], log)
                metrics = json.loads([x for x in log.read_text().splitlines() if x.startswith('{"env/perf"')][-1])
                scores.append(dict(bot=bot, seat=seat, money=metrics['env/money'],
                                   win_rate=metrics['env/win_rate'], games=metrics['env/n']))
                (OUT / 'bot_panel.json').write_text(json.dumps(scores, indent=2))
        previous = json.loads(Path('qualification/critic_matrix_20260923/league_v2/bot_panel.json').read_text())
        comparison = []
        for bot in ('pass', 'rules', 'top', 'script', 'adaptive'):
            old = [x for x in previous if x['model']=='selected_champion' and x['deterministic']==0 and x['bot']==bot]
            new = [x for x in scores if x['bot']==bot]
            comparison.append(dict(bot=bot,
                terminal_money=sum(x['money'] for x in old)/len(old),
                shaped_money=sum(x['money'] for x in new)/len(new),
                terminal_win_rate=sum(x['win_rate'] for x in old)/len(old),
                shaped_win_rate=sum(x['win_rate'] for x in new)/len(new)))
        (OUT / 'comparison.json').write_text(json.dumps(comparison, indent=2))
        (OUT / 'COMPLETED.json').write_text(json.dumps(dict(selection=selection,
            active_config_unchanged=Path('config/kaggriculture.ini').read_bytes()==before,
            training_started=False), indent=2))
        phase('DONE | shaped PSRO + comparison complete; no continuation started')
    except BaseException as error:
        (OUT / 'FAILED.json').write_text(json.dumps(dict(error=repr(error)), indent=2))
        phase('STOPPED | inspect shaped_psro_v1/FAILED.json')
        raise


if __name__ == '__main__': main()
