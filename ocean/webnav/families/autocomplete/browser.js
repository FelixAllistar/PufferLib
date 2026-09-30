(()=>{
 const NativeDate=Date;window.__ac={clock:0};
 const nativeTimeout=window.setTimeout,nativeClear=window.clearTimeout;
 const scheduled=new Map();let timerId=1000000000,capture=false;
 window.setTimeout=function(callback,delay,...args){
  if(!capture)return nativeTimeout(callback,delay,...args);
  const id=++timerId;scheduled.set(id,{due:__ac.clock+Number(delay||0),callback,args});return id;
 };
 window.clearTimeout=function(id){if(scheduled.delete(id))return;return nativeClear(id)};
 window.Date=class extends NativeDate{constructor(...a){super(...(a.length?a:[__ac.clock]))}static now(){return __ac.clock}};
 __ac.snapshot=()=>{
  const input=document.querySelector('#tags'),instance=$('#tags').autocomplete('instance');
  const menu=instance.menu.element,open=menu.is(':visible');
  const items=open?[...menu[0].querySelectorAll('.ui-menu-item')]:[];
  const active=instance.menu.active;
  return {query:document.querySelector('#query').textContent,value:input.value,
   start:input.selectionStart,end:input.selectionEnd,
   focus:document.activeElement===input?1:document.activeElement===document.querySelector('#subbtn')?2:0,
   menu:open,items:items.map(e=>e.textContent),active:active&&active.length?items.indexOf(active[0])+1:0,
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
 };
 __ac.reset=seed=>{
  if(document.activeElement&&document.activeElement.blur)document.activeElement.blur();
  scheduled.clear();
  __ac.clock=0;Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  const instance=$('#tags').autocomplete('instance');
  if(instance.options.delay===300&&!instance.__clockInstalled){
   const original=instance._searchTimeout;
   instance._searchTimeout=function(event){
    capture=true;try{return original.call(this,event)}finally{capture=false}
   };
   instance.__clockInstalled=true;
  }
  return __ac.snapshot();
 };
 __ac.tick=ms=>{
  if(!WOB_DONE_GLOBAL)for(;;){
   let next=null;
   for(const [id,timer]of scheduled)if(timer.due<=ms&&timer.due<10000&&
      (!next||timer.due<next[1].due))next=[id,timer];
   if(!next)break;
   scheduled.delete(next[0]);__ac.clock=next[1].due;
   next[1].callback(...next[1].args);
  }
  __ac.clock=ms;if(!WOB_DONE_GLOBAL&&ms>=10000)core.endEpisode(-1,false,'timed out');return true;
 };
 __ac.point=ref=>{
  const e=ref===1?document.querySelector('#tags'):ref===2?document.querySelector('#subbtn'):$('#tags').autocomplete('widget')[0].querySelectorAll('.ui-menu-item')[ref-3];
  if(!e)return null;e.scrollIntoView({block:'nearest'});const r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};
 };
 __ac.settled=()=>new Promise(resolve=>setTimeout(()=>resolve(__ac.snapshot()),10));
 return true;
})()
