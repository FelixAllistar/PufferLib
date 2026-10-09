// Positive control for the adapter + HAR + official scorer, never a learned
// policy result or training demonstration. Only task 157 is used for this check.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {ROOT,loadManifest}=require('./manifest.cjs');
const {resetSite}=require('./evaluate.cjs');
const {collectView,executeAction,WF}=require('./browser_view.cjs');
const {BrowserResponse,responseArtifact}=require('./browser_response.cjs');
const {chromium}=require(path.join(ROOT,'node/node_modules/playwright-core'));
async function main() {
  const out=path.resolve(process.argv[2]||path.join(ROOT,'runs',`${Date.now()}-calibration`));
  const preset=process.argv[3]==='--preset'?process.argv[4]:'legacy-navigation';
  if((process.argv.length>3&&process.argv[3]!=='--preset')||process.argv.length>5||
     !['legacy-navigation','response-v1'].includes(preset))throw Error('Usage: calibrate.cjs OUT [--preset legacy-navigation|response-v1]');
  const dir=path.join(out,'157');
  if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Refusing to overwrite calibration evidence');
  fs.mkdirSync(dir,{recursive:true});
  const task=loadManifest().tasks.find(t=>t.task_id===157);
  fs.writeFileSync(path.join(out,'config.json'),JSON.stringify({environments:{__SHOPPING_ADMIN__:{
    urls:['http://127.0.0.1:7780/admin'],active_url_idx:0,use_header_login:true,
    credentials:{username:'admin',password:'admin1234'}}}},null,2));
  fs.writeFileSync(path.join(out,'run.json'),JSON.stringify({driver:'scripted adapter positive control',
    task_id:157,not_policy_performance:true,uses_reference_url:false,preset,
    source_sha256:Object.fromEntries(['calibrate.cjs','evaluate.cjs','browser_view.cjs','browser_response.cjs','response_rpc.c','rpc_client.cjs']
      .map(file=>[file,crypto.createHash('sha256').update(fs.readFileSync(path.join(__dirname,file))).digest('hex')]))},null,2));
  const trace=[];
  const response=preset==='response-v1'?new BrowserResponse():null;
  let error=null,browser=null,context=null,page=null;
  try {
    if(response)await response.reset();
    await resetSite();
    browser=await chromium.launch({executablePath:path.resolve('build/webnav/chrome-headless-shell-linux64/chrome-headless-shell'),
      headless:true,args:['--no-sandbox','--disable-dev-shm-usage','--disable-gpu']});
    context=await browser.newContext({viewport:{width:1280,height:720},serviceWorkers:'block',
      extraHTTPHeaders:{'X-M2-Admin-Auto-Login':'admin:admin1234'},
      recordHar:{path:path.join(dir,'network.har'),mode:'full',content:'embed'}});
    await context.route('**/*',route=>{
      const url=new URL(route.request().url());
      return url.origin==='http://127.0.0.1:7780'||url.protocol==='data:'?route.continue():route.abort();
    });
    page=await context.newPage();
    await page.goto('http://127.0.0.1:7780/admin',{waitUntil:'load',timeout:120000});
    for(const label of ['Customers','All Customers']) {
      let found=false;
      for(let attempt=0;attempt<30;attempt++) {
        const timing={elapsed_ms:trace.length*50,deadline_ms:25600};
        const snapshot=response?await response.collect(page,task.intent,timing):null;
        const view=snapshot?snapshot.view:await collectView(page,task.intent,timing);
        const node=view.nodes.find(n=>n.name.toLowerCase()===label.toLowerCase()&&
          view.capabilities.some(c=>c.kind===WF.CLICK&&c.ref===n.ref));
        if(node) {
          const action={kind:WF.CLICK,target:node.ref,arg0:0,arg1:0,text:''};
          trace.push({view,...(snapshot?{page:snapshot.page}:{}),action});
          if(response)await response.execute(page,action);else await executeAction(page,action);
          found=true;break;
        }
        await page.waitForTimeout(500);
      }
      if(!found)throw Error(`Missing actionable public control: ${label}`);
    }
    await page.waitForLoadState('domcontentloaded');
    await page.waitForTimeout(2000);
    if(response){
      const snapshot=await response.collect(page,task.intent,{elapsed_ms:trace.length*50,deadline_ms:25600});
      const finish=snapshot.view.nodes.find(n=>n.name==='Finish'&&n.parent===response.state.ref_base);
      if(!finish)throw Error('Missing native response Finish control');
      const action={kind:WF.CLICK,target:finish.ref,arg0:0,arg1:0,text:''};
      const result=await response.execute(page,action);trace.push({...snapshot,action,response_phase:result.phase});
      if(result.phase!==1)throw Error('Scripted response was not submitted');
    }
    await page.screenshot({path:path.join(dir,'final.png')});
  } catch(e) {
    error=String(e.stack||e);
    if(page) {
      try {await page.screenshot({path:path.join(dir,'error.png')});}catch {}
      try {fs.writeFileSync(path.join(dir,'error-view.json'),JSON.stringify(await collectView(page,task.intent),null,2));}catch {}
    }
  }
  finally {
    if(context)try {await context.close();}catch(e){error=error||`HAR flush: ${e.message}`;}
    if(browser)try {await browser.close();}catch(e){error=error||`Browser close: ${e.message}`;}
    response?.close();
  }
  fs.writeFileSync(path.join(dir,'trajectory.jsonl'),trace.map(x=>JSON.stringify(x)).join('\n')+'\n');
  fs.writeFileSync(path.join(dir,'agent_response.json'),response?responseArtifact(response.state):
    JSON.stringify({task_type:'NAVIGATE',status:error?'UNKNOWN_ERROR':'SUCCESS',retrieved_data:null,error_details:error},null,2));
  const result={out,driver:'scripted calibration only',preset,actions:trace.length,error,
    ...(response?{response_submitted:response.state?.phase===1}:{} )};
  fs.writeFileSync(path.join(dir,'runner.json'),JSON.stringify(result,null,2));
  console.log(JSON.stringify(result));
  if(error)process.exitCode=1;
}
main().catch(e=>{console.error(e.stack);process.exitCode=1;});
