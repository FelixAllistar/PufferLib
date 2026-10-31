"""Replay pages (env.trainer_replay_dir) -> replay files for the Gen1Recomp mod in this folder (2026-10-31).

Our replay pages hold each battle as Pokemon Showdown protocol text (written by bridge.zig). The mod plays a battle
back through Gen1Recomp's own battle screen, so this turns the text into plain events per turn:
  {"p1": name, "p2": name, "tier": ..., "teams": [[mon...], [mon...]], "turns": [[event...], ...], "winner": 0|1|2}
A mon is {"species", "level", "maxhp"} in order of first send-out (the lead first). Events name sides 1 and 2 (the
active Pokemon of that side): move, damage, heal, status, cure, boost, faint, switch, cant, miss, crit, super, resist,
immune, ohko, fail, start, end, activate, prepare, hitcount, msg. Species and moves keep the engine's names; the mod
matches them to the game's ids.
Usage: python3 convert.py [--p1-name NAME] REPLAY.html... OUT_DIR (writes OUT_DIR/<name>.json and OUT_DIR/index.json)
"""
import json
import pathlib
import re
import sys

STATUS = {'slp': 'SLP', 'psn': 'PSN', 'tox': 'PSN', 'brn': 'BRN', 'frz': 'FRZ', 'par': 'PAR'}


def side(ident):
    return int(ident[1])


def hp(field):
    parts = field.split()
    cur, mx = parts[0].split('/')
    status = parts[1] if len(parts) > 1 else ''
    return int(cur), int(mx), STATUS.get(status), status == 'fnt'


