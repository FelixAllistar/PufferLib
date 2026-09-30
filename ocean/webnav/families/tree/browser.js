(()=>{
 const NativeDate=Date;window.__tr={clock:0};
 window.Date=class extends NativeDate{constructor(...a){super(...(a.length?a:[__tr.clock]))}static now(){return __tr.clock}};
 // The original plugin has no requested animation speed. Disable animation
 // scheduling explicitly so each comparison observes the settled DOM state.
 $.fx.off=true;
 __tr.export=()=>{
  const spans=[...document.querySelectorAll('#tree span.folder,#tree span.file')];
  const query=document.querySelector('#query').textContent;
  const goal=/"([^"]*)"/.exec(query)[1];__tr.nodes=spans;
  return {query,done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,
   nodes:spans.map((span,i)=>{
    const li=span.parentElement,ancestor=li.parentElement.closest('li'),ul=li.querySelector(':scope > ul');
    const parentSpan=ancestor&&ancestor.querySelector(':scope > span');
    return {name:span.textContent,folder:span.classList.contains('folder'),goal:span.textContent===goal,
     parent:parentSpan?spans.indexOf(parentSpan)+1:0,
     end:i+li.querySelectorAll('span.folder,span.file').length,
     expanded:!!ul&&getComputedStyle(ul).display!=='none',
     visible:!!span.getClientRects().length};
   })};
 };
 __tr.reset=seed=>{__tr.clock=0;Math.seedrandom(String(seed));core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();return __tr.export()};
 __tr.tick=ms=>{__tr.clock=ms;if(!WOB_DONE_GLOBAL&&ms>=10000)core.endEpisode(-1,false,'timed out');return true};
 __tr.point=ref=>{const e=__tr.nodes[ref-1];e.scrollIntoView({block:'nearest'});const r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}};
 return true;
})()
