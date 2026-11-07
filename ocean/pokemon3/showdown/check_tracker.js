// Tracker check (2026-11-06): plays gen3randombattle games in-process in Showdown with random choices, feeds tracker.js
// exactly what player 1 sees (p1's protocol and requests), and compares its state string on every p1 decision with
// the full-information state from ocean/pokemon9/showdown_convert.js (what training positions are made with).
// Our side must match field by field; for the opponent, only fields a player can know are compared (species, level,
// types, HP to the rounding of a percentage, status, boosts and side conditions), and the stat estimate is reported.
// Usage: NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/showdown/check_tracker.js GAMES
const {Battle, Teams, PRNG} = require('pokemon-showdown');
const {Tracker} = require('./tracker.js');
const {stateString} = require('../../pokemon9/showdown_convert.js');
const GAMES = Number(process.argv[2] || 50);
const NAMES = ['species', 'level', 't1', 't2', 'bt1', 'bt2', 'hp', 'maxhp', 'ability', 'baseability', 'item', 'nature', 'evs',
  'atk', 'def', 'spa', 'spd', 'spe', 'status', 'rest', 'sleep', 'weight', 'm0', 'm1', 'm2', 'm3'];
const SIDE = ['active', 'conditions', 'vols', 'durations', 'sub', 'atkb', 'defb', 'spab', 'spdb', 'speb', 'accb', 'evab', 'wish', 'wishhp'];
const diffs = {}, count = (k, ex) => { diffs[k] = diffs[k] || [0, ex]; diffs[k][0]++; };
let decisions = 0, foeStat = [0, 0];
const rng = new PRNG([1, 2, 3, 4]);
for (let g = 0; g < GAMES; g++) {
  const b = new Battle({formatid: 'gen3randombattle', seed: [g + 1, 7, 11, 13]});
  b.setPlayer('p1', {name: 'a', team: Teams.pack(Teams.getGenerator('gen3randombattle', [g, 1, 2, 3]).getTeam())});
  b.setPlayer('p2', {name: 'b', team: Teams.pack(Teams.getGenerator('gen3randombattle', [g, 4, 5, 6]).getTeam())});
  const t = new Tracker(0);
  let seenUpTo = 0;
  for (let step = 0; step < 300 && !b.ended; step++) {
    // p1's view of the log: |split|p1 is followed by the p1-only line (exact HP) and then the public one
    const lines = [];
    for (let i = seenUpTo; i < b.log.length; i++) {
      const l = b.log[i];
      if (l.startsWith('|split|')) { const who = l.slice(7); lines.push(who === 'p1' ? b.log[i + 1] : b.log[i + 2]); i += 2; continue; }
      lines.push(l);
    }
    seenUpTo = b.log.length;
    const req = b.p1.activeRequest;
    t.update(lines);
    if (req && !req.wait) {
      t.request(req);
      const truth = stateString(b);
      if (truth) {
        decisions++;
        const mine = t.view().state.split('/'), real = truth.split('/');
        for (const [k, side] of [[0, 'us'], [1, 'foe']]) {
          const tm = mine[k].split('='), rm = real[k].split('=');
          const rs = rm.slice(6), ts = tm.slice(6);
          const ra = b[side === 'us' ? 'p1' : 'p2'].pokemon, actSpecies = ra[0].species.id;
          // mon by species (the converter is in Showdown's current order, the tracker in fixed order)
          for (const rmon of rm.slice(0, 6)) {
            const rf = rmon.split(','), tmon = tm.slice(0, 6).find(x => x.split(',')[0] === rf[0] && x.split(',')[7] !== undefined);
            if (!tmon) { if (side === 'us') count(`${side}: mon missing`, rf[0]); continue; }
            const tf = tmon.split(',');
            NAMES.forEach((n, i) => {
              if (side === 'foe' && !['species', 'level', 't1', 't2', 'hp', 'status'].includes(n)) {
                if (['atk', 'def', 'spa', 'spd', 'spe'].includes(n)) { foeStat[0] += Math.abs(tf[i] - rf[i]) / rf[i]; foeStat[1]++; }
                return;
              }
              if (n === 'nature' || n === 'evs' || n === 'ability' && side === 'foe') return;
              if (n === 'hp' && side === 'foe') { if (Math.abs(tf[6] / tf[7] - rf[6] / rf[7]) > 0.02) count(`${side}: hp`, `${tf[6]}/${tf[7]} vs ${rf[6]}/${rf[7]}`); return; }
              if (n === 'hp' && rf[6] === '0' && tf[6] === '0') return;
              if (/^m\d/.test(n)) { const [ri, , rp] = rf[i].split(';'), [ti, , tp] = (tf[i] ?? '').split(';');
                if (!tf.slice(22, 26).some(x => x.split(';')[0] === ri)) count(`${side}: move missing`, `${ri} not in ${tf.slice(22, 26).join(' ')}`);
                return; }
              if (n === 'status' && rf[6] === '0') return;
              if (n === "rest" || n === "sleep") { if (tf[i] !== rf[i] && rf[i] !== "NaN") count(`${side}: ${n} turns`, `${tf[0]} ${tf[18]} ${tf[19]},${tf[20]} vs ${rf[18]} ${rf[19]},${rf[20]}`); return; }
              if (tf[i] !== rf[i]) count(`${side}: ${n}`, `${rf[0]} ${tf[i]} vs ${rf[i]}`);
            });
          }
          // side fields (active compared by species)
          const tact = tm[Number(ts[0])].split(',')[0];
          if (tact !== actSpecies.toUpperCase().replace(/[^A-Z0-9]/g, '')) count(`${side}: active`, `${tact} vs ${actSpecies}`);
          SIDE.forEach((n, i) => {
            if (i === 0 || n === 'wishhp') return;
            if (ts[i] !== rs[i]) count(`${side}: ${n}`, `${ts[i]} vs ${rs[i]}`);
          });
        }
        const [tw, rw] = [mine[2], real[2]];
        if (tw.split(';')[0] !== rw.split(';')[0]) count('weather', `${tw} vs ${rw}`);
      }
    }
    // random legal choices for both
    for (const s of [b.p1, b.p2]) {
      const r = s.activeRequest;
      if (!r || r.wait) continue;
      if (r.forceSwitch) { const i = s.pokemon.findIndex((p, j) => j > 0 && !p.fainted); b.choose(s.id, i > 0 ? `switch ${i + 1}` : 'pass'); continue; }
      const opts = [];
      r.active[0].moves.forEach((m, i) => { if (!m.disabled && (m.pp ?? 1) > 0) opts.push(`move ${i + 1}`); });
      if (!r.active[0].trapped) s.pokemon.forEach((p, i) => { if (i > 0 && !p.fainted) opts.push(`switch ${i + 1}`); });
      b.choose(s.id, opts.length ? opts[rng.random(opts.length)] : 'default');
    }
  }
}
console.log(`${GAMES} games, ${decisions} p1 decisions compared; foe stat estimate off by ${(100 * foeStat[0] / Math.max(1, foeStat[1])).toFixed(1)}% on average`);
for (const [k, [n, ex]] of Object.entries(diffs).sort((a, b) => b[1][0] - a[1][0])) console.log(`${String(n).padStart(6)}  ${k}   e.g. ${ex}`);
