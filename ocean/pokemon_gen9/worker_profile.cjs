'use strict';
const fs=require('fs'),path=require('path'),cp=require('child_process');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const dir=path.resolve('build/pokemon_gen9/worker');
const timing=path.join(dir,'worker-timing.json'),profile=path.join(dir,'worker.cpuprofile');
fs.mkdirSync(dir,{recursive:true});
const wrapper=path.join(dir,'profile-node.cjs');
fs.writeFileSync(wrapper,'#!/usr/bin/env node\n'+
  "const cp=require('child_process');const r=cp.spawnSync(process.execPath,"+
  JSON.stringify(['--cpu-prof','--cpu-prof-dir='+dir,'--cpu-prof-name=worker.cpuprofile'])+
  ".concat(process.argv.slice(2)),{stdio:'inherit'});if(r.error)throw r.error;process.exit(r.status===null?1:r.status);\n",{mode:0o755});
for(const file of [timing,profile])if(fs.existsSync(file))fs.unlinkSync(file);
const result=cp.spawnSync('build/pokemon_gen9/worker/transport_test',[],{
  env:{...process.env,PG9_NODE:wrapper,PG9_WORKER_TIMING:timing},encoding:'utf8'});
process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
if(result.error||result.status!==0)throw result.error||Error('Profile transport failed');
const phases=JSON.parse(fs.readFileSync(timing));
const samples=JSON.parse(fs.readFileSync(profile)),nodes=new Map(samples.nodes.map(n=>[n.id,n]));
const groups=new Map();let total=0,idle=0;
for(let i=0;i<samples.samples.length;i++){
  const frame=nodes.get(samples.samples[i]).callFrame,elapsed=samples.timeDeltas[i];
  if(frame.functionName==='(idle)'){idle+=elapsed;continue;}
  const key=JSON.stringify([frame.functionName,frame.url,frame.lineNumber]);
  const row=groups.get(key)||{function:frame.functionName||'(anonymous)',url:frame.url,line:frame.lineNumber+1,self_us:0};
  row.self_us+=elapsed;total+=elapsed;groups.set(key,row);
}
const top=[...groups.values()].sort((a,b)=>b.self_us-a.self_us).slice(0,20)
  .map(row=>({...row,self_percent:100*row.self_us/total}));
const summary={kind:'32 complete games through the C transport. Sampling includes startup; timings include IPC waits and one-CPU scheduling. Instrumentation prevents using this as a throughput benchmark.',
  transport:result.stdout.trim(),...phases,sample_non_idle_us:total,sample_idle_us:idle,top};
fs.writeFileSync(path.join(dir,'worker-profile-summary.json'),JSON.stringify(summary,null,2)+'\n');
console.log(JSON.stringify(summary,null,2));
