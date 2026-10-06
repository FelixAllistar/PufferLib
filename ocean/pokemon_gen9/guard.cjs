'use strict';
// Keep the resource scope attached to the launcher even across terminal signals.
const fs=require('fs'), cp=require('child_process'), crypto=require('crypto'), os=require('os');
const scopePattern=/^pokemon-gen9-[0-9]+-[a-f0-9-]+\.scope$/;
function stopWorkers(){
  const list=cp.spawnSync('systemctl',['--user','list-units','--type=scope','--all',
    '--plain','--no-legend','--no-pager','pokemon-gen9-*.scope'],{encoding:'utf8'});
  if(list.error)throw list.error;
  if(list.status!==0)throw Error(list.stderr||'Could not list Pokémon resource scopes');
  const scopes=list.stdout.split('\n').map(row=>row.trim().split(/\s+/)[0]).filter(name=>scopePattern.test(name));
  for(const scope of scopes){
    const result=cp.spawnSync('systemctl',['--user','stop',scope],{stdio:'inherit'});
    if(result.error||result.status!==0)throw result.error||Error('Could not stop '+scope);
  }
  console.log(scopes.length?'Stopped Pokémon resource scopes: '+scopes.join(', '):'No Pokémon resource scopes are running.');
}
function launch(argv){
  fs.mkdirSync('build/webnav/families',{recursive:true});
  const available=Number(fs.readFileSync('/proc/meminfo','utf8').match(/^MemAvailable:\s+(\d+)/m)[1]);
  const cap=Math.min(6442450944,(available-786432)*1024);
  if(cap<536870912)throw Error('Insufficient available memory: '+available+' KiB');
  const scope='pokemon-gen9-'+process.pid+'-'+crypto.randomUUID()+'.scope';
  const runtime=argv.includes('--worker-train-only')||argv.includes('--worker-scale-only')||argv.includes('--worker-batch-only')||
    argv.includes('--continuation-train-only')||argv.includes('--continuation-smoke-only')||argv.includes('--continuation-train-bench-only')||
    argv.includes('--continuation-benchmark-only')||argv.includes('--continuation-scale-only');
  const cpuQuota=100*(runtime?os.availableParallelism():1);
  console.log('Pokémon '+(runtime?'runtime':'build/check')+' CPU quota: '+cpuQuota+'%; memory cap: '+Math.floor(cap/1048576)+' MiB');
  const args=['--nonblock','--conflict-exit-code','75','build/webnav/families/resource.lock',
    'systemd-run','--user','--scope','--unit='+scope,
    '-p','MemoryMax='+cap,'-p','MemorySwapMax=0','-p','TasksMax=128','-p','CPUQuota='+cpuQuota+'%',
    'env','WEBNAV_BUILD_CONFINED=1','timeout','--signal=TERM','--kill-after=5s','1200s',
    process.execPath,require.resolve('./build.cjs'),...argv];
  // The terminal signals only this launcher. Keep flock alive until the whole
  // scope has stopped, so cancellation cannot release the resource lock early.
  const child=cp.spawn('flock',args,{stdio:'inherit',detached:true});
  let interrupted=0,stopping=false,closed=false;
  function stopScope(){
    if(stopping||closed)return;
    stopping=true;
    const stop=cp.spawn('systemctl',['--user','stop',scope],{stdio:'ignore'});
    stop.once('error',error=>{console.error('Scope stop failed:',error.message);stopping=false;});
    stop.once('close',()=>{
      stopping=false;
      // Retry if the interrupt arrived before systemd registered the scope.
      if(!closed)setTimeout(stopScope,100);
    });
  }
  function interrupt(signal){
    if(!interrupted)console.error('\nStopping Pokémon resource scope (trainer and workers)...');
    interrupted=signal==='SIGINT'?130:143;
    stopScope();
  }
  process.on('SIGINT',()=>interrupt('SIGINT'));
  process.on('SIGTERM',()=>interrupt('SIGTERM'));
  child.once('error',error=>{closed=true;console.error(error.message);process.exitCode=1;});
  child.once('close',(code,signal)=>{
    closed=true;
    process.exitCode=interrupted||(code===null?(signal==='SIGINT'?130:1):code);
  });
}
module.exports={launch,stopWorkers};
