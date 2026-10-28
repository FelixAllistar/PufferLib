// Showdown -> policy observation. Rebuilds the 648 bytes pk_observe (../bridge.zig) gives one player, from what
// Showdown sends that player: its protocol lines (own HP exact, foe HP as a percentage) and its request JSON. Used by the
// parity harness (battle.ts, byte-for-byte against our engine on identical battles) and by the live client.
//
// Layout (pk_observe): [0] phase 2, [2..3] turn, then for relative side r (0 self, 1 foe): [4+r] active identity
// (self: party index + 1, foe: order of first appearance), [6+r] pending choice type (0 pass, 1 move, 2 switch),
// [8+r] last move used in the latest update, [10+r] its user; per Pokémon (self by party index, foe by appearance)
// at 16 + 192r + 32i: species, HP/255 (rounded up), status class, fainted, level, two types, then self: four moves,
// four PP, stats/4 (HP, Atk, Def, Spe, Spc); foe: moves revealed in order of first use. Active blocks at 400 + 32r:
// species, types, boosts + 6 (atk, def, spe, spc, accuracy, evasion), 15 volatiles, self: modified stats/4.
// [480..647] the observer's legal-action mask (0-3 move slots, 4-9 switch to party index, 10 pass, 11 forced move).
import {Dex} from '@pkmn/sim';

const dex = Dex.forGen(1);
export const OBS = 648;
const TYPES = ['Normal', 'Fighting', 'Flying', 'Poison', 'Ground', 'Rock', 'Bug', 'Ghost', 'Fire', 'Water', 'Grass',
  'Electric', 'Psychic', 'Ice', 'Dragon'];
const VOLATILES = ['Bide', 'Thrashing', 'Charging', 'Binding', 'Invulnerable', 'Confusion', 'Mist', 'FocusEnergy',
  'Substitute', 'Recharging', 'Rage', 'LeechSeed', 'Toxic', 'LightScreen', 'Reflect'];
const BOOSTS: [number, number][] = [[25, 100], [28, 100], [33, 100], [40, 100], [50, 100], [66, 100], [1, 1],
  [15, 10], [2, 1], [25, 10], [3, 1], [35, 10], [4, 1]];
const STATS = ['atk', 'def', 'spe', 'spc'] as const;
const BINDING = ['Wrap', 'Bind', 'Clamp', 'Fire Spin'];
type Stat = typeof STATS[number];

const speciesNum = (name: string) => dex.species.get(name).num;
const moveNum = (name: string) => dex.moves.get(name).num;
const typeIndex = (t: string) => TYPES.indexOf(t);
const scaled = (v: number) => Math.min(Math.floor(v / 4), 255);
function statusClass(s: string) {
  return ({slp: 1, psn: 2, brn: 3, frz: 4, par: 5, tox: 6} as Record<string, number>)[s] ?? 0;
}

interface Mon {
  species: number; hp: number; maxhp: number; status: string; fainted: boolean; level: number;
  moves: number[]; pp: number[]; stats: Record<string, number>;  // self only: exact moves, PP and stats
  revealed: number[];                                             // foe only: moves in order of first use
}
interface Active {
  species: number; types: number[]; boosts: Record<string, number>; volatiles: Set<string>;
  stats: Record<Stat, number>;  // self only: modified stats
}

function newActive(mon: Mon | undefined): Active {
  const species = mon?.species ?? 0;
  const types = species ? dex.species.get(dex.species.all().find(s => s.num === species)!.name).types.map(typeIndex) : [0, 0];
  const stats = {atk: mon?.stats.atk ?? 0, def: mon?.stats.def ?? 0, spe: mon?.stats.spe ?? 0, spc: mon?.stats.spa ?? 0};
  return {species, types: [types[0], types[1] ?? types[0]], boosts: {}, volatiles: new Set(), stats};
}

export class Tracker {
  readonly side: number;  // 0 for p1, 1 for p2
  turn = 0;
  // Self: party in original (submitted) order; foe: party in order of first appearance.
  party: Mon[][] = [[], []];
  activeIndex = [-1, -1];
  active: Active[] = [newActive(undefined), newActive(undefined)];
  lastMove = [0, 0];
  lastActor = [0, 0];
  foeFaintedThisUpdate = false;

