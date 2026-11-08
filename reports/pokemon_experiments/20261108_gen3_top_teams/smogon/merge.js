// Merge the Smogon ADV teams (build_sets.js output in SMALL) into the main set table (BASE = build/pokemon3/ou3) as OUT
// (build/pokemon3/ou3s): BASE's sets keep their indices, new sets are appended, so BASE's train/heldout pools, draft and
// cores stay valid and are copied. Writes OUT/smogon_{current,old,ages,all}.txt (pool format) and smogon_teams.tsv.
// Usage: node merge.js BASE SMALL TRAIN_LIST TEAMS_TSV OUT
const fs = require('fs');
const {Teams} = require('pokemon-showdown');
const [BASE, SMALL, LIST, TSV, OUT] = process.argv.slice(2);
fs.mkdirSync(OUT, {recursive: true});
const base = fs.readFileSync(`${BASE}/sets.txt`, 'utf8').split('\n').filter(l => l.length);
const index = new Map(base.map((s, i) => [s, i]));
const small = fs.readFileSync(`${SMALL}/sets.txt`, 'utf8').split('\n').filter(l => l.length);
let added = 0;
const map = small.map(s => { if (!index.has(s)) { index.set(s, base.length); base.push(s); added++; } return index.get(s); });
// build_sets.js keeps a team only when it imports as exactly six Pokemon; follow the same files in order.
const files = fs.readFileSync(LIST, 'utf8').trim().split('\n');
const info = new Map(fs.readFileSync(TSV, 'utf8').trim().split('\n').map(l => { const f = l.split('\t'); return [`${f[0]}_${f[1]}_${f[2]}`, f]; }));
const good = files.filter(f => { const p = Teams.import(fs.readFileSync(f, 'utf8')); return p && p.length === 6; });
const teams = fs.readFileSync(`${SMALL}/train.txt`, 'utf8').trim().split('\n').map(l => l.split(' ').map(Number));
if (good.length !== teams.length) throw new Error(`${good.length} good files vs ${teams.length} teams`);
const tiers = {current: [], old: [], ages: [], all: []}, seen = new Set(), rows = [];
good.forEach((f, i) => {
  const key = f.split('/').pop().replace('.txt', ''), t = teams[i].map(j => map[j]), sig = t.join(' ');
  const [tier, id, k, species, name] = info.get(key);
  if (seen.has(sig)) { rows.push(`${tier}\t${id}\t${k}\tduplicate\t${species}\t${name}`); return; }
  seen.add(sig); tiers[tier].push(sig); tiers.all.push(sig);
  rows.push(`${tier}\t${id}\t${k}\t${sig}\t${species}\t${name}`);
});
fs.writeFileSync(`${OUT}/sets.txt`, base.join('\n') + '\n');
for (const f of ['train.txt', 'heldout.txt', 'draft.txt', 'cores.txt']) fs.copyFileSync(`${BASE}/${f}`, `${OUT}/${f}`);
for (const [k, v] of Object.entries(tiers)) fs.writeFileSync(`${OUT}/smogon_${k}.txt`, v.join('\n') + '\n');
fs.writeFileSync(`${OUT}/smogon_teams.tsv`, rows.join('\n') + '\n');
const bad = files.filter(f => !good.includes(f)).map(f => f.split('/').pop());
console.log(`sets: ${base.length - added} base + ${added} new; teams: current ${tiers.current.length}, old ${tiers.old.length}, ages ${tiers.ages.length}, unique ${tiers.all.length}; not 6 Pokemon: ${bad.join(' ')}`);
