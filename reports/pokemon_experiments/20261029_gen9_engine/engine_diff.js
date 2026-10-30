// Engine differential test (2026-10-29): Pokémon Showdown (ground truth) vs poke-engine, Gen 9 random battles.
// 1. Play random-choice gen9randombattle games in Showdown and snapshot positions at random turns.
// 2. Convert each position to poke-engine's state string (full information) and pick a random legal move pair.
// 3. Showdown: clone the battle (sim State.serializeBattle) and play that move pair SAMPLES times with fresh seeds.
//    poke-engine: data/diffcheck lists every outcome with its probability (damage rolls branched).
// 4. Compare the two outcome distributions over the same summary (active Pokémon, each Pokémon's HP and status, the
//    active's boosts) by total variation distance, and write every position with its distance and the mechanics in play.
// Positions using things the converter does not map yet (listed in `skipReasons`) are skipped and counted.
// Usage (box, in build/pokemon/showdown_server): node engine_diff.js POSITIONS SAMPLES OUT.jsonl
const {Battle, Dex, Teams} = require('pokemon-showdown');
const {State} = require('pokemon-showdown/dist/sim/state');
const {PRNG} = require('pokemon-showdown/dist/sim/prng');
const {execFileSync} = require('child_process');
const fs = require('fs');
const [POSITIONS, SAMPLES, OUT, SHARD] = [+process.argv[2] || 50, +process.argv[3] || 400, process.argv[4] || 'diff.jsonl', +process.argv[5] || 0];
const ENGINE = '/workspace/poke-engine/target/release/diffcheck';
const rng = new PRNG([7, 7, 7, 7 + SHARD]);
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
        p.species.weightkg, ...moves, false, !!p.terastallized, id(p.teraType)].join(',');
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
// A choice by identity -> this battle's Showdown choice string.
function resolve(side, c) {
    if (c.move) return `move ${side.active[0].moveSlots.findIndex(m => m.id === c.move) + 1}`;
    return `switch ${side.pokemon.findIndex(p => p.species.id === c.species) + 1}`;
}
// Random legal choice for a side (no tera, no switching when trapped), as [showdown choice, poke-engine choice].
function choice(side) {
    const req = side.activeRequest;
    if (!req || req.wait || req.forceSwitch || req.teamPreview) return null;
    const act = req.active[0], opts = [];
    act.moves.forEach((m, i) => { if (!m.disabled && (m.pp ?? 1) > 0) opts.push([{move: m.id}, m.id === 'hiddenpower' ? null : m.id]); });
    if (!act.trapped) side.pokemon.forEach((p, i) => { if (i > 0 && !p.fainted) opts.push([{species: p.species.id}, Dex.toID(p.species.id)]); });
    const ok = opts.filter(o => o[1]);
    return ok.length ? ok[rng.random(ok.length)] : null;
}
// The summary both engines report: per side the active Pokémon, every Pokémon's HP and status (in the order of the
// pre-turn snapshot), and the active's boosts.
function summary(battle, order) {
    return [battle.p1, battle.p2].map((s, k) => {
        const a = s.active[0], mons = order[k].map(sp => { const p = s.pokemon.find(q => id(q.set.species) === sp);
            return `${sp}:${p ? p.hp : 0}:${STATUS[p?.status ?? ''] ?? 'NONE'}`; });
        while (mons.length < 6) mons.push(mons[mons.length - 1].replace(/:\d+:/, ':0:'));
        const b = a ? a.boosts : {atk: 0, def: 0, spa: 0, spd: 0, spe: 0};
        return `${a ? id(a.species.id) : 'NONE'}#${mons.join(';')}#${b.atk},${b.def},${b.spa},${b.spd},${b.spe}`;
    }).join('/');
}
// Showdown is deterministic given the seed and the choices, so a position is rebuilt by replaying them. (The sim's
// State.serializeBattle clone was unreliable here: after a forced switch the restored team order no longer matched the
// active Pokémon, so "switch N" brought in the wrong one or turned into a move.)
// Random teams come from their own generator (not the battle seed), so both teams are generated once, seeded, packed
// and passed to the live battle and every replay.
function teamsFor(game) {
    return [1, 2].map(k => Teams.pack(Teams.getGenerator('gen9randombattle', [game, k, 41, 43 + SHARD]).getTeam()));
}
function replay(seed, history, teams) {
    const b = new Battle({formatid: 'gen9randombattle', seed: [...seed]});  // the PRNG mutates its seed array
    b.setPlayer('p1', {name: 'a', team: teams[0]}); b.setPlayer('p2', {name: 'b', team: teams[1]});
    for (const [side, c] of history) b.choose(side, c);
    return b;
}
const out = fs.openSync(OUT, 'w');
let written = 0, game = 0;
while (written < POSITIONS) {
    const seed = [game + 11, 3, 5, 7 + 1000 * SHARD], history = [];
    const teams = teamsFor(game), battle = replay(seed, [], teams);
    game++;
    const stopAt = 1 + rng.random(25);
    if (game % 20 === 0) console.error(`game ${game}, written ${written}, skipped ${JSON.stringify(skipReasons)}`);
    let steps = 0;
    while (!battle.ended && battle.turn < stopAt && steps++ < 400) {
        for (const s of [battle.p1, battle.p2]) {
            const r = s.activeRequest; if (!r || r.wait) continue;
            let str;
            if (r.forceSwitch) { const i = s.pokemon.findIndex((p, j) => j > 0 && !p.fainted); str = i > 0 ? `switch ${i + 1}` : 'pass'; }
            else { const c = choice(s); str = c ? resolve(s, c[0]) : 'default'; }
            history.push([s.id, str]); battle.choose(s.id, str);
        }
    }
    if (battle.ended) continue;
    const c1 = choice(battle.p1), c2 = choice(battle.p2);
    if (!c1 || !c2) { skip('no plain choice'); continue; }
    const engineState = stateString(battle);
    if (!engineState) continue;
    const order = [battle.p1, battle.p2].map(s => s.pokemon.map(p => id(p.set.species)));
    const counts = new Map();
    // The replay must reproduce the live position exactly (actives and every HP); otherwise skip it.
    const fingerprint = b => [b.turn, ...[b.p1, b.p2].map(s => s.active[0]?.species.id + ':' + s.pokemon.map(p => p.species.id + p.hp).join(','))].join('|');
    const probe = replay(seed, history, teams);
    if (fingerprint(probe) !== fingerprint(battle)) { skip('replay diverged'); if (!global.firstDiverge) { global.firstDiverge = 1; console.error('LIVE  ', fingerprint(battle)); console.error('REPLAY', fingerprint(probe)); console.error('HIST', JSON.stringify(history.slice(-6))); } continue; }
    let invalid = 0, sampleLog = null, sent = null;
    for (let i = 0; i < SAMPLES; i++) {
        const b = replay(seed, history, teams);
        b.prng = new PRNG([i + 1, game, 13, 17 + SHARD]);
        // Sleep length is hidden (2-4 turns, fixed at the start in Showdown); poke-engine branches on it, so each sample
        // redraws it given the turns already slept.
        for (const p of [...b.p1.pokemon, ...b.p2.pokemon]) if (p.status === 'slp' && p.statusState.source !== p) {
            const slept = p.statusState.startTime - p.statusState.time, lo = Math.max(2, slept + 1);
            if (lo <= 4) { p.statusState.startTime = lo + b.prng.random(5 - lo); p.statusState.time = p.statusState.startTime - slept; }
        }
        const turn = b.turn, r1 = resolve(b.p1, c1[0]), r2 = resolve(b.p2, c2[0]);
        const ok2 = b.choose('p2', r2), act2 = JSON.stringify(b.p2.choice.actions.map(x => [x.choice, x.pokemon?.species.id, x.target?.species.id, x.moveid])), ok1 = b.choose('p1', r1);
        if (!sampleLog) sent = [r1, ok1, r2, ok2, act2, b.p2.pokemon.map(p => p.species.id).join(',')];
        if (!ok1 || !ok2 || (b.turn === turn && !b.ended && !b.p1.activeRequest?.forceSwitch && !b.p2.activeRequest?.forceSwitch)) { invalid++; continue; }
        if (!sampleLog) sampleLog = b.log.slice(b.log.lastIndexOf(`|turn|${turn}`)).filter(l => !l.startsWith('|t:|') && !l.startsWith('|split'));
        const k = summary(b, order); counts.set(k, (counts.get(k) || 0) + 1);
    }
    let engineOut;
    try { engineOut = execFileSync(ENGINE, {input: `${engineState}\t${c1[1]}\t${c2[1]}\n`}).toString().trim(); }
    catch (e) { skip('engine crashed'); fs.writeSync(out, JSON.stringify({crash: true, state: engineState, m1: c1[1], m2: c2[1]}) + '\n'); written++; continue; }
    if (engineOut.startsWith('ERROR')) { skip('engine rejected move'); continue; }
    const engine = new Map();
    // Showdown clears a fainted Pokémon's status and a fainted active's boosts; poke-engine keeps them, which changes
    // nothing later in the battle, so the engine's summary is normalized the same way.
    const norm = k => k.split('/').map(side => { const [a, m, b] = side.split('#');
        const mons = m.split(';').map(x => x.replace(/:0:[A-Z]+$/, ':0:NONE'));
        return [a, mons.join(';'), mons.some(x => x.startsWith(a + ':0:')) ? '0,0,0,0,0' : b].join('#'); }).join('/');
    for (const part of engineOut.split('\t')) { const [p, k] = [parseFloat(part.slice(0, part.indexOf('|'))), norm(part.slice(part.indexOf('|') + 1))]; engine.set(k, (engine.get(k) || 0) + p / 100); }
    if (invalid > SAMPLES / 2) { skip('showdown rejected the choices'); continue; }
    const n = SAMPLES - invalid, sd = new Map([...counts].map(([k, c]) => [k, c / n]));
    const maxhp = [battle.p1, battle.p2].map(s => s.pokemon.map(p => p.maxhp));
    // Parse a summary into its parts: actives+statuses (categorical), boosts, and HP per Pokémon (12 numbers).
    const parts = k => { const sides = k.split('/').map(x => x.split('#'));
        return {cat: sides.map(([a, m]) => a + ':' + m.split(';').map(x => x.split(':')[2]).join(',')).join('|'),
            boost: sides.map(x => x[2]).join('|'), hp: sides.flatMap(([, m], i) => m.split(';').map((x, j) => +x.split(':')[1] / (maxhp[i][j] || 1)))}; };
    const marginal = (dist, f) => { const m = new Map(); for (const [k, p] of dist) { const v = f(parts(k)); m.set(v, (m.get(v) || 0) + p); } return m; };
    const tvdOf = (a, b) => { let t = 0; for (const k of new Set([...a.keys(), ...b.keys()])) t += Math.abs((a.get(k) || 0) - (b.get(k) || 0)); return t / 2; };
    const meanHp = dist => { const v = new Array(12).fill(0); for (const [k, p] of dist) parts(k).hp.forEach((h, i) => v[i] += h * p); return v; };
    const [hs, he] = [meanHp(sd), meanHp(engine)];
    const hpGap = Math.max(...hs.map((h, i) => Math.abs(h - he[i])));
    const catTvd = tvdOf(marginal(sd, x => x.cat), marginal(engine, x => x.cat));
    const boostTvd = tvdOf(marginal(sd, x => x.boost), marginal(engine, x => x.boost));
    const tvd = Math.max(catTvd, boostTvd);
    const top = m => [...m.entries()].sort((a, b) => b[1] - a[1]).slice(0, 3);
    const a1 = battle.p1.active[0], a2 = battle.p2.active[0];
    fs.writeSync(out, JSON.stringify({game, turn: battle.turn, tvd: +tvd.toFixed(3), cat: +catTvd.toFixed(3),
        boost: +boostTvd.toFixed(3), hp: +(100 * hpGap).toFixed(1), invalid, m1: c1[1], m2: c2[1],
        p1: [id(a1.species.id), id(a1.ability), a1.item], p2: [id(a2.species.id), id(a2.ability), a2.item],
        weather: battle.field.weather, terrain: battle.field.terrain,
        showdown: top(sd), engine: top(engine), log: sampleLog, sent, state: engineState}) + '\n');
    written++;
}
console.log(JSON.stringify({written, games: game, skipped: skipReasons}));
