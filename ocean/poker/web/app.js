import Core from './Controller.bend';
import Hand from './Hand.bend';
import Cash from './Cash.bend';
import {loadSession,saveSession} from './storage.js';
import {TableView} from './table.js';

const $ = id => document.getElementById(id);
const categories = ['High card','One pair','Two pair','Three of a kind','Straight','Flush','Full house','Four of a kind','Straight flush'];
const streets = ['Preflop','Flop','Turn','River'];
const presetKinds = ['min','first','second','pot','all'];
const names = {rules:'Rules', range:'Range', fullhouse:'Fullhouse', river:'River candidate'};
const descriptions = {
  mixed:'A mixed field of Rules, Range and Fullhouse bots.',
  rules:'The original fast rules bot. A familiar, straightforward opponent.',
  range:'Our range bot samples possible hidden hands before making its decision.',
  fullhouse:'The external Fullhouse learned policy, running locally at your table.',
  river:'Pro’s latest experimental heads-up river candidate from PR #5. Standard sampled mode.'
};
let settings = {players:6, opponent:'mixed', pace:600};
let worker, state, busy = true, failed = false, generation = 0, requestId = 0, pending = new Map();
let session = Core.empty(), lastActions = [], recordedHand = 0, selectedRaise = 0, selectedPreset = null, sound = true, audio;
let buyins=20000,tableBuyins=Array(6).fill(20000),latestCheckpoint=null;
let learningOpen=innerWidth>=1280,lastLearning=null,learningPending='',showBest=false;
const tableView=new TableView();
let latestAction = '';
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
function unlockAudio() {
  if(!sound)return;
  try {audio??=new AudioContext();if(audio.state==='suspended')audio.resume();}catch{}
}
document.addEventListener('pointerdown',unlockAudio,{capture:true});
document.addEventListener('keydown',unlockAudio,{capture:true});
function soundEffect(kind,p=0) {
  if (!sound || document.hidden || !audio || audio.state!=='running') return;
  try {
    const now=audio.currentTime;
    const output=audio.createStereoPanner();
    const actor=document.querySelector(`[data-seat="${p}"]`),stage=$('stage').getBoundingClientRect();
    const x=actor?actor.getBoundingClientRect().x+actor.offsetWidth/2-stage.x:stage.width/2;
    output.pan.value=Math.max(-.65,Math.min(.65,(x/stage.width-.5)*1.3));output.connect(audio.destination);setTimeout(()=>output.disconnect(),2000);
    // Short filtered noise gives cards a swish and chips a dry click.
    const noise=(duration,frequency,volume)=>{
      const buffer=audio.createBuffer(1,Math.ceil(audio.sampleRate*duration),audio.sampleRate),data=buffer.getChannelData(0);
      for(let i=0;i<data.length;i++)data[i]=Math.random()*2-1;
      const source=audio.createBufferSource(),filter=audio.createBiquadFilter(),gain=audio.createGain();
      source.buffer=buffer;filter.type='bandpass';filter.frequency.value=frequency;filter.Q.value=.7;
      gain.gain.setValueAtTime(volume,now);gain.gain.exponentialRampToValueAtTime(.0001,now+duration);
      source.connect(filter).connect(gain).connect(output);source.start(now);source.stop(now+duration);
    };
    const tone=(freq,start,duration,volume=.035,end=freq)=>{
      const osc=audio.createOscillator(),gain=audio.createGain();osc.type='sine';
      osc.frequency.setValueAtTime(freq,start);osc.frequency.exponentialRampToValueAtTime(end,start+duration);
      gain.gain.setValueAtTime(.0001,start);gain.gain.exponentialRampToValueAtTime(volume,start+.005);gain.gain.exponentialRampToValueAtTime(.0001,start+duration);
      osc.connect(gain).connect(output);osc.start(start);osc.stop(start+duration);
    };
    if(kind==='turn'){tone(660,now,.18,.04);tone(880,now+.15,.28,.04);}
    else if(kind==='check'){tone(190,now,.045,.07,95);tone(190,now+.12,.045,.06,95);}
    else if(kind==='fold'){noise(.22,1400,.13);tone(170,now,.09,.018,90);}
    else if(kind==='deal'){noise(.15,2800,.08);tone(900,now,.055,.02,300);}
    else if(kind==='win'){[660,830,990].forEach((f,i)=>tone(f,now+i*.11,.25,.035));}
    else if(kind==='pot'){noise(.25,3200,.09);tone(1100,now,.08,.025,600);}
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
  renderHistory();
}
const escape=value=>String(value).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
function renderHistory(){ $('history').innerHTML=history.map(h=>`<li class="${h.kind==='history-street'?'history-street':''}"><span>${h.actor?`<span class="history-actor">${escape(h.actor)}</span> `:''}${escape(h.text)}</span><span class="history-amount">${escape(h.amount)}</span></li>`).join(''); }
function persist(){
  if(!state||!latestCheckpoint)return;
  const ok=saveSession({version:1,settings,sound,session,buyins,tableBuyins,recordedHand,checkpoint:latestCheckpoint,publicState:state,history,lastActions,latestAction,selectedRaise,showBest});
  $('saved-state').textContent=ok?'Saved in this tab':'Tab storage unavailable';
}
function renderLearning(){
  $('learning-context').textContent=lastLearning?`Hand ${lastLearning.hand} · ${streets[lastLearning.street]} decision`:'Analysis appears at your next decision';
  if(!lastLearning){$('equity-value').textContent='—';$('hand-chances').innerHTML='';return;}
  const d=lastLearning.data,pct=(n,total)=>`${(Cash.percent(n,total)/100).toFixed(1)}%`;
  $('equity-value').textContent=pct(d[1],d[0]);
  $('equity-detail').textContent=`Tie ${pct(d[2],d[0])} · Equity ${(d[3]/100).toFixed(1)}% · ${d[15]} opponent${d[15]===1?'':'s'}`;
  $('call-odds').textContent=d[14]?`Call price: ${(d[14]/100).toFixed(1)}% of the eligible pot`:'No call required at this decision';
  $('draw-method').textContent=d[16]?`All ${d[4].toLocaleString()} possible board runouts`:'4,096 sampled board runouts';
  $('hand-chances').innerHTML=categories.map((name,i)=>{
    const n=d[5+i],basis=Cash.percent(n,d[4]),label=!n&&!d[16]?'Not sampled':basis>0&&basis<10?'<0.1%':`${(basis/100).toFixed(1)}%`;
    return `<div class="chance-row"><span>${name}</span><strong>${escape(label)}</strong><i style="--chance:${basis/100}%"></i></div>`;
  }).join('');
}
async function updateLearning(){
  if(!learningOpen||!heroTurn()||busy||failed||isDone())return;
  const key=`${generation}:${state[15]}:${state[8]}`;
  if(learningPending===key||lastLearning?.key===key)return;
  learningPending=key;const hand=state[15],street=state[2];
  try{const reply=await request('learn');if(key!==`${generation}:${state?.[15]}:${state?.[8]}`)return;lastLearning={key,data:reply.learning,hand,street};renderLearning();}
  catch{}finally{if(learningPending===key)learningPending='';}
}
function toggleLearning(open){learningOpen=open;$('learning-panel').hidden=!open;document.body.classList.toggle('learning-open',open);$('learn-button').setAttribute('aria-expanded',String(open));if(open){toggleHistory(false);renderLearning();updateLearning();}}
function logAction(before, action) {
  const p = before[1], r = seat(before,p), call = Math.min(before[4]-r[1],r[0]);
  let verb, amount='';
  if (action < 0) verb = 'folds';
  else if (action === 0) {verb = call ? 'calls' : 'checks'; if(call) amount = `${fmt(call)} BB`;}
  else {verb = before[4] ? 'raises to' : 'bets'; amount = `${fmt(action)} BB`;}
  if (action >= 0 && (action > 0 ? action-r[1] : call) === r[0]) verb += ' · all in';
  lastActions[p] = action < 0 ? 'Folded' : `${verb.charAt(0).toUpperCase()+verb.slice(1)}${amount ? ` ${amount}` : ''}`;
  const kind=action<0?'fold':action>0?'raise':call?'call':'check';
  tableView.noteAction(p,kind,amount,action>0?(before[4]?'Raise to':'Bet'):'Call');
  latestAction=`${label(p)} ${verb}${amount?' '+amount:''}`;
  soundEffect(kind,p);
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
  $('stage').dataset.seats = settings.players;
  $('table-type').textContent = `${settings.players} seats${settings.players === 2 ? ' · Heads-up' : ''}`;
  $('street-label').textContent = done ? 'HAND COMPLETE' : streets[state[2]].toUpperCase();
  $('hand-label').textContent = `HAND ${String(state[15]).padStart(3,'0')}`;
  $('pot').textContent = `${fmt(done ? state.slice(24).filter((_,i)=>i%9===2).reduce((a,b)=>a+b,0) : state[3])} BB`;
  tableView.render(state,{previous,label,position,focus:showBest});
  $('betting-controls').hidden = done;
  $('betting-controls').style.display = done ? 'none' : '';
  $('finished-controls').hidden = !done;
  const hero=seat(state,0);
  $('hand-rank').textContent = hero[6]>=0 && hero[5] ? Hand.name(state[21])+(state[23]?' · Board plays':'') : hero[5]?Hand.holding(hero[3],hero[4]):'You folded';
  $('hand-rank').setAttribute('aria-pressed',String(showBest));
  document.querySelector('.hand-summary>span').textContent=hero[6]>=0&&hero[5]?(showBest?'Show all cards':'Show best five'):'0.5 / 1 BB';
  $('learning-hand').textContent=hero[6]>=0?Hand.name(state[21]):Hand.holding(hero[3],hero[4]);
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
  const winners=Array.from({length:settings.players},(_,p)=>({p,r:seat(state,p)})).filter(({r})=>done&&r[0]-r[8]+r[2]>0);
  const awarded=winners.reduce((a,{r})=>a+r[0]-r[8]+r[2],0);
  const message=done?winners.length>1?`Pot awarded · ${fmt(awarded)} BB`:`${winners[0]?.p===0?'You win':`${label(winners[0]?.p??0)} wins`} ${fmt(awarded)} BB`:'';
  $('table-message').textContent = message;
  $('table-message').classList.toggle('result',done);
  $('turn-label').textContent = failed ? 'Table paused · open a new table to retry' : done ? 'Hand complete' : myTurn ? 'Your turn' : `${label(state[1])} to act`;
  $('turn-dot').style.background = myTurn ? 'var(--mint)' : '#a9b5bd';
  $('turn-label').parentElement.classList.toggle('thinking',!myTurn&&!done);
  if (done) {
    $('result-title').textContent = message;
    $('result-detail').textContent = `${state[14]&&hero[5]?Hand.name(state[21]):hero[5]?'Everyone folded':'You folded'} · Your result ${signed(hero[7])} BB`;
    $('next-button').innerHTML=hero[0]===0?'Rebuy 200 BB & deal <span>→</span>':'Deal next hand <span>→</span>';
    $('next-button').disabled=busy||failed;
  }
  const net=session.won-session.lost;
  $('session-net').innerHTML=`${signed(net)}<span>BB</span>`;
  $('session-net').className=`session-net${net>0?' positive':net<0?' negative':''}`;
  $('session-hands').textContent=session.hands; $('session-wins').textContent=session.wins;
  $('session-buyins').textContent=`${fmt(buyins)} BB`;
  $('cash-stack').textContent=`Stack ${fmt(hero[0])} BB · Net ${signed(net)} BB`;
  $('opponent-description').textContent=settings.players===9&&settings.opponent==='mixed'?'A mixed field of Rules and Range bots.':descriptions[settings.opponent];
}
function acceptState(next, before, checkpoint) {
  state=next;
  if(checkpoint)latestCheckpoint=checkpoint;
  if (before && state[2]!==before[2] && (!isDone() || state[14])) {
    lastActions=lastActions.map(a=>a==='Folded'?a:'');
    addHistory('',streets[state[2]].toUpperCase(),'','history-street'); soundEffect('deal');
  }
  if (Cash.should_record(isDone(),state[15],recordedHand)) {
    const net=seat(state,0)[7]; session=Core.record(Math.max(0,net),Math.max(0,-net),session); recordedHand=state[15];
    soundEffect(net>0?'win':'pot');
    addHistory('You',net>0?'win':net<0?'finish':'split',`${signed(net)} BB`);
  }
  render(before);
  persist();renderLearning();updateLearning();
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
      logAction(before,reply.action); busy=false; acceptState(reply.state,before,reply.checkpoint);
    } catch(error) {if(generation!==tableGeneration)return; fail(error);}
  },settings.pace===80?80:settings.pace===900?1300:900);
}
function fail(error) {busy=false;failed=true;clearTimeout(botTimer);tellError(`The table paused: ${error.message}. Open Table settings to start again.`);if(state)render(state);}
async function act(action) {
  if(busy||!heroTurn()||failed)return;
  const kind=action<0?0n:action===0?1n:2n;
  if(!Core.allowed(isDone(),state[1],0,kind,state[12],Number(!!(state[9]&~3)),state[4],state[10],state[11],Math.max(0,action)))return;
  const before=state, tableGeneration=generation; busy=true;render(before);
  try {const reply=await request('act',{action});if(generation!==tableGeneration)return;logAction(before,action);busy=false;acceptState(reply.state,before,reply.checkpoint);}catch(error){if(generation===tableGeneration)fail(error);}
}
async function openTable(nextSettings,saved=null) {
  generation++;clearTimeout(botTimer);worker?.terminate();pending.forEach(p=>p.reject(Error('Table replaced.')));pending.clear();
  tableView.s=null;tableView.version='';settings=nextSettings; busy=true;failed=false;state=undefined;history=[];lastActions=[];recordedHand=0;session=Core.empty();latestAction='';latestCheckpoint=null;buyins=20000;tableBuyins=Array(settings.players).fill(20000);lastLearning=null;
  $('history').innerHTML='';$('table-message').textContent='Shuffling the deck…';$('turn-label').textContent='Opening the table';
  worker=new Worker(new URL('./worker.js',import.meta.url),{type:'module'});
  worker.onmessage=({data})=>{const p=pending.get(data.id);if(!p)return;pending.delete(data.id);data.error?p.reject(Error(data.error)):p.resolve(data);};
  worker.onerror=e=>{pending.forEach(p=>p.reject(Error(e.message||'The poker engine could not load.')));pending.clear();fail(Error('The poker engine could not load.'));};
  startedAt=Date.now();
  const tableGeneration=generation;
  try {
    const seed=crypto.getRandomValues(new Uint32Array(1))[0];
    const reply=saved?await request('restore',{checkpoint:saved.checkpoint}):await request('init',{players:settings.players,experimental:settings.opponent==='river',seed});
    if(generation!==tableGeneration)return;
    if(saved){
      if(JSON.stringify(reply.state)!==JSON.stringify(saved.publicState))throw Error('Saved hand did not replay exactly.');
      session={...Core.empty(),...saved.session};buyins=saved.buyins;tableBuyins=saved.tableBuyins;recordedHand=saved.recordedHand;history=saved.history;lastActions=saved.lastActions;latestAction=typeof saved.latestAction==='string'?saved.latestAction.slice(0,200):'';sound=!!saved.sound;showBest=false;renderHistory();
    }else addHistory('','PREFLOP','','history-street');
    busy=false;acceptState(reply.state,undefined,reply.checkpoint);
    if(saved&&heroTurn()&&Number.isInteger(saved.selectedRaise))setRaise(saved.selectedRaise);
    $('sound-button').textContent=sound?'Sound on':'Sound off';$('sound-button').setAttribute('aria-pressed',String(sound));
    $('seat-count').value=String(settings.players);$('opponent').value=settings.opponent;$('speed').value=String(settings.pace);settingsDetail();soundEffect('deal');
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
  const previous=state,stacks=Array.from({length:settings.players},(_,p)=>Cash.next_stack(seat(state,p)[0],20000));
  busy=true;render(state);lastActions=[];latestAction='';lastLearning=null;
  const tableGeneration=generation;
  try {const reply=await request('next',{stacks});if(generation!==tableGeneration)return;
    for(let p=0;p<settings.players;p++)if(seat(previous,p)[0]===0){tableBuyins[p]+=20000;if(p===0)buyins+=20000;addHistory(label(p),'rebuys','200 BB');}
    addHistory('',`HAND ${reply.state[15]} · PREFLOP`,'','history-street');busy=false;acceptState(reply.state,previous,reply.checkpoint);soundEffect('deal');}catch(error){if(generation===tableGeneration)fail(error);}
};
$('settings-button').onclick=()=>$('settings').showModal();$('help-button').onclick=()=>$('help').showModal();
function toggleHistory(open) {$('table-drawer').hidden=!open;$('history-button').setAttribute('aria-expanded',String(open));if(open)toggleLearning(false);}
$('history-button').onclick=()=>toggleHistory($('table-drawer').hidden);$('close-drawer').onclick=()=>toggleHistory(false);
$('learn-button').onclick=()=>toggleLearning(!learningOpen);$('close-learning').onclick=()=>toggleLearning(false);
$('hand-rank').onclick=()=>{showBest=!showBest;render(state);persist();};
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
$('sound-button').onclick=()=>{sound=!sound;$('sound-button').textContent=sound?'Sound on':'Sound off';$('sound-button').setAttribute('aria-pressed',String(sound));if(sound){unlockAudio();soundEffect('turn');}persist();};
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'){toggleHistory(false);if(innerWidth<1200)toggleLearning(false);}
  if(e.repeat||e.ctrlKey||e.metaKey||e.altKey||document.querySelector('dialog[open]')||/INPUT|SELECT|TEXTAREA/.test(e.target.tagName))return;
  if(e.key.toLowerCase()==='f'&&!$('fold-button').disabled)act(-1);
  if(e.key.toLowerCase()==='c'&&!$('call-button').disabled)act(0);
  if(e.key===' '&&isDone()){e.preventDefault();$('next-button').click();}
});
document.addEventListener('visibilitychange',()=>{if(document.hidden)clearTimeout(botTimer);else scheduleBot();});
// A small inspectable surface for deterministic browser acceptance tests.
window.poker={snapshot:()=>state?.slice(),session:()=>({...session}),status:()=>({busy,failed,settings:{...settings},lastBotTime,startedAt,buyins,tableBuyins:[...tableBuyins]}),learning:()=>lastLearning?{...lastLearning}:null};
toggleLearning(learningOpen);
const saved=loadSession();
if(saved&&!saved.invalid)openTable(saved.settings,saved);else {openTable(settings);if(saved?.invalid)tellError('The saved session could not be read. A new cash table is open.');}
