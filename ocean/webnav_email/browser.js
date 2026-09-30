(()=>{
  const NativeDate=Date;
  const state={clock:0,selected:0};
  window.Date=class extends NativeDate{
    constructor(...args){super(...(args.length?args:[state.clock]));}
    static now(){return state.clock;}
  };
  // The original handler's public thread element supplies a stable index.
  // No original email array, target, task action, or expected text is read.
  const originalClickEmail=clickEmail;
  clickEmail=function(event){
    state.selected=Number(this.getAttribute('data-index'))+1;
    return originalClickEmail.call(this,event);
  };
  const visible=e=>!!e&&!e.closest('.hide')&&!!e.getClientRects().length;
  const text=e=>e?e.textContent.trim():'';
  const query=s=>document.querySelector(s);
  const screen=()=>{
    for(const [id,n] of [['reply',3],['forward',4],['email',2],['search',1]]){
      const e=document.getElementById(id);
      if(visible(e))return n;
    }
    return 0;
  };
  const element=ref=>{
    if(ref===1)return query('#open-search');
    if(ref===2)return query('#search-input');
    if(ref===3)return query('#search-cancel');
    if(ref>=10&&ref<106){
      const index=Math.floor((ref-10)/8),slot=(ref-10)%8;
      if(slot<=2){
        const e=query(`#main .email-thread[data-index="${index}"]`);
        return slot===0?e:e&&e.querySelector(slot===1?'.star':'.trash');
      }
      if(slot<=5){
        const e=query(`#search .email-thread[data-index="${index}"]`);
        return slot===3?e:e&&e.querySelector(slot===4?'.star':'.trash');
      }
      if(index+1!==state.selected)return null;
      return query(`#email .email-actions ${slot===6?'.star':'.trash'}`);
    }
    const fixed={112:'#close-email',113:'#email .email-reply',
      114:'#email .email-forward',115:'#close-reply',116:'#send-reply',
      117:'#reply-text',118:'#close-forward',119:'#send-forward',
      120:'#forward .forward-sender',121:'#forward-text'};
    return query(fixed[ref]||'#missing');
  };
  const node=(ref,role,name,value,el,parent=0,checked=false,capacity=0)=>({
    ref,parent,role,
    flags:(visible(el)?1:0)|(role===13?0:(el&&!el.disabled?2:0))|(role===13?0:4)|
      (checked?8:0)|(el&&document.activeElement===el?16:0),
    name,value,capacity
  });
  const button=(ref,label,parent=0,checked=false)=>
    node(ref,1,label,'',element(ref),parent,checked);
  const thread=(e,index,mode)=>{
    const base=10+8*index+(mode?3:0),sender=text(e.querySelector('.email-sender'));
    const subject=text(e.querySelector('.email-subject'));
    return [node(base,1,sender,subject,element(base)),
      button(base+1,'Star',base,!mode&&!!e.querySelector('.star.clicked')),
      button(base+2,'Trash',base)];
  };
  const snapshot=()=>{
    const s=screen(),nodes=[];
    if(s===0){
      nodes.push(button(1,'Search'));
      for(const e of document.querySelectorAll('#main .email-thread'))
        nodes.push(...thread(e,Number(e.dataset.index),0));
    }else if(s===1){
      nodes.push(button(3,'Back'));
      const el=element(2);
      nodes.push(node(2,3,'Search',el.value,el,0,false,128));
      for(const e of document.querySelectorAll('#search-results .email-thread'))
        nodes.push(...thread(e,Number(e.dataset.index),1));
    }else if(s===2){
      const i=state.selected-1;
      nodes.push(button(112,'Back'));
      nodes.push(button(10+8*i+6,'Star',0,!!query('#email .star.clicked')));
      nodes.push(button(10+8*i+7,'Trash'));
      nodes.push(node(110,13,text(query('#email .email-sender')),
        text(query('#email .email-subject')),query('#email .email-sender')));
      nodes.push(node(111,13,'Body',text(query('#email .email-body')),
        query('#email .email-body')));
      nodes.push(button(113,'Reply'),button(114,'Forward'));
    }else if(s===3){
      nodes.push(button(115,'Back'),button(116,'Send'));
      nodes.push(node(110,13,'To',text(query('#reply .reply-sender')),
        query('#reply .reply-sender')));
      const el=element(117);
      nodes.push(node(117,16,'Reply',el.value,el,0,false,160));
    }else{
      nodes.push(button(118,'Back'),button(119,'Send'));
      const subject=text(query('#forward .forward-subject'));
      nodes.push(node(110,13,'Subject',subject.replace(/^subject:\s*/i,''),
        query('#forward .forward-subject')));
      const to=element(120),body=element(121);
      nodes.push(node(120,3,'To',to.value,to,0,false,64));
      nodes.push(node(121,16,'Forward body',body.value,body,0,false,160));
    }
    return {instruction:text(query('#query')),elapsed:state.clock,
      deadline:core.EPISODE_MAX_TIME,nodes,
      done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL};
  };
  window.__wfe={
    reset:async seed=>{
      state.clock=0;state.selected=0;
      Math.seedrandom(String(seed));core.startEpisodeReal();
      if(core.EP_TIMER!==null)clearTimeout(core.EP_TIMER);
      core.EP_TIMER=-1;core.clearTimer();
      await new Promise(resolve=>requestAnimationFrame(resolve));
      return snapshot();
    },
    snapshot,
    point:ref=>{
      const e=element(ref);if(!visible(e))return null;
      e.scrollIntoView({block:'nearest',inline:'nearest'});
      const r=e.getBoundingClientRect();
      return {x:r.x+r.width/2,y:r.y+r.height/2,visible:!!(r.width&&r.height)};
    },
    tick:ms=>{
      state.clock=ms;
      if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
        core.endEpisode(-1,false,'timed out');
      return snapshot();
    },
    searchKeyup:()=>query('#search-input').dispatchEvent(new KeyboardEvent('keyup',{bubbles:true}))
  };
  return Promise.all(['search','left-arrow','left-arrow-white','star',
    'star-clicked','delete','reply','forward','send'].map(async name=>{
      const icon=new Image();
      icon.src='../common/special/email-inbox/'+name+'.png';
      await icon.decode();
  })).then(()=>true);
})()
