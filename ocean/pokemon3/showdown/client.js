// Gen 3 Showdown client for our policy (2026-11-06; cut down from ocean/pokemon/showdown/client.ts, the Gen 1 bridge).
// Connects to a Showdown server, plays battles with the policy server (policy_server.c) choosing every action from the
// tracker's view (tracker.js), and logs each result.
//
// Usage: NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/showdown/client.js
//          --server ws://localhost:8000/showdown/websocket --name NAME --checkpoint CK.bin [--hidden 1024 --layers 2]
//          [--embed 32 --mid 64 (structured net only)]
//          [--format gen3ou] [--battles N] [--challenge USER | --accept | --ladder] [--sample]
//          [--team FILE | --team-dir DIR (a random team file per battle)] [--results FILE] [--logs DIR]
// Random formats need no team. Login: a local server (--no-security) takes any name; on a public server an unregistered
// name logs in without a password and a registered one needs PS_PASSWORD in the environment (never on the command
// line). [--status TEXT | --status-file FILE (first line, re-read every 10 s)] sets the visible status (bot label).
const fs = require('fs');
const path = require('path');
const {spawn} = require('child_process');
const readline = require('readline');
const {Teams} = require('pokemon-showdown');
const {Tracker} = require('./tracker.js');

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(`--${k}`); return i >= 0 ? args[i + 1] : d; };
const flag = k => args.includes(`--${k}`);
const server = opt('server', 'ws://localhost:8000/showdown/websocket'), name = opt('name');
const format = opt('format', 'gen3ou'), battlesWanted = Number(opt('battles', '1')), results = opt('results'), logs = opt('logs');
if (logs) fs.mkdirSync(logs, {recursive: true});
const clock = () => new Date().toISOString().slice(11, 19);
const userid = n => n.toLowerCase().replace(/[^a-z0-9]/g, '');

const teamFiles = opt('team-dir') ? fs.readdirSync(opt('team-dir')).filter(f => !f.startsWith('.') && !/\.(csv|json|md)$/.test(f)).map(f => path.join(opt('team-dir'), f))
  : opt('team') ? [opt('team')] : [];
let teamLabel = '';
function drawTeam() {
  if (format.includes('random') || !teamFiles.length) return 'null';
  const file = teamFiles[Math.floor(Math.random() * teamFiles.length)];
  teamLabel = path.basename(file);
  const text = fs.readFileSync(file, 'utf8');
  const sets = text.includes('|') && !text.includes('\n\n') ? Teams.unpack(text.trim()) : Teams.import(text);
  return Teams.pack(sets);
}

const policy = spawn(opt('policy-server', path.join(__dirname, '../../../build/pokemon3/showdown/policy_server')),
  [opt('checkpoint'), opt('hidden', '1024'), opt('layers', '2'), ...(flag('sample') ? ['--sample'] : []),
    // Structured-net widths (policy.pokemon_embed / pokemon_mid); the server tells the two nets apart by file size.
    ...(opt('embed') ? ['--embed', opt('embed')] : []), ...(opt('mid') ? ['--mid', opt('mid')] : [])],
  {stdio: ['pipe', 'pipe', 'inherit']});
const waiting = new Map();
readline.createInterface({input: policy.stdout}).on('line', line => {
  const sp = line.indexOf(' '), room = line.slice(0, sp), rest = line.slice(sp + 1);
  const cb = waiting.get(room); waiting.delete(room);
  if (cb) cb(rest);
});
const act = (room, v, mask) => new Promise(resolve => {
  waiting.set(room, resolve);
  policy.stdin.write(`${room} ${v.turn}\t${mask.join('')}\t${v.seen}\t${v.state}\n`);
});

