import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const dir=path.resolve(root,process.argv[2]||'reports/pokemon_benchmarks/20261014');
const read=p=>JSON.parse(fs.readFileSync(p));
const score=x=>(x.wins+0.5*x.draws)/x.games;
const interval=x=>{const p=score(x),z=1.96,n=x.games,d=1+z*z/n,c=(p+z*z/(2*n))/d,h=z*Math.sqrt(p*(1-p)/n+z*z/(4*n*n))/d;return [c-h,c+h];};
function aggregate(results,names) {
    const table=Object.fromEntries(names.map(name=>[name,{name,games:0,wins:0,draws:0,losses:0,timeouts:0}]));
    for(const r of results) {
        for(const [name,w,l] of [[r.a,r.wins,r.losses],[r.b,r.losses,r.wins]]) {
            if(!table[name])continue;
            const x=table[name];x.games+=r.games;x.wins+=w;x.draws+=r.draws;x.losses+=l;x.timeouts+=r.timeouts;
        }
    }
    return Object.values(table).map(x=>({...x,score:score(x),approximate_95_interval:interval(x)})).sort((a,b)=>b.score-a.score);
}
const panels={};
for(const [mode,location] of [['stochastic',dir],['argmax',path.join(dir,'argmax')]]) {
    const plan=read(path.join(location,'league-plan.json')),results=read(path.join(location,'league-results.json'));
    if(results.length!==plan.tasks.length)throw Error('Incomplete '+mode+' benchmark');
    const names=plan.candidates.map(x=>x.name),pairs=results.filter(r=>r.b!=='random');
    panels[mode]={games:results.reduce((n,x)=>n+x.games,0),blocks:results.length,
        ranking:aggregate(pairs,names),random_floor:aggregate(results.filter(r=>r.b==='random'),names),
        master_vs_ou:aggregate(pairs.filter(r=>r.a==='master'&&r.b!=='free4_master'),['master'])[0],
        master_vs_free4:aggregate(pairs.filter(r=>r.a==='master'&&r.b==='free4_master'),['master'])[0],
        comparison_matrix:pairs};
}
const external=[];
for(const f of fs.readdirSync(dir).filter(f=>f.endsWith('.json') && /-\d+-(argmax|sample)/.test(f))) {
    const r=read(path.join(dir,f));if(r.opponent && !r.validate) {
        const {records,...metadata}=r;
        external.push({...metadata,score:score(r),approximate_95_interval:interval(r),source_file:f});
    }
}
const allPlans=[dir,path.join(dir,'argmax')].map(d=>read(path.join(d,'league-plan.json')));
const provenance={};
for(const p of allPlans)for(const c of p.candidates) {
    const hash=crypto.createHash('sha256').update(fs.readFileSync(c.path)).digest('hex');
    if(hash!==c.sha256)throw Error('Checkpoint changed during benchmark');
    const config=path.join(path.dirname(c.path),'config.ini');
    provenance[c.name]={...c,config_sha256:crypto.createHash('sha256').update(fs.readFileSync(config)).digest('hex')};
}
const summary={created:new Date().toISOString(),verdict:{
    learned_skill:'Every evaluated policy strongly exceeds random legal play.',
    strong_external_play:'Not established; surviving policies are badly outmatched by the full-information Oak search baseline.',
    longitudinal_improvement:'Unverifiable retrospectively: earlier comparable checkpoints were deleted. Cross-policy ranking and training dashboards are not a learning curve.'},
    total_games:Object.values(panels).reduce((n,p)=>n+p.games,0)+external.reduce((n,p)=>n+p.games,0),
    protocol:{engine:'pkmn/engine 9b88fd6c5467f703c38951d5b2e8a660314d410b, Showdown mode',
        rules:'RBY OU, level 100, max stats, 512 update cap, root draft starts, no shaping or resets',
        seats:'Equal games in both seats for each block',
        intervals:'Approximate Wilson score intervals; draw counts as half. No multiple-comparison or winner-selection adjustment.',
        external_information:'Oak receives full state; neural policy receives ordinary private observations.',
        oak_engine:'Native engine controls all actual outcomes. Oak fork is not byte-identical; native fields and legal choices checked every turn.',
        oak_search_rng:'Internally random_device seeded; archived game/draft seeds and per-game outcomes do not imply bitwise search repeatability.',
        hardware:'CPU evaluation; GPU service and user checkpoints unchanged'},
    checkpoint_provenance:provenance,panels,external};
