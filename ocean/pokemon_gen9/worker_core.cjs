'use strict';
// Original simulator + per-player public client state. No mechanics are ported.
const path=require('path');
const {Dex,oracle,root,pin}=require('./reference.cjs');
const {Battle,extractChannelMessages}=require(oracle+'/dist/sim/battle');
const {Battle:ClientBattle}=require(path.join(root,'build/pokemon_gen9/worker-deps/node_modules/@pkmn/client'));
const {Generations}=require(path.join(root,'build/pokemon_gen9/worker-deps/node_modules/@pkmn/data'));
const cat=require(path.join(root,'build/pokemon_gen9/catalog/catalog.json'));
const gens=new Generations(Dex, data=>data.exists);
const ACTIONS=15,ABI=1;
const norm=s=>String(s||'').toLowerCase().replace(/[^a-z0-9]/g,'');
const sizes={};for(const key of ['species','abilities','items','types','moves','conditions'])
  sizes[key]=Math.max(...Object.values(cat.ids[key].map))+1;
const MON=32+sizes.species+sizes.abilities+sizes.items+2*sizes.types+sizes.moves;
const ACTIVE=8+sizes.conditions;
const MOVE=16+sizes.moves;
const OBS=32+3*sizes.conditions+12*MON+2*ACTIVE+4*MOVE;
function one(a,at,kind,id){const i=cat.ids[kind].map[norm(id)];a[at+(i===undefined?0:i)]=1;}
function keys(a,at,kind,object){for(const id of Object.keys(object||{}))one(a,at,kind,id);}
function maskFor(request,pending=false){
  const mask=new Uint8Array(ACTIONS);
  if(!request||request.wait||pending){mask[0]=1;return mask;}
  if(request.teamPreview)throw Error('Unexpected team preview in pinned random battles');
  const active=request.active?.[0],team=request.side?.pokemon||[];
  if(active)for(let i=0;i<active.moves.length&&i<4;i++){
    const m=active.moves[i];if(!m.disabled&&(m.pp===undefined||m.pp>0)){
      mask[1+i]=1;if(active.canTerastallize)mask[5+i]=1;
    }
  }
  const forced=!!request.forceSwitch?.[0],reviving=!!team[0]?.reviving;
  if(forced||active&&!active.trapped)for(let i=0;i<team.length&&i<6;i++){
    const p=team[i],fainted=p.condition.endsWith(' fnt');
    if(!p.active&&(reviving?fainted:!fainted))mask[9+i]=1;
  }
  if(!mask.some(Boolean)){
    if(request.forceSwitch&&!forced)mask[0]=1;
    else throw Error('No legal public action: '+JSON.stringify(request));
  }
  return mask;
}
function choice(action){
  if(action===0)return 'pass';
  if(action<5)return 'move '+action;
  if(action<9)return 'move '+(action-4)+' terastallize';
  return 'switch '+(action-8);
}
const stats=['hp','atk','def','spa','spd','spe'],boosts=['atk','def','spa','spd','spe','accuracy','evasion'];
const status=['','brn','par','slp','frz','psn','tox'];
// This function cannot access the simulator. All variable inputs are the
// player's client state, their own request, and their own pending-choice bit.
function encode(view,request,seat,pending,out=new Float32Array(OBS)){
  out.fill(0);const sides=[view.sides[seat],view.sides[1-seat]];
  out[0]=view.turn/1000;out[1]=+!!request?.forceSwitch?.[0];out[2]=+!!request?.wait;
  out[3]=+pending;out[4]=+!!request?.active?.[0]?.canTerastallize;
  out[5]=+!!request?.active?.[0]?.trapped;out[6]=+!!request?.active?.[0]?.maybeTrapped;
  out[7]=+!!request?.active?.[0]?.maybeDisabled;
  out[8]=(view.field.weatherState.minDuration||0)/8;out[9]=(view.field.weatherState.maxDuration||0)/8;
  out[10]=(view.field.terrainState.minDuration||0)/8;out[11]=(view.field.terrainState.maxDuration||0)/8;
  let at=32;keys(out,at,'conditions',view.field.pseudoWeather);
  one(out,at,'conditions',view.field.weatherState.id);one(out,at,'conditions',view.field.terrainState.id);
  at+=sizes.conditions;
  for(const side of sides){keys(out,at,'conditions',side.sideConditions);at+=sizes.conditions;}
  for(let s=0;s<2;s++)for(let j=0;j<6;j++){
    const p=sides[s].team[j],base=at;at+=MON;if(!p)continue;
    const own=s===0?request?.side?.pokemon?.[j]:null;
    out[base]=1;out[base+1]=+p.fainted;out[base+2]=+sides[s].active.includes(p);
    out[base+3]=p.level/100;out[base+4]=p.maxhp?p.hp/p.maxhp:0;
    out[base+5]=+(s===0);out[base+6]=s===0?p.hp/1024:0;out[base+7]=s===0?p.maxhp/1024:0;
    out[base+8]=+!!p.terastallized;out[base+9]=+!!p.shiny;
    out[base+10]=p.statusState.sleepTurns/4;out[base+11]=p.statusState.toxicTurns/16;
    const si=status.indexOf(p.status||'');if(si>=0)out[base+12+si]=1;
    for(let k=0;k<6;k++)out[base+19+k]=(p.species.baseStats?.[stats[k]]||0)/255;
    for(let k=1;k<6;k++)out[base+24+k]=(own?.stats?.[stats[k]]||0)/1024;
    out[base+30]=+(p.gender==='M');out[base+31]=+(p.gender==='F');
    let b=base+32;one(out,b,'species',p.speciesForme);b+=sizes.species;
    one(out,b,'abilities',p.ability);b+=sizes.abilities;
    one(out,b,'items',p.item);b+=sizes.items;
    for(const t of p.types)one(out,b,'types',t);b+=sizes.types;
    one(out,b,'types',s===0?own?.teraType||p.terastallized:p.terastallized);b+=sizes.types;
    for(const m of p.moveSlots)one(out,b,'moves',m.id);
    for(const m of own?.moves||[])one(out,b,'moves',m);
  }
  for(const side of sides){
    const p=side.active[0];if(p){for(let k=0;k<7;k++)out[at+k]=(p.boosts[boosts[k]]||0)/6;
      out[at+7]=p.timesAttacked/100;keys(out,at+8,'conditions',p.volatiles);}
    at+=ACTIVE;
  }
  for(let i=0;i<4;i++){
    const m=request?.active?.[0]?.moves?.[i];if(m){
      const d=Dex.moves.get(m.id);out[at]=1;out[at+1]=m.pp===undefined?0:m.pp/64;
      out[at+2]=m.maxpp===undefined?0:m.maxpp/64;out[at+3]=+!!m.disabled;
      out[at+4]=d.exists?d.basePower/250:0;out[at+5]=d.exists?(d.accuracy===true?1:d.accuracy/100):0;
      out[at+6]=d.exists?d.priority/7:0;out[at+7]=+(d.category==='Physical');
      out[at+8]=+(d.category==='Special');out[at+9]=+(d.category==='Status');
      out[at+10]=+(m.id==='recharge');out[at+11]=+(m.id==='struggle');
      one(out,at+16,'moves',m.id);
    }at+=MOVE;
  }
  if(at!==OBS)throw Error('Observation layout mismatch');return out;
}
function seed(n,salt){
  let x=(n^Math.imul(salt,0x9e3779b9))>>>0;const result=[];
  for(let i=0;i<4;i++){x=(x+0x9e3779b9)>>>0;let z=x;z=Math.imul(z^(z>>>16),0x21f0aaad);z=Math.imul(z^(z>>>15),0x735a2d97);result.push((z^(z>>>15))&65535);}
  return result.join(',');
}
class Game{
  constructor(n,options={},Engine=Battle){this.n=n;this.episode=0;this.options=options;this.Engine=Engine;this.reset();}
  reset(){
    if(this.b)this.b.destroy();if(this.views)for(const v of this.views)v.destroy();
    const n=(this.n+Math.imul(this.episode++,0x9e3779b9))>>>0;
    this.views=[new ClientBattle(gens,'one'),new ClientBattle(gens,'two')];
    this.errors=[];this.logPos=0;this.steps=0;this.decisions=0;this.retries=0;
    this.b=new this.Engine({formatid:'gen9randombattle',seed:seed(n,1),
      p1:{name:'one',seed:seed(n,2)},p2:{name:'two',seed:seed(n,3)},...this.options,
      send:(type,data)=>{if(type==='sideupdate'&&typeof data==='string'&&data.includes('|error|'))this.errors.push(data);}});
    this.refresh();
  }
  refresh(){
    const messages=extractChannelMessages(this.b.log.slice(this.logPos).join('\n'),[1,2]);
    this.logPos=this.b.log.length;
    this.requests=this.b.sides.map(s=>structuredClone(s.activeRequest));
    this.pending=this.b.sides.map(s=>s.isChoiceDone());
    this.masks=[];
    for(let s=0;s<2;s++){
      for(const line of messages[s+1])this.views[s].add(line);
      if(this.requests[s])this.views[s].add('|request|'+JSON.stringify(this.requests[s]));
      this.masks[s]=maskFor(this.requests[s],this.pending[s]||this.b.ended);
    }
  }
  observe(s,out){return encode(this.views[s],this.requests[s],s,this.pending[s],out);}
  step(actions){
    if(this.b.ended)throw Error('Reset required after terminal');
    for(let s=0;s<2;s++)if(!Number.isInteger(actions[s])||!this.masks[s][actions[s]])
      throw Error('Action outside public mask: '+actions[s]);
    const waiting=this.masks.map(m=>!!m[0]);this.steps++;
    for(let s=0;s<2&&!this.b.ended;s++)if(!waiting[s]){
      this.errors=[];this.decisions++;
      if(!this.b.choose('p'+(s+1),choice(actions[s]))){
        if(!this.errors.some(e=>e.includes('|error|[Unavailable choice]')))
          throw Error('Simulator rejected masked action: '+JSON.stringify({actions,errors:this.errors,request:this.requests[s]}));
        this.retries++;
      }
    }
    this.refresh();
    const ended=this.b.ended,win=this.b.winner==='one'?1:this.b.winner==='two'?-1:0;
    return {ended,win,turns:this.b.turn,steps:this.steps,decisions:this.decisions,retries:this.retries};
  }
  close(){this.b.destroy();for(const v of this.views)v.destroy();}
}
module.exports={Game,encode,maskFor,choice,seed,OBS,ACTIONS,ABI,MON,ACTIVE,MOVE,sizes,pin,
  encoderBindings:{cat,norm,stats,boosts,status,Dex}};
