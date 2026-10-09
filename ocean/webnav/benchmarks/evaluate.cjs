// Real website evaluation only. The agent receives public views and instructions;
// official references stay in the separate, unchanged upstream scorer.
const fs = require('node:fs');
const path = require('node:path');
const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
const crypto = require('node:crypto');
const {ROOT, loadManifest} = require('./manifest.cjs');
const {collectView, executeAction} = require('./browser_view.cjs');
const {lineRpc} = require('./rpc_client.cjs');
const {BrowserResponse, RESPONSE_PRESET, RESPONSE_REF_BASE, responseArtifact} = require('./browser_response.cjs');
const {chromium} = require(path.join(ROOT, 'node/node_modules/playwright-core'));
const exec = promisify(execFile);

async function resetSite() {
  await exec('bash', ['ocean/webnav/benchmarks/runtime.sh', 'reset'], {timeout:120000});
  console.error('[webnav benchmark] Waiting for fresh site initialization');
  // The pinned image already runs env-ctrl-init with the external URL supplied
  // by runtime.sh. Wait for that one initializer; POST /init here would race it.
  const initDeadline=Date.now()+600000;
  let initialized=false;
  while(Date.now()<initDeadline) {
    let status='';
    try {
      status=(await exec('bash',['ocean/webnav/benchmarks/runtime.sh','docker','exec',
        'webnav-bench-admin','supervisorctl','-s','unix:///run/supervisord.sock',
        'status','env-ctrl-init'],{timeout:10000})).stdout;
    } catch(e) {status=e.stdout||'';}
    if(/\b(EXITED|FATAL|BACKOFF)\b/.test(status)) {
      const {stdout}=await exec('bash',['ocean/webnav/benchmarks/runtime.sh','docker','exec',
        'webnav-bench-admin','tail','-n','40','/tmp/env-ctrl-init.log'],{timeout:10000});
      if(!/\bEXITED\b/.test(status)||!/^\[OK\]\s*$/m.test(stdout)||/^\[FAILED\]/m.test(stdout))
        throw Error(`Benchmark automatic initialization failed: ${status.trim()} ${stdout.trim()}`);
      initialized=true;break;
    }
    await new Promise(resolve=>setTimeout(resolve,3000));
  }
  if(!initialized)throw Error('Benchmark automatic initialization timed out');
  let ready=false;
  const readinessDeadline=Date.now()+300000;
  while(Date.now()<readinessDeadline) {
    try {
      // Allow cold service checks to finish instead of queueing abandoned ones.
      const r=await fetch('http://127.0.0.1:7781/status',{
        signal:AbortSignal.timeout(Math.min(60000,Math.max(1,readinessDeadline-Date.now())))});
      if(r.ok&&(await r.json()).success){ready=true;break;}
    } catch {}
    await new Promise(resolve=>setTimeout(resolve,1000));
  }
  if(!ready) throw Error('Fresh benchmark container did not become healthy');
  console.error('[webnav benchmark] Site initialized and healthy');
}