  constructor(side: number) { this.side = side; }

  private rel(ident: string) { return Number(ident[1]) - 1 === this.side ? 0 : 1; }
  private find(r: number, name: string) {
    const num = speciesNum(name);
    return this.party[r].findIndex(m => m.species === num);
  }
  private applyStatus(r: number) {
    const mon = this.party[r][this.activeIndex[r]], a = this.active[r];
    if (r !== 0 || !mon) return;
    if (mon.status === 'par') a.stats.spe = Math.max(Math.floor(a.stats.spe / 4), 1);
    else if (mon.status === 'brn') a.stats.atk = Math.max(Math.floor(a.stats.atk / 2), 1);
  }
  private recompute(r: number, stat: Stat) {
    const mon = this.party[r][this.activeIndex[r]], a = this.active[r];
    if (r !== 0 || !mon) return;
    const base = stat === 'spc' ? mon.stats.spa : mon.stats[stat];
    const [n, d] = BOOSTS[(a.boosts[stat] ?? 0) + 6];
    a.stats[stat] = Math.min(Math.max(Math.floor(base * n / d), 1), 999);
  }

  // The first request gives our own team in submitted order: that order is the engine's party index.
  initSelf(request: any) {
    if (this.party[0].length) return;
    this.party[0] = request.side.pokemon.map((p: any) => {
      const [hp, maxhp] = p.condition.split(' ')[0].split('/').map(Number);
      const moves = p.moves.map(moveNum);
      return {species: speciesNum(p.details.split(',')[0]), hp, maxhp, status: '', fainted: false, level: 100, moves,
        pp: moves.map((m: number) => Math.floor(dex.moves.all().find(x => x.num === m)!.pp * 8 / 5)),
        stats: {...p.stats, hp: maxhp}, revealed: []};
    });
  }

