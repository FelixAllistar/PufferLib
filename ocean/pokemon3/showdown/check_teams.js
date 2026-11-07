// Check for pk3_new_teams (2026-11-06): turn-1 Showdown states of random pairs of pool teams, with each team as set
// indices into build_sets.js's sets.txt, for check_teams.c to rebuild in the engine and compare.
// Usage: NODE_PATH=... node ocean/pokemon3/showdown/check_teams.js TEAMLIST SETS N > pairs.txt
//   Output lines: "a0 ... a5 b0 ... b5<TAB>STATE".
const {Battle, Teams, PRNG} = require('pokemon-showdown');
const fs = require('fs');
const {mon, stateString} = require('../../pokemon9/showdown_convert.js');
const [LIST, SETS, N] = [process.argv[2], process.argv[3], +process.argv[4] || 1000];
const index = new Map(fs.readFileSync(SETS, 'utf8').split('\n').filter(Boolean).map((s, i) => [s, i]));
const files = fs.readFileSync(LIST, 'utf8').split('\n').filter(Boolean);
const rng = new PRNG([1, 2, 3, 4]);
const pack = f => { const s = Teams.import(fs.readFileSync(f, 'utf8')); for (const x of s) x.level = x.level || 100; return Teams.pack(s); };
let written = 0;
for (let g = 0; written < N && g < 20 * N; g++) {
  try {
    const t = [pack(files[rng.random(files.length)]), pack(files[rng.random(files.length)])];
    const solo = t.map(team => { const b = new Battle({formatid: 'gen3ou'}); b.setPlayer('p1', {name: 'a', team}); return b.p1.pokemon.map(p => index.get(mon(p))); });
    if (solo.flat().some(i => i === undefined)) continue;
    const b = new Battle({formatid: 'gen3ou', seed: [g, 7, 11, 13]});
    b.setPlayer('p1', {name: 'a', team: t[0]});
    b.setPlayer('p2', {name: 'b', team: t[1]});
    const s = stateString(b);
    if (!s || b.turn !== 1) continue;
    console.log(solo.flat().join(' ') + '\t' + s);
    written++;
  } catch (e) {}
}
