// Showdown client for our policy: connects to a Pokémon Showdown server, plays Gen 1 OU battles with the policy server
// (policy_server.c) choosing every action from the tracker's observation (tracker.ts), and logs each result.
//
// Usage: node build/pokemon/showdown_parity/client.js --server ws://localhost:8000/showdown/websocket --name NAME
//          --team TEAMFILE --checkpoint CK.bin [--battles N] [--challenge USER | --accept | --ladder] [--format gen1ou]
//          [--results FILE] [--status TEXT] [--logs DIR] [--accept-from USER (with --accept: only this user)]
//          [--teams ROTATION (instead of --team: lines "WEIGHT TEAMFILE NAME"; each battle draws a team by weight)]
// Login: a local server started with noguestsecurity accepts any name. On a public server an unregistered name logs in
// without a password; a registered one needs PS_PASSWORD in the environment (never on the command line).
import fs from 'fs';
import {spawn} from 'child_process';
import readline from 'readline';
import {Dex, Teams} from '@pkmn/sim';
import {Tracker} from './tracker';

const args = process.argv.slice(2);
const opt = (k: string, d?: string) => { const i = args.indexOf(`--${k}`); return i >= 0 ? args[i + 1] : d; };
const flag = (k: string) => args.includes(`--${k}`);
const server = opt('server', 'ws://localhost:8000/showdown/websocket')!;
const name = opt('name')!, format = opt('format', 'gen1ou')!, battlesWanted = Number(opt('battles', '1'));
const results = opt('results');
// --logs DIR: each battle's complete protocol as this player saw it, one file per battle (for review; replays of hidden
// ladder rooms are not public unless uploaded).
const logs = opt('logs');
if (logs) fs.mkdirSync(logs, {recursive: true});
const userid = (n: string) => n.toLowerCase().replace(/[^a-z0-9]/g, '');
const clock = () => new Date().toISOString().slice(11, 19);
const hex = (a: Uint8Array) => Array.from(a, x => x.toString(16).padStart(2, '0')).join('');

// Our team: level 100, Gen 1 rules (no ability or item), maximum stat experience and DVs, as our engine assumes.
function packTeam(file: string) {
  const sets = Teams.import(fs.readFileSync(file, 'utf8'))!;
  for (const set of sets) {
    set.level = 100; set.ability = 'No Ability'; set.item = '';
    set.evs = {hp: 255, atk: 255, def: 255, spa: 255, spd: 255, spe: 255};
    set.ivs = {hp: 31, atk: 31, def: 31, spa: 31, spd: 31, spe: 31};
  }
  return Teams.pack(sets);
}
// Team rotation (2026-10-28): with --teams, every battle draws one team by weight (a mixed strategy, e.g. the Nash mix
// of a team round robin), so repeat opponents cannot prepare for one team. The policy reads our side from Showdown's
// requests, so only /utm changes.
const rotation = opt('teams') ? fs.readFileSync(opt('teams')!, 'utf8').split('\n').filter(l => l.trim()).map(l => {
  const [w, file, label] = l.trim().split(/\s+/); return {weight: Number(w), team: packTeam(file), label: label ?? file};
}) : [{weight: 1, team: packTeam(opt('team')!), label: ''}];
let teamLabel = '';
function drawTeam() {
  let x = Math.random() * rotation.reduce((a, t) => a + t.weight, 0);
  const pick = rotation.find(t => (x -= t.weight) < 0) ?? rotation[rotation.length - 1];
  teamLabel = pick.label; return pick.team;
}

// Policy server: one recurrent state per battle room.
const policy = spawn('build/pokemon/policy_server', [opt('checkpoint')!, ...(flag('sample') ? ['--sample'] : [])],
  {stdio: ['pipe', 'pipe', 'inherit']});
const waiting = new Map<string, (a: number) => void>();
readline.createInterface({input: policy.stdout!}).on('line', line => {
  const [id, a] = line.split(' ');
  waiting.get(id)?.(Number(a)); waiting.delete(id);
});
const act = (room: string, obs: Uint8Array) => new Promise<number>(resolve => {
  waiting.set(room, resolve); policy.stdin!.write(`${room} ${hex(obs)}\n`);
});

interface Room { tracker?: Tracker; side?: number; lines: string[]; request?: any; answered?: number; ready?: boolean;
  players: Record<string, [string, string]>; announced?: boolean; timer?: boolean; heard?: number; turn?: number; result?: string;
  team?: string }
const rooms = new Map<string, Room>();
// Earlier results (a resumed run) carry over into the battle numbers and record; --battles counts this run only.
const prior = results && fs.existsSync(results) ? fs.readFileSync(results, 'utf8').split('\n').filter(Boolean) : [];
let finished = prior.length, started = 0, wins = prior.filter(l => l.includes(' win ')).length;
const ws = new WebSocket(server);
const send = (text: string) => ws.send(text);

