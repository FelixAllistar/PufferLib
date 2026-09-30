(()=>{
 const NativeDate=Date;window.__dw={clock:0};
 window.Date=class extends NativeDate{constructor(...a){super(...(a.length?a:[__dw.clock]))}static now(){return __dw.clock}};
 __dw.snapshot=()=>{
  const svg=document.querySelector('svg'),dot=svg.querySelector('#circ'),path=svg.querySelector('path');
  const box=path?path.getBoundingClientRect():null;
  return {query:document.querySelector('#query').textContent,cx:Number(dot.getAttribute('cx')),cy:Number(dot.getAttribute('cy')),
    points:path?arrayPoints:[],active:typeof d3.select(window).on('mousemove.drag')==='function',
    width:box?box.width:0,height:box?box.height:0,
    elapsed:__dw.clock,done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
 };
 __dw.reset=seed=>{
  __dw.clock=0;Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();return __dw.snapshot();
 };
 __dw.tick=ms=>{__dw.clock=ms;if(!WOB_DONE_GLOBAL&&ms>=10000)core.endEpisode(-1,false,'timed out');return true};
 __dw.point=(x,y)=>{const svg=document.querySelector('svg'),point=svg.createSVGPoint();point.x=x;point.y=y;const p=point.matrixTransform(svg.getScreenCTM());return {x:p.x,y:p.y}};
 __dw.submit=()=>{const b=document.querySelector('#controls button');b.scrollIntoView({block:'nearest'});const r=b.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}};
 return true;
})()
