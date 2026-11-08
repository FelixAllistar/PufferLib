// Split the downloaded Smogon ADV pastes (raw/ID.txt, sources.tsv) into one Showdown team file per team (teams/), six
// Pokemon each in paste order; a paste with 12 or 18 Pokemon is 2 or 3 teams; leftovers (not a multiple of 6) dropped.
const fs = require('fs');
const {Teams} = require('pokemon-showdown');
fs.mkdirSync('teams', {recursive: true});
const out = [];
for (const line of fs.readFileSync('sources.tsv', 'utf8').trim().split('\n')) {
  const [tier, id, name] = line.split('\t');
  const sets = Teams.import(fs.readFileSync(`raw/${id}.txt`, 'utf8')) || [];
  const n = Math.floor(sets.length / 6);
  for (let k = 0; k < n; k++) {
    const f = `teams/${tier}_${id}_${k}.txt`;
    fs.writeFileSync(f, Teams.export(sets.slice(6 * k, 6 * k + 6)));
    out.push(`${tier}\t${id}\t${k}\t${sets.slice(6 * k, 6 * k + 6).map(s => s.species).join('/')}\t${name}`);
  }
  if (sets.length % 6) console.error(`${id} (${name}): ${sets.length} Pokemon, ${sets.length % 6} left over`);
}
fs.writeFileSync('teams.tsv', out.join('\n') + '\n');
console.log(`${out.length} teams`);
