// Validate and print the checked-in planning/coverage registry; no inferred credit.
const fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../../..');
const inventory=JSON.parse(fs.readFileSync(path.join(root,'ocean/webnav/miniwob_inventory.json')));
const registry=JSON.parse(fs.readFileSync(path.join(__dirname,'registry.json')));
const names=new Set(inventory.tasks.filter(t=>t.registered).map(t=>t.name));
if(process.argv.includes('--verify-source')) {
 const crypto=require('crypto');
 const reference=path.join(root,'build/webnav/reference/MiniWoB-plusplus-'+inventory.commit);
 if(inventory.tasks.length!==inventory.html_tasks)throw Error('HTML task count mismatch');
 for(const task of inventory.tasks) {
  const bytes=fs.readFileSync(path.join(reference,task.upstream_path));
  const hash=crypto.createHash('sha1').update('blob '+bytes.length+'\0')
   .update(bytes).digest('hex');
  if(hash!==task.git_blob_sha)throw Error('Pinned source mismatch: '+task.name);
 }
 console.error('PASS: '+inventory.html_tasks+' pinned HTML Git-blob fingerprints');
}
if(registry.tasks.length!==125||names.size!==125)throw Error('Registered denominator changed; audit explicitly');
const ids=new Set(),seen=new Set();
for(const t of registry.tasks){if(!names.has(t.name)||seen.has(t.name)||ids.has(t.id)||!registry.families[t.family])throw Error('Invalid task entry '+t.name);seen.add(t.name);ids.add(t.id);if(!['pending','legacy-bounded','checked'].includes(t.behavior))throw Error('Unrecognized status');}
if(process.argv.includes('--json'))console.log(JSON.stringify(registry,null,2));
else {
 console.log('125 registered task names; family assignment is planning, not implementation credit.');
 console.log('Legacy-bounded entries record 5c coverage, not completed 5.0 runtime conversion.');
 for(const [name,f]of Object.entries(registry.families)){const ts=registry.tasks.filter(t=>t.family===name);console.log(`${name.padEnd(16)} ${String(ts.length).padStart(2)} tasks; ${ts.filter(t=>t.behavior==='checked').length} family-checked; ${ts.filter(t=>t.behavior==='legacy-bounded').length} legacy bounded`);}
}
