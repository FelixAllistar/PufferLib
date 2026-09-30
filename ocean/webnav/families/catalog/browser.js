/* Runs inside the pinned original page. All interactions use its own DOM and
   handlers. Private problem data is exported only at reset for model import. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 const task=location.pathname.split('/').pop().replace(/\.html$/,'');
 window.__catalog={task,clock:0,problem:null};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__catalog.clock]))}
  static now(){return epoch+__catalog.clock}
 };
 if(task!=='order-food'){
  const original=window.bindClickEvents;
  window.bindClickEvents=function(problem,details){
   __catalog.problem=task==='phone-book'
    ?{contacts:problem.map(c=>({...c})),index:details.index,property:details.property}
    :{results:problem.results.map(r=>({...r})),expectedSearch:problem.expectedSearch,
      expectedIndex:problem.expectedIndex};
   return original.apply(this,arguments);
  };
 }
 __catalog.reset=seed=>{
  __catalog.clock=0;__catalog.problem=null;
  Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __catalog.snapshot(true);
 };
 __catalog.advance=ms=>{
  __catalog.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
  return true;
 };
 __catalog.page=()=>{
  const active=document.querySelector('#pagination li.active');
  return active?Number(active.textContent.trim()):0;
 };
 __catalog.snapshot=initial=>{
  const result={task,query:document.querySelector('#query').textContent,
   deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,
   raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
  if(task==='phone-book'){
   result.page=__catalog.page();
   result.name=document.querySelector('#contact .name')?.textContent||'';
   result.phone=document.querySelector('#contact a.phone')?.textContent||'';
   result.email=document.querySelector('#contact a.email')?.textContent||'';
   result.address=document.querySelector('#contact a.address')?.textContent||'';
  }else if(task==='order-food'){
   result.items=[...document.querySelectorAll('.food-item')].map(e=>({
    id:Number(e.dataset.id),name:e.dataset.item,
    qty:Number(e.dataset.quantity),
    types:[...e.querySelectorAll('.types img')].map(i=>i.alt),
    course:e.parentElement.id}));
  }else{
   const input=document.querySelector('#search-text');
   result.input=input.value;result.start=input.selectionStart;
   result.end=input.selectionEnd;result.page=__catalog.page();
   result.results=[...document.querySelectorAll('#page-content .search-title')]
    .map(e=>({index:Number(e.dataset.result),title:e.textContent}));
  }
  if(initial)result.problem=__catalog.problem;
  return result;
 };
 __catalog.element=ref=>{
  if(task==='phone-book'){
   if(ref>=1&&ref<=5){
    const page=__catalog.page();
    if(ref===page-1)return document.querySelector('#pagination li.prev a');
    if(ref===page+1)return document.querySelector('#pagination li.next a');
    if(ref===page)return document.querySelector('#pagination li.active a');
    return null;
   }
   return document.querySelector('#contact a.'+(['phone','email','address'][ref-100]||'missing'));
  }
  if(task==='order-food'){
   if(ref===1)return document.querySelector('#submit-order button');
   const id=Math.floor(ref/16)-1,slot=ref%16;
   const item=document.querySelector('.food-item[data-id="'+id+'"]');
   return item?.querySelector(slot===0?'.remove':slot===1?'.add':'.missing')||null;
  }
  if(ref===1)return document.querySelector('#search-text');
  if(ref===2)return document.querySelector('#search');
  if(ref>=10&&ref<=12)return [...document.querySelectorAll('#pagination li.page-item a')]
   .find(e=>e.textContent.trim()===String(ref-9))||null;
  if(ref>=100&&ref<=108)return document.querySelector('#page-content a[data-result="'+(ref-100)+'"]');
  if(ref>=200&&ref<=202)return document.querySelectorAll('#page-content a[data-result="-1"]')[ref-200]||null;
  return null;
 };
 __catalog.point=ref=>{
  const e=__catalog.element(ref);
  if(!e)return {visible:false,reason:'missing element',ref};
  e.scrollIntoView({block:'center',inline:'center'});
  const r=e.getBoundingClientRect(),s=getComputedStyle(e);
  // An inline address can wrap over several lines. Its union rectangle can
  // have an empty midpoint, or sit behind pagination. Use a real hittable
  // fragment of the original element without changing its DOM or geometry.
  let point=null;
  for(const rect of e.getClientRects()){
   if(!rect.width||!rect.height)continue;
   for(const fy of [.5,.2,.8])for(const fx of [.5,.2,.8]){
    const x=rect.x+rect.width*fx,y=rect.y+rect.height*fy;
    const hit=document.elementFromPoint(x,y);
    if(!point&&hit&&(hit===e||e.contains(hit)))point={x,y};
   }
  }
  return {x:point?.x,y:point?.y,width:r.width,height:r.height,
   display:s.display,visibility:s.visibility,tag:e.tagName,
   outer:e.outerHTML.slice(0,180),readyState:document.readyState,
   visible:!!point&&s.display!=='none'&&s.visibility!=='hidden'};
 };
 return true;
})()
