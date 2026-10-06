'use strict';
const fs=require('fs'),path=require('path'),cp=require('child_process'),os=require('os');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const relative=fs.readFileSync('/proc/self/cgroup','utf8').split('\n').find(row=>row.startsWith('0::')).slice(3);
const cgroup=path.join('/sys/fs/cgroup',relative);
function usage(){
  return Object.fromEntries(fs.readFileSync(path.join(cgroup,'cpu.stat'),'utf8').trim().split('\n').map(row=>row.split(' ')).map(([key,value])=>[key,Number(value)]));
}
const trials=[];
for(const [workers,games,episodes] of [[1,4,32],[1,16,64],[4,4,16],[4,16,32],[8,16,16]]){
  const before=usage(),start=performance.now();
  const result=cp.spawnSync('build/pokemon_gen9/worker/scale_test',[workers,games,episodes].map(String),{encoding:'utf8'});
  const elapsed=(performance.now()-start)/1000,after=usage();
  if(result.stderr)process.stderr.write(result.stderr);
  if(result.error||result.status!==0)throw result.error||Error('Scaling case failed: '+result.status);
  const trial=JSON.parse(result.stdout.trim());
  trial.whole_process_seconds=elapsed;
  trial.scope_cpu_seconds=(after.usage_usec-before.usage_usec)/1e6;
  trial.average_busy_cpu_cores=trial.scope_cpu_seconds/elapsed;
  trials.push(trial);console.log(JSON.stringify(trial));
}
const report={backend:'Original Showdown + public client + dense C binary transport',
  cpu_cores:os.availableParallelism(),cpu_max:fs.readFileSync(path.join(cgroup,'cpu.max'),'utf8').trim(),
  observation_floats:53819,kind:'CPU-only random-policy scaling benchmark; actor rows include forced waits. No GPU inference, PPO, or strength measurement. Finite observations are periodically sampled; masks and zero-sum terminals checked every boundary. Startup excluded from step rates; complete process CPU usage includes startup.',trials};
fs.writeFileSync('build/pokemon_gen9/worker/scaling.json',JSON.stringify(report,null,2)+'\n');
console.log('Saved build/pokemon_gen9/worker/scaling.json');