  // Protocol lines from one update (one makeChoices), as this player sees them, with the request that follows them
  // (it carries our team; needed before our first switch-in can be read).
  update(lines: string[], request?: any) {
    if (request?.side) this.initSelf(request);
    this.lastMove = [0, 0]; this.lastActor = [0, 0]; this.foeFaintedThisUpdate = false;
    for (const line of lines) {
      const f = line.split('|');
      const kind = f[1];
      if (kind === 'turn') { this.turn = Number(f[2]); continue; }
      if (!f[2] || !/^p[12]a?:/.test(f[2])) {
        if (kind === '-clearallboost') for (const a of this.active) { a.boosts = {}; for (const s of STATS) this.recompute(this.active.indexOf(a), s); }
        continue;
      }
      const r = this.rel(f[2]), name = f[2].split(': ')[1];
      if (kind === 'switch' || kind === 'drag' || kind === 'replace') {
        const species = f[3].split(',')[0];
        let i = this.find(r, species);
        if (i < 0 && r === 1) {
          this.party[1].push({species: speciesNum(species), hp: 0, maxhp: 100, status: '', fainted: false,
            level: Number(/, L(\d+)/.exec(f[3])?.[1] ?? 100), moves: [], pp: [], stats: {}, revealed: []});
          i = this.party[1].length - 1;
        }
        this.activeIndex[r] = i;
        this.active[r] = newActive(this.party[r][i]);
        this.setHp(r, i, f[4]);
        this.applyStatus(r);
        continue;
      }
      const i = this.find(r, name), mon = this.party[r][i], a = this.active[r];
      if (!mon) continue;
      switch (kind) {
        case 'move': {
          const m = moveNum(f[3]);
          this.lastMove[r] = m; this.lastActor[r] = i + 1;
          if (r === 1 && !mon.revealed.includes(m) && mon.revealed.length < 4) mon.revealed.push(m);
          if (r === 0 && !line.includes('[from]')) { const slot = mon.moves.indexOf(m); if (slot >= 0) mon.pp[slot] = Math.max(0, mon.pp[slot] - 1); }
          if (['Rage'].includes(f[3])) a.volatiles.add('Rage');
          // Binding (Wrap, Bind, Clamp, Fire Spin) marks the user while it keeps the foe trapped; Showdown never announces
          // it, so set it on a fresh binding move and clear it when the user next acts any other way.
          if (BINDING.includes(f[3]) && !line.includes('[from]') && !line.includes('[miss]')) a.volatiles.add('Binding');
          else if (!BINDING.includes(f[3])) a.volatiles.delete('Binding');
          a.volatiles.delete('Charging'); a.volatiles.delete('Invulnerable');
          break;
        }
        case '-damage': case '-heal': case '-sethp': this.setHp(r, i, f[3]); break;
        case 'faint': mon.hp = 0; mon.fainted = true; mon.status = ''; if (i === this.activeIndex[r]) a.volatiles.clear(); if (r === 1) this.foeFaintedThisUpdate = true; break;
        case '-status': mon.status = f[3]; if (i === this.activeIndex[r]) { this.applyStatus(r); if (f[3] === 'tox') a.volatiles.add('Toxic'); } break;
        case '-curestatus': mon.status = ''; if (i === this.activeIndex[r]) a.volatiles.delete('Toxic'); break;
        case '-boost': case '-unboost': {
          if (f[3] === 'spd') break;  // Gen 1 Special: Showdown announces spa and spd; count it once
          const stat = (f[3] === 'spa' ? 'spc' : f[3]) as Stat | 'accuracy' | 'evasion';
          const n = Number(f[4]) * (kind === '-boost' ? 1 : -1);
          a.boosts[stat] = Math.max(-6, Math.min(6, (a.boosts[stat] ?? 0) + n));
          if ((STATS as readonly string[]).includes(stat)) this.recompute(r, stat as Stat);
          // Gen 1 stat modification glitch (pkmn boost/unboost): a successful boost re-applies the opponent's paralysis or
          // burn drop; a stat drop re-applies the target's own.
          this.applyStatus(kind === '-boost' ? 1 - r : r);
          break;
        }
        case '-start': case '-end': {
          const what = f[3].replace(/^move: /, '');
          const v = ({Bide: 'Bide', confusion: 'Confusion', Mist: 'Mist', 'Focus Energy': 'FocusEnergy',
            Substitute: 'Substitute', 'Leech Seed': 'LeechSeed', 'Light Screen': 'LightScreen', Reflect: 'Reflect'} as Record<string, string>)[what];
          if (v) { if (kind === '-start') a.volatiles.add(v); else a.volatiles.delete(v); }
          if (kind === '-start' && what === 'typechange') a.types = f[4].split('/').map(typeIndex).concat([]).slice(0, 2).map((t, _, x) => t ?? x[0]);
          break;
        }
        case '-transform': {
          const target = this.party[1 - r][this.activeIndex[1 - r]];
          const ta = this.active[1 - r];
          a.species = ta.species; a.types = [...ta.types]; a.boosts = {...ta.boosts};
          void target;
          break;
        }
        case '-mustrecharge': a.volatiles.add('Recharging'); break;
        case 'cant': if (f[3] === 'recharge') a.volatiles.delete('Recharging'); break;
        case '-prepare': a.volatiles.add('Charging'); if (['Dig', 'Fly'].includes(f[3])) a.volatiles.add('Invulnerable'); break;
      }
    }
  }

  private setHp(r: number, i: number, condition: string | undefined) {
    if (!condition) return;
    const mon = this.party[r][i];
    const [hpText, status] = condition.split(' ');
    if (hpText === '0') { mon.hp = 0; mon.fainted = true; mon.status = ''; return; }
    else { const [hp, max] = hpText.split('/').map(Number); mon.hp = hp; if (max) mon.maxhp = max; }
    mon.status = status === 'fnt' ? mon.status : (status ?? '');
  }

