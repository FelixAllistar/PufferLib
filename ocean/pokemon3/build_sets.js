// Gen 3 OU set and team tables for ocean/pokemon3's team pool and free-pick draft (2026-11-06). Every distinct set
// (species, item, ability, nature, EVs, IVs, moves) of a pool of Showdown team files becomes one poke-engine Pokemon
// string (Showdown computes the stats; same converter as the positions), and each team six indices into that table, so
// the env can build any pairing, or a team it drafts, in its own engine (pk3_new_teams applies the leads' switch-in).
// Usage: NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/build_sets.js TRAIN_LIST HELDOUT_LIST OUT
//   TRAIN_LIST / HELDOUT_LIST: one team file per line. OUT/sets.txt (one Pokemon per line), OUT/train.txt and
//   OUT/heldout.txt (six set indices per line, lead first), OUT/draft.txt (draft catalog: per line a species, its team
//   count and up to SETS_PER_SPECIES set indices, most used first; top SPECIES species), OUT/cores.txt (top CORES
//   species pairs by teams sharing them, as two draft.txt line numbers), OUT/summary.json.
const {Battle, Teams} = require('pokemon-showdown');
const fs = require('fs');
const {mon} = require('../pokemon9/showdown_convert.js');
const [TRAIN, HELDOUT, OUT] = process.argv.slice(2);
const SPECIES = 128, SETS_PER_SPECIES = 6, CORES = 256;
fs.mkdirSync(OUT, {recursive: true});
const sets = [], index = new Map(), speciesOf = [];
const bad = {};
function team(file) {
  try {
    const parsed = Teams.import(fs.readFileSync(file, 'utf8'));
    if (!parsed || parsed.length !== 6) { bad.size = (bad.size || 0) + 1; return null; }
    for (const s of parsed) s.level = s.level || 100;
    // Only side one: no battle start, so no switch-in effects (Trace, Intimidate) are baked into the sets.
    const b = new Battle({formatid: 'gen3ou'});
    b.setPlayer('p1', {name: 'a', team: Teams.pack(parsed)});
    return b.p1.pokemon.map(p => {
      const s = mon(p);
      if (!index.has(s)) { index.set(s, sets.length); sets.push(s); speciesOf.push(p.species.id); }
      return index.get(s);
    });
  } catch (e) { bad[e.message.slice(0, 40)] = (bad[e.message.slice(0, 40)] || 0) + 1; return null; }
}
function table(list, out) {
  const files = fs.readFileSync(list, 'utf8').split('\n').filter(Boolean), teams = [];
  for (const f of files) { const t = team(f); if (t) teams.push(t); }
  fs.writeFileSync(out, teams.map(t => t.join(' ')).join('\n') + '\n');
  return teams;
}
const train = table(TRAIN, `${OUT}/train.txt`);
const heldout = table(HELDOUT, `${OUT}/heldout.txt`);
fs.writeFileSync(`${OUT}/sets.txt`, sets.join('\n') + '\n');

// Draft catalog from the training teams only.
const speciesTeams = new Map(), setUse = new Map(), pairs = new Map();
for (const t of train) {
  const sp = [...new Set(t.map(i => speciesOf[i]))].sort();
  for (const s of sp) speciesTeams.set(s, (speciesTeams.get(s) || 0) + 1);
  for (const i of t) setUse.set(i, (setUse.get(i) || 0) + 1);
  for (let a = 0; a < sp.length; a++) for (let b = a + 1; b < sp.length; b++) {
    const k = sp[a] + ' ' + sp[b]; pairs.set(k, (pairs.get(k) || 0) + 1);
  }
}
const top = [...speciesTeams].sort((a, b) => b[1] - a[1]).slice(0, SPECIES).map(x => x[0]);
const line = new Map(top.map((s, i) => [s, i]));
const bySpecies = new Map(top.map(s => [s, []]));
for (const [i, n] of setUse) if (bySpecies.has(speciesOf[i])) bySpecies.get(speciesOf[i]).push([i, n]);
fs.writeFileSync(`${OUT}/draft.txt`, top.map(s => [s, speciesTeams.get(s),
  ...bySpecies.get(s).sort((a, b) => b[1] - a[1]).slice(0, SETS_PER_SPECIES).map(x => x[0])].join(' ')).join('\n') + '\n');
const cores = [...pairs].filter(([k]) => k.split(' ').every(s => line.has(s))).sort((a, b) => b[1] - a[1]).slice(0, CORES);
fs.writeFileSync(`${OUT}/cores.txt`, cores.map(([k]) => k.split(' ').map(s => line.get(s)).join(' ')).join('\n') + '\n');
fs.writeFileSync(`${OUT}/summary.json`, JSON.stringify({sets: sets.length, train: train.length, heldout: heldout.length,
  species: speciesTeams.size, draft_species: top.length, cores: cores.length, bad}, null, 1));
console.log(fs.readFileSync(`${OUT}/summary.json`, 'utf8'));
