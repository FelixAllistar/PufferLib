"""Continue the completed terminal-only trial in remote tmux, preserving its settings."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

source = Path('qualification/critic_matrix_20260923/terminal_only_v1')
output = Path('qualification/critic_matrix_20260923/terminal_continue_300m_v1')
run_id = 'terminal_continue_300m_v1'


def main():
    pane = os.environ.get('TMUX_PANE')
    if not pane or not os.isatty(1):
        raise RuntimeError('Requires a real tmux terminal')
    for name in ('puffer', 'kag_bc'):
        if subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError(f'{name} already running')
    completed = json.loads((source / 'COMPLETED.json').read_text())
    checkpoint = Path(completed['checkpoint'])
    if not checkpoint.is_file():
        raise RuntimeError('Missing completed checkpoint')
    commands = [json.loads(line) for line in (source / 'commands.jsonl').read_text().splitlines()]
    trains = [c for c in commands if c[:3] == ['./puffer', 'train', 'kaggriculture']]
    assert len(trains) == 1
    output.mkdir(exist_ok=False)
    (output / 'active-config-before.ini').write_bytes(Path('config/kaggriculture.ini').read_bytes())
    overrides = {'base.load_model_path': str(checkpoint), 'base.run_id': run_id,
                 'train.total_timesteps': '300000000'}
    command = [x for x in trains[0] if x.split('=', 1)[0] not in overrides]
    command += [f'{k}={v}' for k, v in overrides.items()]
    fields = dict(x.split('=', 1) for x in command[3:])
    assert fields['env.reward_money_timing'] == '0'
    for key in ('reward_growth_land', 'reward_growth_crop', 'reward_growth_animal',
                'reward_alive_daily', 'reward_quality_scale', 'reward_pbrs_scale'):
        assert float(fields['env.' + key]) == 0
    (output / 'command.json').write_text(json.dumps(command, indent=2))
    (output / 'initialization.json').write_text(json.dumps(dict(checkpoint=str(checkpoint),
        prior_steps=completed['steps'], additional_requested_steps=300000000,
        optimizer_restored=False, overrides=overrides), indent=2))
    def status(label):
        subprocess.run(['tmux', 'select-pane', '-t', pane, '-T', label], check=True)
        (output / 'status.json').write_text(json.dumps(dict(phase=label, updated=time.time()), indent=2))
        print(label, flush=True)
    subprocess.run(['tmux', 'set-option', '-w', '-t', pane, 'pane-border-status', 'top'], check=True)
    subprocess.run(['tmux', 'set-option', '-w', '-t', pane, 'pane-border-format', '#{pane_title}'], check=True)
    status('TERMINAL ONLY | continue 20.64M checkpoint | +300M PPO | resets .8 | eMAG OFF')
    subprocess.run(['tmux', 'pipe-pane', '-t', pane,
        'tee -a ' + shlex.quote(str((output / 'train.console.log').resolve())) + ' >/dev/null'], check=True)
    try:
        subprocess.run(command, check=True)
        status('DONE | terminal-only +300M continuation; defaults unchanged')
    except BaseException as error:
        (output / 'FAILED.json').write_text(json.dumps(dict(error=repr(error))))
        status('STOPPED | terminal-only continuation: inspect FAILED.json')
        raise
    finally:
        subprocess.run(['tmux', 'pipe-pane', '-t', pane], check=True)


if __name__ == '__main__':
    main()