const rooms = new Map();
let lastSearch = {}, researchPending = false;
const prior = results && fs.existsSync(results) ? fs.readFileSync(results, 'utf8').split('\n').filter(Boolean) : [];
let finished = prior.length, started = 0, wins = prior.filter(l => l.includes(' win ')).length;
const ws = new WebSocket(server);
const send = text => ws.send(text);
async function login(challstr) {
  const password = process.env.PS_PASSWORD;
  if (server.includes('localhost')) { send(`|/trn ${name},0,`); return; }
  if (!password) {
    const res = await fetch('https://play.pokemonshowdown.com/action.php?' + new URLSearchParams({act: 'getassertion', userid: userid(name), challstr}));
    const assertion = (await res.text()).trim();
    if (assertion.startsWith(';')) throw new Error(`${name} is registered: set PS_PASSWORD in the environment`);
    if (!res.ok || !/^[\w,;.+/=-]+$/.test(assertion)) throw new Error(`login server unavailable (HTTP ${res.status})`);
    send(`|/trn ${name},0,${assertion}`); return;
  }
  const res = await fetch('https://play.pokemonshowdown.com/api/login', {method: 'POST', body: new URLSearchParams({act: 'login', name, pass: password, challstr})});
  if (!res.ok) throw new Error(`login server unavailable (HTTP ${res.status})`);
  const data = JSON.parse((await res.text()).slice(1));
  if (!data.assertion || data.assertion.startsWith(';')) throw new Error(`login failed for ${name}`);
  send(`|/trn ${name},0,${data.assertion}`);
}
function status() {
  if (opt('status')) send(`|/status ${opt('status')}`);
  if (!opt('status-file')) return;
  let shown = '';
  const sync = () => { let t = ''; try { t = fs.readFileSync(opt('status-file'), 'utf8').split('\n')[0].trim(); } catch {}
    if (t && t !== shown) { shown = t; send(`|/status ${t}`); } };
  sync(); setInterval(sync, 10000);
}

function next() {
  if (started >= battlesWanted) return;
  if (flag('ladder')) { started++; send(`|/utm ${drawTeam()}`); send(`|/search ${format}`); }
  else if (opt('challenge')) { started++; send(`|/utm ${drawTeam()}`); send(`|/challenge ${opt('challenge')}, ${format}`); }
}

// Decide once per request, when both it and its turn's protocol have arrived (|turn| ends a turn's protocol);
// forced switches after a faint come mid-turn: decide on arrival.
async function decide(id, r, force = false) {
  const req = r.request;
  if (!req || req.wait || req.teamPreview || r.answered === req.rqid || !r.tracker || !(r.ready || req.forceSwitch || force)) return;
  r.answered = req.rqid; r.ready = false;
  r.tracker.update(r.lines); r.lines = [];
  r.tracker.request(req);
  const mask = r.tracker.mask(req), view = r.tracker.view();
  const out = await act(id, view, mask);
  const a = Number(out);
  let choice;
  if (!Number.isInteger(a)) { console.error(`${clock()} ${id}: policy server: ${out}`); choice = 'default'; }
  else if (a <= 3) choice = `move ${req.active && !req.forceSwitch?.[0] ? Math.min(a + 1, req.active[0].moves.length) : 1}`;
  else {
    const s = r.tracker.sides[r.tracker.me];
    const k = req.side.pokemon.findIndex(p => s.byName[p.ident.slice(p.ident.indexOf(':') + 1).trim()] === a - 4);
    choice = `switch ${k + 1}`;
  }
  send(`${id}|/choose ${choice}|${req.rqid}`);
  if (logs) fs.appendFileSync(`${logs}/${id}.log`, `# rqid ${req.rqid}: mask ${mask.join('')} action ${out} -> ${choice}\n# state ${view.state}\n# seen ${view.seen}\n`);
}

