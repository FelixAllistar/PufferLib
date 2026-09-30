/* Public DOM projection for a learned policy on the pinned original pages.
 * The policy receives only snapshot().nodes, instruction, and its own visits. */
(()=>{
 const NativeDate=Date,epoch=NativeDate.UTC(2026,0,1);
 const task=location.pathname.split('/').pop().replace(/\.html$/,'');
 let clock=0;
 window.Date=class extends NativeDate{
  constructor(...args){super(...(args.length?args:[epoch+clock]))}
  static now(){return epoch+clock}
 };
 const V=1,E=2,C=4,F=16,S=128;
 const BUTTON=1,INPUT=3,LINK=4,TEXT=13;
 const node=(ref,parent,role,flags,name,value)=>({ref,parent,role,flags,name:String(name||''),value:String(value||''),start:0,end:0});
 const page=()=>Number(document.querySelector('#pagination li.active')?.textContent.trim()||0);
 const browser={};
 browser.reset=seed=>{
  clock=0;Math.seedrandom(String(seed));core.startEpisodeReal();
  clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
  return browser.snapshot();
 };
 browser.tick=ms=>{
  clock=ms;
  if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
   core.endEpisode(-1,false,'timed out');
  return true;
 };
 browser.snapshot=()=>{
  const nodes=[],instruction=document.querySelector('#query')?.textContent||'';
  if(task==='phone-book'){
   const p=page();
   for(const n of [p-1,p,p+1])if(n>=1&&n<=5)
    nodes.push(node(n,0,BUTTON,V+(n===p?S:E+C),'Page '+n,''));
   nodes.push(node(99,0,TEXT,V,document.querySelector('#contact .name')?.textContent||'',''));
   for(const [i,label] of ['Phone','Email','Address'].entries()){
    const value=document.querySelector('#contact a.'+label.toLowerCase())?.textContent||'';
    nodes.push(node(100+i,0,LINK,V+E+C,label,value));
   }
  }else if(task==='order-food'){
   for(const [i,e] of [...document.querySelectorAll('.food-item')].entries()){
    const parent=16*(i+1)+2;
    nodes.push(node(parent,0,TEXT,V,e.dataset.item||'',e.dataset.quantity||'0'));
    nodes.push(node(parent-2,0,BUTTON,V+E+C,'Remove',''));
    nodes.push(node(parent-1,0,BUTTON,V+E+C,'Add',''));
    for(const img of e.querySelectorAll('.types img')){
     const bit=['dairy','gluten-free','meat','peanuts','vegan'].indexOf(img.alt);
     if(bit>=0)nodes.push(node(200+i*5+bit,parent,TEXT,V,img.alt,''));
    }
   }
   nodes.push(node(1,0,BUTTON,V+E+C,'Order!',''));
  }else{
   const input=document.querySelector('#search-text');
   const n=node(1,0,INPUT,V+E+C+(document.activeElement===input?F:0),
                'Search text',input?.value||'');
   n.start=input?.selectionStart||0;n.end=input?.selectionEnd||0;nodes.push(n);
   nodes.push(node(2,0,BUTTON,V+E+C,'Search',''));
   const p=page();
   if(p){
    for(let i=1;i<=3;i++)nodes.push(node(9+i,0,BUTTON,V+(i===p?S:E+C),'Page '+i,''));
    for(const [slot,e] of [...document.querySelectorAll('#page-content .search-title')].entries()){
     const index=Number(e.dataset.result),ref=index>=0?100+index:200+slot;
     const result=e.closest('.search-result')||e.parentElement;
     const url=result?.querySelector('.search-url')?.textContent||'';
     const desc=result?.querySelector('.search-desc')?.textContent||'';
     nodes.push(node(ref,0,LINK,V+E+C,e.textContent||'',url));
     nodes.push(node(index>=0?300+index:400+slot,ref,TEXT,V,'Description',desc));
    }
   }
  }
  return {instruction,deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,
          raw:WOB_RAW_REWARD_GLOBAL,nodes};
 };
 browser.element=ref=>{
  if(task==='phone-book'){
   if(ref>=1&&ref<=5){
    const p=page();
    return document.querySelector(ref===p-1?'#pagination li.prev a':
      ref===p+1?'#pagination li.next a':'.wfc-missing');
   }
   return document.querySelector('#contact a.'+(['phone','email','address'][ref-100]||'missing'));
  }
  if(task==='order-food'){
   if(ref===1)return document.querySelector('#submit-order button');
   const i=Math.floor(ref/16)-1,slot=ref%16;
   const e=document.querySelector('.food-item[data-id="'+i+'"]');
   return e?.querySelector(slot===0?'.remove':slot===1?'.add':'.wfc-missing')||null;
  }
  if(ref===1)return document.querySelector('#search-text');
  if(ref===2)return document.querySelector('#search');
  if(ref>=10&&ref<=12)return [...document.querySelectorAll('#pagination li.page-item a')]
    .find(e=>e.textContent.trim()===String(ref-9))||null;
  if(ref>=100&&ref<=108)return document.querySelector('#page-content a[data-result="'+(ref-100)+'"]');
  if(ref>=200&&ref<=202)return document.querySelectorAll('#page-content a[data-result="-1"]')[ref-200]||null;
  return null;
 };
 browser.point=ref=>{
  const e=browser.element(ref);if(!e)return {visible:false};
  e.scrollIntoView({block:'center',inline:'center'});
  const s=getComputedStyle(e);let point=null;
  for(const r of e.getClientRects()){
   if(!r.width||!r.height)continue;
   for(const fy of [.5,.2,.8])for(const fx of [.5,.2,.8]){
    const x=r.x+r.width*fx,y=r.y+r.height*fy,hit=document.elementFromPoint(x,y);
    if(!point&&hit&&(hit===e||e.contains(hit)))point={x,y};
   }
  }
  return {visible:!!point&&s.display!=='none'&&s.visibility!=='hidden',
          x:point?.x,y:point?.y};
 };
 window.__wfc=browser;return true;
})()
