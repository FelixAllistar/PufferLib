/* Runs against the pinned original pages. Initial instance data is exported
   once; no post-action browser value is copied back into the Bend row. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 window.__ti={clock:0,elapsed:0,task:location.pathname.split('/').pop().replace(/\.html$/,'')};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__ti.clock]))}
  static now(){return epoch+__ti.clock}
 };
 const end=core.endEpisode;
 core.endEpisode=function(reward,scaled,reason){
  __ti.elapsed=Date.now()-core.ept0;
  return end(reward,scaled,reason);
 };
 const scalars=s=>Array.from(s).map(ch=>ch.codePointAt(0));
 const slots=()=>{
  const out=[];let span='';
  for(const e of document.querySelector('#area').children){
   if(e.tagName==='SPAN'){span=e.textContent;continue;}
   if(e.tagName==='BR')continue;
   if(e.tagName==='DIV')out.push({kind:0,text:e.textContent,units:scalars(e.textContent)});
   else if(e.tagName==='INPUT'){
    out.push({kind:1,text:span,units:scalars(span),value:e.value});span='';
   }else if(e.tagName==='BUTTON')
    out.push({kind:2,text:e.innerHTML,units:scalars(e.innerHTML)});
  }
  if(out.length!==6)throw Error('expected six unicode slots, got '+out.length);
  return out;
 };
 __ti.reset=seed=>{
  __ti.clock=0;__ti.elapsed=0;
  Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __ti.export();
 };
 __ti.advance=ms=>{
  __ti.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
 };
 __ti.export=()=>{
  const query=document.querySelector('#query').textContent;
  return {query,queryUnits:scalars(query),deadline:core.EPISODE_MAX_TIME,
   value:document.querySelector('#tt')?.value||'',
   slots:__ti.task==='unicode-test'?slots():[],
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
   reward:WOB_REWARD_GLOBAL,
   elapsed:WOB_DONE_GLOBAL?__ti.elapsed:__ti.clock};
 };
 __ti.act=(kind,ref,value)=>{
  if(kind===2){
   const input=document.querySelector('#tt');if(!input||ref!==1)throw Error('input');
   input.value=value;
  }else if(kind===1){
   if(ref===2)document.querySelector('#subbtn').click();
   else{
   const children=slots();const slot=children[ref-3];
    if(!slot||slot.kind!==2)throw Error('button slot '+ref);
    const buttonIndex=children.slice(0,ref-3).filter(x=>x.kind===2).length;
    document.querySelectorAll('#area button')[buttonIndex].click();
   }
  }
  return __ti.export();
 };
 return true;
})()
