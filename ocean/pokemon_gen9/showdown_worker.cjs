'use strict';
const fs=require('fs');
const {performance}=require('perf_hooks');
const {Game,OBS,ACTIONS,ABI}=require('./worker_core.cjs');
const profileFile=process.env.PG9_WORKER_TIMING;
const phases={};
function timed(name,fn){
  if(!profileFile)return fn();
  const start=performance.now(),cpu=process.cpuUsage();
  try{return fn();}finally{
    const used=process.cpuUsage(cpu),row=phases[name]||(phases[name]={calls:0,wall_ms:0,cpu_ms:0});
    row.calls++;row.wall_ms+=performance.now()-start;row.cpu_ms+=(used.user+used.system)/1000;
  }
}
const count=Number(process.argv[2]),seed=Number(process.argv[3]);
if(!Number.isInteger(count)||count<1||count>16||!Number.isInteger(seed))throw Error('Invalid worker configuration');
const MAGIC=0x39504750,games=Array.from({length:count},(_,i)=>new Game((seed+Math.imul(i,0x85ebca6b))>>>0));
const header=Buffer.alloc(16);header.writeUInt32LE(MAGIC,0);header.writeUInt32LE(ABI,4);header.writeUInt32LE(OBS,8);header.writeUInt32LE(ACTIONS,12);
function write(buffer){for(let n=0;n<buffer.length;)n+=fs.writeSync(1,buffer,n,buffer.length-n);}
function read(buffer){for(let n=0;n<buffer.length;){const got=fs.readSync(0,buffer,n,buffer.length-n,null);if(!got)return false;n+=got;}return true;}
write(header);
// Eight metadata floats per game, then each actor's float observations and
// 16 mask bytes (15 actions plus alignment padding). No JSON on the C wire.
const actorBytes=OBS*4+16,gameBytes=32+2*actorBytes;
const packet=Buffer.alloc(count*gameBytes),request=Buffer.alloc(4+count*8);
while(timed('read_actions',()=>read(request))){
  const op=request.readUInt32LE(0);if(op===3)break;if(op!==1&&op!==2)throw Error('Unknown worker operation');
  packet.fill(0);
  for(let i=0;i<count;i++){
    const g=games[i],at=i*gameBytes;let result={ended:false,win:0,turns:0,steps:0,decisions:0,retries:0};
    if(op===1)timed('reset',()=>g.reset());
    else result=timed('step_rules_and_public_client',()=>g.step([request.readInt32LE(4+i*8),request.readInt32LE(8+i*8)]));
    [result.win,+result.ended,result.turns,result.steps,result.decisions,result.retries].forEach((v,k)=>packet.writeFloatLE(v,at+4*k));
    if(result.ended)timed('reset',()=>g.reset());
    for(let s=0;s<2;s++){
      const offset=at+32+s*actorBytes;
      timed('encode_observation',()=>g.observe(s,new Float32Array(packet.buffer,packet.byteOffset+offset,OBS)));
      packet.set(g.masks[s],offset+OBS*4);
    }
  }
  timed('write_observations',()=>write(packet));
}
for(const g of games)g.close();
if(profileFile)fs.writeFileSync(profileFile,JSON.stringify({OBS,bytes_per_actor:actorBytes,phases},null,2)+'\n');
