// Presentation only. The worker remains the authority for every chip and card.
import {awardFor} from './showdown.js';
const $=id=>document.getElementById(id);
const ranks='23456789TJQKA',suits=['♣','♦','♥','♠'];
const colors=['#00bbaa','#885ac4','#c44b36','#4e87bc','#b4762f','#638054','#9e5d94','#4c8c94','#8170b7'];
const row=(s,p)=>s.slice(24+9*p,33+9*p);
const fmt=n=>(n/100).toLocaleString('en-US',{maximumFractionDigits:2});
const reduced=()=>matchMedia('(prefers-reduced-motion: reduce)').matches;
export function card(id){
  const el=document.createElement('span');el.className='card';el.dataset.card=id;
  if(id===52){el.classList.add('back');el.setAttribute('aria-label','Face-down card');return el;}
  const raw=ranks[id>>2],rank=raw==='T'?'10':raw,suit=suits[id&3];
  if([1,2].includes(id&3))el.classList.add('red');
  el.setAttribute('aria-label',`${rank}${suit}`);
  el.innerHTML=`<span class="corner">${rank}</span><span class="suit">${suit}</span>`;
  return el;
}
function syncCards(node,ids,mask,focus,deal){
  ids.forEach((id,i)=>{
    let el=node.children[i];
    if(!el||el.dataset.card!==String(id)){
      const next=id===null?Object.assign(document.createElement('span'),{className:'card empty'}):card(id);
      next.dataset.card=String(id);if(id===null)next.setAttribute('aria-hidden','true');
      if(el)el.replaceWith(next);else node.append(next);el=next;
      if(id!==null&&deal&&!reduced())el.animate([{opacity:0,transform:'translateY(-18px) scale(.94)'},{opacity:1,transform:'none'}],{duration:280,delay:i*45,easing:'ease-out'});
    }
    el.classList.toggle('best',focus&&!!(mask&(1<<i)));
    el.classList.toggle('not-best',focus&&id!==null&&!(mask&(1<<i)));
  });
}
// Coordinates anchor the stack, not the changing card/status bounding box.
function location(p,n,mobile){
  if(p===0)return [50,mobile?88:87];
  if(n===2)return [50,17];
  if(n===6)return [[50,87],[15.5,mobile?75:65],[15.5,28],[50,16.5],[84.5,28],[84.5,mobile?75:65]][p];
  const a=Math.PI/2+2*Math.PI*p/n;
  return [50+(mobile?39:38)*Math.cos(a),49+33*Math.sin(a)];
}
export class TableView{
  constructor(){
    this.nodes=[];this.cues=new Map();this.pending=null;this.version='';this.frame=0;this.resultTimers=[];
    $('stage').insertAdjacentHTML('beforeend','<div class="bets" id="bets"></div><div class="table-fx" id="table-fx" aria-hidden="true"></div>');
    new ResizeObserver(()=>this.layout()).observe($('stage'));
  }
  noteAction(p,kind,amount,verb){this.pending={p,kind,amount,verb};}
  reset(){
    this.cues.forEach(t=>clearTimeout(t));this.cues.clear();this.resultTimers.forEach(clearTimeout);this.resultTimers=[];
    $('table-fx').replaceChildren();this.pending=null;
    this.nodes=Array.from({length:this.players},(_,p)=>{
      const el=document.createElement('div');el.dataset.seat=p;el.className=`seat${p===0?' hero':''}`;
      el.style.setProperty('--seat-color',colors[p]);
      el.innerHTML='<div class="seat-cards"></div><span class="dealer" title="Dealer button">D</span><div class="seat-info"><span class="seat-number"></span><div class="seat-details"><div class="seat-stack"></div><div class="seat-name"></div></div><span class="position-tag"></span></div><div class="seat-cue" aria-hidden="true"></div>';
      return el;
    });$('seats').replaceChildren(...this.nodes);$('bets').replaceChildren();
  }
  render(s,{previous,label,position,focus=false}){
    const newHand=!this.s||this.s[15]!==s[15]||this.players!==s[0];
    const version=`${s[15]}:${s[8]}:${s[1]}:${s[2]}`,changed=this.version!==version;
    const oldBets=changed?[...$('bets').children].map(n=>n.getBoundingClientRect()):[];
    const transition=changed&&previous&&s[15]===previous[15]&&(s[2]!==previous[2]||s[1]===255);
    this.players=s[0];this.s=s;this.label=label;
    if(newHand)this.reset();
    const done=s[1]===255;
    $('stage').dataset.seats=s[0];$('stage').classList.toggle('hand-complete',done);
    $('stage').classList.toggle('hero-to-act',!done&&s[1]===0);
    $('board').classList.toggle('preflop',s[13]===0);
    syncCards($('board'),Array.from({length:5},(_,i)=>i<s[13]?s[16+i]:null),s[22]>>2,focus&&s[13]>=3,changed);
    this.nodes.forEach((el,p)=>{
      const r=row(s,p),pos=position(p),payout=awardFor(s,p);
      el.classList.toggle('folded',!r[5]);el.classList.toggle('active',!done&&s[1]===p);
      el.classList.toggle('winner',payout>0);el.classList.toggle('all-in',!!r[5]&&!done&&r[0]===0);
      el.setAttribute('aria-label',`${label(p)}, ${fmt(r[0])} big blinds${!r[5]?', folded':''}${!done&&s[1]===p?', to act':''}${payout>0?`, wins ${fmt(payout)} big blinds`:''}`);
      syncCards(el.querySelector('.seat-cards'),[r[3],r[4]],s[22]&3,focus&&p===0&&s[13]>=3,newHand||changed&&done);
      el.querySelector('.seat-number').textContent=p===0?'Y':p;
      el.querySelector('.seat-stack').innerHTML=`${fmt(r[0])}<small>BB</small>`;
      el.querySelector('.seat-name').textContent=label(p);
      const tag=el.querySelector('.position-tag');tag.textContent=pos.short==='D'?'':pos.short;tag.hidden=!tag.textContent;
      el.querySelector('.dealer').hidden=s[7]!==p;
      if(done){this.clearCue(p);this.cue(p,payout>0?'win':!r[5]?'fold':'',payout>0?`Wins ${fmt(payout)} BB`:!r[5]?'Folded':'',true);}
      else if(newHand||transition||s[1]===p){this.clearCue(p);if(r[5]&&r[0]===0)this.cue(p,'all','All in',true);}
    });
    // Stable bet nodes avoid replaying animations on unrelated input changes.
    for(let p=0;p<s[0];p++){
      let bet=$('bets').querySelector(`[data-bet-seat="${p}"]`),n=row(s,p)[1];
      if(done||!n){bet?.remove();continue;}
      if(!bet){bet=document.createElement('div');bet.className='seat-bet';bet.dataset.betSeat=p;bet.innerHTML='<span class="bet-chip"></span><span></span>';$('bets').append(bet);}
      bet.lastElementChild.textContent=fmt(n);bet.setAttribute('aria-label',`${label(p)}: ${fmt(n)} BB bet`);
    }
    this.layout();
    if(changed&&this.pending){
      const {p,kind,amount,verb}=this.pending;this.pending=null;
      if(!done&&s[1]!==p)this.cue(p,kind,kind==='fold'?'Folded':kind==='check'?'Check':`${verb} ${amount}`,false);
      this.layout();
      if(kind==='check'&&!reduced())this.nodes[p].querySelector('.seat-info').animate([{transform:'translateY(0)'},{transform:'translateY(3px)'},{transform:'translateY(0)'},{transform:'translateY(3px)'},{transform:'translateY(0)'}],{duration:320});
      if(kind==='raise'&&!reduced())this.nodes[p].querySelector('.seat-info').animate([{scale:1},{scale:1.06},{scale:1}],{duration:350});
      if(kind==='call'||kind==='raise')this.fly(this.nodes[p].querySelector('.seat-info').getBoundingClientRect(),$('bets').querySelector(`[data-bet-seat="${p}"]`)?.getBoundingClientRect()||document.querySelector('.pot-wrap')?.getBoundingClientRect(),0);
    }
    if(transition){
      const pot=document.querySelector('.pot-wrap').getBoundingClientRect();
      oldBets.forEach(b=>this.fly(b,pot,0));
      if(done){
        this.nodes.forEach((el,p)=>{const r=row(s,p);if(awardFor(s,p)>0){this.fly(pot,el.querySelector('.seat-info').getBoundingClientRect(),oldBets.length?360:100,true);}});
      }
    }
    this.version=version;
  }
  clearCue(p){clearTimeout(this.cues.get(p));this.cues.delete(p);const el=this.nodes[p].querySelector('.seat-cue');el.className='seat-cue';el.textContent='';}
  cue(p,kind,text,persist){
    // Folds use the card tuck and crossed seat marker; no extra action bubble.
    this.clearCue(p);if(!kind||kind==='fold')return;
    const el=this.nodes[p].querySelector('.seat-cue');el.className=`seat-cue cue-${kind} visible`;
    const icon=kind==='check'?'✓':kind==='fold'?'↘':kind==='win'?'◆':kind==='raise'?'↑':kind==='all'?'●':'●';
    el.textContent=`${icon} ${text}`;
    if(!persist){this.cues.set(p,setTimeout(()=>{el.classList.remove('visible');this.cues.delete(p);},kind==='fold'?650:1800));}
  }
  fly(from,to,delay=0,award=false){
    if(!from||!to||reduced())return;
    const root=$('stage').getBoundingClientRect();
    for(let i=0;i<3;i++){
      const el=document.createElement('i');el.className=`flying-chip bet-chip${award?' award-chip':''}`;
      el.style.left=`${from.x+from.width/2-root.x+i*3}px`;el.style.top=`${from.y+from.height/2-root.y-i*3}px`;$('table-fx').append(el);
      const dx=to.x+to.width/2-from.x-from.width/2,dy=to.y+to.height/2-from.y-from.height/2;
      const animation=el.animate([{opacity:0,transform:'translate(-50%,-50%) scale(.8)'},{opacity:1,offset:.12},{opacity:1,offset:.85},{opacity:0,transform:`translate(calc(-50% + ${dx}px),calc(-50% + ${dy}px)) scale(1)`}],{duration:award?620:420,delay:delay+i*45,easing:'cubic-bezier(.2,.7,.25,1)',fill:'both'});
      animation.finished.then(()=>el.remove()).catch(()=>el.remove());
    }
  }
  layout(){
    if(!this.s)return;
    const stage=$('stage').getBoundingClientRect(),mobile=stage.width<700;
    this.nodes.forEach((el,p)=>{const [x,y]=location(p,this.players,mobile);el.style.left=`${x}%`;el.style.top=`${y}%`;});
    const obstacles=[...document.querySelectorAll('.seat-info,.seat-cards,.position-tag:not([hidden]),#board,.pot-wrap')].filter(el=>!el.closest('.folded')||!el.classList.contains('seat-cards')).map(el=>el.getBoundingClientRect());
    const intersects=(a,b,g=6)=>a.x<b.x+b.width+g&&a.x+a.width+g>b.x&&a.y<b.y+b.height+g&&a.y+a.height+g>b.y;
    const findSpot=(width,height,preferred)=>{
      const rect=(x,y)=>({x:x-width/2,y:y-height/2,width,height});
      const valid=a=>a.x>=stage.x+4&&a.x+a.width<=stage.right-4&&a.y>=stage.y+4&&a.y+a.height<=stage.bottom-4&&!obstacles.some(b=>intersects(a,b));
      const direct=rect(...preferred);if(valid(direct))return direct;
      let best=null,cost=Infinity;
      // Bounded grid fallback has no per-frame candidate arrays or sorting.
      for(let y=stage.y+height/2+6;y<stage.bottom-height/2-6;y+=10)for(let x=stage.x+width/2+6;x<stage.right-width/2-6;x+=10){
        const d=(x-preferred[0])**2+(y-preferred[1])**2;if(d>=cost)continue;
        const a=rect(x,y);if(valid(a)){best=a;cost=d;}
      }
      return best;
    };
    for(const el of this.nodes){
      const dealer=el.querySelector('.dealer');if(dealer.hidden)continue;
      const info=el.querySelector('.seat-info').getBoundingClientRect(),parent=el.getBoundingClientRect();
      const chosen=findSpot(dealer.offsetWidth,dealer.offsetHeight,[info.right+8+dealer.offsetWidth/2,info.y+info.height/2]);
      if(chosen){dealer.style.right='auto';dealer.style.left=`${chosen.x-parent.x}px`;dealer.style.top=`${chosen.y-parent.y}px`;obstacles.push(chosen);}
    }
    // Action bubbles share the same collision space as bets, including compact
    // nine-seat layouts where a bubble below one badge would cover another hand.
    for(const el of this.nodes){
      const cue=el.querySelector('.seat-cue');if(!cue.classList.contains('visible')||el.classList.contains('folded'))continue;
      const info=el.querySelector('.seat-info').getBoundingClientRect(),parent=el.getBoundingClientRect();
      const chosen=findSpot(cue.offsetWidth,cue.offsetHeight,[info.x+info.width/2,info.bottom+9+cue.offsetHeight/2]);
      if(chosen){cue.style.left=`${chosen.x-parent.x}px`;cue.style.top=`${chosen.y-parent.y}px`;obstacles.push(chosen);}
    }
    for(const bet of $('bets').children){
      const p=Number(bet.dataset.betSeat),info=this.nodes[p].querySelector('.seat-info').getBoundingClientRect(),cards=this.nodes[p].querySelector('.seat-cards').getBoundingClientRect();
      bet.hidden=false;
      const bw=bet.offsetWidth,bh=bet.offsetHeight,cx=info.x+info.width/2,cy=info.y+info.height/2;
      const toward=cx<stage.x+stage.width/2?1:-1;
      const preferred=p===0?[cards.x+cards.width+10+bw/2,cards.y+cards.height*.45]:Math.abs(cx-stage.x-stage.width/2)<stage.width*.1?[cx,info.bottom+35]:[cx+toward*(info.width/2+18+bw/2),cy];
      const chosen=findSpot(bw,bh,preferred);
      if(chosen){bet.style.left=`${chosen.x-stage.x}px`;bet.style.top=`${chosen.y-stage.y}px`;obstacles.push(chosen);bet.hidden=false;}else bet.hidden=true;
    }
  }
}
