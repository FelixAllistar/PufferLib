// Showdown simulator speed: gen9 random battles played in-process (no server, no streams), random legal choices.
const {Battle, Teams} = require("pokemon-showdown");
const {PRNG} = require("pokemon-showdown/dist/sim/prng");
const games = +process.argv[2] || 200, prng = new PRNG([1, 2, 3, 4]);
function choice(side) {
  const req = side.activeRequest;
  if (!req || req.wait) return null;
  if (req.forceSwitch) {
    const opts = side.pokemon.map((p, i) => i).filter(i => i >= side.active.length && !side.pokemon[i].fainted);
    return opts.length ? `switch ${opts[prng.random(opts.length)] + 1}` : "pass";
  }
  if (req.teamPreview) return "default";
  const act = req.active[0], moves = act.moves.map((m, i) => !m.disabled && (m.pp ?? 1) > 0 ? `move ${i + 1}` : null).filter(Boolean);
  const sw = act.trapped ? [] : side.pokemon.map((p, i) => i).filter(i => i >= side.active.length && !side.pokemon[i].fainted).map(i => `switch ${i + 1}`);
  const all = [...moves, ...sw];
  return all.length ? all[prng.random(all.length)] : "default";
}
let turns = 0, done = 0;
const t0 = process.hrtime.bigint();
for (let g = 0; g < games; g++) {
  const b = new Battle({formatid: "gen9randombattle", seed: [g + 1, 2, 3, 4], debug: false});
  b.setPlayer("p1", {name: "a", team: null}); b.setPlayer("p2", {name: "b", team: null});
  let n = 0;
  while (!b.ended && n < 600) {
    const c1 = choice(b.p1) ?? undefined, c2 = choice(b.p2) ?? undefined;
    if (c1) b.choose("p1", c1); if (c2) b.choose("p2", c2);
    if (!c1 && !c2) break;
    n++;
  }
  turns += b.turn; if (b.ended) done++;
}
const s = Number(process.hrtime.bigint() - t0) / 1e9;
console.log(`${games} games (${done} ended), ${turns} turns in ${s.toFixed(2)}s: ${(turns / s).toFixed(0)} turns/s, ${(games / s).toFixed(1)} games/s, ${(turns / games).toFixed(1)} turns/game (one core)`);
