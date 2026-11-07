// Gen 3 battle tracker for the Showdown bridge (2026-11-06). Follows one battle from our seat (Showdown protocol lines
// and requests) and writes what the policy server needs: a poke-engine state string with side one = us, the SEEN
// string (what we have seen of the opponent), the legal-action MASK, and the turn number. The state uses the same
// format as ocean/pokemon9/showdown_convert.js (the Showdown differential test's converter, which training positions
// come from), so the observation is the env's own (pk3_view in ../engine/src/lib.rs).
//
// Party order is fixed, as in training: ours is the order of the first request (lead first), the opponent's the order
// in which their Pokemon appear. Action 4+p means our party slot p.
//
// What a player cannot know about the opponent is estimated the way Gen 3 random battles build sets: stats from base
// stats with 85 EVs, 31 IVs and a neutral nature at the shown level; PP from the moves seen used. Unseen opponent
// Pokemon, moves, items and abilities are placeholders the observation blanks out (SEEN).
const {Dex} = require('pokemon-showdown');
const dex = Dex.mod('gen3');
const id = s => Dex.toID(s).toUpperCase();
const STATUS = {'': 'NONE', brn: 'BURN', par: 'PARALYZE', slp: 'SLEEP', frz: 'FREEZE', psn: 'POISON', tox: 'TOXIC'};
const WEATHER = {none: 'NONE', sunnyday: 'SUN', raindance: 'RAIN', sandstorm: 'SAND', hail: 'HAIL'};
// Volatiles the state string carries (showdown_convert.js VOLATILE); the rest are not part of the observation.
const VOLATILE = {confusion: 'CONFUSION', leechseed: 'LEECHSEED', substitute: 'SUBSTITUTE', taunt: 'TAUNT', encore: 'ENCORE'};
const BOOSTS = ['atk', 'def', 'spa', 'spd', 'spe', 'accuracy', 'evasion'];

function moveId(m) {
  // Request ids: "hiddenpowerground" (Gen 3 Hidden Power is always 70 in random battles; OU sets with other IVs are
  // rare), "return102"/"frustration102" (power suffix): poke-engine wants HIDDENPOWERGROUND70 and RETURN.
  const x = Dex.toID(m);
  if (x.startsWith('hiddenpower')) return (/\d+$/.test(x) ? x : (x === 'hiddenpower' ? 'hiddenpowerfighting' : x) + '70').toUpperCase();
  return x.replace(/\d+$/, '').toUpperCase();
}
function maxPP(m) {
  const mv = dex.moves.get(Dex.toID(m).replace(/\d+$/, '').replace(/^hiddenpower.*/, 'hiddenpower'));
  return mv.exists ? Math.floor(mv.pp * 8 / 5) : 16;  // PP Ups, as Showdown gives every set
}
function estimateStats(species, level) {
  const b = species.baseStats, s = k => Math.floor((2 * b[k] + 31 + 21) * level / 100) + 5;
  return {hp: species.id === 'shedinja' ? 1 : Math.floor((2 * b.hp + 31 + 21) * level / 100) + level + 10,
    atk: s('atk'), def: s('def'), spa: s('spa'), spd: s('spd'), spe: s('spe')};
}
const parseDetails = d => { const [name, ...rest] = d.split(', '); const l = rest.find(x => /^L\d+$/.test(x));
  return {species: dex.species.get(name), level: l ? Number(l.slice(1)) : 100}; };

