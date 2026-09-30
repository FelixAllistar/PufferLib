/* Matched original MiniWoB pages. Reset imports one initial instance; after an
   action no browser result is copied into the Bend row. Pointer presets use
   the page's own jQuery UI/D3/cube mouse handlers. */
(()=>{
 const names=['drag-box','drag-circle','drag-cube','drag-items',
  'drag-items-grid','drag-shapes','drag-shapes-2','drag-single-shape',
  'drag-sort-numbers','resize-textarea'];
 const page=location.pathname.split('/').pop().replace(/\.html$/,'');
 const task=names.indexOf(page);if(task<0)throw Error('unexpected drag page '+page);
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 window.__drag={task,clock:0,elapsed:0,held:null,mouse:null,original:[]};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__drag.clock]))}
  static now(){return epoch+__drag.clock}
 };
 const finish=core.endEpisode;
 core.endEpisode=function(reward,scaled,reason){
  __drag.elapsed=Date.now()-core.ept0;
  return finish(reward,scaled,reason);
 };
 const round10=x=>Math.round(x*10);
 const rect=e=>e.getBoundingClientRect();
 const middle=e=>{const r=rect(e);return {x:r.left+r.width/2,y:r.top+r.height/2}};
 const emit=(e,type,x,y,buttons)=>{
  const event=new MouseEvent(type,{bubbles:true,cancelable:true,view:window,
   clientX:x,clientY:y,button:0,buttons});
  Object.defineProperty(event,'which',{value:1});e.dispatchEvent(event);
 };
 const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>resolve()));
 const text=e=>e?e.textContent:'';
 const glyphs=()=>[...document.querySelectorAll('#area svg circle,#area svg rect:not(#shape-container),#area svg polygon')];
 const dirShape=()=>document.querySelector('#area svg circle,#area svg rect,#area svg polygon');
 const details=e=>{
  const b=e.getBBox(),p=shapes.gridCoords(e);
  return {kind:e.tagName.toLowerCase()==='circle'?0:
    e.tagName.toLowerCase()==='rect'?1:2,
   color:e.getAttribute('fill'),size:Math.round(b.width),
   initialX:round10(b.x),initialY:round10(b.y),
   x:round10(p.x),y:round10(p.y)};
 };
 __drag.snapshot=()=>{
  const s={task,query:text(document.querySelector('#query')),
   deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,
   raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,
   elapsed:WOB_DONE_GLOBAL?__drag.elapsed:__drag.clock};
  if(task===0){
   s.small={x:parseInt($('#draggableSmall').css('left'),10),
            y:parseInt($('#draggableSmall').css('top'),10)};
   s.large={x:parseInt($('#draggableLarge').css('left'),10),
            y:parseInt($('#draggableLarge').css('top'),10)};
  }else if(task===1||task===7){
   s.shape=details(dirShape());
  }else if(task===2){
   s.face=Number(document.querySelector('.cube-image.active').textContent);
  }else if(task===3||task===4||task===8){
   s.items=[...document.querySelectorAll('#sortable li')]
    .filter(e=>!e.classList.contains('ui-sortable-placeholder'))
    .map(e=>text(e.querySelector('div')));
  }else if(task===5||task===6){
   s.shapes=glyphs().map(details);
   s.boxX=containerX;s.boxY=containerY;
  }else if(task===9){
   const e=$('#tt'),style=document.querySelector('#tt').style;
   s.width=e.width();s.height=e.height();
   s.left=parseInt(style.left,10);s.top=parseInt(style.top,10);
  }
  return s;
 };
 __drag.reset=seed=>{
  __drag.clock=0;__drag.elapsed=0;__drag.held=null;__drag.mouse=null;
  Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  if(task===3||task===4||task===8)
   __drag.original=[...document.querySelectorAll('#sortable li')];
  return __drag.snapshot();
 };
 __drag.advance=ms=>{
  __drag.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
 };
 const downElement=ref=>{
  if(task===0)return document.querySelector(ref===1?'#draggableSmall':'#draggableLarge');
  if(task===1||task===7)return dirShape();
  if(task===2)return document.querySelector('.cube');
  if(task===3||task===4||task===8)return __drag.original[ref-1];
  if(task===5||task===6)return glyphs()[ref-1];
  if(task===9)return document.querySelector('.ui-resizable-se');
  return null;
 };
 const moveTo=(x,y)=>{emit(document,'mousemove',x,y,1);__drag.mouse={x,y}};
 const moveSvg=async(targetX,targetY)=>{
  const e=__drag.held,desiredX=(targetX-2560)/10,desiredY=(targetY-2560)/10;
  const factor=e.tagName.toLowerCase()==='circle'?.6:
   e.tagName.toLowerCase()==='rect'?.5:.4;
  const offset=e.getBBox().width*factor;
  const point=e.ownerSVGElement.createSVGPoint();
  point.x=desiredX+offset;point.y=desiredY+offset;
  const screen=point.matrixTransform(e.parentNode.getScreenCTM());
  moveTo(Math.round(screen.x),Math.round(screen.y));
  await tick();
 };
 const moveSort=async(position)=>{
  const others=[...document.querySelectorAll('#sortable li')]
   .filter(e=>e!==__drag.held&&!e.classList.contains('ui-sortable-placeholder'));
  const successor=others[position-1],predecessor=others[position-2];
  if(position<1||position>others.length+1)throw Error('bad sort target '+position);
  const point=(e,fraction)=>{
   const r=rect(e);
   const horizontal=$('#sortable').sortable('instance').floating;
   return horizontal?{x:r.left+r.width*fraction,y:r.top+r.height/2}:
    {x:r.left+r.width/2,y:r.top+r.height*fraction};
  };
  const primary=successor?point(successor,.15):point(predecessor,.85);
  const begin=__drag.mouse,end=primary,steps=8;
  for(let i=1;i<=steps;i++){
   moveTo(begin.x+(end.x-begin.x)*i/steps,
          begin.y+(end.y-begin.y)*i/steps);
  }
  await tick();
  const slot=()=>{
   const nodes=[...document.querySelectorAll('#sortable li')]
    .filter(e=>e!==__drag.held);
   return nodes.findIndex(e=>e.classList.contains('ui-sortable-placeholder'))+1;
  };
  if(slot()===position)return;
  /* jQuery UI can move its placeholder one neighbor at a time, especially
     across wrapped grid rows. Cross the current adjacent item, then read the
     placeholder's new slot before choosing the next move. */
  const visited=[];
  for(let step=0;step<others.length*12;step++){
   const current=[...document.querySelectorAll('#sortable li')]
    .filter(e=>e!==__drag.held);
   const at=slot();
   visited.push(at);
   if(at===position)return;
   if(!at)break;
   const backward=at>position;
   const neighbor=current[backward?at-2:at];
   if(!neighbor||neighbor.classList.contains('ui-sortable-placeholder'))break;
   /* Preposition outside the neighbor: with grid float reflow, two points
      inside the old rectangle can move in the wrong screen direction. */
   const fractions=backward?[1.5,.05]:[-.5,.95];
   for(const fraction of fractions){
    const p=point(neighbor,fraction);moveTo(p.x,p.y);
    await tick();
    if(slot()===position)return;
   }
  }
  const widget=$('#sortable').sortable('instance');
  throw Error('sortable task='+task+' query='+text(document.querySelector('#query'))+
   ' held='+text(__drag.held.querySelector('div'))+' placeholder='+slot()+
   ' target='+position+' visited='+visited.join(',')+
   ' floating='+widget.floating+' mouse='+__drag.mouse.x+','+__drag.mouse.y+
   ' order='+[...document.querySelectorAll('#sortable li')]
    .map(e=>{const r=rect(e);return (text(e.querySelector('div'))||e.className)+
     '@'+Math.round(r.left)+','+Math.round(r.top);}).join('|'));
 };
 const moveCube=async(face)=>{
  /* Feedback through the widget's own mouse handlers. The page's viewport
     is read to steer toward a sector; neither angles nor active class are
     assigned by this actuator. */
  const v=cube.viewport;
  const target={1:[180,264],2:[180,180],3:[90,180],
   4:[0,180],5:[270,180],6:[180,88]}[face];
  const wrap=x=>((x+180)%360+360)%360-180;
  const clamp=x=>Math.max(-38,Math.min(38,x));
  for(let i=0;i<220;i++){
   const current=Number(document.querySelector('.cube-image.active').textContent);
   if(current===face&&Math.abs(v.torqueX)<0.7&&Math.abs(v.torqueY)<0.7)return;
   const ex=wrap(target[0]-v.positionX),ey=wrap(target[1]-v.positionY);
   const upside=v.positionY>90&&v.positionY<270;
   const desiredX=(upside?-ex:ex)/3.5,desiredY=-ey/3.5;
   const dx=clamp((desiredX-0.83*v.torqueX)/0.2);
   const dy=clamp((desiredY-0.83*v.torqueY)/0.2);
   moveTo(__drag.mouse.x+dx,__drag.mouse.y+dy);
   await new Promise(resolve=>setTimeout(resolve,24));
  }
  throw Error('cube failed to settle on '+face);
 };
 __drag.act=async(kind,ref,arg0=0,arg1=0)=>{
  if(kind===1){
   const selector=task===9?'#submit':
    task===3||task===4?'#no-submit':'#subbtn';
   const button=document.querySelector(selector);if(!button)throw Error('no submit');
   button.click();
  }else if(kind===13){
   const e=downElement(ref);if(!e)throw Error('bad down ref '+ref);
   const p=middle(e);__drag.held=e;__drag.mouse=p;
   if(task===2){
    emit(document,'mousemove',p.x,p.y,0);
    await new Promise(resolve=>setTimeout(resolve,24));
   }
   if(task===9)emit(e,'mouseover',p.x,p.y,0);
   emit(e,'mousedown',p.x,p.y,1);
  }else if(kind===14){
   if(__drag.held){
    if(task===0){
     const e=__drag.held,oldX=parseInt($(e).css('left'),10),
      oldY=parseInt($(e).css('top'),10);
     moveTo(__drag.mouse.x+(arg0-256-oldX),
            __drag.mouse.y+(arg1-256-oldY));
    }else if(task===1||task===7||task===5||task===6){
     await moveSvg(arg0,arg1);
    }else if(task===2){await moveCube(arg0);
    }else if(task===3||task===4||task===8){await moveSort(arg0);
    }else if(task===9){
     const field=$('#tt');moveTo(__drag.mouse.x+(arg0-field.width()),
                                 __drag.mouse.y+(arg1-field.height()));
    }
   }
  }else if(kind===15){
   if(__drag.mouse)emit(document,'mouseup',__drag.mouse.x,__drag.mouse.y,0);
   __drag.held=null;
  }
  await tick();
  return __drag.snapshot();
 };
 return true;
})()
