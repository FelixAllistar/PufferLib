// Independent native rank oracle for the presentation evaluator, not a second
// copy of its algorithm. Run with the stock Bend loader under the build guard.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import Hand from './Hand.bend';
import Cash from './Cash.bend';
import {resultFor} from './showdown.js';
const out='build/poker/web';fs.mkdirSync(out,{recursive:true});
const c=`#include <stdio.h>
#include "${process.cwd()}/ocean/poker/core.h"
int main(){uint64_t rng=739811;for(int n=5;n<=7;n++)for(int h=0;h<4000;h++){uint8_t deck[52];for(int i=0;i<52;i++)deck[i]=i;for(int i=0;i<n;i++){int j=i+poker_random(&rng)%(52-i);uint8_t t=deck[i];deck[i]=deck[j];deck[j]=t;}printf("%u",poker_rank(deck,n));for(int i=0;i<7;i++)printf(" %u",i<n?deck[i]:52);puts("");}return 0;}`;
fs.writeFileSync(`${out}/hand_oracle.c`,c);
let r=spawnSync('clang-19',['-O2','-std=c11','-DPOKER_PLAYERS=2','-DPOKER_BATCH=1',`${out}/hand_oracle.c`,'-lm','-o',`${out}/hand_oracle`],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);
r=spawnSync(`${out}/hand_oracle`,[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);
const categories=new Set();let checks=0;
for(const line of r.stdout.trim().split('\n')){
 const [expected,...cards]=line.split(' ').map(Number);assert.equal(Hand.rank7(...cards),expected,`Native rank mismatch: ${cards}`);categories.add(expected>>>24);checks++;
}
assert.equal(Hand.name(Hand.rank7(0,5,10,15,48,52,52)),'Straight, five high');
assert.equal(Hand.name(Hand.rank7(35,39,43,47,51,52,52)),'Straight flush, ace high');
function view(stacks,initial,totals,holdings){
 const n=stacks.length,s=Array(24+9*n).fill(0);s[0]=n;s[1]=255;s[13]=5;s[14]=1;s[15]=1;s.splice(16,5,0,5,10,35,48);
 for(let p=0;p<n;p++)s.splice(24+9*p,9,stacks[p],totals[p],totals[p],...holdings[p],1,-1,stacks[p]-initial[p],initial[p]);return s;
}
// Refund only: the deep-stacked loser gets 150 BB of uncalled chips back.
const refund=resultFor(view([15000,10000],[20000,5000],[20000,5000],[[20,25],[49,50]]));
assert.deepEqual(refund.winners.map(w=>[w.p,w.amount]),[[1,10000]]);
// Short-stack main winner, net-losing side-pot winner, unmatched refund.
const side=resultFor(view([30000,10000,5000],[10000,15000,20000],[10000,15000,20000],[[49,50],[33,34],[20,25]]));
assert.deepEqual(side.winners.map(w=>[w.p,w.amount]),[[0,30000],[1,10000]]);
assert.ok(side.winners.every(w=>w.cards.length===5));
const folded=view([20100,19900],[20000,20000],[100,100],[[12,17],[52,52]]);folded[14]=0;folded[24+9+5]=0;
const unseen=resultFor(folded);assert.ok(unseen.winners.every(w=>w.cards.length===0&&w.rank===null));
for(const terminal of [false,true])for(const stack of [0,1,20000])for(const paused of [false,true])for(const time of [0,1,3000])assert.equal(Cash.auto_ready(terminal,stack,paused,time),Number(terminal&&stack>0&&!paused&&time===0));
const report={native_rank_matches:checks,card_counts:[5,6,7],categories:[...categories].sort(),wheel_and_royal:true,refund_excluded:true,net_losing_side_pot_recipient:true,fold_win_hides_hand:true,auto_deal_boundaries:true};
fs.writeFileSync(`${out}/hand-validation.json`,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
