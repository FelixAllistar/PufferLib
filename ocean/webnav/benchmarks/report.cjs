// Archive measured evidence; missing/error tasks stay in the requested denominator.
// Run after learned/random browser runs and official scoring, from the repo root.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const assert=require('node:assert/strict');
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const json=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const rows=file=>fs.readFileSync(file,'utf8').trim().split('\n').map(JSON.parse);
const sum=(values,key)=>values.reduce((n,r)=>n+(r[key]||0),0);
function native(file) {
  const values=rows(file);
  assert.equal(values.length,125);
  assert.equal(new Set(values.map(r=>r.task)).size,125);
  assert.equal(sum(values,'episodes'),604);
  for(const r of values)assert.ok(r.episodes===4||r.episodes===8);
  const steps=sum(values,'steps');
  return {file,sha256:sha(file),tasks:values.length,episodes:sum(values,'episodes'),
    wins:sum(values,'wins'),macro_full_credit_rate:values.reduce((n,r)=>n+r.wins/r.episodes,0)/values.length,
    micro_full_credit_rate:sum(values,'wins')/sum(values,'episodes'),
    steps,rejected:sum(values,'rejected'),
    local_step_fraction:values.every(r=>'local_steps'in r)?sum(values,'local_steps')/steps:null,
    per_task:values};
}
function browser(dir) {
  const run=json(path.join(dir,'run.json'));
  const ids=run.options.tasks==='all'?run.manifest.tasks.map(t=>t.task_id):run.options.tasks.split(',').map(Number);
  assert.equal(new Set(ids).size,ids.length);
  const tasks=ids.map(task_id=>{
    const folder=path.join(dir,String(task_id)),runnerFile=path.join(folder,'runner.json');
    const runner=fs.existsSync(runnerFile)?json(runnerFile):null;
    const evalFile=path.join(folder,'eval_result.json');
    const grade=fs.existsSync(evalFile)?json(evalFile):null;
    if(grade)assert.equal(grade.task_id,task_id);
    const artifacts={};
    for(const file of ['runner.json','trajectory.jsonl','network.har','agent_response.json','eval_result.json']) {
      const full=path.join(folder,file);
      if(fs.existsSync(full))artifacts[file]=sha(full);
    }
    return {task_id,official_score:grade?.score??null,official_status:grade?.status??'missing',
      official_full_success:grade?.status==='success'&&grade.score===1,
      scorer_version:grade?.webarena_verified_version??null,
      scorer_checksum:grade?.webarena_verified_evaluator_checksum??null,
      dataset_checksum:grade?.webarena_verified_data_checksum??null,
      runner,artifacts};
  });
  return {directory:dir,run_sha256:sha(path.join(dir,'run.json')),options:run.options,
    checkpoint_sha256:run.checkpoint_sha256,policy_binary_sha256:run.policy_binary_sha256,
    source_sha256:run.source_sha256,dataset_sha256:run.manifest.dataset_sha256,
    timing:run.timing,runtime:run.runtime,
    requested_tasks:ids.length,graded_tasks:tasks.filter(t=>t.official_score!==null).length,
    full_successes:tasks.filter(t=>t.official_full_success).length,
    full_success_rate:tasks.filter(t=>t.official_full_success).length/ids.length,
    mean_score_missing_as_zero:tasks.reduce((n,t)=>n+(t.official_score??0),0)/ids.length,
    harness_errors:tasks.filter(t=>!t.runner||t.runner.error).length,
    action_errors:tasks.reduce((n,t)=>n+(t.runner?.action_errors||0),0),tasks};
}
const [learnedDir,randomDir,controlDir]=process.argv.slice(2);
if(!learnedDir||!randomDir||!controlDir)throw Error('Usage: node report.cjs LEARNED_DIR RANDOM_DIR CALIBRATION_DIR');
const learned=browser(path.resolve(learnedDir)),random=browser(path.resolve(randomDir));
assert.deepEqual(learned.tasks.map(t=>t.task_id),random.tasks.map(t=>t.task_id));
assert.equal(learned.options.steps,random.options.steps);
assert.equal(random.options.checkpoint,'random');
assert.notEqual(learned.options.checkpoint,'random');
assert.equal(learned.policy_binary_sha256,random.policy_binary_sha256);
assert.deepEqual(learned.source_sha256,random.source_sha256,'Paired browser runs must use identical adapter/runner sources');
assert.equal(learned.dataset_sha256,random.dataset_sha256);
assert.equal(learned.dataset_sha256,json('ocean/webnav/benchmarks/manifest.json').dataset_sha256);
assert.deepEqual(learned.timing,random.timing);
assert.deepEqual(learned.runtime,random.runtime);
const controlFile=path.join(controlDir,'157/eval_result.json'),control=json(controlFile);
assert.equal(control.task_id,157);assert.equal(control.score,1);assert.equal(control.status,'success');
for(const task of [...learned.tasks,...random.tasks])if(task.official_score!==null) {
  assert.equal(task.dataset_checksum,learned.dataset_sha256);
  assert.equal(task.scorer_checksum,control.webarena_verified_evaluator_checksum);
  assert.equal(task.scorer_version,control.webarena_verified_version);
}
const checkpoint='checkpoints/webnav_unified/1790999586398/0000000000998400.bin';
assert.equal(learned.checkpoint_sha256,sha(checkpoint),'Browser run must use the reported checkpoint');
for(const task of learned.tasks)if(task.runner)
  assert.equal(task.runner.policy,learned.options.sample?'sampled':'greedy');
