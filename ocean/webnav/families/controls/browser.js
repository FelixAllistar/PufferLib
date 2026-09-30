(()=>{
  const NativeDate=Date;
  const state={clock:0,task:0};
  window.Date=class extends NativeDate {
    constructor(...args){super(...(args.length?args:[state.clock]));}
    static now(){return state.clock;}
  };
  const rgb=css=>{
    const m=css.match(/rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/);
    if(!m)throw Error('Unexpected CSS color: '+css);
    return (+m[1]<<16)|(+m[2]<<8)|+m[3];
  };
  const goal=()=>{
    const q=document.querySelector('#query').textContent.trim();
    if(state.task===0)return q.match(/^Select (.*?) from the list/)[1];
    if(state.task===1||state.task===3)return +q.match(/^Select (-?\d+)/)[1];
    if(state.task===2)return JSON.parse(q.match(/\[[^\]]+\]/)[0]);
    if(state.task===4)return parseInt(ui_utils.colorNameToHex(q.match(/^Select (.*?) with the color picker/)[1]).slice(1),16);
    return rgb(getComputedStyle(document.querySelector('.cc')).backgroundColor);
  };
  const slider=e=>({
    min:$(e).slider('option','min'),max:$(e).slider('option','max'),
    orientation:$(e).slider('option','orientation'),value:$(e).slider('value')
  });
  // The family action is a continuous normalized track fraction. Preserve
  // fractional coordinates into the original jQuery handlers: MouseEvent's
  // constructor truncates coordinates, which can move a 100-step/50px slider
  // by one or two values. Native pixel dispatch is a separate conformance scope.
  const mouse=(e,type,x,y)=>$(e).trigger($.Event(type,{
    which:1,button:0,buttons:type==='mouseup'?0:1,
    clientX:x,clientY:y,pageX:x+scrollX,pageY:y+scrollY
  }));
  window.__controls={
    reset(task,seed){
      state.clock=0;state.task=task;
      Math.seedrandom(String(seed));
      core.startEpisodeReal();
      if(core.EP_TIMER!==null)clearTimeout(core.EP_TIMER);
      core.EP_TIMER=-1;
      core.clearTimer();
      return this.snapshot();
    },
    advance(ms){
      state.clock=ms;
      if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
      return this.snapshot();
    },
    snapshot(){
      const t=state.task,q=document.querySelector('#query').textContent.trim();
      const out={task:t,instruction:q,deadline:core.EPISODE_MAX_TIME,
        goal:goal(),done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
        reward:WOB_REWARD_GLOBAL};
      if(t===0){
        const s=document.querySelector('#options');
        out.options=[...s.options].map(o=>o.textContent);
        out.selected=s.selectedIndex;
      }else if(t===1)out.sliders=[slider(document.querySelector('#slider'))];
      else if(t===2)out.sliders=[1,2,3].map(i=>slider(document.querySelector('#slider-'+i)));
      else if(t===3)out.value=document.querySelector('#spinner').value;
      else out.color=document.querySelector('#col').value;
      return out;
    },
    select(index){
      const e=document.querySelector('#options');e.selectedIndex=index;
      e.dispatchEvent(new Event('change',{bubbles:true}));return this.snapshot();
    },
    slide(index,fraction){
      const e=document.querySelector(state.task===1?'#slider':'#slider-'+(index+1));
      const r=e.getBoundingClientRect(),v=$(e).slider('option','orientation')==='vertical';
      const x=v?r.left+r.width/2:r.left+r.width*fraction/1000;
      const y=v?r.top+r.height*fraction/1000:r.top+r.height/2;
      mouse(e,'mousedown',x,y);mouse(document,'mousemove',x,y);mouse(document,'mouseup',x,y);
      return this.snapshot();
    },
    spin(up){
      const e=document.querySelector(up?'.ui-spinner-up':'.ui-spinner-down');
      const r=e.getBoundingClientRect(),x=r.left+r.width/2,y=r.top+r.height/2;
      mouse(e,'mousedown',x,y);mouse(e,'mouseup',x,y);
      return this.snapshot();
    },
    type(hex){
      const e=document.querySelector('#col');
      e.focus();e.select();
      e.value=hex;
      e.dispatchEvent(new InputEvent('input',{bubbles:true,data:hex,inputType:'insertText'}));
      return this.snapshot();
    },
    submit(){document.querySelector('#subbtn, #area button').click();return this.snapshot();}
  };
  return true;
})()
