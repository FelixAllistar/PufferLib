/* Controlled-clock fixture for the pinned original stock-market page.
 * Generated prices are captured only at reset. The page's own drawPrices and
 * Buy handler perform every compared transition. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 const mk=window.__mk={clock:0,elapsed:0,ticks:0,prices:null,ticker:null};
 window.Date=class extends NativeDate{
   constructor(...args){super(...(args.length?args:[epoch+mk.clock]))}
   static now(){return epoch+mk.clock}
 };
 const nativeSet=window.setInterval,nativeClear=window.clearInterval;
 const fakeTimers=new Set();let nextTimer=1000000;
 window.setInterval=function(callback,delay,...args){
   if(delay===100){
     const id=++nextTimer;fakeTimers.add(id);
     mk.ticker=()=>callback(...args);return id;
   }
   return nativeSet.call(window,callback,delay,...args);
 };
 window.clearInterval=function(id){
   if(fakeTimers.has(id)){fakeTimers.delete(id);mk.ticker=null;return}
   return nativeClear.call(window,id);
 };
 const generate=window.generatePrices;
 window.generatePrices=function(){
   const prices=generate.apply(this,arguments);
   mk.prices=prices.slice();return prices;
 };
 const end=core.endEpisode;
 core.endEpisode=function(reward,timeProportional,reason){
   mk.elapsed=Date.now()-core.ept0;
   return end(reward,timeProportional,reason);
 };
 mk.snapshot=initial=>{
   const s={query:document.querySelector('#query').textContent,
     symbol:document.querySelector('#stock-symbol').textContent,
     price:document.querySelector('#stock-price').textContent,
     tick:priceIndex,deadline:core.EPISODE_MAX_TIME,
     done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
     reward:WOB_REWARD_GLOBAL,
     elapsed:WOB_DONE_GLOBAL?mk.elapsed:mk.clock};
   if(initial)s.prices=mk.prices.slice();
   return s;
 };
 mk.reset=seed=>{
   mk.clock=0;mk.elapsed=0;mk.ticks=0;mk.prices=null;
   /* genProblem leaves this text from the prior episode; this fixture uses
    * fresh-page initial observations for each matched instance. */
   document.querySelector('#stock-price').textContent='';
   Math.seedrandom(String(seed));core.startEpisodeReal();
   clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
   if(!mk.prices||mk.prices.length!==100||!mk.ticker)throw Error('reset price stream');
   return mk.snapshot(true);
 };
 mk.advance=ms=>{
   if(ms<mk.clock)throw Error('nonmonotone clock');
   const through=Math.min(Math.floor(ms/100),99);
   for(let tick=mk.ticks+1;tick<=through;tick++){
     mk.clock=tick*100;mk.ticker();mk.ticks=tick;
   }
   mk.clock=ms;
   if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
     core.endEpisode(-1,false,'timed out');
   return mk.snapshot(false);
 };
 mk.point=()=>{
   const e=document.querySelector('#buy'),r=e.getBoundingClientRect();
   return {x:r.x+r.width/2,y:r.y+r.height/2,
     visible:!!(r.width&&r.height)&&!!e.getClientRects().length};
 };
 return true;
})()
