// Gen 1 random-battle team pool (2026-11-05) from Showdown's own generator (pokemon-showdown 0.11.11,
// Teams.generate('gen1randombattle')), written in the trainer-file format our env reads (ocean/pokemon cartridge
// trainers.tsv): "class size" then per Pokemon "species level move1 move2 move3 move4" (move 0 = empty slot).
// Order is Showdown's (the first Pokemon leads). Usage: node gen_teams.js PS_DIR N OUT.tsv RATINGS.txt [SEED]
const [psdir, n, out, ratings] = process.argv.slice(2);
const {Teams, Dex, PRNG} = require(psdir + '/node_modules/pokemon-showdown');
const dex = Dex.forGen(1);
const fs = require('fs');
const lines = [], stats = {levels: {}, sizes: {}, evs: {}, ivs: {}};
for (let i = 0; i < Number(n); i++) {
  const team = Teams.generate('gen1randombattle');
  const mons = team.map(s => {
    const sp = dex.species.get(s.species).num, moves = s.moves.map(m => dex.moves.get(m).num);
    if (!sp || moves.some(m => !m || m > 165) || moves.length < 1 || moves.length > 4) throw new Error('bad set ' + JSON.stringify(s));
    stats.levels[s.level] = (stats.levels[s.level] || 0) + 1;
    stats.evs[JSON.stringify(s.evs)] = (stats.evs[JSON.stringify(s.evs)] || 0) + 1;
    stats.ivs[JSON.stringify(s.ivs)] = (stats.ivs[JSON.stringify(s.ivs)] || 0) + 1;
    while (moves.length < 4) moves.push(0);
    return [sp, s.level, ...moves].join(' ');
  });
  stats.sizes[mons.length] = (stats.sizes[mons.length] || 0) + 1;
  lines.push(`0 ${mons.length} ${mons.join(' ')}`);
}
fs.writeFileSync(out, lines.join('\n') + '\n');
fs.writeFileSync(ratings, lines.map(() => '0').join('\n') + '\n');
const top = o => Object.entries(o).sort((a, b) => b[1] - a[1]).slice(0, 6);
console.log(`teams ${lines.length}; sizes`, stats.sizes, '; levels', top(stats.levels), '; evs', top(stats.evs), '; ivs', top(stats.ivs));
