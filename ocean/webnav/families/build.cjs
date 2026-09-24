// Incremental stock-CPU Bend family build. Usage: node .../build.cjs FAMILY [--test]
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const root=path.resolve(__dirname,'../../..');process.chdir(root);
// All supported family builds enter a serial, kernel-limited process scope.
// Never fall back to an unconfined compile if systemd/cgroups are unavailable.
if(process.env.WEBNAV_BUILD_CONFINED!=='1'){
 const result=cp.spawnSync('bash',[path.join(__dirname,'safe_build.sh'),...process.argv.slice(2)],{stdio:'inherit'});
 if(result.error)console.error(result.error.message);
 process.exit(result.status===null?1:result.status);
}
require('./resource_guard.cjs').verify();
const [family,...flags]=process.argv.slice(2);
if(!/^[a-z][a-z0-9_]*$/.test(family||'')||flags.some(f=>f!=='--test'))throw Error('Usage: build.cjs FAMILY [--test]');
const src=`ocean/webnav/families/${family}`,out=`build/webnav/families/${family}`;
fs.mkdirSync(out,{recursive:true});
const lock=path.join(out,'build.lock');
try {const fd=fs.openSync(lock,'wx');fs.writeFileSync(fd,String(process.pid));fs.closeSync(fd);}
catch(error){
 if(error.code!=='EEXIST')throw error;
 const pid=Number(fs.readFileSync(lock,'utf8'));let alive=true;
 try{process.kill(pid,0);}catch(e){if(e.code==='ESRCH')alive=false;else throw e;}
 if(alive)throw Error('Family build already running (PID '+pid+')');
 fs.unlinkSync(lock);const fd=fs.openSync(lock,'wx');fs.writeFileSync(fd,String(process.pid));fs.closeSync(fd);
}
process.on('exit',()=>{try{if(fs.readFileSync(lock,'utf8')===String(process.pid))fs.unlinkSync(lock);}catch{}});
const bend=process.env.BEND_BIN||path.join(__dirname,'bend_cpu.sh');
const cc=process.env.CC||'clang-19';
const env={...process.env,BEND_NO_TELEMETRY:'1'};
function run(command,args){console.log([command,...args].join(' '));const r=cp.spawnSync(command,args,{stdio:'inherit',env});if(r.error)throw r.error;if(r.status!==0)throw Error(`${command} failed (${r.status})`);}
function output(command,args){const r=cp.spawnSync(command,args,{encoding:'utf8',env});if(r.error||r.status!==0)throw r.error||Error(r.stderr);return r.stdout.trim();}
const version=output(bend,['--version']);if(version!=='bend 2.0.6')throw Error(`Expected stock bend 2.0.6; found ${version}`);
const compiler=output(cc,['--version']);
function closure(entry,seen=new Set()){
 const file=path.resolve(entry);if(seen.has(file))return seen;seen.add(file);
 const text=fs.readFileSync(file,'utf8');
 if(file.endsWith('.bend'))for(const m of text.matchAll(/^\s*import\s+(?:"([^"]+)"|([^\s]+))/gm)){
  const name=m[1]||m[2];if(name.startsWith('.'))closure(path.resolve(path.dirname(file),name),seen);
 }
 return seen;
}
function digest(files,extra=''){const h=crypto.createHash('sha256').update(extra);for(const f of [...new Set(files)].sort()){h.update(f);h.update(fs.readFileSync(f));}return h.digest('hex');}
const manifestFile=`${out}/build-cache.json`;let cache={};try{cache=JSON.parse(fs.readFileSync(manifestFile));}catch{}
function save(){const tmp=manifestFile+'.tmp';fs.writeFileSync(tmp,JSON.stringify(cache,null,2)+'\n');fs.renameSync(tmp,manifestFile);}
const proof=`${src}/PROOF.bend`,main=`${src}/Main.bend`,generated=`${out}/generated.c`;
const proofHash=digest(closure(proof),version+bend);
if(cache.proof!==proofHash){run(bend,[proof]);cache.proof=proofHash;save();}else console.log(`${family}: proofs unchanged (checked)`);
const runtimeHash=digest(closure(main),version+bend);
if(cache.runtime!==runtimeHash||!fs.existsSync(generated)||cache.generated!==digest([generated])){
 const tmp=`${out}/generated.tmp.c`;run(bend,[main,'-o',tmp]);fs.renameSync(tmp,generated);cache.runtime=runtimeHash;cache.generated=digest([generated]);save();
}else console.log(`${family}: generated CPU C unchanged`);
const common=['-O3','-std=c11','-fPIC','-fvisibility=hidden',`-I${src}`,`-I${out}`,'-Iocean/webnav/families/common','-Iocean/webnav','-Ivendor'];
function localFiles(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?localFiles(path.join(dir,e.name)):/\.(c|h)$/.test(e.name)?[path.join(dir,e.name)]:[]);}
function cIncludes(file,seen=new Set()) {
 const resolved=path.resolve(file);if(seen.has(resolved))return seen;seen.add(resolved);
 for(const m of fs.readFileSync(resolved,'utf8').matchAll(/^\s*#\s*include\s+"([^"]+)"/gm)) {
  const candidate=[path.dirname(resolved),src,out,'ocean/webnav/families/common','ocean/webnav','vendor'].map(d=>path.resolve(d,m[1])).find(f=>fs.existsSync(f));
  if(candidate)cIncludes(candidate,seen);
 }
 return seen;
}
const headers=[...new Set([...localFiles(src),...localFiles('ocean/webnav/families/common')].flatMap(f=>[...cIncludes(f)]))];
const objects=[];
for(const name of ['bridge','family_api']){
 const file=`${src}/${name}.c`,object=`${out}/${name}.o`;const args=[...common,`-DWF_GENERATED="${path.resolve(generated)}"`,'-c',file,'-o',object];
 const dependencies=[...cIncludes(file)];if(name==='bridge')dependencies.push(generated);
 const hash=digest(dependencies,compiler+JSON.stringify(args));
 if(cache[name]!==hash||!fs.existsSync(object)){run(cc,args);cache[name]=hash;save();}
 objects.push(object);
}
const library=`${out}/lib${family}.so`,linkArgs=['-shared','-Wl,--no-undefined',...objects,'-lpthread','-lm','-o',library];
const linkHash=digest(objects,compiler+JSON.stringify(linkArgs));
if(cache.link!==linkHash||!fs.existsSync(library)){run(cc,linkArgs);cache.link=linkHash;save();}
if(flags.includes('--test')){
 const tests=fs.readdirSync(src).filter(f=>/^test.*\.c$/.test(f));if(!tests.length)throw Error(`No native tests for ${family}`);
 for(const name of tests){const file=`${src}/${name}`,binary=`${out}/${name.slice(0,-2)}`;const args=[...common,file,...objects,'-lpthread','-lm','-o',binary];const hash=digest([...cIncludes(file),...objects],compiler+JSON.stringify(args));
  if(cache[name]!==hash||!fs.existsSync(binary)){run(cc,args);cache[name]=hash;save();}run('timeout',['-s','KILL','120s',binary]);}
}
console.log(`Built ${library}`);
