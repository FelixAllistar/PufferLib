'use strict';
// Direct pinned Showdown baseline. No server, websocket, Python per step or
// external action search. This is not the PufferLib worker/encoder benchmark.
const fs=require('fs'),path=require('path');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const oracle=path.resolve(process.env.PG9_JS_PROBE_ORACLE||'build/pokemon_gen9/oracle');
const pin='9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e';
if(JSON.parse(fs.readFileSync(path.join(oracle,'revision.json'))).revision!==pin)throw Error('Unpinned Showdown');
const {Battle}=require(path.join(oracle,'dist/sim/battle'));
const {RandomPlayerAI}=require(path.join(oracle,'dist/sim/tools/random-player-ai'));
// Source records Date.now() in log timestamps. Supply the same diagnostic
// clock to both runtimes so full logs can match; timings use monotonic clocks.
Date.now=()=>1791194400000;
class RequestAI extends RandomPlayerAI {
  choose(choice){this.selected=choice;}
  pick(request){
    this.selected=null;
    this.receiveRequest(request);
    if(this.selected&&this.selected.startsWith('move ')&&request.active?.[0]?.canTerastallize&&this.prng.random()<0.1)
      this.selected+=' terastallize';
    return this.selected;
  }
}
function seed(n,salt){return [n&65535,(n*47111+salt)&65535,(n*32117+salt*17)&65535,(65535-n+salt)&65535].join(',');}
function runOne(n){
  const b=new Battle({formatid:'gen9randombattle',seed:seed(n,1),
    p1:{name:'one',seed:seed(n,2)},p2:{name:'two',seed:seed(n,3)}});
  const bots=[new RequestAI(null,{seed:seed(n,4),move:0.95}),new RequestAI(null,{seed:seed(n,5),move:0.95})];
  let decisions=0,retries=0,boundaries=0;
  while(!b.ended){
    if(++boundaries>10000)throw Error('Battle did not finish within diagnostic boundary budget');
    let acted=false;
    // Read both actor-only requests before submitting either side's choice.
    const choices=b.sides.map((side,i)=>side.activeRequest&&!side.activeRequest.wait&&!side.isChoiceDone()?bots[i].pick(side.activeRequest):null);
    for(let i=0;i<2&&!b.ended;i++)if(choices[i]){
      acted=true;decisions++;
      if(!b.choose(b.sides[i].id,choices[i])){
        if(!b.sides[i].choice.error.startsWith("Can't switch: The active Pokémon is trapped"))
          throw Error('Choice rejected: '+b.sides[i].choice.error);
        retries++;
      }
    }
    if(!acted&&!b.ended)throw Error('No actionable request');
  }
  const result={seed:n,turns:b.turn,winner:b.winner,decisions,retries,
    tera:b.sides.reduce((count,side)=>count+side.pokemon.filter(p=>p.terastallized).length,0),
    log:b.log.join('\n')};
  b.destroy();return result;
}
const warmup=4,count=Number(process.env.PG9_JS_PROBE_GAMES||32);
if(!Number.isSafeInteger(count)||count<32||count>4096)throw Error('Invalid diagnostic game count');
const label=process.env.PG9_JS_PROBE_LABEL||(process.versions.bun?'bun-compiled':'node-baseline');
if(!/^[a-z0-9-]+$/.test(label))throw Error('Invalid artifact label');
for(let n=0;n<warmup;n++)runOne(10000+n);
const start=process.hrtime.bigint(),cpu=process.cpuUsage(),games=[];
let turns=0,decisions=0,retries=0,teras=0;
for(let n=0;n<count;n++){
  const game=runOne(n);turns+=game.turns;decisions+=game.decisions;retries+=game.retries;teras+=game.tera;
  // Hash complete source logs; later runtime candidates must match these games.
  const digest=require('crypto').createHash('sha256').update(game.log).digest('hex');
  games.push({...game,log:undefined,sha256:digest});
}
const seconds=Number(process.hrtime.bigint()-start)/1e9,used=process.cpuUsage(cpu),cpuSeconds=(used.user+used.system)/1e6;
const result={revision:pin,runtime:process.version,operation:'direct complete Showdown games',
  warmup_games:warmup,measured_games:count,clock_ms:1791194400000,turns,actor_decisions:decisions,retries,terastallizations:teras,
  seconds,cpu_seconds:cpuSeconds,games_per_second:count/seconds,turns_per_second:turns/seconds,
  actor_decisions_per_second:decisions/seconds,max_rss_kib:process.resourceUsage().maxRSS,
  includes:'team generation, full original battle rules/logging/request construction, source bot decisions and final-log hashing',
  excludes:'PufferLib encoder/transport, CUDA inference/training and worker scaling',cpu_quota:'100%',games};
const dir=path.resolve('build/pokemon_gen9/js-probe');fs.mkdirSync(dir,{recursive:true});
if(process.versions.bun)result.bun=process.versions.bun;
fs.writeFileSync(path.join(dir,label+'.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({...result,games:undefined},null,2));
