'use strict';
const fs=require('fs'),assert=require('assert'),crypto=require('crypto');
const {performance}=require('perf_hooks');
const source=require('./worker_core.cjs'),batch=require('./compact_core.cjs');
const {RandomPlayerAI}=require(require('./reference.cjs').oracle+'/dist/sim/tools/random-player-ai');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
Date.now=()=>1791194400000;
const count=Number(process.env.PG9_BENCH_GAMES||64);
assert(Number.isInteger(count)&&count>0&&count<=512);
const seed=(n,s)=>[n&65535,(n*47111+s)&65535,(n*32117+s*17)&65535,(65535-n+s)&65535].join(',');
const options=n=>({seed:seed(n,1),p1:{name:'one',seed:seed(n,2)},p2:{name:'two',seed:seed(n,3)}});
const digest=game=>crypto.createHash('sha256').update(game.b.log.join('\n')).digest('hex');
class Bot extends RandomPlayerAI {
  choose(c){this.selected=c;}
  pick(request){this.selected=null;this.receiveRequest(request);
    if(this.selected?.startsWith('move ')&&request.active?.[0]?.canTerastallize&&this.prng.random()<0.1)this.selected+=' terastallize';
    return this.selected;}
}
console.log('Generating identical legal action tapes from '+count+' original-source games (untimed).');
const tapes=[];
for(let n=0;n<count;n++){
  const game=new source.Game(n,options(n)),actions=[],bots=[new Bot(null,{seed:seed(n,4),move:0.95}),new Bot(null,{seed:seed(n,5),move:0.95})];
  while(!game.b.ended){assert(game.steps<10000);
    const action=game.requests.map((r,s)=>{
      if(game.masks[s][0])return 0;
      const command=bots[s].pick(r);
      return Array.from({length:15},(_,i)=>i).find(i=>source.choice(i)===command);
    });
    actions.push(action);game.step(action);
  }
  tapes.push({actions,sha256:digest(game),decisions:game.decisions});game.close();
}
class SourceCompactGame extends source.Game {
  observe(s,out){return batch.encode(this.views[s],this.requests[s],s,this.pending[s],out);}
}
const reports=[];
for(const [name,Game,OBS,batched] of [['source_dense',source.Game,source.OBS,false],
    ['source_compact',SourceCompactGame,batch.OBS,false],['continuations_compact',batch.Game,batch.OBS,true]]){
  // Tape generation warms the source engine. Give every measured backend the
  // same complete untimed replay, including observation encoding, before timing.
  const warm=Array.from({length:count},(_,n)=>new Game(n,options(n)));
  const scratch=[new Float32Array(OBS),new Float32Array(OBS)];
  while(warm.some(g=>!g.b.ended)){
    const active=warm.filter(g=>!g.b.ended),inputs=active.map(g=>tapes[g.n].actions[g.steps]);
    if(batched)batch.stepBatch(active,inputs);else active.forEach((g,i)=>g.step(inputs[i]));
    for(const g of active)for(let s=0;s<2;s++)g.observe(s,scratch[s]);
  }
  for(const g of warm){assert.equal(digest(g),tapes[g.n].sha256);g.close();}
  const games=Array.from({length:count},(_,n)=>new Game(n,options(n)));
  const obs=games.map(()=>[new Float32Array(OBS),new Float32Array(OBS)]);
  let rows=0,rounds=0;batch.runtime.resetMetrics();const start=performance.now();
  while(games.some(g=>!g.b.ended)){
    const indices=games.map((g,i)=>g.b.ended?-1:i).filter(i=>i>=0),active=indices.map(i=>games[i]);
    const inputs=indices.map(i=>tapes[i].actions[games[i].steps]);
    if(batched)batch.stepBatch(active,inputs);else active.forEach((g,i)=>g.step(inputs[i]));
    for(const i of indices)for(let s=0;s<2;s++)games[i].observe(s,obs[i][s]);
    rows+=2*indices.length;rounds++;
  }
  const seconds=(performance.now()-start)/1000;
  const decisions=games.reduce((n,g)=>n+g.decisions,0);
  for(let i=0;i<count;i++){assert.equal(digest(games[i]),tapes[i].sha256);assert.equal(games[i].decisions,tapes[i].decisions);games[i].close();}
  const report={name,games:count,seconds,agent_rows:rows,decisions,agent_rows_per_second:rows/seconds,
    decisions_per_second:decisions/seconds,observations:OBS,bytes_per_actor:OBS*4,rounds,
    ...(batched?{runtime:batch.runtime.snapshot()}:{} )};
  reports.push(report);console.log(JSON.stringify({...report,runtime:batched?'see artifact':undefined}));
}
fs.writeFileSync('build/pokemon_gen9/continuation/benchmark.json',JSON.stringify({kind:
  'One process: full source rules, public clients and both observations; identical action tapes and terminal logs. Each backend receives an equal full untimed replay to warm its rules and encoder. Excludes startup/team generation, IPC and PPO. No native output verifier or profiling in timed path.',
  continuation_source_ir_sha256:JSON.parse(fs.readFileSync('build/pokemon_gen9/continuation/manifest.json')).source_ir_sha256,
  schema_sha256:batch.schema.schema_sha256,kernel_source_ir_sha256:batch.runtime.manifest.source_ir_sha256,reports},null,2)+'\n');
