let engine, checkpoint;
const FORMAT='puffer-cash-1';
function state(){const at=engine._web_state()>>>2,n=engine.HEAP32[at];return Array.from(engine.HEAP32.subarray(at,at+24+9*n));}
async function load(players,experimental){
  if(![2,6,9].includes(players))throw Error('Invalid table size.');
  const name=experimental&&players===2?'poker2pro':`poker${players}`;
  const manifest=await (await fetch('./build-manifest.json',{cache:'no-store'})).json();
  const hash=manifest.artifacts[`${name}.wasm`];
  const {default:create}=await import(`./${name}.mjs?v=${hash}`);
  engine=await create({locateFile:file=>new URL(`${file}?v=${hash}`,self.location.href).href});
  return hash;
}
function open(seed,hand,stacks){
  if(!Number.isInteger(seed)||seed<0||seed>0xffffffff||!Number.isInteger(hand)||hand<1||hand>10000000||!Array.isArray(stacks)||stacks.length!==checkpoint.players||stacks.some(v=>!Number.isInteger(v)||v<1||v>10000000))throw Error('Invalid saved table.');
  stacks.forEach((s,p)=>{if(!engine._web_stack(p,s))throw Error('Invalid stack.');});
  if(!engine._web_open(seed,hand))throw Error('Could not open the hand.');
  checkpoint={...checkpoint,seed,hand,stacks:stacks.slice(),commands:[]};
}
function apply(command){
  let action=command.action;
  if(command.kind==='bot'){
    if(![1,4,7].includes(command.policy))throw Error('Invalid saved opponent.');
    const chosen=engine._web_bot(command.policy);
    if(action!==undefined&&action!==chosen)throw Error('Saved strategy no longer matches this engine.');
    action=chosen;
  } else if(command.kind!=='act')throw Error('Invalid saved action.');
  if(!Number.isInteger(action)||action< -1||action>10000000||!engine._web_act(action))throw Error('Invalid action in saved hand.');
  checkpoint.commands.push({...command,action});return action;
}
self.onmessage=async({data})=>{
  const {id,command}=data;
  try{
    let chosen;
    if(command==='init'){
      const hash=await load(data.players,data.experimental);
      checkpoint={format:FORMAT,hash,players:data.players,experimental:!!data.experimental};
      open(data.seed,1,Array(data.players).fill(20000));
    }else if(command==='restore'){
      const saved=data.checkpoint;
      if(saved?.format!==FORMAT||!Array.isArray(saved.commands)||saved.commands.length>5000)throw Error('Saved table format is not supported.');
      const hash=await load(saved.players,saved.experimental);
      if(hash!==saved.hash)throw Error('This saved hand uses an older engine. Open a new table to continue.');
      checkpoint={format:FORMAT,hash,players:saved.players,experimental:!!saved.experimental};
      open(saved.seed,saved.hand,saved.stacks);
      for(const action of saved.commands)apply(action);
    }else if(command==='next'){
      if(state()[1]!==255)throw Error('Finish this hand first.');
      const seed=crypto.getRandomValues(new Uint32Array(1))[0];
      open(seed,checkpoint.hand+1,data.stacks);
    }else if(command==='act')chosen=apply({kind:'act',action:data.action});
    else if(command==='bot')chosen=apply({kind:'bot',policy:data.policy});
    else if(command==='learn'){
      const at=engine._web_learning()>>>2;
      self.postMessage({id,learning:Array.from(engine.HEAP32.subarray(at,at+18))});return;
    }else throw Error('Unknown table command.');
    self.postMessage({id,state:state(),action:chosen,checkpoint});
  }catch(error){self.postMessage({id,error:error.message||String(error)});}
};
