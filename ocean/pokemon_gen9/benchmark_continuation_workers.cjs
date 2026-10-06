'use strict';
const fs=require('fs'),path=require('path'),cp=require('child_process'),os=require('os');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const group=fs.readFileSync('/proc/self/cgroup','utf8').split('\n').find(row=>row.startsWith('0::')).slice(3);
const cgroup=path.join('/sys/fs/cgroup',group);
const usage=()=>Number(fs.readFileSync(path.join(cgroup,'cpu.stat'),'utf8').match(/^usage_usec (\d+)/m)[1]);
const trials=[];
for(const [workers,games] of [[1,32],[4,32],[4,128],[4,256]]){
  const before=usage(),start=performance.now();
  const result=cp.spawnSync('build/pokemon_gen9/continuation/scale_test',[workers,games,games].map(String),{encoding:'utf8'});
  if(result.stderr)process.stderr.write(result.stderr);
  if(result.error||result.status!==0)throw result.error||Error('Batch scaling failed '+result.status);
  const trial=JSON.parse(result.stdout.trim());trial.whole_process_seconds=(performance.now()-start)/1000;
  trial.scope_cpu_seconds=(usage()-before)/1e6;trial.average_busy_cpu_cores=trial.scope_cpu_seconds/trial.whole_process_seconds;
  trials.push(trial);console.log(JSON.stringify(trial));
  // Save each qualified case if a later larger case exceeds the resource cap.
  fs.writeFileSync('build/pokemon_gen9/continuation/scaling.json',JSON.stringify({
    backend:'Source continuations + in-process C numeric batches + public client + compact binary transport',
    cpu_cores:os.availableParallelism(),cpu_max:fs.readFileSync(path.join(cgroup,'cpu.max'),'utf8').trim(),
    schema_sha256:JSON.parse(fs.readFileSync('build/pokemon_gen9/compact/schema.json')).schema_sha256,
    kind:'CPU-only random-policy throughput; finite observation samples, masks and zero-sum terminals checked. Rates exclude startup; CPU usage includes it. Actor rows include waits. No GPU, PPO or strength measurement.',trials},null,2)+'\n');
}