function next() {
  if (started >= battlesWanted) return;
  if (flag('ladder')) { started++; send(`|/utm ${drawTeam()}`); send(`|/search ${format}`); }
  else if (opt('challenge')) { started++; send(`|/utm ${drawTeam()}`); send(`|/challenge ${opt('challenge')}, ${format}`); }
}

async function login(challstr: string) {
  const password = process.env.PS_PASSWORD;
  if (!password) {
    if (server.includes('localhost')) { send(`|/trn ${name},0,`); return; }
    // Unregistered name on a public server: the login server hands out an assertion without a password.
    const res = await fetch('https://play.pokemonshowdown.com/action.php?' + new URLSearchParams({act: 'getassertion', userid: name, challstr}));
    const assertion = (await res.text()).trim();
    if (assertion.startsWith(';')) throw new Error(`${name} is registered: set PS_PASSWORD in the environment`);
    // A real assertion is one line of hex/base64 fields; anything else (an outage page, an error) must never be sent.
    if (!res.ok || !/^[\w,;.+/=-]+$/.test(assertion)) throw new Error(`login server unavailable (HTTP ${res.status}): ${assertion.slice(0, 80)}`);
    send(`|/trn ${name},0,${assertion}`);
    return;
  }
  const body = new URLSearchParams({act: 'login', name, pass: password, challstr});
  const res = await fetch('https://play.pokemonshowdown.com/api/login', {method: 'POST', body});
  if (!res.ok) throw new Error(`login server unavailable (HTTP ${res.status})`);
  const data = JSON.parse((await res.text()).slice(1));
  if (!data.assertion || data.assertion.startsWith(';')) throw new Error(`login failed for ${name}`);
  send(`|/trn ${name},0,${data.assertion}`);
}

// Decide once per request, when both it and the protocol of the turn it belongs to have arrived (|turn| ends a turn's
// protocol; Showdown may send either first). Switch requests (after a faint) follow their protocol: decide on arrival.
async function decide(id: string, r: Room, force = false) {
  const req = r.request;
  if (!req || req.wait || r.answered === req.rqid || !r.tracker || !(r.ready || req.forceSwitch || force)) return;
  if (force) console.error(`${clock()} watchdog: answering ${id} rqid ${req.rqid} after 30 s without a decision`);
  r.answered = req.rqid; r.ready = false;
  r.tracker.update(r.lines, req); r.lines = [];
  const a = await act(id, r.tracker.observe(req));
  let choice: string;
  if (a <= 3) choice = `move ${a + 1}`;
  else if (a <= 9) {
    // The policy names a party index (submitted order); Showdown wants the Pokémon's current slot.
    const species = r.tracker.party[0][a - 4].species;
    choice = `switch ${req.side.pokemon.findIndex((p: any) => speciesOf(p) === species) + 1}`;
  } else choice = 'move 1';
  send(`${id}|/choose ${choice}|${req.rqid}`);
  if (logs) fs.appendFileSync(`${logs}/${id}.log`, `# decided rqid ${req.rqid}: action ${a} -> ${choice}\n`);
}
const dex = Dex.forGen(1);
const speciesOf = (p: any) => dex.species.get(p.details.split(',')[0]).num;

