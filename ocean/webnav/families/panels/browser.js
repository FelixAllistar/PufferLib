/* Conformance-only matched-instance export. Goal flags are sent only to the
 * simulator's private state; they never enter WFView or a learned policy. */
(()=>{
 const nativeDate=Date;window.__pan={clock:0};
 window.Date=class extends nativeDate{constructor(...args){super(...(args.length?args:[__pan.clock]))}static now(){return __pan.clock}};
 __pan.settle=()=>{$('#area').find(':animated').finish();};
 __pan.reset=(seed,task)=>{
  __pan.clock=0;__pan.task=task;Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();__pan.settle();
  __pan.accordion=task>=5;
  __pan.headers=[...document.querySelectorAll(__pan.accordion?'#area > h3':'#area > ul a')];
  __pan.panels=__pan.headers.map(h=>__pan.accordion?h.nextElementSibling:document.querySelector(h.getAttribute('href')));
  __pan.nodes=__pan.headers.map((e,i)=>({e,kind:0,panel:i+1}));
  for(const e of document.querySelectorAll('#area .alink')){
   const panel=__pan.panels.findIndex(p=>p.contains(e))+1;
   __pan.nodes.push({e,kind:1,panel});
  }
  const submit=document.querySelector('#subbtn');if(submit)__pan.nodes.push({e:submit,kind:2,panel:0});
  return __pan.snapshot(true);
 };
 __pan.snapshot=(initial=false)=>{
  __pan.settle();const query=document.querySelector('#query').textContent;
  const quoted=query.match(/"([^"]*)"/);const target=quoted?quoted[1]:'';
  const active=$('#area')[__pan.accordion?'accordion':'tabs']('option','active');
  const present=__pan.nodes.some(n=>n.kind===1&&n.e.innerHTML===target);
  const result={query,active:active===false?0:active+1,panels:__pan.accordion?$('#area').accordion('instance').headers.length:__pan.headers.length,accordion:__pan.accordion,
    target_tab:__pan.task===0?Number(query.match(/#(\d)/)[1]):0,medium:__pan.task===3?(present?2:1):0,
    deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
  if(initial)result.nodes=__pan.nodes.map(n=>({kind:n.kind,panel:n.panel,goal:n.kind===1&&n.e.innerHTML===target,name:n.e.textContent.trim()}));
  return result;
 };
 __pan.point=ref=>{const n=__pan.nodes[ref-1];if(!n)return null;const r=n.e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,visible:!!(r.width&&r.height)&&$(n.e).is(':visible')};};
 __pan.tick=ms=>{__pan.clock=ms;if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');return true;};
 return true;
})()
