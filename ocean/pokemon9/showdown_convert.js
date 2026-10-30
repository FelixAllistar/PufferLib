// Pokémon Showdown battle -> poke-engine state string (2026-10-29; moved here from the Gen 9 differential test,
// reports/pokemon_experiments/20261029_gen9_engine/engine_diff.js, which also uses it). Full information: both sides'
// whole teams. Positions using things the converter does not map yet return null and are counted in skipReasons.
// Needs pokemon-showdown on NODE_PATH (on the box: build/pokemon/showdown_server/node_modules).
const {Dex} = require('pokemon-showdown');
const id = s => Dex.toID(s).toUpperCase();
const STATUS = {'': 'NONE', brn: 'BURN', par: 'PARALYZE', slp: 'SLEEP', frz: 'FREEZE', psn: 'POISON', tox: 'TOXIC'};
const WEATHER = {'': 'NONE', sunnyday: 'SUN', raindance: 'RAIN', sandstorm: 'SAND', snowscape: 'SNOW', hail: 'HAIL',
    desolateland: 'HARSHSUN', primordialsea: 'HEAVYRAIN'};
const TERRAIN = {'': 'NONE', electricterrain: 'ELECTRICTERRAIN', psychicterrain: 'PSYCHICTERRAIN',
    mistyterrain: 'MISTYTERRAIN', grassyterrain: 'GRASSYTERRAIN'};
// Showdown volatiles the converter carries over; any other volatile on an active Pokémon skips the position.
const VOLATILE = {confusion: 'CONFUSION', leechseed: 'LEECHSEED', substitute: 'SUBSTITUTE', taunt: 'TAUNT',
    encore: 'ENCORE', saltcure: 'SALTCURE', protosynthesis: null, quarkdrive: null};
const IGNORED_VOLATILES = new Set(['dynamax', 'choicelock', 'stall', 'lockedmove', 'mustrecharge', 'roost']);
// Not converted: a Ditto transformed by Imposter or Transform (it would become a real copy of its target).
const skipReasons = {};
const skip = r => { skipReasons[r] = (skipReasons[r] || 0) + 1; return null; };

function mon(p) {
    const t = p.getTypes(), bt = p.species.types, ev = p.set.evs || {};
    const moves = [0, 1, 2, 3].map(i => {
        const m = p.moveSlots[i];
        return m ? `${id(m.id === 'hiddenpower' ? 'hiddenpower' + p.hpType + '60' : m.id)};${!!m.disabled};${m.pp}` : 'NONE;false;0';
    });
    const st = p.storedStats;
    const rest = p.status === 'slp' && p.statusState.source === p;
    const sleep = p.status === 'slp' && !rest ? (p.statusState.startTime - p.statusState.time) : 0;
    return [id(p.species.id), p.level, id(t[0]), t[1] ? id(t[1]) : 'TYPELESS', id(bt[0]), bt[1] ? id(bt[1]) : 'TYPELESS',
        p.hp, p.maxhp, id(p.ability), id(p.baseAbility), p.item ? id(p.item) : 'NONE', id(p.set.nature || 'serious'),
        ['hp', 'atk', 'def', 'spa', 'spd', 'spe'].map(k => ev[k] ?? 0).join(';'),
        st.atk, st.def, st.spa, st.spd, st.spe, STATUS[p.status] ?? 'NONE',
        rest ? Math.max(1, Math.min(3, p.statusState.time || 0)) : 0, Math.max(0, sleep),
        p.species.weightkg, ...moves, false, !!p.terastallized, id(p.teraType),
        !!(p.swordBoost || p.shieldBoost)].join(',');  // Intrepid Sword / Dauntless Shield used (once per battle)
}
// poke-engine's last_used_move: the active's last move by slot (Encore, choice locks), else none.
function lastUsed(a) {
    const i = a.lastMove ? a.moveSlots.findIndex(m => m.id === a.lastMove.id) : -1;
    return i >= 0 ? `move:${i}` : 'move:none';
}
function sideString(s) {
    const a = s.active[0];
    if (!a) return skip('no active');
    if (s.pokemon.some(p => p.transformed)) return skip('transformed');
    for (const v of Object.keys(a.volatiles)) if (!(v in VOLATILE) && !IGNORED_VOLATILES.has(v)) return skip(`volatile:${v}`);
    for (const c of Object.keys(s.sideConditions)) if (!['reflect', 'lightscreen', 'auroraveil', 'spikes', 'stealthrock',
        'stickyweb', 'toxicspikes', 'tailwind', 'safeguard', 'mist'].includes(c)) return skip(`side:${c}`);
    const sc = s.sideConditions, layers = c => sc[c] ? (sc[c].layers ?? 1) : 0, dur = c => sc[c] ? (sc[c].duration ?? 0) : 0;
    const stall = a.volatiles.stall ? Math.round(Math.log(a.volatiles.stall.counter) / Math.log(3)) : 0;
    const conditions = [dur('auroraveil'), 0, 0, dur('lightscreen'), 0, 0, 0, dur('mist'), stall, 0, dur('reflect'),
        dur('safeguard'), layers('spikes'), layers('stealthrock'), layers('stickyweb'), dur('tailwind'),
        a.status === 'tox' ? (a.statusState.stage || 0) : 0, layers('toxicspikes'), 0].join(';');
    const vols = Object.keys(a.volatiles).map(v => VOLATILE[v]).filter(Boolean).map(v => v + ':').join('');
    // Taunt and Encore: Showdown stores turns remaining (3 when applied), poke-engine turns elapsed.
    const d = k => a.volatiles[k] ? Math.max(0, Math.min(2, 3 - (a.volatiles[k].duration ?? 3))) : 0;
    const wish = s.slotConditions[0]?.wish;  // a pending Wish always lands at the end of this turn
    const b = a.boosts, team = s.pokemon.map(mon);
    while (team.length < 6) team.push(team[team.length - 1].replace(/,\d+,(\d+),/, ',0,$1,'));
    return [...team, 0, conditions, vols, [0, d('encore'), 0, 0, d('taunt'), 0].join(';'),
        a.volatiles.substitute ? a.volatiles.substitute.hp : 0, b.atk, b.def, b.spa, b.spd, b.spe, b.accuracy, b.evasion,
        wish ? 1 : 0, wish ? Math.floor(wish.hp) : 0, 0, 0, false, 'NONE', false, false, false, lastUsed(a), false].join('=');
}
function stateString(battle) {
    const f = battle.field;
    // Format rules (sleepclausemod, ...) are stored as pseudo-weathers; only real field effects count.
    const field = Object.keys(f.pseudoWeather).filter(k => k !== 'trickroom' && !/clause|mod$/.test(k));
    if (field.length) return skip(`field:${field[0]}`);
    const s1 = sideString(battle.p1), s2 = sideString(battle.p2);
    if (!s1 || !s2) return null;
    const w = WEATHER[f.weather] ?? null, t = TERRAIN[f.terrain] ?? null;
    if (w === null || t === null) return skip('weather/terrain');
    const tr = f.pseudoWeather.trickroom;
    return [s1, s2, `${w};${f.weatherState.duration ?? 0}`, `${t};${f.terrainState.duration ?? 0}`,
        `${!!tr};${tr ? tr.duration : 0}`, false].join('/');
}
module.exports = {id, STATUS, skip, skipReasons, mon, sideString, stateString};
