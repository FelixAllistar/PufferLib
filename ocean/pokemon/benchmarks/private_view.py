"""Project ONLY ABI-3 player observations into Metamon's public interface.

No engine state, enemy checkpoint/config, or enemy team is accepted here.
Native HP is a public 0..255 fraction, not Showdown's display quantization.
This adapter is a native-engine benchmark, not a Showdown ladder rating.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TYPES = 'normal fighting flying poison ground rock bug ghost fire water grass electric psychic ice dragon'.split()
STATUS = ['nostatus', 'slp', 'psn', 'brn', 'frz', 'par', 'tox']
EFFECTS = ['bide', 'lockedmove', 'twoturnmove', 'partiallytrapped',
           'invulnerable', 'confusion', 'mist', 'focusenergy', 'substitute',
           'mustrecharge', 'rage', 'leechseed', 'tox', 'lightscreen', 'reflect']
# Engine volatiles that Showdown's replay stream never reports as an effect
# (-mustrecharge, -prepare, binding and Thrash locks are separate messages), so
# no Metamon vocabulary contains them: shown as effects they become unknown
# tokens. Metamon instead infers them from the previous move, as in training.
UNREPORTED_EFFECTS = {'lockedmove', 'twoturnmove', 'partiallytrapped',
                      'invulnerable', 'mustrecharge'}


def normalize(name):
    return ''.join(c.lower() for c in name if c.isalnum())


class PublicMechanics:
    def __init__(self):
        self.data = json.loads((ROOT / 'ocean/pokemon/data/freepick.json').read_text())
        self.species = {m['id']: m for m in self.data['species']}
        self.moves = {m['id']: m for m in self.data['moves']}

    def move(self, move_id, pp=None):
        if not move_id:
            return dict(name='nomove', move_type='nomove', category='nomove',
                        base_power=0, accuracy=1., priority=0, current_pp=0, max_pp=0)
        m = self.moves[move_id]
        name = normalize(m['name'])
        if name == 'vicegrip':
            name = 'visegrip'
        maximum = m['pp'] * 8 // 5
        return dict(name=name, move_type=m['type'].lower(),
                    category=('status' if not m['power'] else
                              'physical' if m['type'].lower() in TYPES[:8] else 'special'),
                    base_power=m['power'], accuracy=m['accuracy']/256,
                    priority=1 if name == 'quickattack' else -1 if name == 'counter' else 0,
                    current_pp=maximum if pp is None else pp, max_pp=maximum)


class PrivateView:
    def __init__(self, mechanics=None):
        self.mechanics = mechanics or PublicMechanics()
        self.reset()

    def reset(self):
        self.previous_moves = {}
        self.used_pp = {}
        self.effects = {}
        self.active_ids = [None, None]
        self.step = 0

    def project(self, observation):
        o = bytes(observation)
        if len(o) != 648 or o[0] != 2:
            raise ValueError('Expected a battle-phase ABI-3 player observation')
        if any(x not in (0, 1) for x in o[480:]):
            raise ValueError('Invalid legal-action mask')
        self.step += 1
        for side in (0, 1):
            actor, move = o[10+side], o[8+side]
            if actor and move:
                self.previous_moves[side, actor] = move
                if side == 1:
                    key = actor, move
                    self.used_pp[key] = self.used_pp.get(key, 0) + 1
            active = o[4+side]
            base = 400 + side * 32
            # Effects disappear on switching; only observable transitions used.
            if self.active_ids[side] != active:
                self.effects[side] = {}
                self.active_ids[side] = active
            visible = {name for j, name in enumerate(EFFECTS)
                       if o[base+9+j] and name not in UNREPORTED_EFFECTS}
            effects = self.effects.setdefault(side, {})
            for name in list(effects):
                if name not in visible:
                    del effects[name]
            for name in visible:
                effects.setdefault(name, self.step)

        def mon(side, index, active=False):
            base = 16 + side*192 + index*32
            original = o[base]
            if not original:
                raise ValueError('An unrevealed Pokemon cannot be constructed')
            ab = 400 + side*32
            species = o[ab] if active else original
            info = self.mechanics.species[species]
            boost = [o[ab+3+j]-6 for j in range(6)] if active else [0]*6
            type_ids = o[ab+1:ab+3] if active else o[base+5:base+7]
            type_names = sorted({TYPES[t] for t in type_ids})
            if len(type_names) == 1:
                type_names.append('notype')
                type_names.sort()
            moves = []
            for j, move in enumerate(o[base+8:base+12]):
                if not move:
                    continue
                pp = o[base+12+j] if side == 0 else None
                data = self.mechanics.move(move, pp)
                if side == 1:
                    data['current_pp'] = max(0, data['max_pp'] - self.used_pp.get((index+1, move), 1))
                moves.append(data)
            effects = self.effects.get(side, {}) if active else {}
            effect = min(effects, key=lambda e: (effects[e], EFFECTS.index(e))) if effects else 'noeffect'
            return dict(name=normalize(info['name']),
                        base_species=normalize(self.mechanics.species[original]['name']),
                        hp_pct=o[base+1]/255, types=' '.join(type_names),
                        item='noitem', ability='noability', tera_type='notype',
                        lvl=o[base+4], status='fnt' if o[base+3] else STATUS[o[base+2]],
                        effect=effect, moves=moves,
                        atk_boost=boost[0], def_boost=boost[1], spe_boost=boost[2],
                        spa_boost=boost[3], spd_boost=boost[3],
                        accuracy_boost=boost[4], evasion_boost=boost[5],
                        **{f'base_{k}':v for k,v in info['stats'].items()})

        active = o[4]-1
        opponent = o[5]-1
        if not (0 <= active < 6 and 0 <= opponent < 6):
            raise ValueError('Missing public active Pokemon identity')
        switches = [(i, mon(0, i)) for i in range(6)
                    if i != active and o[16+i*32] and not o[16+i*32+3]]
        switches.sort(key=lambda pair:pair[1]['name'])
        player = mon(0, active, True)
        enemy = mon(1, opponent, True)
        # Metamon indexes ALL known moves/reserves alphabetically, masking
        # legality separately. Do not compact to just legal moves/switches.
        move_order = sorted(enumerate(player['moves']), key=lambda pair:pair[1]['name'])
        action_map = {}
        for target, (source, _) in enumerate(move_order):
            if o[480+source]:
                action_map[target] = source
        if o[480+11]:  # Struggle: upstream maps each regular move button to it.
            action_map.update({i:11 for i in range(max(1, len(move_order)))})
        for target, (source, _) in enumerate(switches, 4):
            if o[480+4+source]:
                action_map[target] = 4+source
        if o[480+10]:
            action_map[0] = 10  # waiting player; caller bypasses actor.
        if set(action_map.values()) != {i for i,v in enumerate(o[480:]) if v}:
            raise ValueError('Incomplete native/Metamon legal-action mapping')
        state = dict(format='gen1ou', player_active_pokemon=player,
                     opponent_active_pokemon=enemy,
                     available_switches=[m for _,m in switches],
                     player_prev_move=self.mechanics.move(self.previous_moves.get((0, active+1), 0)),
                     opponent_prev_move=self.mechanics.move(self.previous_moves.get((1, opponent+1), 0)),
                     opponents_remaining=6-sum(bool(o[208+i*32] and o[208+i*32+3]) for i in range(6)),
                     player_conditions='noconditions', opponent_conditions='noconditions',
                     weather='noweather', battle_field='nofield',
                     forced_switch=o[6] == 2, battle_won=False, battle_lost=False,
                     can_tera=False, opponent_teampreview=[])
        return state, action_map


def heuristic_action(observation, mechanics=None):
    """Public-info type/STAB/base-power baseline; not claimed to be Metamon AI."""
    rules = mechanics or PublicMechanics()
    o = bytes(observation)
    legal = [i for i,v in enumerate(o[480:]) if v]
    if not legal:
        raise ValueError('No legal action')
    active = 16+(o[4]-1)*32
    scores = {}
    for action in legal:
        if action >= 4:
            continue
        m = rules.moves[o[active+8+action]]
        if not m.get('power'):
            scores[action] = 0.
            continue
        attack_type = TYPES.index(m['type'].lower())
        defensive_types = set(o[433:435])
        multiplier = 1.
        for defensive in defensive_types:
            multiplier *= rules.data['type_chart'][attack_type][defensive]
        stab = 1.5 if attack_type in o[401:403] else 1.
        scores[action] = m['power'] * multiplier * stab * m['accuracy']/256
    if scores:
        return max(scores, key=lambda a:(scores[a], -a))
    # Forced switch: use only public type effectiveness against active enemy.
    switches = [a for a in legal if 4 <= a < 10]
    if switches:
        def score(action):
            base = 16+(action-4)*32
            return max(rules.data['type_chart'][t][d]
                       for t in set(o[base+5:base+7]) for d in set(o[433:435]))
        return max(switches, key=lambda a:(score(a), -a))
    return legal[0]