ws.onmessage = async (event: any) => {
  const text = String(event.data);
  if (process.env.CLIENT_DEBUG) console.error('<<', text.slice(0, 300).replace(/\n/g, ' \\n '));
  const id = text.startsWith('>') ? text.slice(1, text.indexOf('\n')) : '';
  const lines = (id ? text.slice(text.indexOf('\n') + 1) : text).split('\n');
  for (const line of lines) {
    const f = line.split('|');
    if (f[1] === 'challstr') await login(f.slice(2).join('|'));
    // Server notices (battle bans, locks, refused status changes) arrive as popups or /error PMs; print them, since a
    // banned client otherwise just waits on a search that never matches (29 Oct).
    else if (f[1] === 'popup') console.log(`${clock()} ! Showdown: ${f.slice(2).join('|').replace(/\|\|/g, ' ')}`);
    else if (f[1] === 'pm' && f[4]?.startsWith('/error ')) console.log(`${clock()} ! Showdown: ${f[4].slice(7)}`);
    else if (f[1] === 'updateuser' && f[2]?.trim().toLowerCase().replace(/[^a-z0-9]/g, '') ===
             name.toLowerCase().replace(/[^a-z0-9]/g, '') && f[3] === '1' && !started) {
      if (opt('status')) send(`|/status ${opt('status')}`);  // e.g. "AI bot (PufferLib RL)": how other players see it's a bot
      setTimeout(next, 2000);
    }
    // Incoming challenges arrive as a PM "/challenge FORMAT" (older servers: updatechallenges).
    else if (f[1] === 'pm' && flag('accept') && f[4]?.startsWith('/challenge ') && f[4].slice(11).split('|')[0] === format
             && (!opt('accept-from') || userid(f[2]) === userid(opt('accept-from')!))) {
      send(`|/utm ${drawTeam()}`); send(`|/accept ${f[2].trim()}`);
    }
    else if (f[1] === 'updatechallenges' && flag('accept')) {
      const c = JSON.parse(f[2]).challengesFrom ?? {};
      for (const [user, fmt] of Object.entries(c)) if (fmt === format && (!opt('accept-from') || userid(user) === userid(opt('accept-from')!))) { send(`|/utm ${drawTeam()}`); send(`|/accept ${user}`); }
    }
    // After a reconnect, rejoin unfinished battles (Showdown replays their log and resends the request).
    if (f[1] === 'updatesearch') {
      const games = JSON.parse(f[2]).games ?? {};
      for (const room of Object.keys(games)) if (room.startsWith('battle-') && !rooms.has(room)) send(`|/join ${room}`);
    }
    if (!id.startsWith('battle-')) continue;
    if (!rooms.has(id)) { rooms.set(id, {lines: [], players: {}, team: teamLabel}); policy.stdin!.write(`${id} new\n`); }
    const r = rooms.get(id)!;
    if (logs && line) fs.appendFileSync(`${logs}/${id}.log`, line + '\n');
    if (f[1] === 'request') {
      if (!f[2]) continue;
      r.request = JSON.parse(f.slice(2).join('|'));
      // Watchdog: a move request must never stay unanswered (one battle hung at turn 1 waiting for its |turn| pairing).
      const rqid = r.request.rqid;
      setTimeout(() => { if (rooms.get(id) === r && r.request?.rqid === rqid && r.answered !== rqid) decide(id, r, true); }, 30000);
      if (r.side === undefined) { r.side = r.request.side.id === 'p1' ? 0 : 1; r.tracker = new Tracker(r.side); }
      await decide(id, r);
      continue;
    }
    if (f[1] === 'error') { console.error(id, line); r.answered = undefined; continue; }
    r.heard = Date.now();  // any protocol line means the battle is moving
    // Dashboard: one line when a battle starts (both players and ratings), one when it ends.
    if (f[1] === 'player' && f[3]) {
      r.players[f[2]] = [f[3], f[5] ?? ''];
      const all = Object.values(r.players);
      if (all.length === 2 && !r.announced) {
        r.announced = true;
        const me = all.find(p => p[0] === name), foe = all.find(p => p[0] !== name);
        console.log(`${clock()} ▶ battle ${finished + 1}: ${me?.[0]} (${me?.[1] || '?'}) vs ${foe?.[0]} (${foe?.[1] || '?'})${r.team ? ` · team ${r.team}` : ''}`);
      }
    }
    if (f[1] === 'turn') r.turn = Number(f[2]);
    const rated = f[1] === 'raw' && f[2]?.startsWith(`${name}'s rating:`) ? /(\d+) &rarr; <strong>(\d+)<\/strong>/.exec(f[2]) : null;
    if (rated && r.result) console.log(`${clock()}   rating ${rated[1]} → ${rated[2]}`);
    r.lines.push(line);
    if (f[1] === 'turn') { r.ready = true; await decide(id, r); }
    if (f[1] === 'win' || f[1] === 'tie') {
      const won = f[1] === 'win' && f[2]?.trim() === name;
      finished++; if (won) wins++;
      r.result = f[1] === 'tie' ? 'tie' : won ? 'win' : 'loss';
      const msg = `${id} ${r.result} (${wins}/${finished})${r.team ? ` team=${r.team}` : ''}`;
      const foe = Object.values(r.players).find(p => p[0] !== name)?.[0] ?? '?';
      console.log(`${clock()} ${won ? '✔' : f[1] === 'tie' ? '=' : '✘'} ${r.result} vs ${foe} in ${r.turn ?? '?'} turns · record ${wins}-${finished - wins}`);
      if (results) fs.appendFileSync(results, msg + '\n');
      policy.stdin!.write(`${id} end\n`);
      setTimeout(() => { send(`|/leave ${id}`); rooms.delete(id); }, 3000);  // stay for the rating update
      if (finished - prior.length >= battlesWanted && !flag('accept')) { policy.stdin!.end(); ws.close(); process.exit(0); }
      next();
    }
  }
};
// No battle timer by default (normal etiquette). Only when we have answered and then heard nothing for 3 minutes
// (the opponent went quiet or left) do we turn it on; Showdown then gives them their usual time to come back.
setInterval(() => {
  for (const [id, r] of rooms) {
    if (r.timer || !r.request || r.answered !== r.request.rqid || Date.now() - (r.heard ?? Date.now()) < 180000) continue;
    r.timer = true; send(`${id}|/timer on`);
    console.log(`${clock()}   opponent idle 3 min in ${id}: battle timer on`);
  }
}, 15000);

ws.onerror = (e: any) => { console.error('websocket error', e.message ?? e); process.exit(1); };
