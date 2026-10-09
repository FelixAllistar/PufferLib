const assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const cap=(kind,ref)=>({kind,ref,wire_target:ref,flags:0,text_capacity:0,
  min0:0,max0:0,step0:1,unit0:0,min1:0,max1:0,step1:1,unit1:0});
const node=(ref,name)=>({ref,parent:0,role:1,flags:7,name,value:'',
  x:10,y:20,width:70,height:30,selection_start:0,selection_end:0,capacity:0,
  scroll_x:0,scroll_y:0,scroll_max_x:0,scroll_max_y:0});
const view={version:2,instruction:'Open the customer list',elapsed_ms:0,deadline_ms:25600,
  omitted:0,text_truncated:0,incomplete:0,nodes:[node(41,'Customers'),node(99,'Products')],
  capabilities:[cap(0,0),cap(1,41),cap(1,99)]};
function run(policy,requests) {
  const p=spawnSync('build/webnav/benchmarks/policy_rpc',[policy],{
    input:requests.map(x=>typeof x==='string'?x:JSON.stringify(x)).join('\n')+'\n',
    encoding:'utf8',timeout:30000,maxBuffer:2*1024*1024});
  assert.equal(p.status,0,p.stderr||String(p.error));
  const rows=p.stdout.trim().split('\n').map(JSON.parse);
  assert.equal(rows.length,requests.length);return rows;
}
for(const policy of ['random','checkpoints/webnav_unified/1790766678997/0000000000031616.bin']) {
  const reset={reset:true,seed:9123};
  const baseline=run(policy,[reset,view,view]);
  const bad=structuredClone(view);bad.nodes[0].parent=41;
  const badTarget=structuredClone(view);badTarget.capabilities[1].wire_target=999;
  const malformed=['{"reset":true,"seed":01}', '{"reset":true,"seed":1e9999}',
    '{"reset":true,"seed":1,"seed":2}', '{"instruction":"\\u0000"}',
    {...view,private_goal:41},bad,badTarget];
  const tested=run(policy,[reset,view,...malformed,view,reset,view]);
  assert.deepEqual(tested[1],baseline[1]);
  for(let i=2;i<2+malformed.length;i++)assert.equal(typeof tested[i].error,'string');
  assert.deepEqual(tested.at(-3),baseline[2],'malformed requests changed policy state');
  assert.deepEqual(tested.at(-1),baseline[1],'reset did not restore deterministic inference');
  for(const a of [baseline[1],baseline[2]]) {
    assert.equal(a.local,false);assert.ok([0,1].includes(a.kind));
    assert.ok([0,41,99].includes(a.target));
  }
}
console.log('PASS: random and checkpoint batch-one RPC, legal public targets, deterministic reset, malformed-input state isolation');
