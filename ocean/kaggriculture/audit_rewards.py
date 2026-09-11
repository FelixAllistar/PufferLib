"""Print the resolved reward ledger without editing config or starting training."""
import argparse
import configparser
from pathlib import Path

def audit(path):
    c=configparser.ConfigParser(interpolation=None)
    c.read(Path(__file__).resolve().parents[2]/'config/default.ini')
    with Path(path).open() as f:c.read_file(f)
    def env(key,default=0):return c.getfloat('env',key,fallback=default)
    start=env('starting_money',3000)
    gamma=env('reward_potential_gamma')
    train_gamma=c.getfloat('train','gamma')
    terminal=env('reward_money_scale')+env('reward_progress_terminal_money_scale')
    print(f'Config on disk: {path} (not necessarily the running process config)')
    print(f'Terminal cash: ({env("reward_money_scale"):g} + {env("reward_progress_terminal_money_scale"):g}) * (cash - {start:g}) / {start:g}')
    print(f'Terminal win: {env("reward_progress_win_scale"):g}; draw: {env("reward_progress_win_scale")*.5:g}; loss: 0')
    print(f'Legacy potential scale={env("reward_potential_scale"):g}: RETAINS terminal net worth')
    print(f'Cash shaping scale={env("reward_cash_scale"):g}: RETAINS terminal cash contribution')
    print(f'Progress potential scale={env("reward_progress_scale"):g}: ZERO terminal successor; includes cash and daily hired-hand capital even with component multipliers zero')
    print(f'Maintenance scale product={env("reward_progress_scale")*env("reward_progress_maintenance_scale"):g}: action guidance, NOT telescoping')
    print(f'Expansion scale={env("reward_expansion_scale"):g}: bounded peak-achievement reward, cap <= {5*env("reward_expansion_scale"):g}; land target counts TOTAL plots')
    print(f'Phase scale={env("reward_phase_scale"):g}: fuzzy state reward, full-window cap <= {4*env("reward_phase_scale"):g}; NOT telescoping')
    print(f'Curriculum enabled={env("curriculum_enabled"):g}, success reward={env("curriculum_reward"):g}')
    if gamma<=0:print('WARNING: legacy undiscounted shaping mode; potential scale uses raw dollars.')
    elif abs(gamma-train_gamma)>1e-8:print('WARNING: shaping gamma differs from training gamma; cancellation guarantee does not apply.')
    if c.getfloat('train','reward_clip',fallback=0)!=0:print('WARNING: reward clipping can break telescoping/accounting relationships.')
    if terminal==0:print('NOTE: zero direct terminal cash does NOT disable legacy/cash potential objectives.')
    if env('reset_state_prob') or env('reset_opening_prob') or env('curriculum_enabled'):
        print('NOTE: non-root/curriculum episodes are not comparable to full root-start cash.')

if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('config',type=Path)
    audit(p.parse_args().config)