  // The observation for a decision, given this player's request (null on a wait request).
  observe(request: any): Uint8Array {
    const o = new Uint8Array(OBS);
    if (request?.side) {
      this.initSelf(request);
      // Exact own HP, status, PP and active moves from the request.
      request.side.pokemon.forEach((p: any) => {
        const i = this.find(0, p.details.split(',')[0]), mon = this.party[0][i];
        const [hpText, status] = p.condition.split(' ');
        mon.hp = Number(hpText.split('/')[0]); mon.fainted = status === 'fnt' || mon.hp === 0;
        mon.status = status === 'fnt' ? '' : (status ?? '');
      });
      if (request.active && this.activeIndex[0] >= 0) {
        const mon = this.party[0][this.activeIndex[0]];
        for (const m of request.active[0].moves) {
          const slot = mon.moves.indexOf(moveNum(m.id));
          if (slot >= 0 && m.pp !== undefined) mon.pp[slot] = m.pp;
        }
      }
    }
    o[0] = 2; o[2] = this.turn & 255; o[3] = this.turn >> 8;
    const pending = this.pending(request);
    for (let r = 0; r < 2; r++) {
      o[4 + r] = this.activeIndex[r] + 1;
      o[6 + r] = pending[r];
      o[8 + r] = this.lastMove[r];
      o[10 + r] = this.lastActor[r];
      this.party[r].forEach((mon, i) => {
        const b = 16 + r * 192 + i * 32;
        o[b] = mon.species;
        // Foe HP is exact when Showdown sends it so, else a percentage (HP Percentage Mod): then this approximates.
        o[b + 1] = mon.fainted ? 0 : Math.ceil(mon.hp * 255 / mon.maxhp);
        o[b + 2] = statusClass(mon.status);
        o[b + 3] = mon.fainted ? 1 : 0;
        o[b + 4] = mon.level;
        const t = dex.species.all().find(s => s.num === mon.species)!.types.map(typeIndex);
        o[b + 5] = t[0]; o[b + 6] = t[1] ?? t[0];
        if (r === 0) {
          for (let j = 0; j < 4; j++) { o[b + 8 + j] = mon.moves[j] ?? 0; o[b + 12 + j] = mon.pp[j] ?? 0; }
          o[b + 16] = scaled(mon.maxhp); o[b + 17] = scaled(mon.stats.atk); o[b + 18] = scaled(mon.stats.def);
          o[b + 19] = scaled(mon.stats.spe); o[b + 20] = scaled(mon.stats.spa);
        } else {
          for (let j = 0; j < 4; j++) o[b + 8 + j] = mon.revealed[j] ?? 0;
        }
      });
      const a = this.active[r], b = 400 + r * 32;
      o[b] = a.species; o[b + 1] = a.types[0]; o[b + 2] = a.types[1];
      ['atk', 'def', 'spe', 'spc', 'accuracy', 'evasion'].forEach((s, j) => { o[b + 3 + j] = (a.boosts[s] ?? 0) + 6; });
      VOLATILES.forEach((v, j) => { o[b + 9 + j] = a.volatiles.has(v) ? 1 : 0; });
      if (r === 0) { o[b + 24] = scaled(a.stats.atk); o[b + 25] = scaled(a.stats.def); o[b + 26] = scaled(a.stats.spe); o[b + 27] = scaled(a.stats.spc); }
    }
    this.mask(request, o.subarray(480));
    return o;
  }

  private pending(request: any): [number, number] {
    if (!request || request.wait) return [0, 2];
    if (request.forceSwitch) return [2, this.foeFaintedThisUpdate ? 2 : 0];
    return [1, 1];
  }

  private mask(request: any, out: Uint8Array) {
    if (!request || request.wait) { out[10] = 1; return; }
    const switches = () => request.side.pokemon.forEach((p: any, i: number) => {
      if (i > 0 && !p.condition.endsWith(' fnt')) out[4 + this.find(0, p.details.split(',')[0])] = 1;
    });
    if (request.forceSwitch) { switches(); return; }
    const active = request.active[0];
    const mon = this.party[0][this.activeIndex[0]];
    const forced = active.moves.length === 1 && (mon.moves.length > 1 || ['struggle', 'recharge'].includes(active.moves[0].id));
    // A forced move (recharge, a locked move) is Showdown's 'move 1', which the engine (Showdown mode) also encodes as slot 0;
    // Struggle is the engine's no-move choice (11).
    if (forced) out[active.moves[0].id === 'struggle' ? 11 : 0] = 1;
    else active.moves.forEach((m: any, j: number) => { if (!m.disabled && m.pp > 0) out[j] = 1; });
    if (!active.trapped) switches();
  }
}
