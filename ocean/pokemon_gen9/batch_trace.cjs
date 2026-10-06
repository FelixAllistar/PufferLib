'use strict';
// Trace original execution, then check generated kernels under different
// cross-battle scheduling orders. The oracle still executes unconverted rules.
const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto'),cp=require('child_process');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const {Dex,oracle,pin}=require('./reference.cjs');
const {Game,choice,ACTIONS}=require('./worker_core.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {RandomPlayerAI}=require(oracle+'/dist/sim/tools/random-player-ai');
const dir='build/pokemon_gen9/batch',manifest=JSON.parse(fs.readFileSync(dir+'/manifest.json'));
assert.equal(manifest.revision,pin);
const strings=Object.assign(Object.create(null),manifest.strings);
function intern(s){return strings[s]??(strings[s]=Object.keys(strings).length);}
const byKey=new Map(manifest.kernels.map(k=>[k.key,k]));
const helpers=new Map();for(const k of manifest.kernels)if(!k.owner){const list=helpers.get(k.name)||[];list.push(k);helpers.set(k.name,list);}
const originals={trunc:Dex.trunc};
for(const name of ['chain','chainModify','modify'])originals[name]=Battle.prototype[name];
for(const kernel of manifest.kernels)if(kernel.kind==='expression')
  originals[kernel.name]=Function('return ('+kernel.expression.reference_source+');')();
const originalGetCallback=Battle.prototype.getCallback;
const abilityFunctions=new Map(),counts=new Map();
for(const k of manifest.kernels)if(k.owner){
  const fn=Dex.abilities.get(k.owner)[k.name];assert.equal(typeof fn,'function',k.key);
  if(abilityFunctions.has(fn))assert.equal(abilityFunctions.get(fn).source_sha256,k.source_sha256);
  abilityFunctions.set(fn,k);
}
let current=null,depth=0;
function context(lane,battle){
  const event=battle.event;if(!event||typeof event!=='object')return 0;
  let id=lane.events.get(event);if(id===undefined){id=++lane.contexts;lane.events.set(event,id);}return id;
}
function modifier(battle){return battle.event?Number(battle.event.modifier):1;}
function project(kernel,args){
  return kernel.slots.map(slot=>{
    if(slot.kind==='argument'){
      let v=args[slot.parameter];if(v===undefined&&slot.default!==null&&slot.default!==undefined)v=slot.default;
      if(slot.index!==undefined)v=v[slot.index];return Number(v);
    }
    const names=slot.path.split('.'),parameter=kernel.params.findIndex(p=>p.name===names[0]);
    assert(parameter>=0);let v=args[parameter];for(const name of names.slice(1))v=v?.[name];
    if(slot.type==='string'){assert.equal(typeof v,'string',kernel.key+':'+slot.path);return intern(v);}
    if(slot.type==='flag')return +!!v;
    return Number(v);
  });
}
function record(kernel,battle,args,invoke){
  if(!current||depth)return invoke();
  assert.equal(battle.gen,9);assert(!battle.debugMode,'debugMode must be false');
  const row={op:kernel.id,context:context(current,battle),before:modifier(battle),input:project(kernel,args)};
  current.rows.push(row);depth++;
  let result;try{result=invoke();}finally{depth--;}
  row.after=modifier(battle);row.tag=result===undefined?0:result===null?3:typeof result==='boolean'?2:1;
  assert(result===undefined||result===null||typeof result==='boolean'||typeof result==='number',kernel.key);
  row.result=row.tag===1?result:row.tag===2?+result:0;
  counts.set(kernel.key,(counts.get(kernel.key)||0)+1);return result;
}
function pickHelper(name,args){
  const found=helpers.get(name).find(k=>k.shapes.every((shape,i)=>shape===(Array.isArray(args[i])?'tuple':'number')));
  assert(found,'Unsupported helper arguments '+name);return found;
}
for(const name of ['chain','chainModify','modify']){
  Battle.prototype[name]=function(...args){return record(pickHelper(name,args),this,args,()=>originals[name].apply(this,args));};
}
const callbackWrappers=new WeakMap();
Battle.prototype.getCallback=function(...args){
  const fn=originalGetCallback.apply(this,args),kernel=abilityFunctions.get(fn);if(!kernel)return fn;
  let wrapper=callbackWrappers.get(fn);if(!wrapper){
    wrapper=function(...values){return record(kernel,this,values,()=>fn.apply(this,values));};
    Object.defineProperty(wrapper,'length',{value:fn.length});callbackWrappers.set(fn,wrapper);
  }return wrapper;
};
class Bot extends RandomPlayerAI{
  choose(c){this.selected=c;}
  pick(req){this.selected=null;this.receiveRequest(req);
    if(this.selected?.startsWith('move ')&&req.active?.[0]?.canTerastallize&&this.prng.random()<0.1)this.selected+=' terastallize';
    return this.selected;
  }
}
function seed(n,s){return [n&65535,(n*47111+s)&65535,(n*32117+s*17)&65535,(65535-n+s)&65535].join(',');}
function lane(){return {rows:[],events:new WeakMap(),contexts:0};}
Date.now=()=>1791194400000;
const baseline=JSON.parse(fs.readFileSync('build/pokemon_gen9/js-probe/node-profile.json'));
const lanes=[];let decisions=0,turns=0;
for(let n=0;n<128;n++){
  current=lane();lanes.push(current);
  const g=new Game(n,{seed:seed(n,1),p1:{name:'one',seed:seed(n,2)},p2:{name:'two',seed:seed(n,3)}});
  g.b.trunc=(...args)=>record(pickHelper('trunc',args),g.b,args,()=>originals.trunc.apply(undefined,args));
  const bots=[new Bot(null,{seed:seed(n,4),move:0.95}),new Bot(null,{seed:seed(n,5),move:0.95})];
  while(!g.b.ended){
    assert(g.steps<10000);
    const actions=g.requests.map((req,s)=>{
      if(g.masks[s][0])return 0;
      const c=bots[s].pick(req),a=Array.from({length:ACTIONS},(_,i)=>i).find(i=>choice(i)===c);
      assert(a!==undefined&&g.masks[s][a]);return a;
    });g.step(actions);
  }
  const digest=crypto.createHash('sha256').update(g.b.log.join('\n')).digest('hex');
  assert.equal(digest,baseline.games[n].sha256,'Instrumented source log at seed '+n);
  assert.equal(g.decisions,baseline.games[n].decisions);decisions+=g.decisions;turns+=g.b.turn;
  g.close();if((n+1)%32===0)console.log('Traced/matched '+(n+1)+' complete original games');
}
current=null;
// Restore before invoking raw source functions on explicit synthetic inputs.
for(const name of ['chain','chainModify','modify'])Battle.prototype[name]=originals[name];
Battle.prototype.getCallback=originalGetCallback;
const boundaries=[-Infinity,-1e20,-4294967297,-2147483648,-1,-0,0,1,33.9,4096,65535,2147483647,4294967295,4294967296,1e20,Infinity,NaN];
const types=['Fire','Water','Grass','Bug','Dragon','Electric','Rock','Normal','Fighting','Flying','Poison','Ground','Psychic','Ice','Ghost','Dark','Steel','Fairy','???'];
function setPath(object,path,value){const parts=path.split('.');let at=object;for(const p of parts.slice(0,-1))at=at[p]||(at[p]={});at[parts.at(-1)]=value;}
function fixture(kernel,index){
  const args=kernel.params.map((p,i)=>kernel.shapes[i]==='object'?{}:
    kernel.shapes[i]==='tuple'?[boundaries[(index+i)%boundaries.length],[0,1,2,4096][index%4]]:
    kernel.name==='trunc'&&i===1?[-1,0,1,8,16,31,32,64][index%8]:
    !kernel.owner?boundaries[(index+i)%boundaries.length]:[0,1,33,100,255,4096][index%6]);
  for(const field of kernel.fields){
    const parts=field.path.split('.'),parameter=kernel.params.findIndex(p=>p.name===parts[0]),last=parts.at(-1);
    let val;
    if(field.type==='string')val=last==='status'?['','brn','par','slp','frz','psn','tox'][index%7]:
      last==='type'?types[index%types.length]:last==='category'?['Physical','Special','Status'][index%3]:['','M','F','N'][index%4];
    else if(field.type==='flag')val=index%2?true:undefined;
    else val=last==='maxhp'?100:last==='hp'?[0,1,33,34,50,100][Math.floor(index/types.length)%6]:[0,1,2,50,100][index%5];
    setPath(args[parameter],parts.slice(1).join('.'),val);
  }return args;
}
const fixtureLanes=Array.from({length:128},lane);lanes.push(...fixtureLanes);
for(const kernel of manifest.kernels)for(let i=0;i<128;i++){
  current=fixtureLanes[i];const args=fixture(kernel,i);
  const battle={gen:9,debugMode:false,event:{modifier:1},...originals,debug:()=>{}};
  const fn=kernel.owner?Dex.abilities.get(kernel.owner)[kernel.name]:originals[kernel.name];
  record(kernel,battle,args,()=>fn.apply(battle,args));
}
// Explicit mixed-kernel chains use persistent frame state with no source reset.
for(let i=0;i<128;i++){
  current=fixtureLanes[i];const battle={gen:9,debugMode:false,event:{modifier:1},...originals,debug:()=>{}};
  const chain=byKey.get('helper.chainModify.tuple-number'),power=byKey.get('ability.hugepower.onModifyAtk');
  for(let j=0;j<12;j++){
    record(chain,battle,[[4915,4096]],()=>originals.chainModify.call(battle,[4915,4096]));
    record(power,battle,[100],()=>Dex.abilities.get('hugepower').onModifyAtk.call(battle,100));
  }
}current=null;
function u32(n){const b=Buffer.allocUnsafe(4);b.writeUInt32LE(n);return b;}
const file=dir+'/trace.bin',fd=fs.openSync(file,'w');
const write=b=>{for(let n=0;n<b.length;)n+=fs.writeSync(fd,b,n,b.length-n);};
write(u32(0x54424739));write(u32(1));write(Buffer.from(manifest.source_ir_sha256));write(u32(lanes.length));write(u32(manifest.kernels.length));
for(const l of lanes){
  write(u32(l.rows.length));
  for(const r of l.rows){
    const b=Buffer.allocUnsafe(36+8*r.input.length);b.writeUInt32LE(r.op,0);b.writeUInt32LE(r.context,4);b.writeUInt32LE(r.tag,8);
    [r.before,r.after,r.result,...r.input].forEach((v,i)=>b.writeDoubleLE(v,12+8*i));write(b);
  }
}fs.closeSync(fd);
const replays=[];
for(const order of ['largest','rotate']){
  const result=cp.spawnSync(dir+'/replay',[file,order],{encoding:'utf8'});
  if(result.stderr)process.stderr.write(result.stderr);
  if(result.error||result.status!==0)throw result.error||Error('Batched replay failed ('+order+')');
  replays.push(JSON.parse(result.stdout.trim()));console.log(result.stdout.trim());
}
const validation={revision:pin,source_ir_sha256:manifest.source_ir_sha256,source_games:128,turns,decisions,
  generated_kernels:manifest.kernels.length,generated_ability_handlers:manifest.kernels.filter(k=>k.owner).length,
  synthetically_checked_kernels:manifest.kernels.length,operations_by_kernel:Object.fromEntries(counts),replays,
  checks:'Exact full source-game hashes and decisions; generated numeric/ability results and event modifier mutations match the source; two cross-lane scheduling orders; synthetic numeric edge and conditional inputs; persistent mixed-handler chains.',
  limits:'Trace-driven kernel/scheduler validation, not an autonomous simulator. Unconverted rules and frame resets are supplied by the original source. No training-readiness or whole-environment throughput claim.'};
fs.writeFileSync(dir+'/validation.json',JSON.stringify(validation,null,2)+'\n');
console.log(JSON.stringify({...validation,operations_by_kernel:undefined},null,2));
