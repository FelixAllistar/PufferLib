"""Build an isolated mode-2/executor-2 submission; never submits or trains."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('checkpoint',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--config',type=Path,required=True);p.add_argument('--stochastic',action='store_true')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    meta={}
    for key in ('policy_version','obs_version','macro_mode','executor_version','hidden_size','num_layers','param_alignment','macro_decision_interval','macro_score_features'):
        meta[key]=int(Path(str(a.checkpoint)+'.'+key).read_text())
    assert tuple(meta[k] for k in ('policy_version','obs_version','macro_mode','executor_version','macro_decision_interval','macro_score_features'))==(5,3,2,2,1,0),meta
    c=configparser.ConfigParser(interpolation=None);c.read(a.config)
    assert c.getint('env','land_buy_min_days')==0
    assert c.getint('env','policy_max_hands')==16 and c.getint('env','policy_market_slots')==10
    defaults={'episode_steps':720,'board_size':10,'starting_money':3000,'max_market_orders_per_turn':10,'turns_per_day':24,'shed_capacity':100,'town_shop_unlock_interval':3,'town_shop_sell_interval':4,'town_center_sell_interval':24,'farm_hand_cost_mult':1}
    assert all(c.getint('env',k)==v for k,v in defaults.items())
    assert c.getint('policy','hidden_size')==meta['hidden_size'] and c.getint('policy','num_layers')==meta['num_layers']
    meta['observation_version']=meta.pop('obs_version');meta['macro_executor_version']=meta.pop('executor_version')
    meta['deterministic']=not a.stochastic
    meta['checkpoint_sha256']=hashlib.sha256(a.checkpoint.read_bytes()).hexdigest()
    meta['source_checkpoint']=str(a.checkpoint)
    (a.output/'policy_metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
    shutil.copyfile(HERE/'submission/entity_agent.py',a.output/'main.py')
    shutil.copyfile(a.checkpoint,a.output/'model.bin')
    subprocess.run(['gcc','-O2','-std=c17','-shared','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I'+str(REPO),'-I'+str(REPO/'src'),'-I'+str(REPO/'vendor'),'-I'+str(REPO/'raylib-5.5_linux_amd64/include'),
        str(HERE/'submission/entity_bridge.c'),'-lm','-o',str(a.output/'entity_bridge.so')],check=True)
    with tarfile.open(str(a.output)+'.tar.gz','x:gz') as tar:
        for name in ('main.py','model.bin','entity_bridge.so','policy_metadata.json'):tar.add(a.output/name,arcname=name)
    print('Built',str(a.output)+'.tar.gz','model SHA256',meta['checkpoint_sha256'])

if __name__=='__main__':main()
