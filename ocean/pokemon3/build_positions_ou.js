// Gen 3 OU starting positions for ocean/pokemon3 (2026-11-06): turn-1 poke-engine states (after both leads are in)
// of battles between two teams drawn from a pool of real Gen 3 OU teams (Showdown export / paste files, e.g. Metamon's
// replay-based team sets). Same converter and format as build_positions.js (the random-battle one). Each team keeps
// its own lead (first Pokemon) as written. One state per line.
// Usage: NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/build_positions_ou.js TEAMLIST N OUT SHARD
//   TEAMLIST: a file with one team file path per line (the pool). Positions whose teams Showdown cannot load are skipped.
const {Battle, Teams, PRNG} = require('pokemon-showdown');
const fs = require('fs');
const {stateString, skipReasons} = require('../pokemon9/showdown_convert.js');
const [LIST, N, OUT, SHARD] = [process.argv[2], +process.argv[3] || 1000, process.argv[4] || 'positions_ou.txt', +process.argv[5] || 0];
const files = fs.readFileSync(LIST, 'utf8').split('\n').filter(Boolean);
const rng = new PRNG([11, 13, 17, 19 + SHARD]);
const packed = new Map();
function team(file) {
  if (!packed.has(file)) {
    let p = null;
    try {
      const sets = Teams.import(fs.readFileSync(file, 'utf8'));
      if (sets && sets.length === 6) { for (const s of sets) s.level = s.level || 100; p = Teams.pack(sets); }
    } catch (e) {}
    packed.set(file, p);
  }
  return packed.get(file);
}
const out = fs.openSync(OUT, 'w');
let written = 0, games = 0, bad = 0;
while (written < N && games < 20 * N) {
  games++;
  const t1 = team(files[rng.random(files.length)]), t2 = team(files[rng.random(files.length)]);
  if (!t1 || !t2) { bad++; continue; }
  let b;
  try {
    b = new Battle({formatid: 'gen3ou', seed: [games, 3, 5, 23 + SHARD]});
    b.setPlayer('p1', {name: 'a', team: t1});
    b.setPlayer('p2', {name: 'b', team: t2});
  } catch (e) { bad++; continue; }
  if (b.turn !== 1 || b.ended) continue;
  const s = stateString(b);
  if (!s) continue;
  fs.writeSync(out, s + '\n');
  written++;
}
console.log(JSON.stringify({written, games, bad_teams: bad, skipped: skipReasons}));
