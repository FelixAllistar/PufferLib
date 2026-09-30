/* Original-page learned evaluation. Only visible DOM data enters snapshot(). */
(()=>{
  const RealDate=Date;
  const N=window.__numppo={task:0,clock:0};
  window.Date=class extends RealDate {
    constructor(...args){super(...(args.length?args:[N.clock]));}
    static now(){return N.clock;}
  };
  N.controls=()=>{
    const d=document,task=N.task;
    const el=(selector)=>d.querySelector(selector);
    const node=(ref,element,role,name,value,extra={})=>({ref,element,role,name,value,...extra});
    if(task===0){
      const texts=[...d.querySelectorAll('svg text')];
      const rects=[...d.querySelectorAll('svg rect[data-index]')];
      if(texts.length)return texts.map(e=>node(+e.textContent,e,1,e.textContent,e.textContent));
      return rects.map(e=>node(+e.getAttribute('data-index'),e,1,'',''));
    }
    if(task===1)return [
      ...[...d.querySelectorAll('#cardholder .card')].map((e,i)=>
        node(i+1,e,1,'Card '+(i+1),e.classList.contains('hidden')?'':e.querySelector('.card-value').textContent,
             {selected:!e.classList.contains('hidden')})),
      node(4,el('#submit'),1,'Submit','')];
    if(task===2)return [node(1,el('#generate'),1,'Generate',''),
      node(2,el('#submit'),1,'Submit',''),
      node(3,el('#display-number'),13,'Generated number',el('#display-number').textContent.trim()||'-')];
    if(task===3){const feedback=el('#feedback div:not(.hide)');return [
      node(1,el('#tt'),3,'Guess',el('#tt').value),node(2,el('#subbtn'),1,'Submit',''),
      node(3,feedback||el('#feedback'),13,'Feedback',feedback?feedback.textContent.trim():'')];}
    if(task===4)return [node(1,el('#touch-area'),15,'Touch area',''),
      node(2,el('#display'),13,'Temperature',el('#display').textContent.trim())];
    if(task===5)return [
      ...[...d.querySelectorAll('#checkboxes input[type=checkbox]')].map((e,i)=>
        node(i+1,e,2,'Row '+(Math.floor(i/4)+1)+' col '+(i%4+1),'',{checked:e.checked})),
      node(29,el('#subbtn'),1,'Submit','')];
    if(task===6){const xs=[];const rows=[...d.querySelectorAll('#numbers .row')];
      rows.forEach((row,i)=>{const number=row.querySelector('.display-number').textContent.trim();
        xs.push(node(2*i+1,row.querySelector('.odd'),1,'Odd '+number,'',
                     {selected:row.querySelector('.odd').classList.contains('selected')}));
        xs.push(node(2*i+2,row.querySelector('.even'),1,'Even '+number,'',
                     {selected:row.querySelector('.even').classList.contains('selected')}));});
      xs.push(node(7,el('#submit'),1,'Submit',''));return xs;}
    return [node(1,el('#math-answer'),3,'Answer',el('#math-answer').value),
      node(2,el('#subbtn'),1,'Submit',''),
      node(3,el('#math-problem'),13,'Problem',el('#math-problem').textContent.trim())];
  };
  N.snapshot=()=>({
    instruction:document.querySelector('#query').textContent.trim(),
    deadline:core.EPISODE_MAX_TIME,
    nodes:N.controls().filter(n=>n.element).map(n=>{
      const r=n.element.getBoundingClientRect();
      let selectionStart=0,selectionEnd=0;
      if(n.role===3)try{selectionStart=n.element.selectionStart||0;
                          selectionEnd=n.element.selectionEnd||0;}catch(_){}
      return {ref:n.ref,role:n.role,name:n.name,value:n.value,
        flags:1|2|4|(n.checked?8:0)|(n.selected?128:0),
        x:r.x,y:r.y,width:r.width,height:r.height,
        selectionStart,selectionEnd,capacity:n.role===3?23:0};
    }),
    done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL
  });
  N.reset=(seed,task)=>{
    N.task=task;N.clock=0;WOB_DATA_MODE='test';Math.seedrandom(String(seed));
    core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
    return N.snapshot();
  };
  N.tick=ms=>{N.clock=ms;
    if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out');
    return true;
  };
  N.point=ref=>{const n=N.controls().find(n=>n.ref===ref);if(!n)return null;
    const r=n.element.getBoundingClientRect();return {x:r.left+r.width/2,y:r.top+r.height/2};};
  N.focus=ref=>{const n=N.controls().find(n=>n.ref===ref&&n.role===3);
    if(!n)return false;n.element.focus();return true;};
  return true;
})();