summary.external_panels=[];
for(const name of ['master','zam_egg_cloy_don'])for(const deterministic of [false,true]) {
    const records=external.filter(r=>r.checkpoint===provenance[name].path&&r.budget===4096&&r.deterministic===deterministic&&!r.fixed_team);
    if(records.length===3) {
        const totals=records.reduce((x,r)=>({games:x.games+r.games,wins:x.wins+r.wins,draws:x.draws+r.draws,losses:x.losses+r.losses}),{games:0,wins:0,draws:0,losses:0});
        summary.external_panels.push({name,deterministic,...totals,score:score(totals),approximate_95_interval:interval(totals),teams:records.map(r=>r.team)});
    }
}
summary.future_benchmark_commands=[
    'bash ocean/pokemon/benchmarks/build.sh',
    'PK_BENCHMARK_BINARY=build/pokemon/benchmark_viewer_blas node ocean/pokemon/benchmarks/league_benchmark.mjs reports/pokemon_benchmarks/NEW_RUN',
    'PK_BENCHMARK_BINARY=build/pokemon/benchmark_viewer_blas PK_BENCHMARK_DETERMINISTIC=1 PK_BENCHMARK_BLOCK_GAMES=32 node ocean/pokemon/benchmarks/league_benchmark.mjs reports/pokemon_benchmarks/NEW_RUN/argmax',
    'build/pokemon/oak-benchmark-env/bin/python ocean/pokemon/benchmarks/oak_benchmark.py CHECKPOINT --games 128 --budget 4096 --deterministic --fast --team zam_egg_mie --output reports/pokemon_benchmarks/NEW_RUN',
    'node ocean/pokemon/benchmarks/summarize.mjs reports/pokemon_benchmarks/NEW_RUN'];
summary.frozen_baselines={};
for(const [label,name] of [['ou_master','master'],['ou_argmax_winner','zam_egg_cloy_don'],['free4_master','free4_master']]) {
    const file=path.join(dir,'frozen_baselines',label,'weights.bin');
    if(fs.existsSync(file)) {
        const actual=crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
        if(actual!==provenance[name].sha256)throw Error('Frozen baseline hash mismatch');
        summary.frozen_baselines[label]={path:file,sha256:actual};
    }
}
summary.retention_note='Three small frozen policy/config pairs are retained outside the league cleanup target. Compare future snapshots on the same external panel and budgets; older rounds are unavailable.';
summary.source_sha256={};
for(const relative of ['ocean/pokemon/benchmarks/adapter.c','ocean/pokemon/benchmarks/oak_benchmark.py',
    'ocean/pokemon/benchmarks/league_benchmark.mjs','ocean/pokemon/benchmarks/blas.c',
    'ocean/pokemon/bridge.zig','ocean/pokemon/semantic_cpu.h','ocean/pokemon/semantic_contract.h'])
    summary.source_sha256[relative]=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,relative))).digest('hex');
fs.writeFileSync(path.join(dir,'summary.json'),JSON.stringify(summary,null,2)+'\n');
console.log(JSON.stringify({total_games:summary.total_games,
    stochastic_top:panels.stochastic.ranking.slice(0,5),argmax_top:panels.argmax.ranking.slice(0,5),
    external:external.map(r=>({file:r.source_file,games:r.games,wins:r.wins,draws:r.draws,losses:r.losses,score:r.score}))},null,2));
