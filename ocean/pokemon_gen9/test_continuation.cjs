'use strict';
const assert=require('assert'),fs=require('fs'),crypto=require('crypto'),path=require('path');
const {performance}=require('perf_hooks');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const original=require('./worker_core.cjs'),candidate=require('./compact_core.cjs');
const {oracle,pin,root}=require('./reference.cjs');
const {RandomPlayerAI}=require(oracle+'/dist/sim/tools/random-player-ai');
const runtime=candidate.runtime;
class Bot extends RandomPlayerAI{
  choose(c){this.selected=c;}
  pick(request){
    this.selected=null;this.receiveRequest(request);
    if(this.selected?.startsWith('move ')&&request.active?.[0]?.canTerastallize&&this.prng.random()<0.1)
      this.selected+=' terastallize';
    return this.selected;
  }
}
const seed=(n,s)=>[n&65535,(n*47111+s)&65535,(n*32117+s*17)&65535,(65535-n+s)&65535].join(',');
const options=n=>({seed:seed(n,1),p1:{name:'one',seed:seed(n,2)},p2:{name:'two',seed:seed(n,3)}});
const digest=x=>crypto.createHash('sha256').update(x).digest('hex');
Date.now=()=>1791194400000;
function actions(game,bots){
  return game.requests.map((request,s)=>{
    if(game.masks[s][0])return 0;
    const command=bots[s].pick(request),action=Array.from({length:original.ACTIONS},(_,i)=>i).find(i=>original.choice(i)===command);
    assert(action!==undefined&&game.masks[s][action],command);return action;
  });
}
function compare(source,target,label){
  assert.deepEqual(target.requests,source.requests,label+' requests');
  assert.deepEqual(target.pending,source.pending,label+' pending');
  assert.deepEqual(target.masks,source.masks,label+' public masks');
  assert.deepEqual(target.b.prng.getSeed(),source.b.prng.getSeed(),label+' PRNG');
  assert.deepEqual(target.b.log,source.b.log,label+' complete ordered log');
  for(let s=0;s<2;s++){
    const a=source.observe(s),packed=target.observe(s),b=candidate.unpack(packed);
    assert(packed.every(Number.isFinite),label+' finite compact observation');
    assert(Buffer.from(a.buffer,a.byteOffset,a.byteLength).equals(Buffer.from(b.buffer,b.byteOffset,b.byteLength)),label+' actor observation '+s);
  }
}
const count=Number(process.env.PG9_CONTINUATION_TEST_GAMES||128),reports=[];
assert(Number.isInteger(count)&&count>0&&count<=128);
const baseline=JSON.parse(fs.readFileSync(path.join(root,'build/pokemon_gen9/js-probe/node-profile.json')));
for(const order of ['rotate','largest']){
  const games=Array.from({length:count},(_,n)=>({n,source:new original.Game(n,options(n)),target:new candidate.Game(n,options(n)),
    bots:[new Bot(null,{seed:seed(n,4),move:0.95}),new Bot(null,{seed:seed(n,5),move:0.95})]}));
  for(const g of games)compare(g.source,g.target,order+' initial '+g.n);
  runtime.resetMetrics();const start=performance.now();let steps=0,decisions=0,rounds=0,done=0;
  try{
    while(done<count){
      assert(rounds++<10000,'Complete games must reach source terminals');
      const active=games.filter(g=>!g.source.b.ended),input=active.map(g=>actions(g.source,g.bots));
      const expected=active.map((g,i)=>g.source.step(input[i]));
      const actual=candidate.stepBatch(active.map(g=>g.target),input,{order,profile:true,verifyNative:true});
      for(let i=0;i<active.length;i++){
        const g=active[i],label=order+' game '+g.n+' step '+g.source.steps;
        assert.deepEqual(actual[i],expected[i],label+' result');compare(g.source,g.target,label);steps++;
        if(g.source.b.ended){
          const sha256=digest(g.target.b.log.join('\n'));
          assert.equal(sha256,baseline.games[g.n].sha256,label+' independent baseline log hash');
          assert.equal(g.target.decisions,baseline.games[g.n].decisions);decisions+=g.target.decisions;done++;
        }
      }
      if(rounds%32===0)console.log(order+': '+done+'/'+count+' games complete, '+steps+' compared decisions/requests');
    }
    const metrics=runtime.snapshot();assert(metrics.native_operations>0);
    if(count>1)assert(metrics.max_batch>1,'Native calls must group independent live battles');
    assert.equal(metrics.synchronous_native_operations,0,'Step path must never synchronously drain a suspended kernel');
    reports.push({order,games:count,decisions,steps,rounds,wall_ms:performance.now()-start,metrics});
    console.log(JSON.stringify({order,games:count,decisions,native_operations:metrics.native_operations,
      batches:metrics.batches,mean_batch:metrics.native_operations/metrics.batches,max_batch:metrics.max_batch}));
  }finally{for(const g of games){g.source.close();g.target.close();}}
}
// The bridge rejects bad buffers before accessing C data.
const addon=require(path.join(root,'build/pokemon_gen9/batch/kernels.node'));
assert.throws(()=>addon.run(99999,1,new Float64Array(1),new Float64Array(1),new Uint32Array(1),new Float64Array(1)),/invalid kernel/);
assert.throws(()=>addon.run(0,2,new Float64Array(1),new Float64Array(2),new Uint32Array(2),new Float64Array(2)),/too short/);
assert.throws(()=>addon.run(0,1,new Float32Array(2),new Float64Array(1),new Uint32Array(1),new Float64Array(1)),/expected/);
const aliased=new Float64Array(4);
assert.throws(()=>addon.run(0,1,aliased,aliased,new Uint32Array(1),new Float64Array(1)),/overlap/);
const report={revision:pin,kernel_source_ir_sha256:runtime.manifest.source_ir_sha256,
  observations:candidate.OBS,dense_observations:original.OBS,abi:candidate.ABI,schema_sha256:candidate.schema.schema_sha256,
  continuation_source_ir_sha256:JSON.parse(fs.readFileSync(path.join(root,'build/pokemon_gen9/continuation/manifest.json'))).source_ir_sha256,
  comparisons:'At every decision: both private requests, masks, pending commitments, PRNG seed, complete ordered log and both public actor observations. Full terminal logs match independent prior 128-game hashes.',
  mechanics:'Original source definitions and JS object behavior retained. Autonomous live continuations invoke admitted C kernels without source traces or captured operation inputs.',
  limitations:'Only admitted numeric operations run as native batches. Scalar constructors/reset generation, JS objects, other source semantics and public-client processing remain. This establishes no throughput speedup or all-state proof.',reports};
fs.writeFileSync(path.join(root,'build/pokemon_gen9/continuation/validation.json'),JSON.stringify(report,null,2)+'\n');
console.log('Autonomous source continuations matched '+count+' full games under both native scheduling orders.');
