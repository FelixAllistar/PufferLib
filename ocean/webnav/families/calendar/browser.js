/* Differential fixture for the pinned MiniWoB pages. It sends DOM events to
   the original handlers and reports visible state; it never scores an action. */
(()=>{
 const NativeDate=Date, epoch=NativeDate.UTC(2026,0,1);
 window.__cal={clock:0,elapsed:0,task:location.pathname.split('/').pop().replace(/\.html$/,'')};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__cal.clock]))}
  static now(){return epoch+__cal.clock}
 };
 const end=core.endEpisode;
 core.endEpisode=function(reward,scaled,reason){
  __cal.elapsed=Date.now()-core.ept0;
  return end(reward,scaled,reason);
 };
 __cal.reset=seed=>{
  __cal.clock=0;__cal.elapsed=0;
  if(__cal.task!=='daily-calendar'){
   $('#datepicker').datepicker('hide');
   $('#datepicker').datepicker('widget').stop(true,true);
  }
  Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __cal.export();
 };
 __cal.advance=ms=>{
  __cal.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
 };
 __cal.export=()=>{
  const query=document.querySelector('#query').textContent;
  const date=__cal.task!=='daily-calendar';
  const input=document.querySelector('#datepicker');
  const widget=date?$('#datepicker').datepicker('widget'):null;
  const open=!!(widget&&widget.is(':visible'));
  const month=open?widget.find('.ui-datepicker-month').text():'';
  const area=document.querySelector('#area');
  const draft=document.querySelector('#newEvent');
  const events=date?[]:[0,1,2].map(i=>{
   const e=document.querySelector('#randomEvent'+i);
   const top=parseFloat(e.style.top);
   return {start:Math.round((top-4)/20.42),duration:Number(e.dataset.duration),
           name:e.textContent};
  });
  return {query,deadline:core.EPISODE_MAX_TIME,value:input?input.value:'',
   open,month,scroll:date?0:area.scrollTop,
   events,draft:!!draft,modal:!!document.querySelector('#create-event'),
   eventName:document.querySelector('#event-name')?.value||'',
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
   reward:WOB_REWARD_GLOBAL,elapsed:WOB_DONE_GLOBAL?__cal.elapsed:__cal.clock};
 };
 __cal.act=async(kind,ref,arg,text)=>{
  if(__cal.task!=='daily-calendar'){
   if(kind===1){
    if(ref===1){document.querySelector('#datepicker').focus();$('#datepicker').datepicker('show');}
    else if(ref===2)document.querySelector('#subbtn').click();
    else if(ref===3)document.querySelector('.ui-datepicker-prev').click();
    else if(ref===4)document.querySelector('.ui-datepicker-next').click();
    else {
     const d=String(ref-4);
     const link=[...document.querySelectorAll('.ui-datepicker-calendar td a')]
       .find(e=>e.textContent.trim()===d);
     if(!link)throw Error('missing date '+d);
     link.click();
    }
   }
   if(ref===1&&kind===1&&__cal.task!=='choose-date-nodelay')
    await new Promise(resolve=>setTimeout(resolve,250));
   $('#datepicker').datepicker('widget').stop(true,true);
  }else{
   const cell=()=>document.querySelector('#hh-'+(ref-1));
   if(kind===12)document.querySelector('#area').scrollTop=arg*20;
   else if(kind===13)cell().dispatchEvent(new MouseEvent('mousedown',{bubbles:true,cancelable:true}));
   else if(kind===14)cell().dispatchEvent(new MouseEvent('mousemove',{bubbles:true,cancelable:true}));
   else if(kind===15)document.querySelector('#newEvent').dispatchEvent(
      new MouseEvent('mouseup',{bubbles:true,cancelable:true}));
   else if(kind===2){const e=document.querySelector('#event-name');e.setRangeText(text)}
   else if(kind===3){
    const e=document.querySelector('#event-name');
    if(e.selectionStart===e.selectionEnd)e.setSelectionRange(Math.max(0,e.selectionStart-1),e.selectionEnd);
    e.setRangeText('');
   }
   else if(kind===9)document.querySelector('#event-name').select();
   else if(kind===1)document.querySelector(ref===54?'#controls .cancel':'#controls .create').click();
  }
  return __cal.export();
 };
 return true;
})()
