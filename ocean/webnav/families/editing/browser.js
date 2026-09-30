/* Pinned original-page fixture. It imports a problem only at reset; actions
   use the original DOM selection, Quill toolbar, and terminal key handlers. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 const task=location.pathname.split('/').pop().replace(/\.html$/,'');
 window.__editing={task,clock:0,problem:null};
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+__editing.clock]))}
  static now(){return epoch+__editing.clock}
 };
 if(task==='find-word'){
  const original=window.bindClickEvent;
  window.bindClickEvent=function(problem){
   __editing.problem={expectedIndex:problem.expectedIndex,
    expectedWord:problem.expectedWord,text:problem.text};
   return original.apply(this,arguments);
  };
 }
 __editing.reset=seed=>{
  __editing.clock=0;__editing.problem=null;
  Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return __editing.snapshot(true);
 };
 __editing.advance=ms=>{
  __editing.clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
  return true;
 };
 __editing.paragraphs=()=>task==='highlight-text'
  ?[document.querySelector('#randomText')?.textContent||'']
  :[...document.querySelectorAll('#randomText p')].map(p=>p.textContent);
 __editing.styles=()=>{
  const text=editor.getText().replace(/\n$/,'');
  const result=[];
  for(const op of editor.getContents().ops){
   const attrs=op.attributes||{};
   const color=attrs.color;
   let code=(attrs.bold?1:0)+(attrs.italic?2:0)+(attrs.underline?4:0);
   if(color){
    for(const [name,colors] of Object.entries(COLOR_MAP)){
     const index=colors.indexOf(color);
     if(index>=0){
      const family=['red','orange','yellow','green','blue','purple'].indexOf(name);
      code+=8*(family*5+index+1);break;
     }
    }
   }
   for(const ch of op.insert)if(result.length<text.length)result.push(code);
  }
  return result;
 };
 __editing.snapshot=initial=>{
  const result={task,query:document.querySelector('#query').textContent,
   deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,
   raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
  if(task==='find-word'){
   result.paragraph=document.querySelector('#area p').textContent;
   result.input=document.querySelector('#answer-input').value;
   result.focus=document.activeElement===document.querySelector('#answer-input');
  }else if(task==='highlight-text'||task==='highlight-text-2'){
   result.paragraphs=__editing.paragraphs();
   result.selected=window.getSelection().toString();
   result.buttonFirst=document.querySelector('#area').firstElementChild?.id==='subbtn';
  }else if(task==='text-editor'){
   result.body=editor.getText().replace(/\n$/,'');
   result.styles=__editing.styles();
   result.colorOpen=!!document.querySelector('.ql-color.ql-expanded');
   const range=editor.getSelection();
   result.start=range?.index??0;result.end=range?range.index+range.length:0;
  }else{
   result.files=currentFiles.slice();
   result.command=document.querySelector('#active-input').textContent;
   result.outputs=[...document.querySelectorAll('.terminal-output .output')]
    .map(e=>e.textContent);
   result.focus=document.activeElement===document.querySelector('#terminal-target');
  }
  if(initial)result.problem=__editing.problem;
  return result;
 };
 __editing.nodes=()=>{
  if(task==='highlight-text')return [document.querySelector('#randomText').firstChild];
  return [...document.querySelectorAll('#randomText p')].map(e=>e.firstChild);
 };
 __editing.select=(start,end)=>{
  if(task==='text-editor'){
   editor.setSelection(start,end-start,'user');return __editing.snapshot();
  }
  const nodes=__editing.nodes();
  const locate=position=>{
   let n=position;
   for(const node of nodes){
    if(n<=node.textContent.length)return [node,n];
    n-=node.textContent.length+1;
   }
   return [nodes[nodes.length-1],nodes[nodes.length-1].textContent.length];
  };
  const [first,a]=locate(start),[last,b]=locate(end);
  const range=document.createRange();range.setStart(first,a);range.setEnd(last,b);
  const selection=window.getSelection();selection.removeAllRanges();selection.addRange(range);
  return __editing.snapshot();
 };
 __editing.element=ref=>{
  if(task==='find-word')return ref===1?document.querySelector('#answer-input'):
   ref===2?document.querySelector('#subbtn'):null;
  if(task==='highlight-text'||task==='highlight-text-2')
   return ref===1?document.querySelector('#subbtn'):null;
  if(task==='text-editor'){
   if(ref===1)return document.querySelector('#subbtn');
   if(ref===5)return document.querySelector('.ql-color .ql-picker-label');
   if(ref>=2&&ref<=4)return document.querySelector(
    ['.ql-bold','.ql-italic','.ql-underline'][ref-2]);
   if(ref>=20&&ref<=49){
    const family=Math.floor((ref-20)/5),shade=(ref-20)%5;
    const name=['red','orange','yellow','green','blue','purple'][family];
    return document.querySelector('.ql-color .ql-picker-item[data-value="'+
     COLOR_MAP[name][shade]+'"]');
   }
   return null;
  }
  return ref===1?document.querySelector('#terminal'):null;
 };
 __editing.point=ref=>{
  const e=__editing.element(ref);
  if(!e)return {visible:false,reason:'missing element',ref};
  e.scrollIntoView({block:'center',inline:'center'});
  const r=e.getBoundingClientRect(),s=getComputedStyle(e);
  let point=null,midHit=null;
  const mid=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
  if(mid)midHit=mid.outerHTML?.slice(0,180)||mid.nodeName;
  for(const rect of e.getClientRects()){
   if(!rect.width||!rect.height)continue;
   for(const fy of [.5,.2,.8])for(const fx of [.5,.2,.8]){
    const x=rect.x+rect.width*fx,y=rect.y+rect.height*fy;
    const hit=document.elementFromPoint(x,y);
    if(!point&&hit&&(hit===e||e.contains(hit)))point={x,y};
   }
  }
  return {visible:!!point&&s.display!=='none'&&s.visibility!=='hidden',
   x:point?.x,y:point?.y,width:r.width,height:r.height,
   tag:e.tagName,classes:e.className,midHit,
   outer:e.outerHTML.slice(0,180),readyState:document.readyState};
 };
 return true;
})()
