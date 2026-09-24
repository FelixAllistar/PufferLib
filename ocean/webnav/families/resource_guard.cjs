// Fail closed: a marker alone does not prove kernel-enforced confinement.
const fs=require('fs'),path=require('path');
function verify() {
 const entry=fs.readFileSync('/proc/self/cgroup','utf8').split('\n').find(s=>s.startsWith('0::'));
 if(!entry)throw Error('WebNav builds require cgroup v2 memory limits');
 let dir=path.join('/sys/fs/cgroup',entry.slice(3));
 while(dir!=='/sys/fs/cgroup') {
  const max=fs.readFileSync(path.join(dir,'memory.max'),'utf8').trim();
  const swap=fs.readFileSync(path.join(dir,'memory.swap.max'),'utf8').trim();
  if(max!=='max'&&Number(max)>0&&Number(max)<=6442450944&&swap==='0')return;
  dir=path.dirname(dir);
 }
 throw Error('Refusing unconfined WebNav build: require MemoryMax <= 6 GiB and MemorySwapMax=0');
}
module.exports={verify};
if(require.main===module){verify();console.log('PASS: kernel memory cap <= 6 GiB; swap disabled');}
