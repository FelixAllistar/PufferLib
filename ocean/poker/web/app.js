import Core from './Controller.bend';

const $ = id => document.getElementById(id);
const ranks = '23456789TJQKA', suits = ['♣','♦','♥','♠'];
const categories = ['High card','One pair','Two pair','Three of a kind','Straight','Flush','Full house','Four of a kind','Straight flush'];
const streets = ['Preflop','Flop','Turn','River'];
const presetKinds = ['min','first','second','pot','all'];
const seatColors = ['#00bbaa','#885ac4','#c44b36','#4e87bc','#b4762f','#638054','#9e5d94','#4c8c94','#8170b7'];
const names = {rules:'Rules', range:'Range', fullhouse:'Fullhouse', river:'River candidate'};
const descriptions = {
  mixed:'A mixed field of Rules, Range and Fullhouse bots.',
  rules:'The original fast rules bot. A familiar, straightforward opponent.',
  range:'Our range bot samples possible hidden hands before making its decision.',
  fullhouse:'The external Fullhouse learned policy, running locally at your table.',
  river:'Pro’s experimental heads-up river candidate from PR #5. Standard sampled mode.'
};
let settings = {players:6, opponent:'mixed', pace:600};
let worker, state, busy = true, failed = false, generation = 0, requestId = 0, pending = new Map();
let session = Core.empty(), lastActions = [], recordedHand = -1, selectedRaise = 0, selectedPreset = null, sound = true, audio;
let actionEffects = new Map(), latestAction = '';
let history = [], botTimer, startedAt = 0, lastBotTime = 0;
const fmt = chips => (chips / 100).toLocaleString('en-US',{maximumFractionDigits:2});
const signed = chips => `${chips > 0 ? '+' : chips < 0 ? '−' : ''}${fmt(Math.abs(chips))}`;
const seat = (s,p) => s.slice(24+9*p,33+9*p);
const isDone = () => state && state[1] === 255;
const heroTurn = () => state && Core.phase(isDone(),state[1],0) === 1;

