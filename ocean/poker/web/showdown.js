import Hand from './Hand.bend';
import Cash from './Cash.bend';
const row=(s,p)=>s.slice(24+9*p,33+9*p);
export function bestFive(cards,rank){
 let chosen=[],holes=3;
 for(let a=0;a<cards.length-4;a++)for(let b=a+1;b<cards.length-3;b++)for(let c=b+1;c<cards.length-2;c++)for(let d=c+1;d<cards.length-1;d++)for(let e=d+1;e<cards.length;e++){
  const indices=[a,b,c,d,e],five=indices.map(i=>cards[i]),used=indices.filter(i=>i<2).length;
  if(used<=holes&&Hand.rank7(...five,52,52)===rank){chosen=five;holes=used;}
 }
 const counts=new Map();for(const id of chosen)counts.set(id>>2,(counts.get(id>>2)||0)+1);
 const kind=rank>>>24,groups=[1,2,3,6,7].includes(kind),wheel=[4,8].includes(kind)&&((rank>>>16)&15)===5;
 const value=id=>wheel&&(id>>2)===12?-1:id>>2;
 return chosen.sort((a,b)=>(groups?(counts.get(b>>2)-counts.get(a>>2)):0)||value(b)-value(a)||(a&3)-(b&3));
}
// This module sees only the public projection. Fold wins deliberately skip
// hand evaluation, even when the human happens to know their own holding.
export function awardFor(s,p){
 if(s[1]!==255)return 0;
 const r=row(s,p),other=Math.max(0,...Array.from({length:s[0]},(_,q)=>q===p?0:row(s,q)[2]));
 return Cash.award(r[0],r[8],r[2],other,!!s[14]);
}
export function resultFor(s){
 if(s[1]!==255)return null;
 const showdown=!!s[14],board=s.slice(16,21),hero=row(s,0);
 const winners=Array.from({length:s[0]},(_,p)=>({p,r:row(s,p)})).filter(({p})=>awardFor(s,p)>0).map(({p,r})=>{
  const revealed=showdown&&r[3]<52&&r[4]<52&&board.every(c=>c<52);
  const cards=revealed?[r[3],r[4],...board]:[],rank=revealed?Hand.rank7(...cards):null;
  return {p,amount:awardFor(s,p),rank,name:revealed?Hand.showdown_name(rank):'Won without showdown',cards:revealed?bestFive(cards,rank):[]};
 });
 return {hand:s[15],showdown,winners,hero:hero[5]&&showdown?Hand.showdown_name(Hand.rank7(hero[3],hero[4],...board)):hero[5]?'Everyone folded':'You folded'};
}
