'use strict';
const path=require('path'),assert=require('assert');
const runtime=require('./continuation_runtime.cjs');
const {root}=require('./reference.cjs');
const {Game:SourceGame,choice,...layout}=require('./worker_core.cjs');
const directory=path.join(root,'build/pokemon_gen9/continuation/runtime');
const {Battle}=require(directory+'/dist/sim/battle');
const {Dex}=require(directory+'/dist/sim/dex');
runtime.install(Battle,Dex);
class Game extends SourceGame{
  constructor(n,options={}){
    super(n,options,Battle);
    if(this.b.gen!==9||this.b.format.id!=='gen9randombattle'){
      this.close();throw Error('Source continuations support the pinned default gen9randombattle format');
    }
  }
  *stepFrames(actions){
    if(this.b.ended)throw Error('Reset required after terminal');
    for(let s=0;s<2;s++)if(!Number.isInteger(actions[s])||!this.masks[s][actions[s]])
      throw Error('Action outside public mask: '+actions[s]);
    const waiting=this.masks.map(m=>!!m[0]);this.steps++;
    for(let s=0;s<2&&!this.b.ended;s++)if(!waiting[s]){
      this.errors=[];this.decisions++;
      if(!(yield* runtime.invoke(this.b.choose,this.b,['p'+(s+1),choice(actions[s])]))){
        if(!this.errors.some(e=>e.includes('|error|[Unavailable choice]')))
          throw Error('Simulator rejected masked action: '+JSON.stringify({actions,errors:this.errors,request:this.requests[s]}));
        this.retries++;
      }
    }
    this.refresh();
    return {ended:this.b.ended,win:this.b.winner==='one'?1:this.b.winner==='two'?-1:0,
      turns:this.b.turn,steps:this.steps,decisions:this.decisions,retries:this.retries};
  }
  step(actions){return runtime.run([this.stepFrames(actions)])[0];}
}
function stepBatch(games,actions,options){
  assert.equal(games.length,actions.length,'Battle/action batch length mismatch');
  if(new Set(games).size!==games.length||new Set(games.map(g=>g.b)).size!==games.length)
    throw Error('A battle cannot occupy two scheduler lanes');
  return runtime.run(games.map((g,i)=>g.stepFrames(actions[i])),options);
}
module.exports={Game,stepBatch,runtime,choice,...layout};