function policyFor(p) {
  if (settings.opponent === 'mixed') {
    const field=settings.players===9?['rules','range']:['rules','range','fullhouse'];
    return field[(p-1)%field.length];
  }
  return settings.opponent;
}
function label(p) { return p === 0 ? 'You' : `${names[policyFor(p)]} ${String(p).padStart(2,'0')}`; }
function card(id, animate=false) {
  if (id === 52) return '<span class="card back" aria-label="Face-down card"></span>';
  const raw = ranks[id >> 2], rank = raw === 'T' ? '10' : raw, suit = suits[id & 3];
  return `<span class="card${(id & 3) === 1 || (id & 3) === 2 ? ' red' : ''}${animate ? ' dealt' : ''}" aria-label="${rank}${suit}"><span class="corner">${rank}<small>${suit}</small></span><span class="suit">${suit}</span><span class="lower">${rank}</span></span>`;
}
function unlockAudio() {
  if(!sound)return;
  try {audio??=new AudioContext();if(audio.state==='suspended')audio.resume();}catch{}
}
document.addEventListener('pointerdown',unlockAudio,{capture:true});
document.addEventListener('keydown',unlockAudio,{capture:true});
function soundEffect(kind) {
  if (!sound || document.hidden || !audio || audio.state!=='running') return;
  try {
    const now=audio.currentTime;
    const tone=(freq,start,duration,volume=.035,end=freq)=>{
      const osc=audio.createOscillator(),gain=audio.createGain();osc.type='sine';
      osc.frequency.setValueAtTime(freq,start);osc.frequency.exponentialRampToValueAtTime(end,start+duration);
      gain.gain.setValueAtTime(.0001,start);gain.gain.exponentialRampToValueAtTime(volume,start+.005);gain.gain.exponentialRampToValueAtTime(.0001,start+duration);
      osc.connect(gain).connect(audio.destination);osc.start(start);osc.stop(start+duration);
    };
    if(kind==='turn'){tone(660,now,.18,.04);tone(880,now+.15,.28,.04);}
    else if(kind==='check'){tone(320,now,.045,.05,150);tone(320,now+.09,.045,.04,150);}
    else if(kind==='fold'){tone(480,now,.13,.035,90);}
    else if(kind==='deal'){tone(900,now,.055,.02,300);tone(1100,now+.06,.055,.02,350);}
    else if(kind==='win'){[660,830,990].forEach((f,i)=>tone(f,now+i*.11,.25,.035));}
    else {const n=kind==='raise'?3:2;for(let i=0;i<n;i++)tone(1500+i*170,now+i*.055,.075,.025,900);}
  } catch {}
}
function tellError(message) {
  $('error').textContent = message; $('error').hidden = false;
  setTimeout(() => { $('error').hidden = true; },7000);
}
function request(command, args={}) {
  return new Promise((resolve,reject) => {
    const id = ++requestId;
    pending.set(id,{resolve,reject}); worker.postMessage({id,command,...args});
  });
}
function addHistory(actor, text, amount='', kind='') {
  history.unshift({actor,text,amount,kind});
  history = history.slice(0,120);
  $('history').innerHTML = history.map(h => `<li class="${h.kind}"><span>${h.actor ? `<span class="history-actor">${h.actor}</span> ` : ''}${h.text}</span><span class="history-amount">${h.amount}</span></li>`).join('');
}
function logAction(before, action) {
  const p = before[1], r = seat(before,p), call = Math.min(before[4]-r[1],r[0]);
  let verb, amount='';
  if (action < 0) verb = 'folds';
  else if (action === 0) {verb = call ? 'calls' : 'checks'; if(call) amount = `${fmt(call)} BB`;}
  else {verb = before[4] ? 'raises to' : 'bets'; amount = `${fmt(action)} BB`;}
  if (action >= 0 && (action > 0 ? action-r[1] : call) === r[0]) verb += ' · all in';
  lastActions[p] = action < 0 ? 'Folded' : `${verb.charAt(0).toUpperCase()+verb.slice(1)}${amount ? ` ${amount}` : ''}`;
  const kind=action<0?'fold':action>0?'raise':call?'call':'check';
  actionEffects.set(p,{kind,at:performance.now()});
  latestAction=`${label(p)} ${verb}${amount?' '+amount:''}`;
  soundEffect(kind);
  addHistory(label(p),verb,amount);
}
function position(p) {
  const sb=settings.players===2?state[7]:(state[7]+1)%settings.players,bb=(sb+1)%settings.players;
  return p===bb?{short:'BB',name:'Big blind',posted:100}:p===sb?{short:'SB',name:'Small blind',posted:50}:p===state[7]?{short:'D',name:'Button',posted:0}:{short:'',name:`Seat ${p+1}`,posted:0};
}
function presetAmount(kind) {
  return Core.preset(BigInt(presetKinds.indexOf(kind)),state[2],state[4],state[3],state[12],100,state[10],state[11]);
}
function setRaise(value, preset=null) {
  if (!state) return;
  selectedPreset = preset;
  selectedRaise = Core.raise_target(Math.max(0,Math.round(value)),state[10],state[11]);
  $('raise-input').value = fmt(selectedRaise).replaceAll(',','');
  $('raise-slider').value = selectedRaise;
  $('raise-button-value').textContent = `${fmt(selectedRaise)} BB`;
  document.querySelectorAll('[data-size]').forEach(b=>b.classList.toggle('selected',b.dataset.size===preset));
}
function render(previous) {
  const done = isDone(), myTurn = heroTurn() && !busy && !failed;
  const newHand = !previous || state[15] !== previous[15];
  $('stage').dataset.seats = settings.players;
  $('table-type').textContent = `${settings.players} seats${settings.players === 2 ? ' · Heads-up' : ''}`;
  $('street-label').textContent = done ? 'HAND COMPLETE' : streets[state[2]].toUpperCase();
  $('hand-label').textContent = `HAND ${String(state[15]).padStart(3,'0')}`;
  $('pot').textContent = `${fmt(done ? state.slice(24).filter((_,i)=>i%9===2).reduce((a,b)=>a+b,0) : state[3])} BB`;
  $('board').innerHTML = Array.from({length:5},(_,i)=>i < state[13] ? card(state[16+i],newHand || !previous || i >= previous[13]) : '<span class="card empty" aria-hidden="true"></span>').join('');
  $('seats').innerHTML = Array.from({length:settings.players},(_,p) => {
    const r=seat(state,p), angle=Math.PI/2+2*Math.PI*p/settings.players;
    const x=50+(settings.players===9?37:35)*Math.cos(angle), y=p===0?83:46+32*Math.sin(angle);
    const pos=position(p), acted=lastActions[p] || (pos.posted?`POST ${pos.short} · ${fmt(pos.posted)} BB`:'');
    const effect=actionEffects.get(p),elapsed=effect?performance.now()-effect.at:2000;
    const effectClass=elapsed<1600?` effect-${effect.kind}`:'';
    const stageRect=$('stage').getBoundingClientRect();
    const effectStyle=`--effect-delay:-${Math.max(0,elapsed)}ms;--to-pot-x:${(50-x)*stageRect.width/100}px;--to-pot-y:${(39-y)*stageRect.height/100}px;`;
    const visibleBefore=previous?seat(previous,p)[3]:52;
    const animate=newHand || (r[3]!==52 && visibleBefore===52);
    const betTop = p===0 ? '-20px' : y<32 ? 'calc(100% + 10px)' : y>60 ? '-18px' : '60%';
    const betX = p!==0 && y>=32 && y<=60 ? (x<50?'130%':'-30%') : '50%';
    const status=!r[5]?'FOLDED':done&&r[7]!==0?`${signed(r[7])} BB${r[6]>=0?` · ${categories[r[6]]}`:''}`:r[0]===0?'ALL IN':!done&&p===state[1]?p===0?'YOUR TURN':'THINKING…':acted;
    return `<div class="seat${p===0?' hero':''}${!r[5]?' folded':''}${!done&&p===state[1]?' active':''}${effectClass}" style="--x:${x}%;--y:${y}%;--bet-top:${betTop};--bet-x:${betX};--seat-color:${seatColors[p]};${effectStyle}" data-seat="${p}" aria-label="${label(p)}, ${fmt(r[0])} big blinds${!r[5]?', folded':''}"><div class="seat-cards">${card(r[3],animate)}${card(r[4],animate)}</div><div class="seat-info">${state[7]===p?'<span class="dealer" title="Dealer button">D</span>':''}<span class="seat-number">${p===0?'Y':p}</span><div class="seat-stack">${fmt(r[0])}<small>BB</small></div></div><div class="seat-name">${label(p)}${pos.short?` <span class="position-tag">${pos.short}</span>`:''}</div><div class="seat-status">${status}</div>${elapsed<600&&(effect.kind==='call'||effect.kind==='raise')?'<span class="action-chip bet-chip" aria-hidden="true"></span>':''}${elapsed<1000&&effect.kind==='check'?'<span class="check-effect" aria-hidden="true">✓</span>':''}${!done&&r[1]>0?`<div class="seat-bet${previous&&r[1]!==seat(previous,p)[1]?' chip-enter':''}"><span class="bet-chip"></span>${fmt(r[1])}</div>`:''}</div>`;
  }).join('');
  $('betting-controls').hidden = done;
  $('betting-controls').style.display = done ? 'none' : '';
  $('finished-controls').hidden = !done;
  const hero=seat(state,0);
  $('hand-rank').textContent = hero[6]>=0 && hero[5] ? categories[hero[6]] : '';
  $('raise-action-label').textContent = state[4] ? 'Raise to' : 'Bet';
  const canRaise = myTurn && !!(state[9]&~3);
  $('fold-button').disabled = !myTurn || !(state[9]&1);
  $('call-button').disabled = !myTurn;
  $('call-button').innerHTML = state[12] ? `Call<strong>${fmt(state[12])} BB</strong>` : 'Check <kbd>C</kbd>';
  $('raise-button').disabled = !canRaise;
  $('raise-input').disabled = !canRaise; $('raise-slider').disabled = !canRaise;
  $('raise-minus').disabled = !canRaise; $('raise-plus').disabled = !canRaise;
  document.querySelectorAll('[data-size]').forEach(b=>{
    const kind=b.dataset.size, preflop=state[2]===0, opened=state[4]>100;
    const labels={min:'Min',first:preflop?(opened?'3× bet':'3 BB'):'½ pot',second:preflop?(opened?'5× bet':'5 BB'):'¾ pot',pot:'Pot',all:'All in'};
    const amount=myTurn?presetAmount(kind):0;
    b.disabled=!canRaise;b.dataset.amount=String(amount);
    b.querySelector('span').textContent=labels[kind];
    b.querySelector('strong').textContent=myTurn?`${fmt(amount)}`:'—';
    b.title=`${labels[kind]}${myTurn?` · ${fmt(amount)} BB total`:''}`;
    b.setAttribute('aria-label',b.title);
    b.classList.toggle('selected',kind===selectedPreset);
  });
  $('raise-slider').min = Math.min(state[10],state[11]); $('raise-slider').max = state[11]; $('raise-slider').step = 1;
  $('raise-input').min = Math.min(state[10],state[11])/100; $('raise-input').max = state[11]/100;
  if (myTurn && (!previous || previous[1]!==0 || previous[8]!==state[8])) setRaise(presetAmount('first'),'first');
  $('call-hint').textContent = myTurn ? state[12] ? `${fmt(state[12])} BB to call` : 'Check for free' : '';
  const ownPosition=position(0);
  $('hero-position').textContent=`You: ${ownPosition.name}${ownPosition.posted?` · posted ${fmt(ownPosition.posted)} BB`:''}`;
  $('last-action').textContent=latestAction;
  $('action-area').classList.toggle('your-turn',myTurn);
  const message=done ? hero[7]>0 ? `You win ${signed(hero[7])} BB` : hero[7]===0 ? 'An even hand' : `${label(state.slice(24).findIndex((_,i)=>i%9===7&&state[24+i]>0)/9|0)} takes the pot` : myTurn ? 'The action is on you' : `${label(state[1])} is thinking…`;
  $('table-message').textContent = message;
  $('table-message').classList.toggle('result',done);
  $('turn-label').textContent = failed ? 'Table paused · open a new table to retry' : done ? 'Hand complete' : myTurn ? 'Your turn' : `${label(state[1])} to act`;
  $('turn-dot').style.background = myTurn ? 'var(--gold)' : 'var(--mint)';
  $('turn-label').parentElement.classList.toggle('thinking',!myTurn&&!done);
  if (done) {
    $('result-title').textContent = hero[7]>0 ? `A nice hand. ${signed(hero[7])} BB` : hero[7]===0 ? 'All square.' : `Next hand, fresh start. ${signed(hero[7])} BB`;
    $('result-detail').textContent = state[14] && hero[5] ? `Showdown · ${categories[hero[6]]}` : hero[5] ? 'The table folded to you.' : 'You folded. The hand has finished.';
    $('next-button').disabled=busy||failed;
  }
  const net=session.won-session.lost;
  $('session-net').innerHTML=`${signed(net)}<span>BB</span>`;
  $('session-net').className=`session-net${net>0?' positive':net<0?' negative':''}`;
  $('session-hands').textContent=session.hands; $('session-wins').textContent=session.wins;
  $('opponent-description').textContent=settings.players===9&&settings.opponent==='mixed'?'A mixed field of Rules and Range bots.':descriptions[settings.opponent];
}
function acceptState(next, before) {
  state=next;
  if (before && state[2]!==before[2] && (!isDone() || state[14])) {
    lastActions=lastActions.map(a=>a==='Folded'?a:'');
    addHistory('',streets[state[2]].toUpperCase(),'','history-street'); soundEffect('deal');
  }
  if (isDone() && recordedHand!==state[15]) {
    const net=seat(state,0)[7]; session=Core.record(Math.max(0,net),Math.max(0,-net),session); recordedHand=state[15];
    if(net>0)soundEffect('win');
    addHistory('You',net>0?'win':net<0?'finish':'split',`${signed(net)} BB`);
  }
  render(before);
  if(heroTurn()) soundEffect('turn');
  scheduleBot();
}
function scheduleBot() {
  clearTimeout(botTimer);
  if (!state || isDone() || heroTurn() || failed || busy) return;
  const tableGeneration=generation;
  botTimer=setTimeout(async()=>{
    if (generation!==tableGeneration) return;
    busy=true; const before=state; render(before);
    try {
      const kind=policyFor(before[1]); const t=performance.now();
      const reply=await request('bot',{policy:kind==='rules'?1:kind==='fullhouse'?4:7});
      lastBotTime=performance.now()-t;
      if(generation!==tableGeneration)return;
      logAction(before,reply.action); busy=false; acceptState(reply.state,before);
    } catch(error) {if(generation!==tableGeneration)return; fail(error);}
  },settings.pace);
}
function fail(error) {busy=false;failed=true;clearTimeout(botTimer);tellError(`The table paused: ${error.message}. Open Table settings to start again.`);if(state)render(state);}
async function act(action) {
  if(busy||!heroTurn()||failed)return;
  const kind=action<0?0n:action===0?1n:2n;
  if(!Core.allowed(isDone(),state[1],0,kind,state[12],Number(!!(state[9]&~3)),state[4],state[10],state[11],Math.max(0,action)))return;
  const before=state, tableGeneration=generation; busy=true;render(before);
  try {const reply=await request('act',{action});if(generation!==tableGeneration)return;logAction(before,action);busy=false;acceptState(reply.state,before);}catch(error){if(generation===tableGeneration)fail(error);}
}
async function openTable(nextSettings) {
  generation++;clearTimeout(botTimer);worker?.terminate();pending.forEach(p=>p.reject(Error('Table replaced.')));pending.clear();
  settings=nextSettings; busy=true;failed=false;state=undefined;history=[];lastActions=[];recordedHand=-1;session=Core.empty();actionEffects.clear();latestAction='';
  $('history').innerHTML='';$('table-message').textContent='Shuffling the deck…';$('turn-label').textContent='Opening the table';
  worker=new Worker(new URL('./worker.js',import.meta.url),{type:'module'});
  worker.onmessage=({data})=>{const p=pending.get(data.id);if(!p)return;pending.delete(data.id);data.error?p.reject(Error(data.error)):p.resolve(data);};
  worker.onerror=e=>{pending.forEach(p=>p.reject(Error(e.message||'The poker engine could not load.')));pending.clear();fail(Error('The poker engine could not load.'));};
  startedAt=Date.now();
  const tableGeneration=generation;
  try {
    const seed=crypto.getRandomValues(new Uint32Array(1))[0];
    const reply=await request('init',{players:settings.players,experimental:settings.opponent==='river',seed});
    if(generation!==tableGeneration)return;busy=false;addHistory('','PREFLOP','','history-street');acceptState(reply.state);soundEffect('deal');
  }catch(error){if(generation===tableGeneration)fail(error);}
}
$('fold-button').onclick=()=>act(-1);$('call-button').onclick=()=>act(0);$('raise-button').onclick=()=>act(selectedRaise);
$('raise-input').onchange=e=>setRaise(Number(e.target.value)*100);$('raise-slider').oninput=e=>setRaise(Number(e.target.value));
$('raise-minus').onclick=()=>setRaise(Math.max(0,selectedRaise-100));$('raise-plus').onclick=()=>setRaise(selectedRaise+100);
document.querySelectorAll('[data-size]').forEach(b=>b.onclick=()=>{
  if(!heroTurn()||busy)return;
  setRaise(presetAmount(b.dataset.size),b.dataset.size);
});
$('next-button').onclick=async()=>{
  if(busy||!isDone()||failed)return;
  busy=true;render(state);lastActions=[];actionEffects.clear();latestAction='';addHistory('',`HAND ${state[15]+1} · PREFLOP`,'','history-street');
  const tableGeneration=generation;
  try {const reply=await request('next');if(generation!==tableGeneration)return;const before=state;busy=false;acceptState(reply.state,before);soundEffect('deal');}catch(error){if(generation===tableGeneration)fail(error);}
};
$('settings-button').onclick=()=>$('settings').showModal();$('help-button').onclick=()=>$('help').showModal();
function toggleHistory(open) {$('table-drawer').hidden=!open;$('history-button').setAttribute('aria-expanded',String(open));}
$('history-button').onclick=()=>toggleHistory($('table-drawer').hidden);$('close-drawer').onclick=()=>toggleHistory(false);
document.querySelectorAll('[data-close]').forEach(b=>b.onclick=()=>$(b.dataset.close).close());
function settingsDetail(){
  const pro=$('opponent').querySelector('[value="river"]');pro.disabled=$('seat-count').value!=='2';
  const fullhouse=$('opponent').querySelector('[value="fullhouse"]');fullhouse.disabled=$('seat-count').value==='9';
  if(pro.disabled&&$('opponent').value==='river')$('opponent').value='range';
  if(fullhouse.disabled&&$('opponent').value==='fullhouse')$('opponent').value='range';
  $('bot-detail').textContent=$('seat-count').value==='9'&&$('opponent').value==='mixed'?'The nine-seat house mix rotates Rules and Range. Fullhouse supports two or six seats.':descriptions[$('opponent').value];
}
$('seat-count').onchange=settingsDetail;$('opponent').onchange=settingsDetail;settingsDetail();
$('settings-form').onsubmit=e=>{e.preventDefault();$('settings').close();openTable({players:Number($('seat-count').value),opponent:$('opponent').value,pace:Number($('speed').value)});};
$('sound-button').onclick=()=>{sound=!sound;$('sound-button').textContent=sound?'Sound on':'Sound off';$('sound-button').setAttribute('aria-pressed',String(sound));if(sound){unlockAudio();soundEffect('turn');}};
document.addEventListener('keydown',e=>{
  if(e.key==='Escape')toggleHistory(false);
  if(e.repeat||e.ctrlKey||e.metaKey||e.altKey||document.querySelector('dialog[open]')||/INPUT|SELECT|TEXTAREA/.test(e.target.tagName))return;
  if(e.key.toLowerCase()==='f'&&!$('fold-button').disabled)act(-1);
  if(e.key.toLowerCase()==='c'&&!$('call-button').disabled)act(0);
  if(e.key===' '&&isDone()){e.preventDefault();$('next-button').click();}
});
document.addEventListener('visibilitychange',()=>{if(document.hidden)clearTimeout(botTimer);else scheduleBot();});
// A small inspectable surface for deterministic browser acceptance tests.
window.poker={snapshot:()=>state?.slice(),session:()=>({...session}),status:()=>({busy,failed,settings:{...settings},lastBotTime,startedAt})};
openTable(settings);
