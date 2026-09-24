/* Original-page differential fixture. This script only resets, exports public
 * controls and owns a deterministic clock; task transitions remain in HTML. */
(()=>{
 const NativeDate=Date;
 window.__fm={clock:0,elapsed:0,task:location.pathname.split('/').pop().replace(/\.html$/,'')};
 window.Date=class extends NativeDate{constructor(...args){super(...(args.length?args:[__fm.clock]))}static now(){return __fm.clock}};
 const end=core.endEpisode;
 core.endEpisode=function(reward,timeProportional,reason){
  __fm.elapsed=Date.now()-core.ept0;
  return end(reward,timeProportional,reason);
 };
 __fm.reset=(seed)=>{
  const active=document.activeElement;if(active&&active.blur)active.blur();
  __fm.clock=0;__fm.elapsed=0;Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  __fm.refresh();
  return __fm.export();
 };
 __fm.advance=(ms)=>{
  __fm.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
  return true;
 };
 __fm.fieldElements=()=>{
  switch(__fm.task){
   case 'enter-text-dynamic':case 'enter-text-2':case 'text-transform':
    return [document.querySelector('#tt')];
   case 'enter-password':return [document.querySelector('#password'),document.querySelector('#verify')];
   case 'copy-paste':return [document.querySelector('#to-copy'),document.querySelector('#answer-input')];
   case 'copy-paste-2':return [document.querySelector('#text-1'),document.querySelector('#text-2'),document.querySelector('#text-3'),document.querySelector('#answer-input')];
   case 'read-table-2':return [document.querySelector('#tt1'),document.querySelector('#tt2')];
   case 'login-user-popup':return [document.querySelector('#username'),document.querySelector('#password')];
   default:return [];
  }
 };
 __fm.fieldLabel=(e,i)=>{
  if(__fm.task==='enter-password')return i===0?'Password':'Verify password';
  if(__fm.task==='copy-paste')return i===0?'Text to copy':'Answer';
  if(__fm.task==='copy-paste-2')return ['1st text area','2nd text area','3rd text area','Answer'][i];
  if(__fm.task==='read-table-2')return document.querySelector(i===0?'#ll1':'#ll2').getAttribute('data-key')+':';
  if(__fm.task==='login-user-popup')return i===0?'Username':'Password';
  return 'Text';
 };
 __fm.goalValues=()=>{
  const q=document.querySelector('#query').textContent;
  const fields=__fm.fieldElements();
  const quoted=[...q.matchAll(/"([^"]*)"/g)].map(m=>m[1]);
  switch(__fm.task){
   case 'enter-text-dynamic':return [quoted[0]];
   case 'enter-text-2':return [/all upper case/.test(q)?quoted[0].toUpperCase():quoted[0].toLowerCase()];
   case 'enter-password':return [quoted[0],quoted[0]];
   case 'text-transform':return [document.querySelector('#captcha').textContent];
   case 'copy-paste':return [null,fields[0].value];
   case 'copy-paste-2':{
    const wanted=/the (1st|2nd|3rd) text area/.exec(q),index=wanted?['1st','2nd','3rd'].indexOf(wanted[1]):0;
    return [null,null,null,fields[index].value];
   }
   case 'read-table-2':{
    const cells=[...document.querySelectorAll('#tab tr')].map(row=>[row.cells[0].textContent,row.cells[1].textContent]);
    return [0,1].map(i=>{const key=document.querySelector(i===0?'#ll1':'#ll2').getAttribute('data-key');return cells.find(pair=>pair[0]===key)[1];});
   }
   case 'login-user-popup':return [quoted[0].toLowerCase(),quoted[1]];
   default:return fields.map(()=>null);
  }
 };
 __fm.staticNodes=()=>{
  const out=[];
  const add=(role,action,visible,name,value,element)=>out.push({role,action,visible:!!visible,name,value,element});
  if(__fm.task==='text-transform'){
   add(13,0,0,'Captcha',document.querySelector('#captcha').textContent,document.querySelector('#captcha'));
   add(1,1,0,'Submit','',document.querySelector('#subbtn'));
  }else if(__fm.task==='copy-paste'||__fm.task==='copy-paste-2'){
   add(1,1,0,'Submit','',document.querySelector('#subbtn'));
  }else if(__fm.task==='read-table-2'){
   for(const row of document.querySelectorAll('#tab tr')){
    add(10,0,0,row.cells[0].textContent,'',row.cells[0]);
    add(10,0,0,'',row.cells[1].textContent,row.cells[1]);
   }
   add(13,0,0,'Label 1',document.querySelector('#ll1').textContent,document.querySelector('#ll1'));
   add(13,0,0,'Label 2',document.querySelector('#ll2').textContent,document.querySelector('#ll2'));
   add(1,1,0,'Submit','',document.querySelector('#subbtn'));
  }else if(__fm.task==='login-user-popup'){
   add(1,1,0,'OK','',document.querySelector('#subbtn'));
   const popup=document.querySelector('#popup');
   add(13,0,1,'Popup',popup?popup.querySelector('p')?.textContent||'':(__fm.popupMessage||'Your session is about to expire.'),popup);
   add(13,0,1,'Popup prompt',popup?popup.querySelectorAll('p')[1]?.textContent||'':'Exit to home page?',popup);
   add(1,2,1,'Popup OK','OK',popup?popup.querySelector('#popup-ok'):null);
   add(1,3,1,'Popup Cancel','Cancel',popup?popup.querySelector('#popup-cancel'):null);
  }else{
   add(1,1,0,'Submit','',document.querySelector('#subbtn'));
  }
  return out;
 };
 __fm.refresh=()=>{
  const elements=__fm.fieldElements();
  const goals=__fm.goalValues();
  const fields=elements.map((e,i)=>({
   role:e.tagName==='TEXTAREA'?16:3,name:__fm.fieldLabel(e,i),value:e.value,
   goal:goals[i]===null?'':goals[i],start:e.selectionStart||0,end:e.selectionEnd||0,
   enabled:!e.disabled,element:e
  }));
  const statics=__fm.staticNodes();
  __fm.fields=fields;__fm.statics=statics;
  __fm.refs=fields.map(x=>x.element).concat(statics.map(x=>x.element));
  return true;
 };
 __fm.export=()=>{
  __fm.refresh();
  const query=document.querySelector('#query').textContent;
  const goals=__fm.fields.map(f=>f.goal);
  const staticOut=__fm.statics.map(({element,...node})=>({...node,enabled:node.role===1&&!!element&&!element.disabled}));
  return {query,deadline:core.EPISODE_MAX_TIME,popup:!!document.querySelector('#popup'),
   popup_mode:__fm.task==='login-user-popup'?__fm.probedMode||0:0,
   fields:__fm.fields.map(({element,...f})=>f),statics:staticOut,
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,
   elapsed:WOB_DONE_GLOBAL?__fm.elapsed:__fm.clock};
 };
 __fm.probePopupMode=(seed)=>{
  __fm.reset(seed);
  const controls=[document.querySelector('#username'),document.querySelector('#password')];
  let mode=0;controls[0].focus();if(document.querySelector('#popup'))mode=1;
  if(!mode){controls[0].blur();controls[1].focus();if(document.querySelector('#popup'))mode=2;}
  const popup=document.querySelector('#popup');
  __fm.popupMessage=popup?popup.querySelector('p').textContent:'';
  const cancel=document.querySelector('#popup-cancel');if(cancel)cancel.click();
  __fm.reset(seed);__fm.probedMode=mode;return mode;
 };
 __fm.point=(ref)=>{
  const e=__fm.refs[ref-1];if(!e)return null;const r=e.getBoundingClientRect();
  return {x:r.x+r.width/2,y:r.y+r.height/2,visible:!!(r.width&&r.height)&&!!e.getClientRects().length};
 };
 __fm.snapshot=()=>{
  __fm.refresh();return {fields:__fm.fields.map(({element,...f})=>f),popup:!!document.querySelector('#popup'),
   done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,elapsed:__fm.elapsed};
 };
 return true;
})()
