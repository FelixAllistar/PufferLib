(()=>{
  const NativeDate=Date;
  window.__email={clock:0,emails:[],expected:null,selected:0};
  window.Date=class extends NativeDate{
    constructor(...args){super(...(args.length?args:[__email.clock]));}
    static now(){return __email.clock;}
  };
  const originalGenerate=generateEmails;
  generateEmails=function(){const mail=originalGenerate();__email.emails=mail;return mail;};
  const originalDisplay=displayQuery;
  displayQuery=function(expected){
    const result=originalDisplay(expected);
    __email.expected=expected;
    return result;
  };
  const originalShow=showEmail;
  showEmail=function(email,expected){
    __email.selected=__email.emails.indexOf(email)+1;
    return originalShow(email,expected);
  };
  __email.reset=async seed=>{
    __email.clock=0;__email.selected=0;__email.expected=null;
    Math.seedrandom(String(seed));core.startEpisodeReal();
    if(core.EP_TIMER!==null)clearTimeout(core.EP_TIMER);
    core.EP_TIMER=-1;core.clearTimer();
    await new Promise(resolve=>requestAnimationFrame(resolve));
    return __email.snapshot(true);
  };
  __email.screen=()=>{
    for(const [id,n] of [['reply',3],['forward',4],['email',2],['search',1]]){
      const e=document.getElementById(id);
      if(e&&!e.classList.contains('hide'))return n;
    }
    return 0;
  };
  __email.element=ref=>{
    const screen=__email.screen();
    if(ref===1)return document.querySelector('#open-search');
    if(ref===2)return document.querySelector('#search-input');
    if(ref===3)return document.querySelector('#search-cancel');
    if(ref>=10&&ref<106){
      const index=Math.floor((ref-10)/8),slot=(ref-10)%8;
      if(slot<=2){
        const thread=document.querySelector(`#main .email-thread[data-index="${index}"]`);
        return slot===0?thread:thread&&thread.querySelector(slot===1?'.star':'.trash');
      }
      if(slot<=5){
        const thread=document.querySelector(`#search .email-thread[data-index="${index}"]`);
        return slot===3?thread:thread&&thread.querySelector(slot===4?'.star':'.trash');
      }
      if(index+1!==__email.selected)return null;
      return document.querySelector(`#email .email-actions ${slot===6?'.star':'.trash'}`);
    }
    const fixed={112:'#close-email',113:'#email .email-reply',114:'#email .email-forward',
      115:'#close-reply',116:'#send-reply',117:'#reply-text',118:'#close-forward',
      119:'#send-forward',120:'#forward .forward-sender',121:'#forward-text'};
    return document.querySelector(fixed[ref]||'#missing');
  };
  __email.point=ref=>{
    const e=__email.element(ref);if(!e)return null;
    const rect=e.getBoundingClientRect();
    return {x:rect.x+rect.width/2,y:rect.y+rect.height/2,
      visible:!!(rect.width&&rect.height)&&!!e.offsetParent};
  };
  __email.preparePoint=ref=>{
    const e=__email.element(ref);if(e)e.scrollIntoView({block:'nearest',inline:'nearest'});
    return __email.point(ref);
  };
  __email.snapshot=(initial=false)=>{
    const screen=__email.screen(),result={screen,selected:screen>=2?__email.selected:0,
      query:document.querySelector('#query').innerText,
      search:document.querySelector('#search-input')?.value||'',
      reply:document.querySelector('#reply-text')?.value||'',
      recipient:document.querySelector('#forward .forward-sender')?.value||'',
      forward:document.querySelector('#forward-text')?.value||'',
      inboxStars:[...document.querySelectorAll('#main .email-thread')].map(e=>!!e.querySelector('.star.clicked')),
      readStar:!!document.querySelector('#email .star.clicked'),
      searchResults:[...document.querySelectorAll('#search-results .email-thread')].map(e=>Number(e.dataset.index)+1),
      done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
    result.visible=[];
    for(const ref of [1,2,3,...Array.from({length:96},(_,i)=>10+i),112,113,114,115,116,117,118,119,120,121]){
      const p=__email.point(ref);if(p&&p.visible)result.visible.push(ref);
    }
    if(initial){
      const expected=__email.expected,action={reply:0,forward:1,delete:2,important:3}[expected.action];
      result.emails=__email.emails.map(e=>({name:e.name,subject:e.subject,body:e.body}));
      result.goal={action,index:__email.emails.indexOf(expected.email)+1,
        reply:expected.reply||'',recipient:expected.forward||''};
      result.deadline=core.EPISODE_MAX_TIME;
    }
    return result;
  };
  __email.tick=ms=>{
    __email.clock=ms;
    if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
    return __email.snapshot();
  };
  // CSS content:url icons acquire their width only after image decoding.
  // Preload the original assets, without replacing their DOM or styling.
  return Promise.all(['search','left-arrow','left-arrow-white','star',
    'star-clicked','delete','reply','forward','send'].map(async name=>{
      const icon=new Image();
      icon.src='../common/special/email-inbox/'+name+'.png';
      await icon.decode();
  })).then(()=>true);
})()