function options(args) {
  const out = {checkpoint: 'random', tasks: '157', steps: 64, headed: false, sample:false, preset:'legacy-navigation'};
  for (let i=0; i<args.length; i++) {
    if (args[i] === '--headed') {out.headed = true; continue;}
    if (args[i] === '--sample') {out.sample = true; continue;}
    if (!['--checkpoint','--tasks','--steps','--out','--preset'].includes(args[i]) || !args[i+1]) throw Error('Invalid arguments');
    out[args[i].slice(2)] = args[++i];
  }
  out.steps = Number(out.steps);
  if (!Number.isInteger(out.steps) || out.steps < 1 || out.steps > 512) throw Error('steps must be 1..512');
  if (!['legacy-navigation','response-v1'].includes(out.preset)) throw Error('preset must be legacy-navigation or response-v1');
  if (!out.out) out.out = path.join(ROOT, 'runs', `${Date.now()}-${out.checkpoint === 'random' ? 'random' : 'greedy'}`);
  out.out = path.resolve(out.out);
  return out;
}
async function main() {
  const opt=options(process.argv.slice(2)), manifest=loadManifest();
  const ids = opt.tasks === 'all' ? manifest.tasks.map(t=>t.task_id) : opt.tasks.split(',').map(Number);
  if (new Set(ids).size !== ids.length || ids.some(id=>!Number.isInteger(id))) throw Error('Invalid/duplicate task IDs');
  const tasks=ids.map(id=>{const t=manifest.tasks.find(t=>t.task_id===id);if(!t)throw Error(`Task ${id} outside navigation preset`);return t;});
  if(fs.existsSync(opt.out)&&fs.readdirSync(opt.out).length)
    throw Error(`Refusing to overwrite existing run evidence: ${opt.out}`);
  fs.mkdirSync(opt.out,{recursive:true});
  const config={environments:{__SHOPPING_ADMIN__:{urls:['http://127.0.0.1:7780/admin'],active_url_idx:0,
    use_header_login:true,credentials:{username:'admin',password:'admin1234'}}}};
  fs.writeFileSync(path.join(opt.out,'config.json'),JSON.stringify(config,null,2));
  const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  fs.writeFileSync(path.join(opt.out,'run.json'),JSON.stringify({manifest,options:opt,
    checkpoint_sha256:opt.checkpoint==='random'?null:hash(opt.checkpoint),
    policy_binary_sha256:hash('build/webnav/benchmarks/policy_rpc'),
    response_binary_sha256:opt.preset==='response-v1'?hash('build/webnav/benchmarks/response_rpc'):null,
    response_library_sha256:opt.preset==='response-v1'?hash('build/webnav/primitives/response_form/libresponse_form.so'):null,
    browser_preset:opt.preset==='response-v1'?
      {name:'response-v1',viewport:[1280,720],dom_capacity:RESPONSE_PRESET,
        host_nodes:16,host_text_bytes:4096,page_metadata:'v1; trace only; not policy features',
        termination:'explicit response controls; budget expiry writes null absence marker'}:
      'DOM 128 nodes; 1280x720; fixed budget; no answer or explicit stop head',
    timing:{logical_ms_per_engine_action:50,post_action_wait_ms:100,initial_wait_ms:1000,
      initial_navigation_wait:'load',initial_navigation_timeout_ms:120000},
    runtime:{image:fs.readFileSync(path.join(__dirname,'image.txt'),'utf8').trim(),
      site_memory_mib:2304,site_cpus:2,runtime_aggregate_memory_mib:3072,
      browser_memory_ceiling_mib:2048,swap:false},
    terminal_convention:opt.preset==='response-v1'?
      'Only explicit Finish creates a response; exact submitted JSON is preserved. Expiry/errors without submission write JSON null for scoring as absent.':
      'At budget, submits NAVIGATE/SUCCESS as an unverified candidate for the official grader; this is not a measured win.',
    source_sha256:Object.fromEntries(['evaluate.cjs','browser_view.cjs','policy_rpc.c','manifest.cjs','rpc_client.cjs',
      'browser_response.cjs','response_rpc.c','../primitives/transport/public_json.h','../primitives/transport/json.h',
      '../primitives/transport/line.h'].map(f=>[f,hash(path.join(__dirname,f))]))},null,2));
  const browser=await chromium.launch({executablePath:path.resolve(opt.headed ? 'build/webnav/chrome-linux64/chrome':'build/webnav/chrome-headless-shell-linux64/chrome-headless-shell'),
    headless:!opt.headed,args:['--no-sandbox','--disable-dev-shm-usage','--disable-gpu']});
  const policy=lineRpc('build/webnav/benchmarks/policy_rpc',[opt.checkpoint,...(opt.sample?['--sample']:[])]);
  const response=opt.preset==='response-v1'?new BrowserResponse():null;
  const results=[];
  try {
    for(const task of tasks) {
      const dir=path.join(opt.out,String(task.task_id));
      if(fs.existsSync(dir)) throw Error(`Refusing to overwrite existing task evidence: ${dir}`);
      fs.mkdirSync(dir);
      const startedAt=Date.now();
      let policyStartedAt=null;
      let context=null,page=null,traceFd=null,phase='setup',error=null, executed=0, local=0,
        actionErrors=0, observations=0, incomplete=0, omitted=0, truncated=0,
        responseActions=0,responseReady=false;
      try {
        traceFd=fs.openSync(path.join(dir,'trajectory.jsonl'),'wx');
        console.error(`[webnav benchmark] Task ${task.task_id}: setup`);
        if(policy.dead)throw Error('Policy process unavailable after earlier failure');
        // Seed is independent of benchmark task IDs and private metadata.
        await policy.request({reset:true,seed:91037});
        if(response){await response.reset();responseReady=true;}
        await resetSite();
        context=await browser.newContext({viewport:{width:1280,height:720},serviceWorkers:'block',
          extraHTTPHeaders:{'X-M2-Admin-Auto-Login':'admin:admin1234'},
          recordHar:{path:path.join(dir,'network.har'),mode:'full',content:'embed'}});
        await context.route('**/*', route=>{
          const url=new URL(route.request().url());
          return url.origin==='http://127.0.0.1:7780' || url.protocol==='data:' ? route.continue() : route.abort();
        });
        page=await context.newPage();page.setDefaultTimeout(5000);
        await page.goto('http://127.0.0.1:7780/admin',{waitUntil:'load',timeout:120000});
        await page.waitForTimeout(1000);
        phase='policy';policyStartedAt=Date.now();
        console.error(`[webnav benchmark] Task ${task.task_id}: policy actions`);
        let view=null,snapshot=null;
        for(let step=0;step<opt.steps;step++) {
          if(!view) {
            const timing={elapsed_ms:executed*50,deadline_ms:25600};
            if(response){snapshot=await response.collect(page,task.intent,timing);view=snapshot.view;}
            else view=await collectView(page,task.intent,timing);
            observations++;incomplete+=!!view.incomplete;
            omitted=Math.max(omitted,view.omitted);truncated=Math.max(truncated,view.text_truncated);
          }
          const command=await policy.request(view);
          const event={step,url:page.url(),view,command};
          if(snapshot)event.page=snapshot.page;
          if(command.local) local++;
          else {
            executed++;
            if(response){
              event.action_domain=command.target>=RESPONSE_REF_BASE?'response':'browser';
              responseActions+=event.action_domain==='response';
            }
            try {
              if(response){const result=await response.execute(page,command);event.response_phase=result.phase;}
              else await executeAction(page,command);
            }
            catch(e){actionErrors++;event.action_error=String(e.message);}
            view=null;
          }
          // A bounded synchronous write makes disk failures ordinary task errors
          // and avoids unbounded buffering on the slower D: artifact filesystem.
          fs.writeFileSync(traceFd,JSON.stringify(event)+'\n');
          if(response?.state?.phase===1)break;
        }
        if(response?.state?.phase===0)await response.expire();
        await page.screenshot({path:path.join(dir,'final.png')});
      } catch(e) {error=String(e.stack||e);}
      finally {
        if(traceFd!==null)try {fs.closeSync(traceFd);}catch(e){error=error||`Trace close: ${e.message}`;}
        const finalUrl=page?.url()||null;
        if(context)try {await context.close();}catch(e){error=error||`HAR flush: ${e.message}`;}
        if(responseReady&&response.state.phase===0&&!response.dead)
          try {await response.expire();}catch(e){error=error||`Response expiry: ${e.message}`;}
        const completion=responseReady?response.state:null;
        fs.writeFileSync(path.join(dir,'agent_response.json'),response?responseArtifact(completion):
          JSON.stringify({task_type:'NAVIGATE',status:error?'UNKNOWN_ERROR':'SUCCESS',retrieved_data:null,error_details:error},null,2));
        const result={task_id:task.task_id,policy:opt.checkpoint==='random'?'random':opt.sample?'sampled':'greedy',
          steps:executed+local,executed,local,action_errors:actionErrors,observations,incomplete_views:incomplete,
          max_omitted:omitted,max_text_truncated:truncated,final_url:finalUrl,error,error_phase:error?phase:null,
          setup_ms:(policyStartedAt||Date.now())-startedAt,
          policy_ms:policyStartedAt?Date.now()-policyStartedAt:null,
          ...(response?{response_actions:responseActions,browser_actions:executed-responseActions,
            response_submitted:completion?.phase===1,
            termination:completion?.phase===1?'explicit_submission':error?'harness_error':'budget_expired'}:{}),
          scoring:response?'pending official evaluator; null means no submitted response; claims are ungraded':
            'pending official evaluator; status SUCCESS above is only the submission claim'};
        fs.writeFileSync(path.join(dir,'runner.json'),JSON.stringify(result,null,2));
        results.push(result);console.log(JSON.stringify(result));
        fs.writeFileSync(path.join(opt.out,'runner_results.json'),JSON.stringify(results,null,2));
      }
    }
  } finally {response?.close();policy.close();await browser.close();}
  fs.writeFileSync(path.join(opt.out,'runner_results.json'),JSON.stringify(results,null,2));
  console.error(`Evidence: ${opt.out}`);
  if(results.some(r=>r.error)) process.exitCode=1;
}
module.exports={resetSite};
if(require.main===module)main().catch(e=>{console.error(e.stack);process.exitCode=1;});