def convert(text):
    log = re.search(r'class="battle-log-data">(.*?)</script>', text, re.S).group(1)
    names, tier, teams, turns, winner = {}, '', [[], []], [[]], 0
    for line in log.splitlines():
        f = line.split('|')[1:]
        if not f or not f[0]:
            continue
        kind, a = f[0], f[1:]
        cur = turns[-1]
        if kind == 'player':
            names[int(a[0][1])] = a[1]
        elif kind == 'tier':
            tier = a[0]
        elif kind == 'turn':
            turns.append([])
        elif kind == 'switch':
            s = side(a[0])
            species, level = a[1].split(', L')
            h, mx, st, _ = hp(a[2])
            key = (species, int(level), mx)
            # one party entry per distinct (species, level, max HP) send-out sequence: a mon switching back in keeps its
            # entry; a second copy (Koga's two Koffing) gets its own once the first has fainted
            entry = next((i for i, m in enumerate(teams[s - 1]) if (m['species'], m['level'], m['maxhp']) == key
                          and not m.get('_fainted')), None)
            if entry is None:
                teams[s - 1].append({'species': species, 'level': int(level), 'maxhp': mx})
                entry = len(teams[s - 1]) - 1
            cur.append({'t': 'switch', 'side': s, 'slot': entry + 1, 'species': species, 'level': int(level), 'hp': h,
                        'maxhp': mx, 'status': st})
            for m in teams[s - 1]:
                m['_active'] = False
            teams[s - 1][entry]['_active'] = True
        elif kind == 'move':
            e = {'t': 'move', 'side': side(a[0]), 'move': a[1]}
            if any(x.startswith('[from]') for x in a[2:]):
                e['from'] = True
            cur.append(e)
        elif kind in ('-damage', '-heal'):
            h, mx, st, fnt = hp(a[1])
            e = {'t': kind[1:], 'side': side(a[0]), 'hp': h, 'maxhp': mx, 'status': st}
            frm = next((x[7:] for x in a[2:] if x.startswith('[from] ')), None)
            if frm:
                e['from'] = frm
            if '[silent]' in a[2:]:
                e['silent'] = True
            cur.append(e)
        elif kind == '-status':
            cur.append({'t': 'status', 'side': side(a[0]), 'status': STATUS.get(a[1]), 'tox': a[1] == 'tox',
                        'silent': '[silent]' in a[2:]})
        elif kind == '-curestatus':
            cur.append({'t': 'cure', 'side': side(a[0]), 'status': STATUS.get(a[1]), 'silent': '[silent]' in a[2:]})
        elif kind in ('-boost', '-unboost'):
            n = int(a[2]) * (1 if kind == '-boost' else -1)
            cur.append({'t': 'boost', 'side': side(a[0]), 'stat': a[1], 'n': n})
        elif kind == 'faint':
            s = side(a[0])
            for m in teams[s - 1]:
                if m.get('_active'):
                    m['_fainted'] = True
            cur.append({'t': 'faint', 'side': s})
        elif kind == 'cant':
            cur.append({'t': 'cant', 'side': side(a[0]), 'why': a[1], 'move': a[2] if len(a) > 2 else None})
        elif kind in ('-miss', '-crit', '-supereffective', '-resisted', '-immune', '-fail', '-mustrecharge'):
            t = {'-miss': 'miss', '-crit': 'crit', '-supereffective': 'super', '-resisted': 'resist',
                 '-immune': 'immune', '-fail': 'fail', '-mustrecharge': 'recharge'}[kind]
            cur.append({'t': t, 'side': side(a[0])})
        elif kind == '-ohko':
            cur.append({'t': 'ohko'})
        elif kind in ('-start', '-end', '-activate'):
            cur.append({'t': kind[1:], 'side': side(a[0]), 'what': a[1].replace('move: ', ''),
                        'move': a[2] if len(a) > 2 and not a[2].startswith('[') else None,
                        'silent': '[silent]' in a[2:]})
        elif kind == '-prepare':
            cur.append({'t': 'prepare', 'side': side(a[0]), 'move': a[1]})
        elif kind == '-hitcount':
            cur.append({'t': 'hitcount', 'n': int(a[1])})
        elif kind == '-transform':
            cur.append({'t': 'transform', 'side': side(a[0]), 'target': side(a[1])})
        elif kind == '-clearallboost':
            cur.append({'t': 'clearboosts'})
        elif kind == '-message':
            cur.append({'t': 'msg', 'text': a[0]})
        if kind in ('-unboost', '-boost') and len(cur) >= 2 and cur[-2].get('text') == 'The trainer used an item!' \
                and (kind == '-unboost' or a[1] == 'atk' and len(cur) >= 3 and cur[-3].get('t') == 'damage'
                     and cur[-3]['side'] == side(a[0])):
            del cur[-2]  # replay pages from before the bridge fix: a drop or a Rage boost is not an item
        elif kind == 'win':
            winner = 1 if a[0] == names.get(1) else 2
        elif kind == 'tie':
            winner = 0
    for team in teams:
        for m in team:
            m.pop('_active', None)
            m.pop('_fainted', None)
    return {'p1': names.get(1, 'P1'), 'p2': names.get(2, 'P2'), 'tier': tier, 'teams': teams, 'turns': turns,
            'winner': winner}


def pretty(name, override=None):
    # trainers_names.txt style "BRUNO 1" -> "BRUNO"; the rival is BLUE in Red; "Player" is the AI challenge's side
    if override:
        return override
    name = re.sub(r'^RIVAL\d (\d+)$', 'BLUE', name)
    return re.sub(r' 1$', '', name)


if __name__ == '__main__':
    args = sys.argv[1:]
    p1_name = None
    if '--p1-name' in args:
        i = args.index('--p1-name')
        p1_name = args[i + 1]
        del args[i:i + 2]
    *pages, out = args
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    index = []
    for page in pages:
        replay = convert(pathlib.Path(page).read_text())
        replay['p1'] = pretty(replay['p1'], p1_name if replay['p1'] == 'Player' else None)
        replay['p2'] = pretty(replay['p2'])
        name = pathlib.Path(page).stem
        (out / f'{name}.json').write_text(json.dumps(replay, separators=(',', ':')))
        index.append({'file': f'{name}.json', 'title': f"{replay['p1']} vs {replay['p2']}", 'turns': len(replay['turns'])})
    (out / 'index.json').write_text(json.dumps(index, indent=1))
    print(f'{len(index)} replays -> {out}')
