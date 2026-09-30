/* Public WFView projection of the pinned MiniWoB panel and menu pages.
 * Original pages generate every episode. Private target fields are never
 * stored here or returned through CDP. */
(()=>{
 const NativeDate=Date;
 window.__nav={clock:0,family:0,task:0,nodes:[],byElement:new Map(),hover:0};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[__nav.clock]));}
  static now(){return __nav.clock;}
 };
 const icons={disk:1,"seek-start":2,stop:3,play:4,"seek-end":5,zoomin:6,zoomout:7,print:8};
 const iconName=id=>Object.keys(icons).find(key=>icons[key]===id)||"";
 const visible=e=>!!e&&$(e).is(':visible');
 __nav.settle=()=>{
  if(__nav.family===0)$('#area').find(':animated').finish();
  else{$('#menu').find(':animated').finish();$('#area').find(':animated').finish();}
 };
 __nav.reset=(family,seed,task)=>{
  __nav.clock=0;__nav.family=family;__nav.task=task;__nav.hover=0;
  Math.seedrandom(String(seed));core.startEpisodeReal();
  if(core.EP_TIMER!==null)clearTimeout(core.EP_TIMER);
  core.EP_TIMER=-1;core.clearTimer();__nav.settle();
  __nav.nodes=[];__nav.byElement=new Map();
  if(family===0){
   __nav.accordion=task>=5;
   const headers=[...document.querySelectorAll(__nav.accordion?'#area > h3':'#area > ul a')];
   __nav.panels=headers.map(h=>__nav.accordion?h.nextElementSibling:document.querySelector(h.getAttribute('href')));
   for(let i=0;i<headers.length;i++)__nav.nodes.push({e:headers[i],kind:0,parent:0,panel:i+1});
   for(const e of document.querySelectorAll('#area .alink')){
    const panel=__nav.panels.findIndex(p=>p&&p.contains(e))+1;
    __nav.nodes.push({e,kind:1,parent:panel,panel});
   }
   const submit=document.querySelector('#subbtn');
   if(submit)__nav.nodes.push({e:submit,kind:2,parent:0,panel:0});
   __nav.instruction=document.querySelector('#query').textContent;
  }else{
   const menu=document.querySelector('#menu');
   if(task===1){
    const button=document.querySelector('#open-menu');
    __nav.nodes.push({e:button,kind:1,parent:0,children:false,enabled:true,icon:0,name:button.textContent.trim()});
    __nav.byElement.set(button,1);
   }
   for(const e of menu.querySelectorAll('li')){
    const own=e.querySelector(':scope > div');
    const parentLi=e.parentElement.closest('li');
    const parent=parentLi?__nav.byElement.get(parentLi)||0:0;
    const icon=own&&own.querySelector('.ui-icon');
    const match=icon&&icon.className.match(/\bui-icon-([a-z-]+)\b/);
    const node={e,kind:0,parent,children:!!e.querySelector(':scope > ul'),
     enabled:!(e.classList.contains('ui-state-disabled')||e.getAttribute('aria-disabled')==='true'),
     icon:match?icons[match[1]]||0:0,name:own?own.textContent.trim():e.textContent.trim()};
    __nav.nodes.push(node);__nav.byElement.set(e,__nav.nodes.length);
   }
   const query=document.querySelector('#query');
   const raw=query.innerText.trim();
   const icon=query.querySelector('.ui-icon');
   const match=icon&&icon.className.match(/\bui-icon-([a-z-]+)\b/);
   const expectedIcon=match?icons[match[1]]||0:0;
   __nav.instruction=expectedIcon?raw.replace(/\bthe\s+icon\b/,`the ${iconName(expectedIcon)} icon`):raw;
  }
  return __nav.snapshot();
 };
 __nav.snapshot=()=>{
  __nav.settle();
  const result={instruction:__nav.instruction,deadline:core.EPISODE_MAX_TIME,
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,nodes:[]};
  if(__nav.family===0){
   const active=$('#area')[__nav.accordion?'accordion':'tabs']('option','active');
   const activePanel=active===false?0:active+1;
   for(let i=0;i<__nav.nodes.length;i++){
    const n=__nav.nodes[i];if(n.kind===1&&n.panel!==activePanel)continue;
    const role=n.kind===0?11:n.kind===1?4:1;
    const flags=7|(n.kind===0&&n.panel===activePanel?(__nav.accordion?32:128):0);
    result.nodes.push({ref:i+1,parent:n.parent,role,flags,name:n.e.textContent.trim(),value:"",
     x:0,y:result.nodes.length,width:1,height:1});
   }
  }else{
   for(let i=0;i<__nav.nodes.length;i++){
    const n=__nav.nodes[i];
    const shown=n.kind===1||visible(n.e);if(!shown)continue;
    const expanded=n.children&&visible(n.e.querySelector(':scope > ul'));
    const flags=1|(n.enabled?6:0)|(expanded?32:0)|(__nav.hover===i+1?128:0);
    result.nodes.push({ref:i+1,parent:n.parent,role:n.kind===1||n.children?1:7,
     flags,name:n.name,value:n.icon?`${iconName(n.icon)} icon`:"",
     x:8,y:result.nodes.length*24,width:128,height:22});
   }
  }
  return result;
 };
 __nav.point=ref=>{
  const n=__nav.nodes[ref-1];if(!n)return null;
  const e=__nav.family===1?(n.e.querySelector(':scope > div')||n.e):n.e;
  const r=e.getBoundingClientRect();
  return {x:r.x+r.width/2,y:r.y+r.height/2,visible:!!(r.width&&r.height)&&
   (__nav.family===1?(n.kind===1||visible(n.e)):visible(n.e))};
 };
 __nav.tick=ms=>{
  __nav.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
  return true;
 };
 document.addEventListener('mousemove',event=>{
  if(__nav.family!==1)return;
  const li=event.target&&event.target.closest?event.target.closest('#menu li'):null;
  __nav.hover=li?(__nav.byElement.get(li)||0):0;
 });
 return true;
})()
