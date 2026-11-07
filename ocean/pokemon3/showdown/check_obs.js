// Observation check for the Gen 3 bridge (2026-11-06): like check_tracker.js, but compares observations (pk3_view via
// build/pokemon3/showdown/view_obs): the tracker's view of our side, our side's conditions and the field against the
// one built from Showdown's full state (showdown_convert.js), per feature, on every p1 decision of random games.
// Usage: NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/showdown/check_obs.js GAMES
const {Battle, Teams, PRNG} = require('pokemon-showdown');
const {execFileSync} = require('child_process');
const {Tracker} = require('./tracker.js');
const {stateString} = require('../../pokemon9/showdown_convert.js');
const MON = 404, SIDE = 158;
const monName = j => j === 0 ? 'known' : j === 1 ? 'active' : j === 2 ? 'fainted' : j === 3 ? 'hp' : j === 4 ? 'maxhp' : j === 5 ? 'level'
  : j < 11 ? 'stat' + (j - 6) : j < 31 ? 'type' : j < 38 ? 'status' : j < 318 ? `move${Math.floor((j - 38) / 70)}.f${(j - 38) % 70}` : j < 390 ? 'ability' : 'item';
const sideName = j => j < 7 ? 'boost' + j : j < 26 ? 'cond' + (j - 7) : j < 154 ? 'volatile' + (j - 26) : ['sub', 'forceswitch', 'trapped', 'wish'][j - 154];
const pairs = [];
const rng = new PRNG([5, 6, 7, 8]);
for (let g = 0; g < Number(process.argv[2] || 20); g++) {
  const b = new Battle({formatid: 'gen3randombattle', seed: [g + 3, 7, 11, 13]});
  b.setPlayer('p1', {name: 'a', team: Teams.pack(Teams.getGenerator('gen3randombattle', [g, 9, 2, 3]).getTeam())});
  b.setPlayer('p2', {name: 'b', team: Teams.pack(Teams.getGenerator('gen3randombattle', [g, 8, 5, 6]).getTeam())});
  const t = new Tracker(0); let seenUpTo = 0;
  for (let step = 0; step < 300 && !b.ended; step++) {
    const lines = [];
    for (let i = seenUpTo; i < b.log.length; i++) { const l = b.log[i];
      if (l.startsWith('|split|')) { lines.push(l.slice(7) === 'p1' ? b.log[i + 1] : b.log[i + 2]); i += 2; continue; } lines.push(l); }
    seenUpTo = b.log.length;
    const req = b.p1.activeRequest; t.update(lines);
    if (req && !req.wait) {
      t.request(req);
      const truth = stateString(b), v = t.view();
      if (truth) pairs.push({mine: v.state, seen: v.seen, turn: v.turn, truth});
    }
    for (const s of [b.p1, b.p2]) { const r = s.activeRequest; if (!r || r.wait) continue;
      if (r.forceSwitch) { const i = s.pokemon.findIndex((p, j) => j > 0 && !p.fainted); b.choose(s.id, i > 0 ? `switch ${i + 1}` : 'pass'); continue; }
      const opts = []; r.active[0].moves.forEach((m, i) => { if (!m.disabled && (m.pp ?? 1) > 0) opts.push(`move ${i + 1}`); });
      if (!r.active[0].trapped) s.pokemon.forEach((p, i) => { if (i > 0 && !p.fainted) opts.push(`switch ${i + 1}`); });
      b.choose(s.id, opts.length ? opts[rng.random(opts.length)] : 'default'); }
  }
}
// The truth gets the tracker's SEEN flags in its own (Showdown, current) order of the opponent's Pokemon, with move bits
// mapped by move id, so both observations hide the same things.
for (const p of pairs) {
  const fm = p.mine.split('/')[1].split('=').slice(0, 6), ft = p.truth.split('/')[1].split('=').slice(0, 6), sn = p.seen.split(';');
  p.truthSeen = ft.map(m => { const f = m.split(','), i = fm.findIndex(x => x.split(',')[0] === f[0]);
    if (i < 0 || sn[i] === undefined || sn[i].startsWith('0')) return '0,0,0,0';
    const [act, mv, it, ab] = sn[i].split(',').map(Number), mine = fm[i].split(',').slice(22, 26).map(x => x.split(';')[0]);
    let bits = 0; f.slice(22, 26).forEach((x, k) => { const j = mine.indexOf(x.split(';')[0]); if (j >= 0 && (mv >> j & 1)) bits |= 1 << k; });
    return `${act},${bits},${it},${ab}`; }).join(';');
}
const input = pairs.flatMap(p => [`${p.turn}\t${p.seen}\t${p.mine}`, `${p.turn}\t${p.truthSeen}\t${p.truth}`]).join('\n') + '\n';
const out = execFileSync('build/pokemon3/showdown/view_obs', {input, maxBuffer: 1 << 30}).toString().trim().split('\n');
const diffs = {};
const add = (k, ex) => { diffs[k] = diffs[k] || [0, ex]; diffs[k][0]++; };
pairs.forEach((p, n) => {
  const a = out[2 * n].split(',').map(Number), b = out[2 * n + 1].split(',').map(Number);
  const names = s => s.split('/')[0].split('=').slice(0, 6).map(m => m.split(',')[0]);
  const mn = names(p.mine), tn = names(p.truth);
  mn.forEach((sp, i) => { const k = tn.indexOf(sp); if (k < 0) return;
    for (let j = 0; j < MON; j++) { const x = a[i * MON + j], y = b[k * MON + j]; if (Math.abs(x - y) > 1e-3) add('us ' + monName(j), `${sp} ${x} vs ${y}`); } });
  const fn = s => s.split('/')[1].split('=').slice(0, 6).map(m => m.split(',')[0]);
  fn(p.mine).forEach((sp, i) => { const k = fn(p.truth).indexOf(sp); if (k < 0 || p.seen.split(';')[i].startsWith('0')) return;
    for (let j = 0; j < MON; j++) { if (j >= 38 && j < 318) continue;
      const x = a[(6 + i) * MON + j], y = b[(6 + k) * MON + j]; if (Math.abs(x - y) > 0.02) add('foe ' + monName(j), `${sp} ${x} vs ${y}`); } });
  for (let j = 0; j < SIDE; j++) { const x = a[12 * MON + SIDE + j], y = b[12 * MON + SIDE + j]; if (Math.abs(x - y) > 1e-3) add('foe side ' + sideName(j), `${x} vs ${y}`); }
  for (let j = 0; j < SIDE; j++) { const x = a[12 * MON + j], y = b[12 * MON + j]; if (Math.abs(x - y) > 1e-3) add('side ' + sideName(j), `${x} vs ${y}`); }
  for (let j = 0; j < 7; j++) { const x = a[12 * MON + 2 * SIDE + j], y = b[12 * MON + 2 * SIDE + j]; if (Math.abs(x - y) > 1e-3) add('field' + j, `${x} vs ${y}`); }
});
console.log(`${pairs.length} decisions`);
for (const [k, [n, ex]] of Object.entries(diffs).sort((a, b) => b[1][0] - a[1][0]).slice(0, 30)) console.log(`${String(n).padStart(6)}  ${k}   e.g. ${ex}`);
