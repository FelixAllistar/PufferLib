/* Pinned original-page fixture. All probes happen at reset with a restored
 * seed. After reset, only the original HTML handles Search, Back and Book. */
(()=>{
 const NativeDate=Date;
 const tr=window.__tr={clock:0,elapsed:0,
   task:location.pathname.split('/').pop().replace(/\.html$/,''),sampled:[]};
 window.Date=class extends NativeDate{
   constructor(...args){super(...(args.length?args:[tr.clock]))}
   static now(){return tr.clock}
 };
 const sample=core.sample;
 core.sample=function(values){
   const picked=sample.apply(this,arguments);
   if(typeof DOMESTIC_FLIGHTS!=='undefined'&&values===DOMESTIC_FLIGHTS)
     tr.sampled.push(picked);
   return picked;
 };
 const end=core.endEpisode;
 core.endEpisode=function(reward,timeProportional,reason){
   tr.elapsed=Date.now()-core.ept0;
   return end(reward,timeProportional,reason);
 };
 tr.once=seed=>{
   const active=document.activeElement;if(active&&active.blur)active.blur();
   tr.clock=0;tr.elapsed=0;tr.sampled=[];
   /* The original reset has a typo in removeClass('.error'); this gives
    * every matched instance a clean page before source genProblem runs. */
   $('.error').removeClass('error');
   Math.seedrandom(String(seed));core.startEpisodeReal();
   clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();
   return tr.snapshot();
 };
 tr.flightRows=()=>[...document.querySelectorAll('#results .flight')].map(e=>({
   price:Number(e.querySelector('.flight-price').getAttribute('data-price')),
   duration:Number(e.querySelector('.time-duration').getAttribute('data-duration'))
 }));
 tr.ticketRows=()=>[...document.querySelectorAll('#area .flight')].map(e=>({
   price:Number(e.querySelector('.buy-ticket').getAttribute('data-price')),
   duration:Number(e.querySelector('.time-duration').getAttribute('data-duration'))
 }));
 tr.snapshot=()=>{
   const ticket=tr.task==='buy-ticket';
   return {query:document.querySelector('#query').textContent,
     deadline:core.EPISODE_MAX_TIME,
     origin:ticket?'':$('#flight-from').val(),
     destination:ticket?'':$('#flight-to').val(),
     date:ticket?'':$('#datepicker').val(),
     phase:ticket?1:!$('#results').hasClass('hide')?1:0,
     errors:ticket?0:($('#flight-from').hasClass('error')?1:0)+
       ($('#flight-to').hasClass('error')?2:0)+
       ($('#datepicker').hasClass('error')?4:0),
     flights:ticket?tr.ticketRows():tr.flightRows(),
     done:!!WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,
     reward:WOB_REWARD_GLOBAL,elapsed:WOB_DONE_GLOBAL?tr.elapsed:tr.clock};
 };
 tr.reset=seed=>{
   if(tr.task==='buy-ticket')return {...tr.once(seed),
     fixture:{tickets:tr.ticketRows()}};
   const first=tr.once(seed);
   const airports=[...new Set([...tr.sampled,...DOMESTIC_FLIGHTS])].slice(0,6);
   if(tr.sampled.length!==2||airports.length!==6)throw Error('airport samples');
   const [origin,destination]=tr.sampled;
   const date=/ on (\d\d\/\d\d\/2016)\./.exec(first.query)?.[1];
   if(!date)throw Error('flight date');
   tr.catalog=airports;tr.goalDate=date;
   $('#flight-from').val(origin);$('#flight-to').val(destination);
   $('#datepicker').val(date);$('#search').click();
   const real=tr.flightRows();
   if(real.length<3||real.length>4)throw Error('real flight count');
   tr.once(seed);
   const requestedOrigin=/ from: (.*?) to: /.exec(first.query)?.[1];
   const wrong=airports.find(x=>x!==origin&&
     requestedOrigin&&x.indexOf(requestedOrigin)===-1);
   if(!wrong)throw Error('no distinct wrong airport');
   $('#flight-from').val(wrong);$('#flight-to').val(destination);
   $('#datepicker').val(date);$('#search').click();
   const fake=tr.flightRows();
   if(fake.length<3||fake.length>4)throw Error('fake flight count');
   const restored=tr.once(seed);
   if(restored.query!==first.query)throw Error('seed restore');
   tr.catalog=airports;tr.goalDate=date;
   return {...restored,fixture:{airports,origin,destination,real,fake,wrong}};
 };
 tr.select=(field,value)=>{
   const ids=['#flight-from','#flight-to','#datepicker'];
   if(field<1||field>3)throw Error('field');
   $(ids[field-1]).val(value);
   return tr.snapshot();
 };
 tr.search=()=>{$('#search').click();return tr.snapshot()};
 tr.back=()=>{$('#menu-back').click();return tr.snapshot()};
 tr.book=index=>{
   const ticket=tr.task==='buy-ticket';
   const buttons=document.querySelectorAll(ticket?'#area .buy-ticket':'#results .flight-price');
   if(!buttons[index])throw Error('book index');
   buttons[index].click();return tr.snapshot();
 };
 tr.advance=ms=>{
   tr.clock=ms;
   if(!WOB_DONE_GLOBAL&&ms>=core.EPISODE_MAX_TIME)
     core.endEpisode(-1,false,'timed out');
   return tr.snapshot();
 };
 return true;
})()
