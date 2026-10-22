#!/usr/bin/env python3
"""Prepare isolated cross-size experiment on Vast; never overwrite an existing setup."""
import configparser
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('/workspace/PufferLib')
EXP = ROOT / 'reports/pokemon_experiments/20261018_capacity1024'
LEAGUE = ROOT / 'leagues/pokemon/rr50m_bf16_20261017'
IDS = ['20355adf07e2b9e9e26e', 'd4de472c51ebaae068e8', 'b164fd987357fcd3be44',
       '74675c24f0b4cf58355a', 'f8669fe1c76b969120fe', '79fab6102ee3aff72e6e',
       '8f6bd0702e5b10d5ff01', '61b6eed5cd98ad226c2f']

def main():
    manifest = json.loads((LEAGUE / 'evaluations/metamon-leaders-20/manifest.json').read_text())
    candidates = next(value for value in manifest.values()
                      if isinstance(value, list) and value and isinstance(value[0], dict)
                      and 'checkpoint' in value[0])
    selected = [next(row for row in candidates if row['id'] == key) for key in IDS]
    assert not (EXP / 'config').exists(), 'Setup already exists'
    native = configparser.ConfigParser(interpolation=None)
    native['native'] = dict(version='3', banks=str(len(selected)), hidden_size='64', num_layers='2',
                            rules_sha='3b71ba8b20aad4803f0238df5eb9a4a9f5285a119b00cd953b45901e9f3481d6',
                            opponents=str(EXP / 'opponents.txt'))
    for index, row in enumerate(selected):
        checkpoint = Path(row['checkpoint'])
        assert hashlib.sha256(checkpoint.read_bytes()).hexdigest() == row['sha256']
        assert hashlib.sha256(checkpoint.with_name('config.ini').read_bytes()).hexdigest() == row['config_sha256']
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.read(checkpoint.with_name('config.ini'))
        assert (cfg['policy']['hidden_size'], cfg['policy']['num_layers']) == ('64', '2')
        native[f'bank.{index}'] = dict(name=f'{row["policy"]}_{row["id"]}', path=str(checkpoint),
                                      team=cfg['env']['learner_team'], lead=cfg['env']['learner_lead'])
    (EXP / 'config').mkdir()
    shutil.copyfile(ROOT / 'config/default.ini', EXP / 'config/default.ini')
    shutil.copyfile(EXP / 'pokemon.ini', EXP / 'config/pokemon.ini')
    with (EXP / 'opponents.ini').open('w') as stream:
        native.write(stream)
    (EXP / 'opponents.txt').write_text(''.join(row['checkpoint']+'\n' for row in selected))
    (EXP / 'pool_manifest.json').write_text(json.dumps(selected, indent=2)+'\n')
    for name in ('ocean', 'build', 'saved', 'runs', 'leagues', 'reports', 'resources'):
        if (ROOT / name).exists():
            (EXP / name).symlink_to(ROOT / name, target_is_directory=True)
    print(f'Prepared {EXP}: {len(selected)} frozen H64 L2 opponents')

if __name__ == '__main__':
    main()
