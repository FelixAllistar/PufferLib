/* Pinned original geometry pages; source event handlers remain in control. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 const g=window.__geom={task:0,clock:0,elapsed:0};
 window.Date=class extends NativeDate{
   constructor(...args){super(...(args.length?args:[epoch+g.clock]))}
   static now(){return epoch+g.clock}
 };
 const end=core.endEpisode;
 core.endEpisode=function(reward,timeProportional,reason){
   g.elapsed=Date.now()-core.ept0;
   return end(reward,timeProportional,reason);
 };
 const circle=e=>({x:Number(e.getAttribute('cx')),
   y:Number(e.getAttribute('cy')),r:Number(e.getAttribute('r'))});
 const all=selector=>Array.from(document.querySelectorAll(selector),circle);
 g.snapshot=initial=>{
   const user=document.querySelector('#blue-circle');
   const s={query:document.querySelector('#query').textContent,
     deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,
     raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,
     elapsed:WOB_DONE_GLOBAL?g.elapsed:g.clock,
     user:user?circle(user):null};
   if(g.task===0){s.black=all('.init-black');s.vertex=all('.init-blue');}
   else if(g.task===1)s.black=all('.black-circle');
   else if(g.task===2)s.black=all('.black-circle');
   else if(g.task===3)s.grid=all('svg circle');
   else {s.black=all('#init-black');s.vertex=all('#init-blue');}
   return s;
 };
 g.reset=(task,seed)=>{
   g.task=task;g.clock=0;g.elapsed=0;
   Math.seedrandom(String(seed));core.startEpisodeReal();
   clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
   return g.snapshot(true);
 };
 g.advance=ms=>{
   if(ms<g.clock)throw Error('nonmonotone geometry clock');
   g.clock=ms;
   if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
     core.endEpisode(-1,false,'timed out');
   return g.snapshot(false);
 };
 g.point=index=>{
   const e=g.task===3?document.querySelectorAll('svg circle')[index]:
     document.querySelector('#subbtn');
   if(!e)throw Error('geometry target missing');
   const r=e.getBoundingClientRect();
   return {x:r.x+r.width/2,y:r.y+r.height/2};
 };
 return true;
})()
