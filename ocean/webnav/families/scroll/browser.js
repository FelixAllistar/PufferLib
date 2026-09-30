/* Matched original-page fixture. Private goals are exported once at reset to
 * the simulator row; snapshots after actions never repair simulator state. */
(()=>{
 const NativeDate=Date;
 window.__scroll={clock:0,task:0};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[__scroll.clock]))}
  static now(){return __scroll.clock}
 };
 __scroll.area=()=>document.getElementById(__scroll.task===0?'options':'text-area');
 __scroll.input=()=>document.getElementById(__scroll.task===3?'name':'answer-input');
 __scroll.snapshot=(initial=false)=>{
  const area=__scroll.area(),query=document.getElementById('query').textContent;
  const state={query,position:area.scrollTop,height:area.scrollHeight,
   client:area.clientHeight,offset:area.offsetHeight,done:!!WOB_DONE_GLOBAL,
   raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,
   deadline:core.EPISODE_MAX_TIME};
  if(__scroll.task===0){
   const options=[...area.options];state.options=options.map(o=>o.value);
   state.selected=options.map(o=>!!o.selected);
   state.optionStride=options.length>1?
    options[1].offsetTop-options[0].offsetTop:options[0].offsetHeight;
   const css=getComputedStyle(area);
   state.selectMetrics={clientHeight:area.clientHeight,offsetHeight:area.offsetHeight,
    scrollHeight:area.scrollHeight,scrollTop:area.scrollTop,
    lineHeight:css.lineHeight,fontSize:css.fontSize,paddingTop:css.paddingTop,
    borderTopWidth:css.borderTopWidth,
    options:options.map((o,i)=>({i,offsetTop:o.offsetTop,
      offsetHeight:o.offsetHeight,rectHeight:o.getBoundingClientRect().height,
      selected:o.selected,value:o.value}))};
   const wanted=query.slice(7).split(' from the scroll list')[0].split(', ');
   state.allowed=options.map(o=>wanted.includes(o.value));state.required=wanted.length;
  }else{
   state.content=area.value;
   if(__scroll.task===2)state.bottom=query.includes('bottom');
   else{
    const field=__scroll.input();state.input=field.value;
    state.start=field.selectionStart;state.end=field.selectionEnd;
    if(__scroll.task===1){
     const words=area.value.split(/[\s]/g);
     state.goal=words[words.length-1].replace('.', '');
    }else{
     state.mode=query==='Click the cancel button.'?0:query.includes('press "Agree"')?1:2;
     state.enabled=!field.disabled;
     state.goal=state.mode===0?'':query.match(/name "([^"]*)"/)[1];
    }
   }
  }
  return state;
 };
 __scroll.reset=(seed,task)=>{
  __scroll.clock=0;__scroll.task=task;Math.seedrandom(String(seed));
  core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;
  core.clearTimer();return __scroll.snapshot(true);
 };
 __scroll.tick=ms=>{
  __scroll.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
  return true;
 };
 __scroll.act=async(kind,target,arg0,content='')=>{
  const area=__scroll.area();
  if(kind===12){area.scrollTop=arg0;area.dispatchEvent(new Event('scroll',{bubbles:true}));}
  else if(kind===21){area.options[target-2].selected=!!arg0;
   area.dispatchEvent(new Event('change',{bubbles:true}));}
  else if(kind===2){const field=__scroll.input();field.setRangeText(content,field.selectionStart,
    field.selectionEnd,'end');field.dispatchEvent(new Event('input',{bubbles:true}));}
  else if(kind===3){const field=__scroll.input();const at=field.selectionStart;
   field.setRangeText('',at===field.selectionEnd?Math.max(0,at-1):at,field.selectionEnd,'start');}
  else if(kind===9){const field=__scroll.input();field.select();}
  else if(kind===1){
   const el=__scroll.task===0?document.querySelector('#area button'):
    __scroll.task===3?document.getElementById(target===3?'cancel':'agree'):
    document.getElementById('subbtn');
   if(target===2&&__scroll.task!==2&&__scroll.task!==0) __scroll.input().focus();
   else el.click();
  }
  if(__scroll.task===0)
   await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
  return __scroll.snapshot();
 };
 return true;
})()
