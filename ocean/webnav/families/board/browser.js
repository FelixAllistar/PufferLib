/* Original pinned page handlers and opponent policy run untouched. The random
   source is a deterministic 0..9999 stream mirrored by Bend's generator/model. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 window.__board={clock:0,elapsed:0,rng:0};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__board.clock]))}
  static now(){return epoch+__board.clock}
 };
 const end=core.endEpisode;
 core.endEpisode=function(reward,scaled,reason){
  __board.elapsed=Date.now()-core.ept0;
  return end(reward,scaled,reason);
 };
 __board.reset=seed=>{
  __board.clock=0;__board.elapsed=0;__board.rng=seed%10000;
  Math.random=()=>{
   __board.rng=(__board.rng*73+19)%10000;
   return __board.rng/10000;
  };
  core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __board.export();
 };
 __board.advance=ms=>{
  __board.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
 };
 __board.export=()=>({
  query:document.querySelector('#query').textContent,
  board:[...document.querySelectorAll('.ttt-row span')].map(e=>
   e.classList.contains('mark-x')?1:e.classList.contains('mark-o')?-1:0),
  rng:__board.rng,deadline:core.EPISODE_MAX_TIME,
  done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
  reward:WOB_REWARD_GLOBAL,
  elapsed:WOB_DONE_GLOBAL?__board.elapsed:__board.clock
 });
 __board.act=(kind,ref)=>{
  if(kind===1){
   const e=document.querySelector('#ttt-'+(ref-1));
   if(!e)throw Error('missing cell '+ref);
   e.click();
  }
  return __board.export();
 };
 return true;
})()
