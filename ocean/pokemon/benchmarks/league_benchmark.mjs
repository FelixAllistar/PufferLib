import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const output=path.resolve(root,process.argv[2]||'reports/pokemon_benchmarks/20261014');
const binary=process.env.PK_BENCHMARK_BINARY||'build/pokemon/benchmark_viewer';
const deterministic=process.env.PK_BENCHMARK_DETERMINISTIC==='1';
const blockGames=Number(process.env.PK_BENCHMARK_BLOCK_GAMES||64);
if(!Number.isInteger(blockGames)||blockGames<2||blockGames%2)throw Error('Need an even block size');
fs.mkdirSync(output,{recursive:true});
const state=JSON.parse(fs.readFileSync(path.join(root,'leagues/pokemon/ou_leads_rr50m/state.json')));
const free=JSON.parse(fs.readFileSync(path.join(root,'leagues/pokemon/free4/state.json')));
const candidates=state.members.map(m=>({name:m.name,path:m.current.path,sha256:m.current.sha256}));
candidates.push({name:'free4_master',...free.members[0].current});
for(const c of candidates) {
    const sha=crypto.createHash('sha256').update(fs.readFileSync(c.path)).digest('hex');
    if(sha!==c.sha256)throw Error('Checkpoint mutated: '+c.name);
}
const tasks=[];
// 128 games/pair: two independent seed blocks, both seats balanced.
for(let i=0;i<candidates.length;i++)for(let j=i+1;j<candidates.length;j++)
    for(const seed of [2026101401,2026101402])tasks.push({a:candidates[i],b:candidates[j],games:blockGames,seed});
// Independent trivial floor; avoid confusing it with a strong public baseline.
for(const c of candidates)tasks.push({a:c,b:{name:'random',path:'random'},games:128,seed:2026101403});
const manifest={created:new Date().toISOString(),binary,sampling:deterministic?'argmax':'stochastic',candidates,tasks};
const planFile=path.join(output,'league-plan.json');
if(fs.existsSync(planFile)) {
    const previous=JSON.parse(fs.readFileSync(planFile));
    if(JSON.stringify(previous.candidates)!==JSON.stringify(candidates) || previous.sampling!==manifest.sampling ||
       previous.tasks[0].games!==blockGames)throw Error('Benchmark inputs changed; use a new output directory');
}
fs.writeFileSync(path.join(output,'league-plan.json'),JSON.stringify(manifest,null,2));
const total=tasks.length;let done=0;const results=[];
async function worker(){while(tasks.length){const t=tasks.shift();const key=t.a.name+'--'+t.b.name+'--'+t.seed;
const log=path.join(output,key+'.log'),saved=path.join(output,key+'.result.json');
if(fs.existsSync(saved)){results.push(JSON.parse(fs.readFileSync(saved)));done++;continue;}
const fd=fs.openSync(log,'w'),args=['eval',t.a.path,t.b.path,'--games='+t.games,'--seed='+t.seed,...(deterministic?['--deterministic']:[])];
const started=Date.now();try{await new Promise((resolve,reject)=>{const p=spawn(path.join(root,binary),args,{cwd:root,stdio:['ignore',fd,fd]});p.on('error',reject);p.on('close',code=>code===0?resolve():reject(Error(key+' exit '+code)));});}finally{fs.closeSync(fd);}
const text=fs.readFileSync(log,'utf8'),m=text.match(/games=(\d+) W=(\d+) D=(\d+) L=(\d+) score=([\d.]+).*timeouts=(\d+)/);
if(!m)throw Error('Missing result '+key);
const result={a:t.a.name,b:t.b.name,a_sha256:t.a.sha256,b_sha256:t.b.sha256,deterministic,seed:t.seed,games:+m[1],wins:+m[2],draws:+m[3],losses:+m[4],score:+m[5],timeouts:+m[6],seconds:(Date.now()-started)/1000};
fs.writeFileSync(saved,JSON.stringify(result,null,2));results.push(result);done++;
console.log(done+'/'+total+' '+t.a.name+' vs '+t.b.name+' '+result.wins+'/'+result.draws+'/'+result.losses+' '+result.seconds+'s');
fs.writeFileSync(path.join(output,'league-results.json'),JSON.stringify(results,null,2));
}}
await Promise.all([worker(),worker()]);
