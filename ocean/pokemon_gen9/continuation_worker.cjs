'use strict';
// One ordinary Node worker owns a batch; its C kernels execute in-process.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {Game,stepBatch,runtime,OBS,ACTIONS,ABI,schema}=require('./compact_core.cjs');
const {performance}=require('perf_hooks');
const count=Number(process.argv[2]),seed=Number(process.argv[3]);
if(!Number.isInteger(count)||count<1||count>512||!Number.isInteger(seed))throw Error('Invalid batched worker configuration');
const games=Array.from({length:count},(_,i)=>new Game((seed+Math.imul(i,0x85ebca6b))>>>0));
const header=Buffer.alloc(16);[0x39504750,ABI,OBS,ACTIONS].forEach((v,i)=>header.writeUInt32LE(v,4*i));
function write(buffer){for(let n=0;n<buffer.length;)n+=fs.writeSync(1,buffer,n,buffer.length-n);}
function read(buffer){for(let n=0;n<buffer.length;){const got=fs.readSync(0,buffer,n,buffer.length-n,null);if(!got)return false;n+=got;}return true;}
write(header);
write(Buffer.from(schema.schema_sha256));
const actorBytes=OBS*4+16,gameBytes=32+2*actorBytes;
const packet=Buffer.alloc(count*gameBytes),request=Buffer.alloc(4+count*8);
const observations=games.map((_,i)=>Array.from({length:2},(_,s)=>new Float32Array(packet.buffer,packet.byteOffset+i*gameBytes+32+s*actorBytes,OBS)));
const phases={};
function timed(name,fn){
  if(!process.env.PG9_WORKER_TIMING)return fn();const start=performance.now();
  try{return fn();}finally{const p=phases[name]||(phases[name]={calls:0,wall_ms:0});p.calls++;p.wall_ms+=performance.now()-start;}
}
runtime.resetMetrics();
while(timed('read_actions',()=>read(request))){
  const op=request.readUInt32LE(0);if(op===3)break;if(op!==1&&op!==2)throw Error('Unknown worker operation');
  packet.fill(0);let results;
  if(op===1){timed('reset',()=>games.forEach(g=>g.reset()));results=games.map(()=>({ended:false,win:0,turns:0,steps:0,decisions:0,retries:0}));}
  else{
    const actions=games.map((_,i)=>[request.readInt32LE(4+i*8),request.readInt32LE(8+i*8)]);
    results=timed('batched_rules_and_public_client',()=>stepBatch(games,actions));
  }
  for(let i=0;i<count;i++){
    const g=games[i],r=results[i],at=i*gameBytes;
    [r.win,+r.ended,r.turns,r.steps,r.decisions,r.retries].forEach((v,k)=>packet.writeFloatLE(v,at+4*k));
    if(r.ended)timed('reset',()=>g.reset());
    for(let s=0;s<2;s++){
      timed('encode_observation',()=>g.observe(s,observations[i][s]));
      packet.set(g.masks[s],at+32+s*actorBytes+OBS*4);
    }
  }
  timed('write_observations',()=>write(packet));
}
for(const game of games)game.close();
if(process.env.PG9_WORKER_TIMING)fs.writeFileSync(process.env.PG9_WORKER_TIMING,JSON.stringify({OBS,ABI,
  schema_sha256:schema.schema_sha256,bytes_per_actor:actorBytes,phases,runtime:runtime.snapshot()},null,2)+'\n');
