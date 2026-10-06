'use strict';
// Source control flow runs in resumable generators. Only the manifest's
// admitted operations run in C; the original object semantics remain in JS.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {root,pin}=require('./reference.cjs');
const factories=new WeakMap(),native=new WeakMap(),names=new WeakMap();
const bound=new WeakMap();
const arrayCallbacks=new Map(['map','filter','flatMap','some','every','find','findIndex','findLast','findLastIndex','forEach','reduce','reduceRight']
  .filter(name=>typeof Array.prototype[name]==='function').map(name=>[Array.prototype[name],name]));
let current=null;
const metrics={source_calls:0,scalar_calls:0,native_operations:0,batches:0,max_batch:0,
  synchronous_native_operations:0,expression_fallbacks:0,operations_by_kernel:{},calls_by_source:{}};
const directory=path.join(root,'build/pokemon_gen9/batch');
const manifest=JSON.parse(fs.readFileSync(path.join(directory,'manifest.json')));
assert.equal(manifest.revision,pin);
const addon=require(path.join(directory,'kernels.node'));
assert.equal(addon.sourceHash,manifest.source_ir_sha256,'Generated source/kernel hash mismatch');
let stringId=Object.keys(manifest.strings).length;
const strings=new Map(Object.entries(manifest.strings));
const buffers=new Map();
const byKey=new Map(manifest.kernels.map(k=>[k.key,k]));
function intern(s){if(!strings.has(s))strings.set(s,stringId++);return strings.get(s);}
function register(original,factory,name){
  if(typeof original!=='function'||typeof factory!=='function')throw Error('Invalid source continuation');
  factories.set(original,factory);names.set(original,name);return original;
}
function dual(original,factory,name,displayName){
  if(displayName!==undefined)Object.defineProperty(original,'name',{value:displayName,configurable:true});
  return register(original,factory,name);
}
function registerNative(fn,kernel){
  const list=native.get(fn)||[];list.push(kernel);native.set(fn,list);
}
function project(kernel,args){
  return kernel.slots.map(slot=>{
    if(slot.kind==='argument'){
      let v=args[slot.parameter];if(v===undefined&&slot.default!==null&&slot.default!==undefined)v=slot.default;
      if(slot.index!==undefined)v=v[slot.index];return Number(v);
    }
    const parts=slot.path.split('.');
    const parameter=kernel.params.findIndex(p=>p.name===parts[0]);
    let v=args[parameter];for(const name of parts.slice(1))v=v?.[name];
    if(slot.type==='string'){
      if(typeof v!=='string')throw Error('Invalid native string field '+kernel.key+':'+slot.path);
      return intern(v);
    }
    if(slot.type==='flag')return +!!v;
    return Number(v);
  });
}
function nativeOperation(fn,receiver,args){
  const variants=native.get(fn);if(!variants)return null;
  const kernel=variants.find(k=>k.owner||k.shapes.every((s,i)=>s===(Array.isArray(args[i])?'tuple':'number')));
  if(!kernel)throw Error('No admitted native helper signature');
  if(kernel.owner&&(receiver.gen!==9||receiver.debugMode))throw Error('Native callback requires Gen 9/debugMode=false');
  const event=receiver?.event;
  return {kernel,inputs:project(kernel,args),event,modifier:event?Number(event.modifier):1,
    reference:current?.verifyNative?{fn,receiver,args}:null};
}
function* invoke(fn,receiver,args){
  if(typeof fn!=='function')throw TypeError('Source call target is not a function');
  // Calling a registered callback through .call/.apply must still suspend.
  if(fn===Function.prototype.call)return yield* invoke(receiver,args[0],args.slice(1));
  if(fn===Function.prototype.apply){
    const list=args[1],values=[];
    if(list!=null){
      if(typeof list!=='object'&&typeof list!=='function')throw TypeError('CreateListFromArrayLike requires an object');
      const length=Math.min(Math.max(Math.trunc(Number(list.length))||0,0),Number.MAX_SAFE_INTEGER);
      for(let i=0;i<length;i++)values.push(list[i]);
    }
    return yield* invoke(receiver,args[0],values);
  }
  if(fn===Function.prototype.bind){
    const result=Reflect.apply(fn,receiver,args);bound.set(result,{fn:receiver,receiver:args[0],args:args.slice(1)});return result;
  }
  if(bound.has(fn)){
    const b=bound.get(fn);return yield* invoke(b.fn,b.receiver,[...b.args,...args]);
  }
  if(Array.isArray(receiver)&&arrayCallbacks.has(fn)&&
     (factories.has(args[0])||native.has(args[0])||bound.has(args[0])))
    return yield* arrayCall(arrayCallbacks.get(fn),receiver,args);
  const operation=nativeOperation(fn,receiver,args);
  if(operation)return yield operation;
  const factory=factories.get(fn);
  if(factory){
    metrics.source_calls++;
    if(current?.profile){const name=names.get(fn);metrics.calls_by_source[name]=(metrics.calls_by_source[name]||0)+1;}
    return yield* factory.apply(receiver,args);
  }
  metrics.scalar_calls++;
  return Reflect.apply(fn,receiver,args);
}
function* expression(key,receiver,args,helpers,reference){
  const kernel=byKey.get(key);
  if(!kernel||kernel.kind!=='expression')throw Error('Missing source expression kernel');
  const dependencies=[...kernel.expression.aliases.map(x=>x.helper),...kernel.expression.direct_helpers];
  const safe=receiver?.gen===9&&args.every((value,i)=>kernel.shapes[i]==='object'?
    value!==null&&typeof value==='object':typeof value==='number')&&helpers.length===dependencies.length&&
    helpers.every((fn,i)=>native.get(fn)?.some(k=>k.kind==='helper'&&k.name===dependencies[i]))&&
    native.get(receiver.trunc)?.some(k=>k.kind==='helper'&&k.name==='trunc');
  if(!safe){metrics.expression_fallbacks++;return reference();}
  const event=receiver.event;
  return yield {kernel,inputs:project(kernel,args),event,modifier:event?Number(event.modifier):1,
    reference:current?.verifyNative?{fn:reference,receiver:undefined,args:[]}:null};
}
function* arrayCall(name,array,args){
  const length=array.length,callback=args[0],thisArg=args[1];
  if(name==='reduce'||name==='reduceRight'){
    const increment=name==='reduce'?1:-1;let i=increment===1?0:length-1,accumulator;
    if(args.length>1)accumulator=args[1];
    else{
      while(i>=0&&i<length&&!(i in array))i+=increment;
      if(i<0||i>=length)throw TypeError('Reduce of empty array with no initial value');
      accumulator=array[i];i+=increment;
    }
    for(;i>=0&&i<length;i+=increment)if(i in array)
      accumulator=yield* invoke(callback,undefined,[accumulator,array[i],i,array]);
    return accumulator;
  }
  const result=name==='map'?new Array(length):[];
  const reverse=name==='findLast'||name==='findLastIndex';
  const find=name==='find'||name==='findIndex'||reverse;
  for(let j=0;j<length;j++){
    const i=reverse?length-1-j:j;if(!find&&!(i in array))continue;
    const value=array[i],selected=yield* invoke(callback,thisArg,[value,i,array]);
    if(name==='map')result[i]=selected;
    else if(name==='filter'){if(selected)result.push(value);}
    else if(name==='flatMap'){
      if(Array.isArray(selected)){for(let k=0;k<selected.length;k++)if(k in selected)result.push(selected[k]);}
      else result.push(selected);
    }else if(name==='some'&&selected)return true;
    else if(name==='every'&&!selected)return false;
    else if(find&&selected)return name==='find'||name==='findLast'?value:i;
  }
  if(name==='some')return false;if(name==='every')return true;
  if(name==='find'||name==='findLast'||name==='forEach')return undefined;
  if(find)return -1;return result;
}
function* call(receiver,key,args){
  return yield* invoke(receiver[key],receiver,args);
}
function* optional(receiver,key,args){
  if(receiver==null)return undefined;
  return yield* call(receiver,key,args);
}
function output(tag,value){
  if(tag===0)return undefined;if(tag===1)return value;if(tag===2)return !!value;if(tag===3)return null;
  throw Error('Invalid native return tag '+tag);
}
function execute(operations){
  const kernel=operations[0].kernel,n=operations.length,slots=kernel.slots.length;
  if(operations.some(o=>o.kernel.id!==kernel.id))throw Error('Mixed native operation group');
  let buffer=buffers.get(kernel.id);
  if(!buffer||buffer.capacity<n){
    const capacity=2**Math.ceil(Math.log2(Math.max(1,n)));
    buffer={capacity,inputs:new Float64Array(capacity*slots),modifiers:new Float64Array(capacity),
      tags:new Uint32Array(capacity),values:new Float64Array(capacity)};buffers.set(kernel.id,buffer);
  }
  const {inputs,modifiers,tags,values}=buffer;
  for(let i=0;i<n;i++){
    const o=operations[i];modifiers[i]=o.modifier;
    for(let s=0;s<slots;s++)inputs[s*n+i]=o.inputs[s];
  }
  addon.run(kernel.id,n,inputs,modifiers,tags,values);
  metrics.batches++;metrics.native_operations+=n;metrics.max_batch=Math.max(metrics.max_batch,n);
  metrics.operations_by_kernel[kernel.key]=(metrics.operations_by_kernel[kernel.key]||0)+n;
  return operations.map((o,i)=>{
    if(o.reference){
      const had=o.event&&Object.hasOwn(o.event,'modifier'),before=o.event?.modifier;
      const expected=Reflect.apply(o.reference.fn,o.reference.receiver,o.reference.args);
      const after=o.event?.modifier;
      if(o.event){if(had)o.event.modifier=before;else delete o.event.modifier;}
      const actual=output(tags[i]&255,values[i]);
      if(!Object.is(actual,expected))throw Error('Native/source return mismatch: '+kernel.key);
      if(o.event&&kernel.mutates_modifier&&!Object.is(modifiers[i],Number(after)))
        throw Error('Native/source frame modifier mismatch: '+kernel.key);
    }
    // A suspended event object belongs to exactly one lane. Native updates go
    // back to that original frame, including nonfinite and signed-zero values.
    if(tags[i]&256){
      if(!o.event||!kernel.mutates_modifier)throw Error('Unadmitted native event-frame mutation');
      o.event.modifier=modifiers[i];
    }
    return output(tags[i]&255,values[i]);
  });
}
function drain(generator){
  let r=generator.next();while(!r.done){
    metrics.synchronous_native_operations++;
    r=generator.next(execute([r.value])[0]);
  }return r.value;
}
function run(generators,{order='rotate',profile=false,verifyNative=false}={}){
  if(current)throw Error('Nested cross-battle scheduler');
  if(!['rotate','largest'].includes(order))throw Error('Unknown ready-operation scheduling order');
  const lanes=generators.map(generator=>({generator,pending:null,done:false,result:undefined}));
  current={profile,verifyNative};let cursor=0,remaining=lanes.length;
  function advance(lane,value){
    const r=lane.generator.next(value);
    if(r.done){lane.done=true;lane.result=r.value;lane.pending=null;remaining--;}
    else{
      if(!r.value?.kernel)throw Error('Unexpected source suspension');
      lane.pending=r.value;
    }
  }
  try{
    for(const lane of lanes)advance(lane);
    while(remaining){
      const groups=new Map();for(const lane of lanes)if(!lane.done){
        const op=lane.pending.kernel.id,list=groups.get(op)||[];list.push(lane);groups.set(op,list);
      }
      let ready;
      if(order==='largest')ready=[...groups.values()].sort((a,b)=>b.length-a.length)[0];
      else{
        for(let offset=0;offset<manifest.kernels.length;offset++){
          const op=(cursor+offset)%manifest.kernels.length;
          if(groups.has(op)){ready=groups.get(op);cursor=(op+1)%manifest.kernels.length;break;}
        }
      }
      if(!ready)throw Error('Source continuation scheduler deadlocked');
      const values=execute(ready.map(l=>l.pending));
      for(let i=0;i<ready.length;i++)advance(ready[i],values[i]);
    }
    return lanes.map(l=>l.result);
  }catch(error){
    // Close suspended source scopes. A failed batch is not a legal terminal.
    for(const lane of lanes)if(!lane.done)try{lane.generator.return();}catch{}
    throw error;
  }finally{current=null;}
}
function install(Battle,Dex){
  for(const kernel of manifest.kernels){
    if(kernel.kind==='expression')continue;
    const fn=kernel.owner?Dex.abilities.get(kernel.owner)[kernel.name]:
      kernel.name==='trunc'?Dex.trunc:Battle.prototype[kernel.name];
    assert.equal(typeof fn,'function',kernel.key);registerNative(fn,kernel);
  }
}
function snapshot(){return structuredClone(metrics);}
function resetMetrics(){
  for(const key of Object.keys(metrics))metrics[key]=typeof metrics[key]==='number'?0:{};
}
module.exports={register,dual,invoke,expression,call,optional,drain,run,install,manifest,snapshot,resetMetrics};