class Mon {
  constructor(species, level) {
    this.species = species; this.baseSpecies = species; this.level = level; this.hp = 1; this.maxhp = 1; this.status = '';
    this.stats = estimateStats(species, level); this.moves = []; this.pp = []; this.ability = ''; this.item = '';
    this.fainted = false; this.sleepTurns = 0; this.restTurns = 0; this.disabled = [];
  }
  string() {
    const t = this.species.types;
    const moves = [0, 1, 2, 3].map(i => this.moves[i] ? `${this.moves[i]};${!!this.disabled[i]};${Math.max(0, this.pp[i])}` : 'NONE;false;0');
    const ability = this.ability || id(this.species.abilities[0]), base = this.baseAbility || ability;
    const asleep = this.status === 'slp';
    return [id(this.species.id), this.level, id(t[0]), t[1] ? id(t[1]) : 'TYPELESS', id(t[0]), t[1] ? id(t[1]) : 'TYPELESS',
      this.fainted ? 0 : this.hp, this.maxhp, ability, base, this.item || 'NONE', 'SERIOUS', '85;85;85;85;85;85',
      this.stats.atk, this.stats.def, this.stats.spa, this.stats.spd, this.stats.spe, STATUS[this.status] ?? 'NONE',
      asleep ? this.restTurns : 0, asleep && !this.restTurns ? this.sleepTurns : 0, this.species.weightkg, ...moves, false, false, 'NORMAL', false].join(',');
  }
}

