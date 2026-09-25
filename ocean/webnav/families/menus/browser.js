(()=>{
 const NativeDate=Date;
 window.__menus={clock:0,task:0,nodes:[],byElement:new Map(),hover:0};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[__menus.clock]));}
  static now(){return __menus.clock;}
 };
 const iconIds={disk:1,"seek-start":2,stop:3,play:4,"seek-end":5,zoomin:6,zoomout:7,print:8};
 const iconName=id=>Object.keys(iconIds).find(k=>iconIds[k]===id)||"";
 __menus.settle=()=>{$('#menu').find(':animated').finish();$('#area').find(':animated').finish();};
 __menus.reset=(seed,task)=>{
  __menus.clock=0;__menus.task=task;__menus.hover=0;
  Math.seedrandom(String(seed));
  core.startEpisodeReal();
  if(core.EP_TIMER!==null)clearTimeout(core.EP_TIMER);
  core.EP_TIMER=-1;core.clearTimer();__menus.settle();
  __menus.nodes=[];__menus.byElement=new Map();
  const menu=document.querySelector('#menu');
  if(task===1){
   const button=document.querySelector('#open-menu');
   __menus.nodes.push({ref:1,e:button,kind:1,parent:0,goal:false,enabled:true,children:false,icon:0,name:button.textContent.trim()});
   __menus.byElement.set(button,1);
  }
  const items=[...menu.querySelectorAll('li')];
  for(const e of items){
   const ref=__menus.nodes.length+1;
   const own=e.querySelector(':scope > div');
   const parentLi=e.parentElement.closest('li');
   const parent=parentLi?__menus.byElement.get(parentLi)||0:0;
   const icon=own&&own.querySelector('.ui-icon');
   const match=icon&&icon.className.match(/\bui-icon-([a-z-]+)\b/);
   const iconId=match?iconIds[match[1]]||0:0;
   const name=own?own.textContent.trim():e.textContent.trim();
   const children=!!e.querySelector(':scope > ul');
   const enabled=!(e.classList.contains('ui-state-disabled')||e.getAttribute('aria-disabled')==='true');
   const item={ref,e,kind:0,parent,goal:false,enabled,children,icon:iconId,name};
   __menus.nodes.push(item);__menus.byElement.set(e,ref);
  }
  const query=document.querySelector('#query');
  const raw=query.innerText.trim();
  const icon=query.querySelector('.ui-icon');
  const iconMatch=icon&&icon.className.match(/\bui-icon-([a-z-]+)\b/);
  const expectedIcon=iconMatch?iconIds[iconMatch[1]]||0:0;
  let instruction=raw;
  if(expectedIcon){instruction=raw.replace(/\bthe\s+icon\b/,`the ${iconName(expectedIcon)} icon`);}
  let expectedLabel='';
  const labelMatch=raw.match(/item labeled "([^"]+)"/);
  if(labelMatch)expectedLabel=labelMatch[1];
  if(task===0){
   const wanted=raw.replace(/^Select\s+/, '').trim().split(/\s*>\s*/).pop().replace(/[.]$/,'').trim();
   for(const item of __menus.nodes)item.goal=!item.children&&item.name===wanted;
  }else{
   for(const item of __menus.nodes){
    if(item.kind)continue;
    item.goal=!item.children&&(expectedIcon?item.icon===expectedIcon:item.name===expectedLabel);
   }
  }
  const target=__menus.nodes.find(n=>n.goal);
  if(!target)throw new Error('menus browser oracle could not identify the original target');
  __menus.instruction=instruction;
  __menus.target=target.ref;
  __menus.open=()=>task===0||$(menu).is(':visible');
  return __menus.snapshot(true);
 };
 __menus.refresh=()=>{
  __menus.settle();
  return __menus.nodes.map(n=>{
   const visible=n.kind===1||$(n.e).is(':visible');
   const submenu=n.children&&$(n.e.querySelector(':scope > ul')).is(':visible');
   return {ref:n.ref,parent:n.parent,kind:n.kind,goal:n.goal,enabled:n.enabled,children:n.children,icon:n.icon,name:n.name,visible,expanded:submenu};
  });
 };
 __menus.snapshot=(initial=false)=>{
  const nodes=__menus.refresh();
  const out={instruction:__menus.instruction,deadline:core.EPISODE_MAX_TIME,open:__menus.open(),hover:__menus.hover,
   visible:nodes.filter(n=>n.visible).map(n=>n.ref),expanded:nodes.filter(n=>n.expanded).map(n=>n.ref),
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
  if(initial)out.nodes=nodes;
  return out;
 };
 __menus.point=ref=>{
  const n=__menus.nodes[ref-1];if(!n)return null;
  const r=(n.e.querySelector(":scope > div")||n.e).getBoundingClientRect();
  return {x:r.x+r.width/2,y:r.y+r.height/2,visible:!!(r.width&&r.height)&&(n.kind===1||$(n.e).is(':visible'))};
 };
 __menus.tick=ms=>{
  __menus.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
  return true;
 };
 document.addEventListener('mousemove',e=>{
  const li=e.target&&e.target.closest?e.target.closest('#menu li'):null;
  __menus.hover=li?(__menus.byElement.get(li)||0):0;
 });
 return true;
})()
