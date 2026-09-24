(()=>{
 const RealDate=Date;window.__num={clock:0,task:0,seed:0,randoms:[],forced:[]};
 const N=window.__num;
 const realRandi=core.randi;
 window.Date=class extends RealDate{constructor(...args){super(...(args.length?args:[N.clock]))}static now(){return N.clock}};
 N.reset=(seed,task)=>{
  N.clock=0;N.seed=seed;N.task=task;N.randoms=[];N.forced=[];N.selectedInput=false;
  core.randi=(min,max)=>{
   if(task===2&&min===0&&max===11&&N.forced.length){const value=N.forced.shift();N.randoms.push({min,max,value,forced:true});return value;}
   const value=realRandi(min,max);N.randoms.push({min,max,value});return value;
  };
  WOB_DATA_MODE='test';Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return N.instance();
 };
 N.forceCandidate=value=>{N.forced.push(value);return true};
 N.controls=()=>{
  const d=document;
  if(N.task===0){const texts=[...d.querySelectorAll('svg text')];if(texts.length)return texts.sort((a,b)=>+a.textContent-+b.textContent);return [...d.querySelectorAll('svg rect[data-index]')].sort((a,b)=>+a.getAttribute('data-index')-+b.getAttribute('data-index'));}
  if(N.task===1)return [...d.querySelectorAll('#cardholder .card'),d.querySelector('#submit')];
  if(N.task===2)return [d.querySelector('#generate'),d.querySelector('#submit'),d.querySelector('#display-number')];
  if(N.task===3)return [d.querySelector('#tt'),d.querySelector('#subbtn'),d.querySelector('#feedback div:not(.hide)')];
  if(N.task===4)return [d.querySelector('#touch-area'),d.querySelector('#display')];
  if(N.task===5)return [...d.querySelectorAll('#checkboxes input[type=checkbox]'),d.querySelector('#subbtn')];
  if(N.task===6){const xs=[];for(const row of d.querySelectorAll('#numbers .row'))xs.push(row.querySelector('.odd'),row.querySelector('.even'));return [...xs,d.querySelector('#submit')];}
  if(N.task===7||N.task===8)return [d.querySelector('#math-answer'),d.querySelector('#subbtn'),d.querySelector('#math-problem')];
  return [];
 };
 N.instance=()=>{
  const d=document,task=N.task,nodes=[];let query=d.querySelector('#query').textContent.trim(),phase=0,cursor=0,secret=0,limit=0,mode=0,expected=0,candidate=0,feedbackCode=0;
  const base=(kind,name,value,el,target=false,number=0)=>{const r=el.getBoundingClientRect();return {kind,name,value:value||'',target,number,selected:!!el.checked||el.classList.contains('selected')||(kind===5&&!el.classList.contains('hidden')),visible:true,x:Math.round(r.x),y:Math.round(r.y),width:Math.round(r.width),height:Math.round(r.height)}};
  if(task===0){
   const xs=[...d.querySelectorAll('svg text')].sort((a,b)=>+a.textContent-+b.textContent),rects=[...d.querySelectorAll('svg rect[data-index]')].sort((a,b)=>+a.getAttribute('data-index')-+b.getAttribute('data-index'));phase=xs.length?0:1;
   cursor=phase?(rects.length?+rects[0].getAttribute('data-index')-1:4):0;
   for(let i=0;i<5;i++){const el=xs[i]||d.querySelector('svg rect[data-index="'+(i+1)+'"]')||d.querySelector('svg'),node=base(0,phase?'':String(i+1),phase?'':String(i+1),el,false,i+1);node.visible=!phase||i>=cursor;nodes.push(node);}
  }else if(task===1){
   const cards=[...d.querySelectorAll('#cardholder .card')],nums=cards.map(e=>+e.querySelector('.card-value').textContent),max=Math.max(...nums);
   for(let i=0;i<3;i++)nodes.push(base(5,'Card '+(i+1),cards[i].classList.contains('hidden')?'':String(nums[i]),cards[i],nums[i]===max,nums[i]));
   nodes.push(base(0,'Submit','',d.querySelector('#submit')));
  }else if(task===2){
   const q=query;let m=q.match(/less than (\d+)/);if(m){mode=0;limit=+m[1];}else if((m=q.match(/greater than (\d+)/))){mode=1;limit=+m[1];}else mode=q.includes('odd number')?2:3;
   const shown=d.querySelector('#display-number').textContent.trim();phase=shown&&shown!=='-'?1:0;candidate=phase?parseInt(shown,10):0;
   nodes.push(base(0,'Generate','',d.querySelector('#generate')));
   nodes.push(base(0,'Submit','',d.querySelector('#submit')));
   nodes.push(base(4,'Generated number',d.querySelector('#display-number').textContent||'-',d.querySelector('#display-number'),false,+d.querySelector('#display-number').textContent||0));
  }else if(task===3){
   const call=N.randoms.find(x=>x.min===0&&x.max===10);secret=call?call.value:0;
   nodes.push(base(2,'Guess',d.querySelector('#tt').value,d.querySelector('#tt')));
   nodes.push(base(0,'Submit','',d.querySelector('#subbtn')));
   const feedback=d.querySelector('#feedback div:not(.hide)');feedbackCode=feedback&&feedback.id==='higher'?1:feedback&&feedback.id==='lower'?2:feedback&&feedback.id==='correct'?3:0;
   nodes.push(base(4,'Feedback',feedback?feedback.textContent.trim():'',feedback||d.querySelector('#feedback')));
  }else if(task===4){
   const x=N.randoms.find(v=>v.min===5&&v.max===155),y=N.randoms.find(v=>v.min===5&&v.max===120);secret=x?x.value:5;limit=y?y.value:5;
   nodes.push(base(3,'Touch area','',d.querySelector('#touch-area')));nodes.push(base(4,'Temperature',d.querySelector('#display').textContent.trim(),d.querySelector('#display')));
  }else if(task===5){
   const m=query.match(/number "(\d+)"/),digit=m?+m[1]:0,pattern=window.PATTERNS[digit];
   for(let i=0;i<28;i++)nodes.push(base(1,'Row '+(Math.floor(i/4)+1)+' col '+(i%4+1),'',d.querySelectorAll('#checkboxes input[type=checkbox]')[i],!!pattern[Math.floor(i/4)][i%4]));
   nodes.push(base(0,'Submit','',d.querySelector('#subbtn')));
   secret=digit;
  }else if(task===6){
   const rows=[...d.querySelectorAll('#numbers .row')];
   for(let i=0;i<rows.length;i++){
    const value=+rows[i].querySelector('.display-number').textContent,odd=(Math.abs(value)%2)===1;
    const o=rows[i].querySelector('.odd'),e=rows[i].querySelector('.even');
    nodes.push(base(0,'Odd '+value,'',o,odd));nodes.push(base(0,'Even '+value,'',e,!odd));
   }
   nodes.push(base(0,'Submit','',d.querySelector('#submit')));
  }else if(task===7||task===8){
   const problem=d.querySelector('#math-problem').textContent.trim();
   const xfirst=problem.match(/^x\s*([+-])\s*(\d+)\s*=\s*(-?\d+)$/),right=problem.match(/^(\d+)\s*([+-x])\s*(\d+)\s*=$/),left=problem.match(/^(\d+)\s*([+-])\s*x\s*=\s*(-?\d+)$/);
   if(xfirst){const a=+xfirst[2],b=+xfirst[3];expected=xfirst[1]==='+'?b-a:b+a;}
   else if(left){const a=+left[1],b=+left[3];expected=left[2]==='+'?b-a:a-b;}
   else if(right){const a=+right[1],b=+right[3];expected=right[2]==='-'?a-b:right[2]==='+'?a+b:a*b;}
   nodes.push(base(2,'Answer',d.querySelector('#math-answer').value,d.querySelector('#math-answer')));
   nodes.push(base(0,'Submit','',d.querySelector('#subbtn')));
   nodes.push(base(4,'Problem',problem,d.querySelector('#math-problem')));
  }
  return {task,query,deadline:core.EPISODE_MAX_TIME,phase,cursor,selected:0,secret,limit,mode,expected,candidate,feedback:feedbackCode,nodes};
 };
 N.point=ref=>{
  if(N.task===0){
   const text=[...document.querySelectorAll('svg text')].find(e=>+e.textContent===ref);
   const el=text||document.querySelector('svg rect[data-index="'+ref+'"]');
   if(!el)return null;const r=el.getBoundingClientRect();return {x:r.left+r.width/2,y:r.top+r.height/2,arg0:0,arg1:0};
  }
  const el=N.controls()[ref-1];if(!el)return null;const r=el.getBoundingClientRect();
  const x=r.left+r.width/2,y=r.top+r.height/2;
  let arg0=0,arg1=0;if(N.task===4){arg0=Math.round(x);arg1=Math.round(y-50);}
  return {x,y,arg0,arg1};
 };
 N.setInput=(ref,text)=>{
  const el=N.controls()[ref-1];if(!el)return false;
  el.value=N.selectedInput?text:(el.value+text);N.selectedInput=false;
  el.dispatchEvent(new Event('input',{bubbles:true}));return true;
 };
 N.edit=(kind,ref,text)=>{
  const el=N.controls()[ref-1];if(!el)return false;
  if(kind===2)return N.setInput(ref,text||'');
  if(kind===9){N.selectedInput=true;return true;}
  if(kind===3){el.value=el.value.slice(0,-1);el.dispatchEvent(new Event('input',{bubbles:true}));return true;}
  if(kind===4){el.value='';el.dispatchEvent(new Event('input',{bubbles:true}));return true;}
  if(kind===7||kind===8||kind===5||kind===6)return true;
  return false;
 };
 N.move=(x,y)=>{const e=document.querySelector('#touch-area');if(!e)return false;e.dispatchEvent(new MouseEvent('mousemove',{bubbles:true,clientX:x,clientY:y+50}));return true;};
 N.tap=(x,y)=>{const e=document.querySelector('#touch-area');if(!e)return false;e.dispatchEvent(new MouseEvent('click',{bubbles:true,clientX:x,clientY:y+50}));return true;};
 N.tick=ms=>{N.clock=ms;if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');return true;};
 N.snapshot=()=>({done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL});
 return true;
})();
