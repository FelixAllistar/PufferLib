"""Every Pokemon Red/Blue trainer's party, built the way the cartridge builds it (2026-10-30).

Reads a pret/pokered checkout (https://github.com/pret/pokered) and writes trainers.json: for each trainer class and
each trainer of that class, the party with species (and its Pokedex number), level and the four moves the game gives
each Pokemon (names and move ids, the cartridge order that pkmn/engine's Move enum also uses).

The rules are those of engine/battle/read_trainer_party.asm (ReadTrainer) and engine/pokemon/evos_moves.asm
(WriteMonMoves):
- A Pokemon starts with its species' level-1 moves (base stats), then walks its learnset in order and learns every move
  with level <= its own level that it does not know yet: into the first empty slot, or, with four moves, by dropping
  the first move and appending (moves shift up). The walk stops at the first move above its level.
- Parties whose data starts with $FF list a level per Pokemon; otherwise one level for the whole party.
- Gym leaders (Brock 1 .. Giovanni 8, set by each gym script in wGymLeaderNo, which shares its address with
  wLoneAttackNo) replace the third move of one Pokemon (LoneMoves; its first byte is the 0-based party index). Only in
  their gym: the value is cleared on every map change, so Giovanni's Rocket Hideout and Silph Co. teams (trainers 1
  and 2 of his class) keep their learnset moves.
- The Elite Four replace the third move of their fifth Pokemon (TeamMoves). The champion rival replaces the third move
  of his first Pokemon with Sky Attack and of his sixth with a move chosen by his starter.
Enemy trainer DVs are fixed (attack 9, defense 8, speed 8, special 8) and stat experience is zero; those are written
too so the engine can compute stats exactly.

--level N rebuilds every party at level N (the level-N learnset moves, special moves kept): the fair-stats
tournament (2026-10-30), where every Pokemon is at the same level with Showdown-standard stats.
Usage: python3 ocean/pokemon/cartridge/extract_trainers.py POKERED_DIR OUT.json [--level N]
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
LEVEL = int(sys.argv[sys.argv.index('--level') + 1]) if '--level' in sys.argv else None


def read(path):
    return (root / path).read_text()


def constants(path, first=0):
    names, value = [], first
    for line in read(path).splitlines():
        m = re.match(r'\s*const(?:_skip)?\s*(\w+)?', line)
        if line.strip().startswith('const_def'):
            arg = re.findall(r'const_def\s+(\S+)', line)
            value = int(arg[0].replace('$', '0x'), 0) if arg else 0
            continue
        if line.strip().startswith('const_skip'):
            value += 1
            continue
        m = re.match(r'\s*(?:trainer_)?const\s+(\w+)', line)
        if m:
            names.append((m.group(1), value))
            value += 1
    return dict(names)


species_id = constants('constants/pokemon_constants.asm')
move_id = constants('constants/move_constants.asm')
trainer_id = {k: v for k, v in constants('constants/trainer_constants.asm').items() if k != 'NOBODY'}
species_name = {v: k for k, v in species_id.items()}

# Level-1 moves from base stats (file names are lowercase species names; read the dex constant inside instead).
base_moves = {}
dex_to_species = {}
dex_number = {}
for name, value in constants('constants/pokedex_constants.asm').items():
    dex_to_species[name] = name.removeprefix('DEX_')
    dex_number[name.removeprefix('DEX_')] = value
for f in sorted((root / 'data/pokemon/base_stats').glob('*.asm')):
    text = f.read_text()
    dex = re.search(r'db (DEX_\w+)', text).group(1)
    moves = re.search(r'db ([\w, ]+?) ; level 1 learnset', text).group(1).split(',')
    base_moves[dex_to_species[dex]] = [m.strip() for m in moves if m.strip() != 'NO_MOVE']

# Learnsets: label per species in pointer-table order.
evos = read('data/pokemon/evos_moves.asm')
order = re.findall(r'dw (\w+)EvosMoves', evos)
learnsets = {}
for label in order:
    block = re.search(rf'^{label}EvosMoves:\n(.*?)(?=^\w+EvosMoves:|\Z)', evos, re.S | re.M).group(1)
    after = block.split('; Learnset', 1)[1]
    learnsets[label] = [(int(lv), mv) for lv, mv in re.findall(r'db (\d+), (\w+)', after)]
label_of_species = {species: order[sid - 1] for species, sid in species_id.items() if 0 < sid <= len(order)}


def moves_at(species, level):
    moves = list(base_moves[species])
    for lv, mv in learnsets[label_of_species[species]]:
        if lv > level:
            break
        if mv in moves:
            continue
        if len(moves) == 4:
            moves.pop(0)
        moves.append(mv)
    return moves + ['NO_MOVE'] * (4 - len(moves))


# Lone moves (gym leaders) and team moves (Elite Four).
special = read('data/trainers/special_moves.asm')
lone = re.findall(r'db (\d), (\w+)', special.split('TeamMoves:')[0])
team = dict(re.findall(r'db (\w+),\s+(\w+)', special.split('TeamMoves:')[1]))
gym_leader_no = {'BROCK': 1, 'MISTY': 2, 'LT_SURGE': 3, 'ERIKA': 4, 'KOGA': 5, 'SABRINA': 6, 'BLAINE': 7,
                 'GIOVANNI': 8}
gym_battle = {'GIOVANNI': 3}  # trainer index fought in the gym; every other leader has one trainer

# Party data by class label, in TrainerDataPointers order (class id = position + 1).
parties_text = read('data/trainers/parties.asm')
class_labels = re.findall(r'dw (\w+)Data', parties_text.split('TrainerDataPointers:')[1].split('\n\n')[0])
classes = sorted(trainer_id.items(), key=lambda kv: kv[1])
assert len(class_labels) == len(classes), (len(class_labels), len(classes))
names_text = read('data/trainers/names.asm')
display = re.findall(r'li "([^"]*)"', names_text)

out = []
for (cls, cid), label in zip(classes, class_labels):
    block = re.search(rf'^{label}Data:\n(.*?)(?=^\w+Data:|\Z)', parties_text, re.S | re.M).group(1)
    for index, line in enumerate(l for l in block.splitlines() if l.strip().startswith('db')):
        values = [v.strip() for v in re.sub(r';.*', '', line).split('db', 1)[1].split(',')]
        assert values[-1] == '0', line
        values = values[:-1]
        if values[0] == '$FF':
            pairs = [(int(values[i]), values[i + 1]) for i in range(1, len(values), 2)]
        else:
            pairs = [(int(values[0]), s) for s in values[1:]]
        if LEVEL:
            pairs = [(LEVEL, s) for _, s in pairs]
        party = [{'species': s, 'dex': dex_number[s], 'level': lv, 'moves': moves_at(s, lv)} for lv, s in pairs]
        if values[0] == '$FF' and cls in gym_leader_no and index + 1 == gym_battle.get(cls, 1):
            slot, move = lone[gym_leader_no[cls] - 1]
            party[int(slot)]['moves'][2] = move
        elif values[0] == '$FF' and cls in team:
            party[4]['moves'][2] = team[cls]
        elif values[0] == '$FF' and cls == 'RIVAL3':
            party[0]['moves'][2] = 'SKY_ATTACK'
            # Starter move by the rival's starter: the last Pokemon is its evolved form.
            last = party[5]['species']
            party[5]['moves'][2] = {'VENUSAUR': 'MEGA_DRAIN', 'CHARIZARD': 'FIRE_BLAST'}.get(last, 'BLIZZARD')
        for p in party:
            p['move_ids'] = [move_id[m] for m in p['moves']]
        out.append({'class': cls, 'class_id': cid, 'name': display[cid - 1], 'index': index + 1,
                    'dvs': {'atk': 9, 'def': 8, 'spe': 8, 'spc': 8}, 'stat_exp': 0, 'party': party})

pathlib.Path(sys.argv[2]).write_text(json.dumps(out, indent=1))
print(f'{len(out)} trainers in {len(classes)} classes -> {sys.argv[2]}')
