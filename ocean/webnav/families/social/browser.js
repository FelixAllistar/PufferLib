/* Matched-instance fixture for the three pinned original MiniWoB pages. It
   observes the DOM and sends ordinary clicks; scoring stays in page JS. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 window.__social={clock:0,task:location.pathname.split('/').pop().replace(/\.html$/,'')};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__social.clock]))}
  static now(){return epoch+__social.clock}
 };
 __social.icons=[];
 __social.preload=async()=>{
  const names=['reply','reply-hover','retweet','retweet-hover',
   'like','like-hover','more','more-hover','share','share-hover'];
  await Promise.all(names.map(name=>new Promise((resolve,reject)=>{
   const image=new Image();__social.icons.push(image);
   image.onload=()=>image.naturalWidth&&image.naturalHeight
    ?resolve():reject(Error('empty icon '+name));
   image.onerror=()=>reject(Error('failed icon '+name));
   image.src=new URL('../common/special/social-media/'+name+'.png',location.href).href;
  })));
  return {ready:true,count:__social.icons.length};
 };
 __social.reset=seed=>{
  __social.clock=0;Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __social.snapshot(true);
 };
 __social.advance=ms=>{
  __social.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
  return true;
 };
 __social.snapshot=initial=>{
  const media=[...document.querySelectorAll('#area .media')];
  const posts=media.map(e=>{
   const active=['reply','retweet','like','share'].reduce((bits,key,i)=>
    bits+(e.querySelector('.controls > .'+key)?.classList.contains('active')?(1<<i):0),0);
   return {name:e.querySelector('.name').textContent,
    username:e.querySelector('.username').textContent,
    body:e.querySelector('.body').textContent,
    time:e.querySelector('.time')?.textContent||'',active};
  });
  const menu=media.findIndex(e=>e.querySelector('.controls ul:not(.hide)'))+1;
  const result={query:document.querySelector('#query').textContent,
   count:posts.length,menu,deadline:core.EPISODE_MAX_TIME,
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
   reward:WOB_REWARD_GLOBAL,posts};
  return result;
 };
 __social.element=ref=>{
  if(ref===1)return document.querySelector('#submitRow button');
  const post=Math.floor(ref/16)-1,slot=ref%16;
  const media=document.querySelectorAll('#area .media')[post];if(!media)return null;
  if(__social.task==='social-media'){
   const cls=['reply','retweet','like','more','share','copy','embed','menu-user','block-user','report'][slot];
   return cls?media.querySelector('.'+cls):null;
  }
  const cls=['reply','retweet','like','share'][slot];
  return cls?media.querySelector('.controls > .'+cls):null;
 };
 __social.point=ref=>{
  const e=__social.element(ref);
  if(!e)return {visible:false,reason:'missing element',ref};
  e.scrollIntoView({block:'center',inline:'center'});
  const r=e.getBoundingClientRect();const style=getComputedStyle(e);
  return {x:r.x+r.width/2,y:r.y+r.height/2,
   width:r.width,height:r.height,display:style.display,visibility:style.visibility,
   tag:e.tagName,classes:e.className,outer:e.outerHTML.slice(0,180),
   scrollTop:document.querySelector('#area').scrollTop,
   readyState:document.readyState,
   visible:!!(r.width&&r.height)&&style.display!=='none'};
 };
 return true;
})()