const old='checkpoints/webnav_unified/1790766678997/0000000000031616.bin';
const bytes=fs.readFileSync(checkpoint);
let maxAbs=0;
for(let i=0;i<bytes.length;i+=4){const x=bytes.readFloatLE(i);assert.ok(Number.isFinite(x));maxAbs=Math.max(maxAbs,Math.abs(x));}
const report={generated_at:new Date().toISOString(),
  scope:'Native 125-task curriculum plus a declared WebArena-Verified shopping-admin navigation subset; not full benchmark performance.',
  training:{checkpoint,sha256:sha(checkpoint),bytes:bytes.length,parameters:bytes.length/4,
    finite_weights:true,maximum_absolute_weight:maxAbs,additional_steps:998400,
    initial_checkpoint:old,initial_sha256:sha(old),initial_steps:31616,
    cumulative_steps:1030016,elapsed_seconds:465,precision:'BF16 training; FP32 CPU evaluation',
    continuation:'Weights warm-started; not an optimizer-state resume.',
    config:'logs/webnav_unified/1790999586398.ini',config_sha256:sha('logs/webnav_unified/1790999586398.ini')},
  native:{sampling:'Same initial 604 instances across all 125 task names; 4 or 8 initial lanes per task. Small cohort, no statistical confidence claim.',
    old_greedy:native('build/webnav_unified/greedy.jsonl'),
    new_greedy:native('build/webnav/benchmarks/native-million.jsonl'),
    new_sampled:native('build/webnav/benchmarks/native-million-sampled.jsonl'),
    random:native('build/webnav_unified/random.jsonl')},
  modern:{manifest:json('ocean/webnav/benchmarks/manifest.json'),image:json('ocean/webnav/benchmarks/image.json'),
    calibration:{directory:path.resolve(controlDir),driver:'scripted public-UI positive control, not policy performance',
      task_id:157,official_status:control.status,official_score:control.score,sha256:sha(controlFile)},learned,random,
    qualification_attempts:[
      {directory:'/mnt/d/puffertank/webnav-bench/runs/20261003-calibration',
        outcome:'Setup timed out while a duplicate initialization raced the image automatic initializer. Fixed by waiting for the automatic initializer.'},
      {directory:'/mnt/d/puffertank/webnav-bench/runs/20261003-learned',
        outcome:'Task 157 hit a navigation observation race after 56 choices; run stopped during task 374 setup. Preserved separately; all three tasks rerun after regression tests.'}]},
  validation:{browser_fixtures:{passed:18,failed:0,log:'build/webnav/benchmarks/browser-tests-navigation.log',
      sha256:sha('build/webnav/benchmarks/browser-tests-navigation.log')},
    policy_rpc:{result:'PASS',log:'build/webnav/benchmarks/rpc-tests.log',sha256:sha('build/webnav/benchmarks/rpc-tests.log')},
    real_browser_harness_errors:learned.harness_errors+random.harness_errors,
    memory:{runtime_mib:3072,browser_ceiling_mib:2048,swap:false,
      measured:json('build/webnav/benchmarks/runtime-memory.json')}},
  limits:['All 125 names implemented; full MiniWoB browser/generator fidelity remains unverified.',
    'Modern preset contains 18 navigation tasks out of 812; only requested task IDs above were evaluated.',
    'Browser views retain at most 128 nodes and 16 KiB text; incomplete coverage is recorded.',
    'No policy finish/answer, URL/history/tab actions or visual encoder yet.',
    'Fixed-budget NAVIGATE submission is a candidate; success comes only from the official scorer.',
    'Scripted calibration is excluded from learned and random scores.',
    'New multi-page Bend workflow compositions are planned, not implemented in this milestone.'],
  sources:Object.fromEntries(fs.readdirSync(__dirname).filter(f=>/\.(c|cjs|sh|txt)$/.test(f)).sort().map(f=>[f,sha(path.join(__dirname,f))])),
  native_source_sha256:sha('ocean/webnav_unified/native_eval.c'),
  native_binary_sha256:sha('build/webnav_unified/native_eval'),
  policy_binary_sha256:sha('build/webnav/benchmarks/policy_rpc'),
  semantic_assets:Object.fromEntries(['build/webnav/reference/potion-tokenizer.json',
    'build/webnav/reference/potion-model.safetensors'].map(file=>[file,sha(file)]))};
fs.writeFileSync(path.join(__dirname,'RESULTS.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({native:Object.fromEntries(Object.entries(report.native).filter(([k])=>k!=='sampling').map(([k,v])=>[k,{tasks:v.tasks,episodes:v.episodes,wins:v.wins,macro:v.macro_full_credit_rate}])),
  browser:{tasks:learned.requested_tasks,learned:learned.full_successes,random:random.full_successes,
    learned_errors:learned.harness_errors,random_errors:random.harness_errors}},null,2));
