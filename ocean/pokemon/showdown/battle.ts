// Plays one Gen 1 battle in Pokémon Showdown (@pkmn/sim 0.9.31, the build pkmn/engine is validated against, with
// pkmn's own test patch for speed-tie ordering and its Gen 1 clause mods) from the seed our engine derives for the same
// 64-bit seed (helpers.Battle.init: PSRNG(seed).newSeed()), with uniformly random legal choices on both sides.
// Prints a trace for engine_replay.c: the seed, both teams (species and move ids), per decision both players' engine
// action indices, and after each decision every Pokémon's exact HP by original team index.
// Build: ocean/pokemon/showdown/build.sh. Usage: node build/pokemon/showdown_parity/battle.js SEED TEAMFILE1 TEAMFILE2
import fs from 'fs';
import {Battle, Dex, Teams} from '@pkmn/sim';
import {Generations} from '@pkmn/data';
import {Tracker} from './tracker';
import {formatFor, patch} from '../../../build/pokemon/engine-9b88fd6c5467f703c38951d5b2e8a660314d410b/src/test/showdown';

const gen = new Generations(Dex as any).get(1);
patch.generation(gen);
const [seedArg, f1, f2] = process.argv.slice(2);
const MASK = (1n << 64n) - 1n;
function showdownSeed(seed: string) {
  let s = BigInt(seed) & MASK;
  const draw = () => { s = (0x5D588B656C078965n * s + 0x269EC3n) & MASK; return Number(((s >> 32n) * 0x10000n) >> 32n); };
  return [draw(), draw(), draw(), draw()];
}
function team(file: string) {
  const sets = Teams.import(fs.readFileSync(file, 'utf8'))!;
  for (const set of sets) {
    set.level = 100; set.ability = "No Ability"; set.item = "";
    set.evs = {hp: 255, atk: 255, def: 255, spa: 255, spd: 255, spe: 255};
    set.ivs = {hp: 31, atk: 31, def: 31, spa: 31, spd: 31, spe: 31};
  }
  return sets;
}
const teams = [team(f1), team(f2)];
const dex = gen.dex;
const ids = teams.map(t => t.map(s => [dex.species.get(s.species).num, ...s.moves.map(m => dex.moves.get(m).num)]));
const out = [`seed ${seedArg}`, ...ids.map((t, p) => `team${p + 1} ${t.map(m => m.join(',')).join(' ')}`)];
let rng = Number(BigInt(seedArg) % 2147483647n) || 1;
const rand = (n: number) => { rng = (rng * 48271) % 2147483647; return rng % n; };

const battle = new Battle({formatid: formatFor(gen), seed: showdownSeed(seedArg).join(',') as any, strictChoices: false} as any);
patch.battle(battle as any, false);
battle.setPlayer('p1', {name: 'p1', team: teams[0]} as any);
battle.setPlayer('p2', {name: 'p2', team: teams[1]} as any);
const original = (p: number, mon: any) => teams[p].findIndex(s => dex.species.get(s.species).num === mon.species.num);

function choose(p: number): {text: string; action: number} {
  const r: any = battle.sides[p].activeRequest;
  if (!r || r.wait) return {text: '', action: 10};
  const side = battle.sides[p];
  const options: {text: string; action: number}[] = [];
  const switches = () => side.pokemon.forEach((m: any, i: number) => {
    if (i > 0 && !m.fainted) options.push({text: `switch ${i + 1}`, action: 4 + original(p, m)});
  });
  if (r.forceSwitch) switches();
  else {
    const active = r.active[0];
    const forced = active.moves.length === 1 && side.active[0].moveSlots.length > 1 || ['struggle', 'recharge'].includes(active.moves[0].id);
    active.moves.forEach((m: any, i: number) => {
      if (!m.disabled && (m.pp === undefined || m.pp > 0 || forced)) options.push({text: `move ${i + 1}`, action: forced ? 11 : i});
    });
    if (!active.trapped && !active.maybeTrapped) switches();
  }
  return options[rand(options.length)];
}
// One player's view of log lines: a split line's secret version goes to that player, the public one to everyone else.
function view(lines: string[], p: number) {
  const out: string[] = [];
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].startsWith('|split|')) {
      const own = lines[i] === `|split|p${p + 1}`;
      out.push(own || !process.env.PERCENT_HP ? lines[own ? i + 1 : i + 2] : percent(lines[i + 1]));
      i += 2;
    } else out.push(lines[i]);
  }
  return out;
}
// PERCENT_HP=1: opponents see HP as Showdown's HP Percentage Mod shows it on the ladder (rounded up, 99 until full).
function percent(line: string) {
  return line.replace(/\|(\d+)\/(\d+)( [a-z]+)?$/, (_, hp, max, st) => {
    let pct = Math.ceil(100 * Number(hp) / Number(max));
    if (pct === 100 && Number(hp) < Number(max)) pct = 99;
    return `|${Number(hp) === 0 ? '0' : `${pct}/100`}${st ?? ''}`;
  });
}
const trackers = [new Tracker(0), new Tracker(1)];
let logged = 0;
const hex = (a: Uint8Array) => Array.from(a, x => x.toString(16).padStart(2, '0')).join('');
for (let step = 0; step < 2000 && !battle.ended; step++) {
  if (process.env.TRACKER_OBS) {
    const fresh = battle.log.slice(logged); logged = battle.log.length;
    for (let p = 0; p < 2; p++) {
      const r: any = battle.sides[p].activeRequest;
      trackers[p].update(view(fresh, p), r);
      out.push(`tobs ${p + 1} ${hex(trackers[p].observe(r && !r.wait ? r : null))}`);
    }
  }
  const c = [choose(0), choose(1)];
  const seen = battle.log.length;
  battle.makeChoices(c[0].text, c[1].text);
  if (process.env.SHOWDOWN_LOG) for (const l of battle.log.slice(seen)) if (l && !l.startsWith('|split')) out.push(`# ${l}`);
  const hp = battle.sides.map((side: any, p: number) => {
    const row = new Array(6).fill(0);
    for (const m of side.pokemon) row[original(p, m)] = m.hp;
    return row.join(',');
  });
  out.push(`do ${c[0].action} ${c[1].action} # ${c.map(x => x.text || 'wait').join(' / ')}`);
  out.push(`hp ${battle.turn} ${hp.join(' ')}`);
}
out.push(`end ${battle.winner ?? 'tie'}`);
console.log(out.join('\n'));