ws.onmessage = async event => {
  const text = String(event.data);
  const id = text.startsWith('>') ? text.slice(1, text.indexOf('\n')) : '';
  const lines = (id ? text.slice(text.indexOf('\n') + 1) : text).split('\n');
  for (const line of lines) {
    const f = line.split('|');
    if (f[1] === 'challstr') await login(f.slice(2).join('|'));
    else if (f[1] === 'popup') {
      console.log(`${clock()} ! Showdown: ${f.slice(2).join('|')}`);
      // the opponent is not online yet (e.g. a model still loading): challenge again in 15 s
      if (opt('challenge') && /was not found|is not accepting challenges|not online/i.test(line)) { started--; setTimeout(next, 15000); }
    }
    else if (f[1] === 'pm' && f[4]?.startsWith('/error ')) {
      console.log(`${clock()} ! Showdown: ${f[4].slice(7)}`);
      if (opt('challenge') && /was not found|not online/i.test(f[4])) { started--; setTimeout(next, 15000); }
    }
    else if (f[1] === 'updateuser' && userid(f[2] ?? '') === userid(name) && f[3] === '1' && !started) { status(); setTimeout(next, 1000); }
    else if (f[1] === 'pm' && flag('accept') && f[4]?.startsWith('/challenge ')) { send(`|/utm ${drawTeam()}`); send(`|/accept ${f[2].trim()}`); }
    // Only battles of our format (2026-11-06): Showdown sends every connection of an account all its battles, so a
    // second bot on the same account (another format) must not answer this one's, nor this one theirs.
    if (f[1] === 'updatesearch' && flag('ladder')) {
      // A match in another format on the same account (a second bot) cancels every search of the account; search
      // again while we are waiting for a battle of ours.
      // Decide 8 s later on the latest status: a match of ours clears the search a moment before it lists the game.
      lastSearch = JSON.parse(f[2] || '{}');
      if (!researchPending) {
        researchPending = true;
        setTimeout(() => {
          researchPending = false;
          const g = Object.keys(lastSearch.games ?? {});
          const ours = g.some(x => x.startsWith(`battle-${format}-`)) || [...rooms.values()].some(r => !r.result);
          if (started > finished - prior.length && !ours && !(lastSearch.searching ?? []).includes(format)) send(`|/search ${format}`);
        }, 8000);
      }
    }
    if (!id.startsWith(`battle-${format}-`)) continue;
    if (!rooms.has(id)) { rooms.set(id, {lines: [], players: {}, team: teamLabel}); policy.stdin.write(`${id} new\n`); }
    const r = rooms.get(id);
    if (logs && line) fs.appendFileSync(`${logs}/${id}.log`, line + '\n');
    if (f[1] === 'request') {
      if (!f[2]) continue;
      r.request = JSON.parse(f.slice(2).join('|'));
      const rqid = r.request.rqid;
      setTimeout(() => { if (rooms.get(id) === r && r.request?.rqid === rqid && r.answered !== rqid) decide(id, r, true); }, 30000);
      if (r.side === undefined) { r.side = r.request.side.id === 'p1' ? 0 : 1; r.tracker = new Tracker(r.side); }
      await decide(id, r);
      continue;
    }
    if (f[1] === 'error') { console.error(id, line); r.answered = undefined; continue; }
    if (f[1] === 'player' && f[3]) {
      r.players[f[2]] = [f[3], f[5] ?? ''];
      const all = Object.values(r.players);
      if (all.length === 2 && !r.announced) {  // one line per battle start, like the Gen 1 bot
        r.announced = true;
        const me = all.find(p => userid(p[0]) === userid(name)), foe = all.find(p => userid(p[0]) !== userid(name));
        console.log(`${clock()} ▶ battle ${finished + 1}: ${me?.[0]} (${me?.[1] || '?'}) vs ${foe?.[0]} (${foe?.[1] || '?'})`);
      }
    }
    // While the opponent replaces a fainted Pokemon we only wait; the env still shows our network that state (its only
    // legal action is 0, "wait"), so feed it before the replacement comes in, to keep the recurrent state in step.
    if (f[1] === 'faint' && r.tracker && !f[2].startsWith(r.tracker.me)) r.pendingWait = true;
    if (f[1] === 'switch' && r.pendingWait && r.tracker && !f[2].startsWith(r.tracker.me)) {
      r.pendingWait = false;
      r.tracker.update(r.lines); r.lines = [];
      await act(id, r.tracker.view(), [1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    }
    if (f[1] === 'turn' || f[1] === 'win' || f[1] === 'tie') r.pendingWait = false;
    if (f[1] === 'turn') r.turn = Number(f[2]);
    const rated = f[1] === 'raw' && f[2]?.startsWith(`${name}'s rating:`) ? /(\d+) &rarr; <strong>(\d+)<\/strong>/.exec(f[2]) : null;
    if (rated && r.result) console.log(`${clock()}   rating ${rated[1]} → ${rated[2]}`);
    r.lines.push(line);
    if (f[1] === 'turn') { r.ready = true; await decide(id, r); }
    if (f[1] === 'win' || f[1] === 'tie') {
      const won = f[1] === 'win' && userid(f[2] ?? '') === userid(name);
      finished++; if (won) wins++;
      r.result = f[1] === 'tie' ? 'tie' : won ? 'win' : 'loss';
      const foe = Object.values(r.players).find(p => userid(p[0]) !== userid(name))?.[0] ?? '?';
      console.log(`${clock()} ${r.result === 'win' ? '✔' : r.result === 'loss' ? '✘' : '='} ${r.result} vs ${foe} in ${r.turn ?? '?'} turns · record ${wins}-${finished - wins}${r.team ? ` · team ${r.team}` : ''}`);
      if (results) fs.appendFileSync(results, `${id} ${r.result} turns=${r.turn ?? 0} vs=${foe}${r.team ? ` team=${r.team}` : ''}\n`);
      policy.stdin.write(`${id} end\n`);
      setTimeout(() => { send(`|/leave ${id}`); rooms.delete(id); }, 3000);  // stay for the rating update
      if (finished - prior.length >= battlesWanted && !flag('accept')) setTimeout(() => process.exit(0), 3500);
      setTimeout(next, 1500);
    }
  }
};
ws.onerror = e => { console.error('websocket error', e.message ?? e); process.exit(1); };
