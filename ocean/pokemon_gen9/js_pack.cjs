'use strict';
// Runtime-packaging experiment, explicitly distinct from native JS AOT.
const fs=require('fs'),path=require('path'),cp=require('child_process'),crypto=require('crypto');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const root=path.resolve('build/pokemon_gen9/js-probe'),tool=path.join(root,'bun-1.4.2'),binary=path.join(tool,'bin/bun');
fs.mkdirSync(root,{recursive:true});
const archive=path.join(root,'bun-linux-x64-1.4.2.tgz');
const integrity='9/E/UXOTpSo3YsV5g+FhtTd/qTpiWoKuxS12cqtuYA1ssu9fRAoPQnipFgGyck3tWO63iUdxBiygq+kELFawng==';
function run(command,args){console.log([command,...args].join(' '));cp.execFileSync(command,args,{stdio:'inherit'});}
if(!fs.existsSync(binary)){
 run('curl',['--fail','--silent','--show-error','--location','--max-time','120',
  'https://registry.npmjs.org/@oven/bun-linux-x64/-/bun-linux-x64-1.4.2.tgz','--output',archive]);
 if(crypto.createHash('sha512').update(fs.readFileSync(archive)).digest('base64')!==integrity)throw Error('Bun checksum mismatch');
 const entries=cp.execFileSync('tar',['tzf',archive],{encoding:'utf8'}).trim().split('\n');
 if(entries.some(name=>!['package/bin/bun','package/package.json','package/README.md'].includes(name)))throw Error('Unexpected archive entries');
 fs.mkdirSync(tool,{recursive:true});run('tar',['xzf',archive,'--strip-components=1','-C',tool]);
}
const version=cp.execFileSync(binary,['--version'],{encoding:'utf8'}).trim();
if(version!=='1.4.2')throw Error('Unexpected Bun version '+version);
const exe=path.join(root,'showdown-bun');
run(process.execPath,['ocean/pokemon_gen9/js_probe.cjs']);
// Preserve the oracle and dereference its development dependency symlink in
// an experiment-local runtime tree. Compiled Bun's external resolver needs a
// physical package here; no upstream or reference files are edited.
const runtime=path.join(root,'runtime');
if(!fs.existsSync(path.join(runtime,'revision.json')))
 fs.cpSync(path.resolve('build/pokemon_gen9/oracle'),runtime,{recursive:true,dereference:true});
// Bundle the sole npm dependency into PRNG's experiment-local module. Keep
// its relative Utils import external so all simulator modules share that one
// module instance. This changes loading only, not RNG/mechanics functions.
const esbuild=require(path.resolve('build/pokemon_gen9/tools/node_modules/esbuild'));
const prng=path.join(runtime,'dist/sim/prng.js'),bundled=prng+'.bundle';
esbuild.buildSync({entryPoints:[path.resolve('build/pokemon_gen9/oracle/dist/sim/prng.js')],
 bundle:true,platform:'node',format:'cjs',target:'es2022',outfile:bundled,external:['../lib/utils'],
 nodePaths:[path.resolve('build/pokemon_gen9/tools/node_modules')],logLevel:'warning'});
fs.renameSync(bundled,prng);
run(binary,['build','ocean/pokemon_gen9/js_probe.cjs','--compile','--outfile',exe]);
console.log(exe);cp.execFileSync(exe,[],{stdio:'inherit',env:{...process.env,PG9_JS_PROBE_ORACLE:runtime,
 }});
const node=JSON.parse(fs.readFileSync(path.join(root,'node-baseline.json'))),bun=JSON.parse(fs.readFileSync(path.join(root,'bun-compiled.json')));
if(node.revision!==bun.revision||JSON.stringify(node.games)!==JSON.stringify(bun.games))throw Error('Compiled runtime game/log mismatch');
const result={revision:node.revision,bun:version,matching_games:node.games.length,
 node_actor_decisions_per_second:node.actor_decisions_per_second,bun_actor_decisions_per_second:bun.actor_decisions_per_second,
 ratio:bun.actor_decisions_per_second/node.actor_decisions_per_second,
 kind:'Executable embeds Bun/JavaScriptCore; dynamically loaded pinned Showdown files remain external. No native-JS AOT claim.',
 cpu_quota:'100%',scope:'Short single-worker shared-machine trials, full games including reset/bot/logging; no encoder/learner/worker scaling'};
fs.writeFileSync(path.join(root,'pack-comparison.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
