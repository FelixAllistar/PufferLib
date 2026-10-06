#!/usr/bin/env node
'use strict';
// Transpile the clean pinned oracle into ignored build output. This code is
// development tooling; it is never linked into a native environment.
const fs = require('fs'), path = require('path'), cp = require('child_process');
const ROOT = path.resolve(__dirname, '../..');
const SOURCE = path.join(ROOT, 'build/pokemon_gen9/showdown');
const OUT = path.join(ROOT, 'build/pokemon_gen9/oracle');
const REVISION = '9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e';
function dependency(name) {
  const roots = [path.join(ROOT,'build/pokemon_gen9/tools'),path.join(ROOT,'build/pokemon/validation'),ROOT];
  return require(require.resolve(name,{paths:roots}));
}
function git(args) { return cp.execFileSync('git',['-C',SOURCE,...args],{encoding:'utf8'}).trim(); }
if (git(['rev-parse','HEAD']) !== REVISION) throw Error('Unexpected Showdown revision');
if (git(['status','--porcelain','--untracked-files=no'])) throw Error('Modified reference source');
const files=[];
function collect(dir) {
  for (const entry of fs.readdirSync(dir,{withFileTypes:true})) {
    const file=path.join(dir,entry.name);
    if (entry.isDirectory()) collect(file);
    else if (/\.tsx?$/.test(entry.name) && !entry.name.endsWith('.d.ts')) files.push(file);
  }
}
for (const directory of ['sim','data','lib','config']) collect(path.join(SOURCE,directory));
dependency('esbuild').buildSync({entryPoints:files,outdir:path.join(OUT,'dist'),outbase:SOURCE,
  format:'cjs',platform:'node',target:'node22',tsconfig:path.join(SOURCE,'tsconfig.json'),logLevel:'warning'});
function copyJSON(dir) {
  for (const entry of fs.readdirSync(dir,{withFileTypes:true})) {
    const file=path.join(dir,entry.name);
    if (entry.isDirectory()) copyJSON(file);
    else if (entry.name.endsWith('.json')) {
      const dest=path.join(OUT,'dist',path.relative(SOURCE,file));
      fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(file,dest);
    }
  }
}
copyJSON(path.join(SOURCE,'data'));
// Resolve the oracle's runtime dependency from the same development install.
const chachaRoot=path.dirname(require.resolve('ts-chacha20/package.json',
  {paths:[path.join(ROOT,'build/pokemon_gen9/tools'),path.join(ROOT,'build/pokemon/validation')]}));
fs.mkdirSync(path.join(OUT,'node_modules'),{recursive:true});
const chachaLink=path.join(OUT,'node_modules/ts-chacha20');
const oldLink=fs.lstatSync(chachaLink,{throwIfNoEntry:false});
if (oldLink?.isSymbolicLink() && fs.readlinkSync(chachaLink) !== chachaRoot)
  fs.unlinkSync(chachaLink);
if (!fs.lstatSync(chachaLink,{throwIfNoEntry:false})) fs.symlinkSync(chachaRoot,chachaLink,'dir');
fs.writeFileSync(path.join(OUT,'revision.json'),JSON.stringify({revision:REVISION},null,2)+'\n');
console.log('Prepared pinned Showdown oracle: '+REVISION);
