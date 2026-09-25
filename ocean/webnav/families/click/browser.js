(()=>{
 const D=Date;window.__cl={clock:0};window.Date=class extends D{constructor(...args){super(...(args.length?args:[__cl.clock]))}static now(){return __cl.clock}};
 if(typeof createCheckboxes==='function'){
  const original=createCheckboxes;window.createCheckboxes=function(...args){const data=original(...args);__cl.goals=data.toclick;return data;};
 }
 __cl.reset=(seed,task,mode)=>{
  __cl.clock=0;__cl.task=task;WOB_DATA_MODE=mode?'test':'train';Math.seedrandom(String(seed));core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  let elems;if(task===3||task===4)elems=[...document.querySelectorAll('.ui-dialog button')];
  else if(task===5)elems=[...document.querySelectorAll('#area .widget input,#area .widget textarea,#area .widget button')];
  else if(task===6)elems=[...document.querySelectorAll('#area input')];
  else if(task>=7)elems=[...document.querySelectorAll('input[type=checkbox]'),document.querySelector('#subbtn')];
  else elems=[...document.querySelectorAll('#area button')];
  __cl.nodes=elems;
  const query=document.querySelector('#query').textContent;
  const quoted=query.match(/"([^"]*)"/);const requested=quoted?quoted[1]:'';
  const names=elems.map(e=>e.type==='checkbox'||e.type==='radio'?e.parentNode.textContent.trim():e.tagName==='INPUT'?e.value:e.textContent.trim());
  const nodes=elems.map((e,i)=>{
   const role=e.tagName==='BUTTON'?1:e.type==='checkbox'?2:e.type==='radio'?5:e.tagName==='TEXTAREA'?6:3;
   let goal=false;
   if(task===0||task===3)goal=true;
   else if(task===1)goal=e.textContent==='ONE';
   else if(task===2)goal=e.textContent===(mode?'TWO':'ONE');
   else if(task===4)goal=e.classList.contains('ui-dialog-titlebar-close')?requested==='x':e.textContent.trim()===requested;
   else if(task===5)goal=e.getAttribute('data-type')===requested;
   else if(task===6)goal=i===['1st','2nd','3rd'].findIndex(ordinal=>query.includes(ordinal));
   else if(role===2)goal=!!__cl.goals[i];
   const rect=e.getBoundingClientRect();return {role,name:names[i],goal,checked:!!e.checked,x:Math.round(rect.x),y:Math.round(rect.y),width:Math.round(rect.width),height:Math.round(rect.height)};
  });
  return {query,nodes,deadline:core.EPISODE_MAX_TIME};
 };
 __cl.point=ref=>{
  const e=__cl.nodes[ref-1];if(!e)return null;const r=e.getBoundingClientRect();
  for(const fy of [0.5,0.25,0.75,0.05,0.95])for(const fx of [0.5,0.25,0.75,0.05,0.95]){
   const x=r.x+r.width*fx,y=r.y+r.height*fy,hit=document.elementFromPoint(x,y);
   if(hit&&(hit===e||e.contains(hit)))return {x,y};
  }return null;
 };
 __cl.tick=ms=>{__cl.clock=ms;if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');return true;};
 __cl.snapshot=()=>({done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,checked:__cl.nodes.map(e=>!!e.checked)});
 return true;
})()
