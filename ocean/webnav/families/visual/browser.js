/* Pinned original-page fixture. Generation, handlers and reward stay in HTML. */
(()=>{
 const NativeDate=Date;
 window.__vi={clock:0,ended:0,task:location.pathname.split('/').pop().replace(/\.html$/,'')};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[__vi.clock]))}
  static now(){return __vi.clock}
 };
 const originalEnd=core.endEpisode;
 core.endEpisode=function(reward,proportional,reason){
  __vi.ended=Date.now()-core.ept0;
  return originalEnd(reward,proportional,reason);
 };
 if(window.shapes){
  const render=shapes.renderGrid;
  shapes.renderGrid=function(svg,grid){
   __vi.grid=grid;return render(svg,grid);
  };
  const describe=shapes.generalDesc;
  shapes.generalDesc=function(shape){
   const desc=describe(shape);__vi.desc=desc;return desc;
  };
  const sample=shapes.sampleDesc;
  shapes.sampleDesc=function(){const desc=sample();__vi.desc=desc;return desc;};
 }
 if(typeof createItems==='function'){
  const create=createItems;
  window.createItems=function(){const result=create();__vi.pie=result;return result;};
 }
 if(typeof createSpan==='function'){
  const create=createSpan;
  window.createSpan=function(hsl,color){__vi.hsl.push(hsl);return create(hsl,color);};
 }
 if(typeof drawShape==='function'){
  const draw=drawShape;
  window.drawShape=function(ctx){const sides=draw(ctx);__vi.sides=sides;return sides;};
  const move=CanvasRenderingContext2D.prototype.moveTo;
  const line=CanvasRenderingContext2D.prototype.lineTo;
  const capture=function(ctx,x,y){
   if(!ctx.canvas||ctx.canvas.id!=='c')return;
   const t=ctx.getTransform();
   __vi.vertices.push({x:Math.round(t.a*x+t.c*y+t.e),
                       y:Math.round(t.b*x+t.d*y+t.f)});
  };
  CanvasRenderingContext2D.prototype.moveTo=function(x,y){capture(this,x,y);return move.call(this,x,y)};
  CanvasRenderingContext2D.prototype.lineTo=function(x,y){capture(this,x,y);return line.call(this,x,y)};
 }
 if(typeof drawShapes==='function'){
  const draw=drawShapes;
  window.drawShapes=function(grid){const result=draw(grid);__vi.category=result;return result;};
 }
 __vi.export=()=>{
  const task=__vi.task,q=document.querySelector('#query').textContent;
  const out={query:q,deadline:core.EPISODE_MAX_TIME,
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
   reward:WOB_REWARD_GLOBAL,elapsed:WOB_DONE_GLOBAL?__vi.ended:__vi.clock};
  if(task==='click-color'){
   out.colors=[...document.querySelectorAll('#area .color')].map(e=>e.getAttribute('data-color'));
   const swatch=document.querySelector('#query-color');
   out.swatch=swatch?swatch.style.backgroundColor:'';
   out.goal=swatch?out.swatch:document.querySelector('#query .bold').textContent;
  }else if(task==='click-pie'||task==='click-pie-nodelay'){
   out.labels=__vi.pie.problemSet;out.goal=__vi.pie.expectedItem;
   out.open=__vi.pie.items.currentPercent>0.5;
  }else if(task==='click-shades'){
   out.shades=[...document.querySelectorAll('#area span')].map((e,i)=>({
    color:e.getAttribute('data-color'),hsl:__vi.hsl[i],
    selected:e.classList.contains('selected')}));
   out.goal=/shades of (red|green|blue)/.exec(q)[1];
  }else if(task==='click-shape'||task==='count-shape'){
   out.shapes=__vi.grid.shapes.map(s=>({x:s.x,y:s.y,color:s.color,
    size:s.size,kind:s.type,glyph:s.text}));
   out.desc=__vi.desc.parts;
   out.buttons=[...document.querySelectorAll('#count-buttons button')].map(e=>+e.textContent);
   out.svg=[...document.querySelectorAll('#area_svg > *')].map(e=>({
    tag:e.tagName.toLowerCase(),fill:e.getAttribute('fill'),
    glyph:e.textContent,font:e.getAttribute('font-size')}));
  }else if(task==='count-sides'){
   out.sides=__vi.sides;
   out.vertices=__vi.vertices.slice(0,__vi.sides);
  }else if(task==='identify-shape'){
   out.category=__vi.category;
   const e=document.querySelector('#area_svg > *');
   out.figure={tag:e.tagName.toLowerCase(),fill:e.getAttribute('fill'),glyph:e.textContent};
  }else if(task==='visual-addition'){
   out.left=document.querySelectorAll('#visual-1 .addition-block').length;
   out.right=document.querySelectorAll('#visual-2 .addition-block').length;
   const e=document.querySelector('#math-answer');
   out.input=e.value;out.focused=document.activeElement===e;
   out.start=e.selectionStart;out.end=e.selectionEnd;
  }
  return out;
 };
 __vi.reset=seed=>{
  document.activeElement?.blur?.();
  __vi.clock=0;__vi.ended=0;__vi.grid=null;__vi.desc=null;
  __vi.pie=null;__vi.hsl=[];__vi.sides=0;__vi.vertices=[];__vi.category=null;
  Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __vi.export();
 };
 __vi.advance=ms=>{
  __vi.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
  return __vi.export();
 };
 __vi.click=ref=>{
  const task=__vi.task;
  if(task==='click-color'){
   const e=ref===1?document.querySelector('#query-color'):
    document.querySelectorAll('#area .color')[ref-10];e.click();
  }else if(task==='click-pie'||task==='click-pie-nodelay'){
   if(ref===1){
    const e=__vi.pie.items.spreader.spreaderPath.node;
    e.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));
   }
   else{
    const item=__vi.pie.items.navItems[ref-10];
    item.navSlice.node.dispatchEvent(new MouseEvent('mouseup',{bubbles:true,cancelable:true}));
   }
  }else if(task==='click-shades'){
   const e=ref===2?document.querySelector('#submit'):
    document.querySelectorAll('#area span')[ref-10];e.click();
  }else if(task==='click-shape'||task==='count-shape'){
   const e=ref===1?document.querySelector('#area_svg'):
    ref>=40?document.querySelectorAll('#count-buttons button')[ref-40]:
    document.querySelectorAll('#area_svg > *')[ref-10];
   e.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));
  }else if(task==='count-sides'||task==='identify-shape'){
   const selector=task==='count-sides'?'#form button':'#area-buttons button';
   document.querySelectorAll(selector)[ref-10].click();
  }else if(task==='visual-addition'){
   (ref===1?document.querySelector('#math-answer'):
             document.querySelector('#subbtn')).click();
  }
  return __vi.export();
 };
 __vi.point=ref=>{
  if(__vi.task!=='visual-addition')return null;
  const e=ref===1?document.querySelector('#math-answer'):
                    document.querySelector('#subbtn');
  if(!e)return null;
  const r=e.getBoundingClientRect();
  return {x:r.x+r.width/2,y:r.y+r.height/2};
 };
 return true;
})()
