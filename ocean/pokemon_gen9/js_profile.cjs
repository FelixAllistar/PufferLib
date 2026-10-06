'use strict';
// Diagnose the direct source engine before spending time on an AOT toolchain.
const fs=require('fs'),path=require('path'),cp=require('child_process');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const dir=path.resolve('build/pokemon_gen9/js-probe');
fs.mkdirSync(dir,{recursive:true});
const name='showdown-node.cpuprofile';
cp.execFileSync(process.execPath,['--cpu-prof','--cpu-prof-dir='+dir,
  '--cpu-prof-name='+name,'ocean/pokemon_gen9/js_probe.cjs'],{
  stdio:'inherit',env:{...process.env,PG9_JS_PROBE_GAMES:'128',PG9_JS_PROBE_LABEL:'node-profile'}});
const trial=JSON.parse(fs.readFileSync(path.join(dir,'node-profile.json')));
const baseline=JSON.parse(fs.readFileSync(path.join(dir,'node-baseline.json')));
if(trial.revision!==baseline.revision||trial.clock_ms!==baseline.clock_ms||
  JSON.stringify(trial.games.slice(0,baseline.games.length))!==JSON.stringify(baseline.games))
  throw Error('Profile games differ from original baseline');
const profile=JSON.parse(fs.readFileSync(path.join(dir,name))),nodes=new Map(profile.nodes.map(n=>[n.id,n]));
const groups=new Map();let total=0;
for(let i=0;i<profile.samples.length;i++){
  const node=nodes.get(profile.samples[i]),frame=node.callFrame;
  const key=JSON.stringify([frame.functionName,frame.url,frame.lineNumber]);
  const item=groups.get(key)||{function:frame.functionName||'(anonymous)',url:frame.url,
    line:frame.lineNumber+1,self_microseconds:0};
  const elapsed=profile.timeDeltas[i];item.self_microseconds+=elapsed;total+=elapsed;groups.set(key,item);
}
const top=[...groups.values()].sort((a,b)=>b.self_microseconds-a.self_microseconds).slice(0,24)
  .map(item=>({...item,self_percent:100*item.self_microseconds/total}));
const result={revision:trial.revision,matching_baseline_games:baseline.games.length,
  measured_games:trial.measured_games,actor_decisions:trial.actor_decisions,
  kind:'Sampling profile includes startup, data loading, four warmup games and 128 measured complete games. Profile overhead prevents speed comparison.',
  profile:name,top};
fs.writeFileSync(path.join(dir,'node-profile-summary.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
