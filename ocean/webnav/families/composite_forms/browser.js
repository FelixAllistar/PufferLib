/* Matched original instances. Browser events run the pinned page's handlers;
 * snapshots never write model state after an action. */
(()=>{
  const NativeDate=Date;
  window.__cf={clock:0};
  window.Date=class extends NativeDate{
    constructor(...args){super(...(args.length?args:[__cf.clock]));}
    static now(){return __cf.clock;}
  };
  __cf.task=location.pathname.split('/').pop().replace(/\.html$/,'');
  __cf.kind=['form-sequence','form-sequence-2','form-sequence-3',
    'multi-layouts','multi-orderings'].indexOf(__cf.task);
  __cf.movieFields=()=>{
    const entries=[...document.querySelectorAll('#area input[type=text]')];
    const result=[null,null,null],order=[];
    for(const entry of entries){
      const row=entry.closest('p,.row,tr,.field,.ui-entry');
      const label=(row||entry.parentElement).textContent;
      const logical=/Genre/i.test(label)?0:/Director/i.test(label)?1:2;
      result[logical]=entry;order.push(logical);
    }
    const permutations=['012','021','102','120','201','210'];
    return {fields:result,order:permutations.indexOf(order.join(''))};
  };
  __cf.layout=()=>{
    if(__cf.kind===4)return 2;
    const area=document.querySelector('#area');
    return area.querySelector('.ui-entry-wrap')?4:
      area.querySelector('.field')?3:area.querySelector('table')?2:
      area.querySelector('.row')?1:0;
  };
  __cf.goals=()=>{
    const query=document.querySelector('#query').textContent;
    if(__cf.kind===0){
      const m=/Select (-?\d+) with the slider, click the (\d)/.exec(query);
      return {requested:Number(m[1])+10,secondary:Number(m[2]),fields:[]};
    }
    if(__cf.kind===1){
      const m=/Check the (\d).*radio button and enter the number "([^"]*)" into the (\d)/.exec(query);
      const fields=['','',''];fields[Number(m[3])-1]=m[2];
      return {requested:Number(m[1]),secondary:Number(m[3]),fields};
    }
    if(__cf.kind===2){
      const m=/Choose (.*?) from the dropdown, then click the button labeled "(.*?)"/.exec(query);
      const options=['','5ft 9in','5ft 10in','5ft 11in','6 ft','6ft 1in','6ft 2in'];
      return {requested:options.indexOf(m[1]),secondary:['','Yes','No','Maybe'].indexOf(m[2]),fields:[]};
    }
    const m=/Search for (.*?) movies directed by (.*?) from year (\d+)/.exec(query);
    return {requested:0,secondary:0,fields:[m[1],m[2],m[3]]};
  };
  __cf.snapshot=()=>{
    const query=document.querySelector('#query').textContent;
    const goals=__cf.goals();let selected=0,checked=0,width=0,layout=0,order=0,values=[];
    if(__cf.kind===0){
      selected=$('#slider').slider('value')+10;
      checked=[1,2,3].reduce((mask,i)=>mask|($('#checkbox-'+i).prop('checked')?1<<(i-1):0),0);
      width=parseInt(document.querySelector('#slider').style.width,10);
    }else if(__cf.kind===1){
      selected=Number(document.querySelector('input[type=radio]:checked')?.value||0);
      values=[1,2,3].map(i=>document.querySelector('#input-'+i).value);
    }else if(__cf.kind===2){
      selected=[...document.querySelector('#dropdown').options].findIndex(o=>o.value===document.querySelector('#dropdown').value);
    }else{
      const movie=__cf.movieFields();values=movie.fields.map(e=>e.value);
      layout=__cf.layout();order=movie.order;
    }
    return {query,selected,checked,width,layout,order,values,
      requested:goals.requested,secondary:goals.secondary,goals:goals.fields,
      deadline:core.EPISODE_MAX_TIME,done:!!WOB_DONE_GLOBAL,
      raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL};
  };
  __cf.reset=seed=>{
    __cf.clock=0;Math.seedrandom(String(seed));core.startEpisodeReal();
    clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
    return __cf.snapshot();
  };
  __cf.advance=ms=>{
    __cf.clock=ms;
    if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
      core.endEpisode(-1,false,'timed out');
  };
  __cf.apply=(kind,target,arg0,text,ms)=>{
    __cf.advance(ms);
    if(WOB_DONE_GLOBAL)return __cf.snapshot();
    if(kind===21){
      if(__cf.kind===0)$('#slider').slider('value',arg0-10);
      else if(__cf.kind===2)$('#dropdown').val(document.querySelector('#dropdown').options[arg0].value).trigger('change');
    }else if(kind===1){
      let element=null;
      if(__cf.kind===0)element=target===5?document.querySelector('#subbtn'):document.querySelector('#checkbox-'+(target-1));
      else if(__cf.kind===1)element=target===7?document.querySelector('#subbtn'):document.querySelectorAll('input[type=radio]')[target-1];
      else if(__cf.kind===2)element=document.querySelectorAll('#buttons button')[target-2];
      else element=document.querySelector('#area button,#area .final,#area .ui-submit');
      element.click();
    }else if(kind===2){
      const element=__cf.kind===1?document.querySelector('#input-'+(target-3)):
        __cf.movieFields().fields[target-1];
      element.value=text;element.dispatchEvent(new Event('input',{bubbles:true}));
      element.dispatchEvent(new Event('change',{bubbles:true}));
    }
    return __cf.snapshot();
  };
  return true;
})()
