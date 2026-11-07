// Re-score an engine_diff.js output against another poke-engine build without re-running Showdown (2026-11-05).
// Same metrics as engine_diff.js: max gap in expected HP per Pokémon (% of max HP), TVD of actives+statuses and of
// boosts. Rows flagged midturn (Gen 3: Showdown stops at a mid-turn faint switch) are counted apart.
// Usage: [KO_SPLIT=1] ENGINE=.../diffcheck node rescore.js IN.jsonl OUT.jsonl
const fs = require('fs'), {execFileSync} = require('child_process');
const [IN, OUT] = process.argv.slice(2), ENGINE = process.env.ENGINE;
const rows = fs.readFileSync(IN, 'utf8').trim().split('\n').map(l => JSON.parse(l)).filter(r => !r.crash && r.sd);

const norm = k => k.split('/').map(side => { const [a, m, b] = side.split('#');
    const mons = m.split(';').map(x => x.replace(/:0:[A-Z]+$/, ':0:NONE'));
    return [a, mons.join(';'), mons.some(x => x.startsWith(a + ':0:')) ? '0,0,0,0,0' : b].join('#'); }).join('/')
    .replace(/MINIOR(RED|ORANGE|YELLOW|GREEN|BLUE|INDIGO|VIOLET)\b/g, 'MINIOR');
const out = [];
let crashes = 0;
for (const r of rows) {
    let eo;
    try { eo = execFileSync(ENGINE, {input: `${r.state}\t${r.m1}\t${r.m2}\n`, stdio: ['pipe', 'pipe', 'ignore']}).toString().trim(); }
    catch (e) { crashes++; out.push({...r, crash: true}); continue; }
    if (eo.startsWith('ERROR')) continue;
    // Both sides normalized the same way (a battle that ends leaves Showdown's fainted Pokémon with its status).
    const engine = new Map(), sd = new Map(), maxhp = r.maxhp;
    for (const [k, p] of r.sd) { const n = norm(k); sd.set(n, (sd.get(n) || 0) + p); }
    for (const part of eo.split('\t')) { const i = part.indexOf('|'), k = norm(part.slice(i + 1)); engine.set(k, (engine.get(k) || 0) + parseFloat(part.slice(0, i)) / 100); }
    // KO_SPLIT=1 (Gen 3): a KO sends the replacement in mid-turn, before end-of-turn effects, so Showdown's KO samples
    // stop there while poke-engine plays the turn out. Then which actives faint is compared with the actives and
    // statuses, and HP and boosts only over the outcomes without a KO.
    const ko = k => k.split('/').map(x => { const [a, m] = x.split('#'); return m.split(';').some(y => y.startsWith(a + ':0:')) ? 'KO' : 'up'; }).join(',');
    const noKo = dist => { const m = new Map([...dist].filter(([k]) => !ko(k).includes('KO'))), t = [...m.values()].reduce((a, b) => a + b, 0);
        return t > 0.05 ? new Map([...m].map(([k, p]) => [k, p / t])) : null; };
    const parts = k => { const sides = k.split('/').map(x => x.split('#'));
        return {cat: sides.map(([a, m]) => a + ':' + m.split(';').map(x => x.split(':')[2]).join(',')).join('|'),
            boost: sides.map(x => x[2]).join('|'), hp: sides.flatMap(([, m], i) => m.split(';').map((x, j) => +x.split(':')[1] / (maxhp[i][j] || 1)))}; };
    const marginal = (dist, f) => { const m = new Map(); for (const [k, p] of dist) { const v = f(parts(k)); m.set(v, (m.get(v) || 0) + p); } return m; };
    const tvdOf = (a, b) => { let t = 0; for (const k of new Set([...a.keys(), ...b.keys()])) t += Math.abs((a.get(k) || 0) - (b.get(k) || 0)); return t / 2; };
    const meanHp = dist => { const v = new Array(12).fill(0); for (const [k, p] of dist) parts(k).hp.forEach((h, i) => v[i] += h * p); return v; };
    const split = process.env.KO_SPLIT === '1', [sdU, enU] = split ? [noKo(sd), noKo(engine)] : [sd, engine];
    const [hs, he] = sdU && enU ? [meanHp(sdU), meanHp(enU)] : [[0], [0]];
    const catOf = dist => { const m = new Map(); for (const [k, p] of dist) { const v = parts(k).cat + (split ? '|' + ko(k) : ''); m.set(v, (m.get(v) || 0) + p); } return m; };
    const cat = tvdOf(catOf(sd), catOf(engine)), boost = sdU && enU ? tvdOf(marginal(sdU, x => x.boost), marginal(enU, x => x.boost)) : 0;
    const top = m => [...m.entries()].sort((a, b) => b[1] - a[1]).slice(0, 3);
    out.push({...r, cat: +cat.toFixed(3), boost: +boost.toFixed(3), tvd: +Math.max(cat, boost).toFixed(3),
        hp: +(100 * Math.max(...hs.map((h, i) => Math.abs(h - he[i])))).toFixed(1), engine: top(engine)});
}
fs.writeFileSync(OUT, out.map(r => JSON.stringify(r)).join('\n') + '\n');
console.log(JSON.stringify({rows: rows.length, scored: out.length, crashes}));