class SideView {
  constructor() { this.party = []; this.active = -1; this.boosts = {}; this.vols = {}; this.cond = {}; this.toxic = 0;
    this.sub = 0; this.wish = 0; this.stall = 0; this.protectNow = false; this.lastMove = -1; this.seen = []; this.byName = {}; this.size = 6; }
  activeMon() { return this.party[this.active]; }
  string() {
    const team = this.party.map(m => m.string());
    while (team.length < 6) team.push(team[0].replace(/^([^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*),\d+,/, '$1,0,'));
    const c = this.cond, n = k => c[k] || 0;
    const conditions = [0, 0, 0, n('lightscreen'), 0, 0, 0, n('mist'), this.stall, 0, n('reflect'), n('safeguard'), n('spikes'),
      0, 0, 0, this.toxic, 0, 0].join(';');
    const vols = Object.keys(this.vols).map(v => VOLATILE[v] + ':').join('');
    const d = k => this.vols[k] !== undefined ? Math.max(0, Math.min(2, this.vols[k])) : 0;
    const b = k => this.boosts[k] || 0;
    return [...team, this.active, conditions, vols, [0, d('encore'), 0, 0, d('taunt'), 0].join(';'), 'substitute' in this.vols ? this.sub : 0,
      ...BOOSTS.map(b), this.wish === 1 ? 1 : 0, this.wish === 1 ? Math.floor(this.activeMon().maxhp / 2) : 0, 0, 0, false, 'NONE',
      false, false, false, this.lastMove >= 0 ? `move:${this.lastMove}` : 'move:none', false].join('=');
  }
}

class Tracker {
  constructor(side) {  // side: 0 if we are p1
    this.me = side === 0 ? 'p1' : 'p2'; this.foe = side === 0 ? 'p2' : 'p1';
    this.sides = {[this.me]: new SideView(), [this.foe]: new SideView()};
    this.weather = 'none'; this.weatherTurns = 0; this.turn = 0;
  }
  // "p2a: Name" -> [side, Mon] (creating the opponent's entry on first sight via switch details)
  who(ident, details) {
    const sid = ident.slice(0, 2), name = ident.slice(ident.indexOf(':') + 1).trim(), s = this.sides[sid];
    let i = s.byName[name];
    if (i === undefined && details) {
      const {species, level} = parseDetails(details);
      i = s.party.length; s.byName[name] = i; s.party.push(new Mon(species, level)); s.seen.push({moves: 0, item: 0, ability: 0});
    }
    return i === undefined ? [s, null, -1] : [s, s.party[i], i];
  }
  setHp(mon, cond, mine) {
    const [hp, status] = cond.split(' ');
    if (hp === '0' || status === 'fnt') { mon.hp = 0; mon.fainted = true; mon.status = ''; return; }
    const [a, b] = hp.split('/').map(Number);
    if (mine) { mon.hp = a; mon.maxhp = b; }
    else { mon.maxhp = mon.stats.hp; mon.hp = Math.max(1, Math.round(a / b * mon.maxhp)); }
    const st = status ?? '';
    if (st !== mon.status) { if (st !== 'slp') { mon.sleepTurns = 0; mon.restTurns = 0; } mon.status = st; }
  }
  // Our side, from a request: species, stats, moves, PP, items and abilities are known exactly.
  request(req) {
    const s = this.sides[this.me];
    req.side.pokemon.forEach(p => {
      const name = p.ident.slice(p.ident.indexOf(':') + 1).trim();
      let i = s.byName[name];
      if (i === undefined) {
        const {species, level} = parseDetails(p.details);
        i = s.party.length; s.byName[name] = i; s.party.push(new Mon(species, level)); s.seen.push({});
      }
      const m = s.party[i];
      m.stats = {...p.stats}; m.item = p.item ? id(p.item) : ''; m.baseAbility = id(p.baseAbility || p.ability || '');
      if (!m.ability || !p.active) m.ability = m.baseAbility;
      if (!m.moves.length) { m.moves = p.moves.map(moveId); m.pp = p.moves.map(maxPP); }
      this.setHp(m, p.condition, true);
      if (p.active) {
        s.active = i;
        (req.active?.[0]?.moves ?? []).forEach((mv, k) => {
          const slot = m.moves.indexOf(moveId(mv.id === 'hiddenpower' ? p.moves[k] : mv.id));
          if (slot >= 0 && mv.pp !== undefined) { m.pp[slot] = mv.pp; m.disabled[slot] = !!mv.disabled; }
        });
      }
    });
  }
  update(lines) {
    for (const line of lines) {
      const f = line.split('|'); if (f.length < 2) continue;
      const tag = f[1], from = f.find(x => x.startsWith('[from]')) ?? '';
      const reveal = (ident) => {  // [from] item: X / ability: X reveals the opponent's
        // "[of] POKEMON" names the holder (e.g. Static paralyzing the attacker, Rough Skin hurting it)
        const of = f.find(x => x.startsWith('[of] '));
        if (of) ident = of.slice(5);
        if (!ident || !ident.startsWith(this.foe)) return;
        const [s, m, i] = this.who(ident);
        if (!m) return;
        const it = /item: (.+)/.exec(from), ab = /ability: (.+)/.exec(from);
        if (it) { m.item = id(it[1]); s.seen[i].item = 1; }
        if (ab) { m.ability = id(ab[1]); s.seen[i].ability = 1; }
      };
      if (tag === 'switch' || tag === 'drag') {
        const [s, m, i] = this.who(f[2], f[3]);
        if (!m) continue;
        const prev = s.activeMon();
        if (prev) { if (prev.baseAbility) prev.ability = prev.baseAbility; prev.species = prev.baseSpecies; }  // Trace, Castform revert
        s.active = i; s.boosts = {}; s.vols = {}; s.sub = 0; s.toxic = 0; s.lastMove = -1; s.stall = 0;
        if (m.skipped && m.status === 'slp') { if (m.restTurns > 0) m.restTurns += m.skipped; else m.sleepTurns = Math.max(0, m.sleepTurns - m.skipped); }
        m.skipped = 0;
        this.setHp(m, f[4], f[2].startsWith(this.me));
      } else if (tag === 'teamsize') this.sides[f[2]].size = Number(f[3]);
      else if (tag === 'move') {
        const [s, m] = this.who(f[2]);
        if (!m) continue;
        const mv = moveId(f[3]);
        let slot = m.moves.indexOf(mv);
        if (slot < 0 && f[2].startsWith(this.foe) && m.moves.length < 4 && !line.includes('[from]')) { m.moves.push(mv); m.pp.push(maxPP(f[3])); slot = m.moves.length - 1; }
        if (slot >= 0 && f[2].startsWith(this.foe) && !line.includes('[from]')) {
          m.pp[slot] -= 1; s.seen[s.byName[f[2].slice(f[2].indexOf(':') + 1).trim()]].moves |= 1 << slot;
        }
        if (slot >= 0) s.lastMove = slot;
        // Gen 3: sleep turns spent on Sleep Talk/Snore are given back on the next switch-in (skippedTime)
        if (m.lastCant && (mv === 'SLEEPTALK' || mv === 'SNORE')) m.skipped = (m.skipped || 0) + 1;
        else if (m.lastCant && !line.includes('[from]')) m.skipped = 0;
        if (!line.includes('[from]')) m.lastCant = false;
      } else if (tag === '-damage' || tag === '-heal' || tag === '-sethp') {
        const [s, m] = this.who(f[2]);
        if (m) this.setHp(m, f[3], f[2].startsWith(this.me));
        reveal(f[2]);
      } else if (tag === '-status') {
        const [, m] = this.who(f[2]); if (m) { m.status = f[3]; m.sleepTurns = 0; if (from.includes('Rest')) m.restTurns = 3; }
        reveal(f[2]);
      } else if (tag === '-curestatus') { const [, m] = this.who(f[2]); if (m) { m.status = ''; m.sleepTurns = 0; m.restTurns = 0; } }
      else if (tag === '-cureteam') { const [s] = this.who(f[2]); s.party.forEach(m => { m.status = ''; }); }
      else if (tag === 'cant') {
        const [, m] = this.who(f[2]);
        if (m && f[3] === 'slp') { if (m.restTurns > 0) m.restTurns--; else m.sleepTurns++; m.lastCant = true; }
      } else if (tag === '-boost' || tag === '-unboost') {
        const [s] = this.who(f[2]); const k = f[3], v = Number(f[4]) * (tag === '-boost' ? 1 : -1);
        s.boosts[k] = Math.max(-6, Math.min(6, (s.boosts[k] || 0) + v));
      } else if (tag === '-setboost') { const [s] = this.who(f[2]); s.boosts[f[3]] = Number(f[4]); }
      else if (tag === '-clearboost') { const [s] = this.who(f[2]); s.boosts = {}; }
      else if (tag === '-clearallboost') { for (const s of Object.values(this.sides)) s.boosts = {}; }
      else if (tag === '-clearnegativeboost') { const [s] = this.who(f[2]); for (const k in s.boosts) if (s.boosts[k] < 0) s.boosts[k] = 0; }
      else if (tag === '-start') {
        const [s] = this.who(f[2]); const v = Dex.toID(f[3].replace(/^move: /, ''));
        if (VOLATILE[v]) s.vols[v] = v === 'taunt' || v === 'encore' ? -1 : 0;
        if (v === 'substitute') s.sub = Math.floor(s.activeMon().maxhp / 4);
        reveal(f[2]);
      } else if (tag === '-end') {
        const [s] = this.who(f[2]); const v = Dex.toID(f[3].replace(/^move: /, ''));
        delete s.vols[v];
      } else if (tag === '-sidestart' || tag === '-sideend') {
        const s = this.sides[f[2].slice(0, 2)], c = Dex.toID(f[3].replace(/^move: /, ''));
        if (tag === '-sidestart') s.cond[c] = c === 'spikes' ? Math.min(3, (s.cond[c] || 0) + 1) : 5;
        else delete s.cond[c];
      } else if (tag === '-weather') {
        const w = Dex.toID(f[2]);
        if (line.includes('[upkeep]')) { if (this.weatherTurns > 0) this.weatherTurns--; }
        else { this.weather = w || 'none'; this.weatherTurns = from.includes('ability') ? 0 : 5; }
        reveal(f.find(x => x.startsWith('[of] '))?.slice(5));
      } else if (tag === '-item') { const [s, m, i] = this.who(f[2]); if (m) { m.item = id(f[3]); if (s === this.sides[this.foe]) s.seen[i].item = 1; } }
      else if (tag === '-enditem') { const [s, m, i] = this.who(f[2]); if (m) { m.item = ''; if (s === this.sides[this.foe]) s.seen[i].item = 1; } }
      else if (tag === '-ability') { const [s, m, i] = this.who(f[2]); if (m) { m.ability = id(f[3]); if (s === this.sides[this.foe]) s.seen[i].ability = 1; } }
      else if (tag === 'faint') { const [, m] = this.who(f[2]); if (m) { m.hp = 0; m.fainted = true; } }
      else if (tag === 'turn') {
        this.turn = Number(f[2]);
        for (const s of Object.values(this.sides)) {
          const a = s.activeMon();
          if (a && a.status === 'tox') s.toxic++;
          if (a && a.lastCant) { a.skipped = 0; a.lastCant = false; }  // asleep and no Sleep Talk/Snore this turn
          for (const k in s.cond) if (k !== 'spikes' && --s.cond[k] <= 0) delete s.cond[k];
          for (const k of ['taunt', 'encore']) if (s.vols[k] !== undefined) s.vols[k]++;
          if (s.wish > 0) s.wish--;
          s.stall = s.protectNow ? s.stall + 1 : 0; s.protectNow = false;
        }
      }
      if (tag === 'move' && f[3] === 'Wish' && !line.includes('[still]')) { const [s] = this.who(f[2]); s.wish = 2; }
      // Protect/Detect/Endure streak (Showdown's stall volatile, poke-engine's protect side condition)
      if (tag === '-singleturn' && /Protect|Endure/.test(f[3])) { const [s] = this.who(f[2]); s.protectNow = true; }
      if (tag === '-formechange' || tag === 'detailschange') { const [, m] = this.who(f[2]); if (m) m.species = dex.species.get(f[3].split(',')[0]); }
    }
  }
  // Legal actions from the request: 0-3 move slots (the active's moves, in party order), 4-9 our party slots.
  mask(req) {
    const s = this.sides[this.me], mask = Array(10).fill(0);
    const pos = p => s.byName[p.ident.slice(p.ident.indexOf(':') + 1).trim()];
    if (req.forceSwitch?.[0] || !req.active) {
      req.side.pokemon.forEach(p => { if (!p.active && !p.condition.endsWith('fnt')) mask[4 + pos(p)] = 1; });
    } else {
      const act = req.active[0], m = s.activeMon();
      act.moves.forEach((mv, k) => {
        if (mv.disabled || mv.pp === 0) return;
        const id0 = mv.id === 'hiddenpower' ? req.side.pokemon.find(p => p.active).moves[k] : mv.id;
        const slot = act.moves.length === 1 && act.moves[0].id === 'struggle' ? 0 : m.moves.indexOf(moveId(id0));
        mask[slot >= 0 ? slot : k] = 1;
      });
      if (!mask.some(x => x)) mask[0] = 1;  // Struggle / recharge / locked moves: "move 1"
      if (!act.trapped && !act.maybeTrapped)
        req.side.pokemon.forEach(p => { if (!p.active && !p.condition.endsWith('fnt')) mask[4 + pos(p)] = 1; });
    }
    return mask;
  }
  view() {
    const foe = this.sides[this.foe], seen = [];
    for (let i = 0; i < 6; i++) {
      const x = foe.seen[i];
      seen.push(x ? `1,${x.moves},${x.item},${x.ability}` : '0,0,0,0');
    }
    const w = WEATHER[this.weather] ?? 'NONE';
    const state = [this.sides[this.me].string(), foe.string(), `${w};${w === 'NONE' ? 0 : this.weatherTurns}`, 'NONE;0', 'false;0', false].join('/');
    return {state, seen: seen.join(';'), turn: this.turn};
  }
}
module.exports = {Tracker, moveId};
