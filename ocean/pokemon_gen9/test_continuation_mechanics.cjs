'use strict';
const assert=require('assert'),source=require('./worker_core.cjs'),candidate=require('./compact_core.cjs');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
Date.now=()=>1791194400000;
const mon=(species,moves,extra={})=>({species,moves,...extra});
const cases=[
  {name:'Illusion',p1:[mon('Mew',['psychic'])],p2:[mon('Zoroark',['darkpulse'],{ability:'Illusion',level:80}),mon('Blissey',['seismictoss'],{level:90})]},
  {name:'Transform',p1:[mon('Ditto',['transform'],{ability:'Limber'})],p2:[mon('Mew',['recover'])]},
  {name:'Terapagos Stellar',p1:[mon('Terapagos',['terastarstorm'],{ability:'Tera Shift',teraType:'Stellar'})],p2:[mon('Blissey',['softboiled'])],actions:[[5,1]]},
  {name:'multi-hit',p1:[mon('Maushold',['populationbomb'],{ability:'Technician',item:'Wide Lens'})],p2:[mon('Blissey',['softboiled'])]},
  {name:'Revival Blessing',p1:[mon('Magikarp',['splash'],{level:1}),mon('Pawmot',['revivalblessing','double shock'])],p2:[mon('Mew',['psychic'])],actions:[[1,1],[10,0],[1,1],[9,0]]},
  {name:'nested events and weather',p1:[mon('Tyranitar',['stoneedge'],{ability:'Sand Stream',item:'Life Orb'}),mon('Blissey',['softboiled'],{ability:'Natural Cure'})],p2:[mon('Corviknight',['roost'],{ability:'Mirror Armor',item:'Rocky Helmet'})]},
  {name:'priority and Trick Room',p1:[mon('Hatterene',['trickroom','psychic'],{ability:'Magic Bounce'})],p2:[mon('Scizor',['bulletpunch'],{ability:'Technician'})],actions:[[1,1],[2,1],[2,1]]},
];
function compare(a,b,label){
  for(const key of ['requests','masks','pending'])assert.deepEqual(b[key],a[key],label+' '+key);
  assert.deepEqual(b.b.log,a.b.log,label+' log');assert.deepEqual(b.b.prng.getSeed(),a.b.prng.getSeed(),label+' RNG');
  for(let s=0;s<2;s++)assert.deepEqual(candidate.unpack(b.observe(s)),a.observe(s),label+' observation');
}
const records=cases.map((c,i)=>{
  const options={p1:{name:'one',team:c.p1},p2:{name:'two',team:c.p2}};
  return {c,a:new source.Game(800+i,structuredClone(options)),b:new candidate.Game(800+i,structuredClone(options))};
});
try{
  for(const {c,a,b} of records){
    compare(a,b,c.name+' initial');
    const obs=b.observe(0),foe=b.b.sides[1].pokemon.at(-1),old=foe.item;foe.item='leftovers';
    assert.deepEqual(b.observe(0),obs,c.name+' hidden state independence');foe.item=old;
  }
  const illusion=records[0].b;
  assert.equal(illusion.views[0].p2.active[0].speciesForme,'Blissey');
  assert.equal(illusion.views[0].p2.active[0].level,90);
  assert(!illusion.views[0].p2.team.some(p=>p.speciesForme==='Zoroark'));
  for(let step=0;step<12;step++){
    const active=records.filter(r=>!r.a.b.ended);
    const actions=active.map(({c,a})=>a.masks.map((mask,s)=>{
      const requested=c.actions?.[step]?.[s];
      if(requested!==undefined&&mask[requested])return requested;
      if(mask[0])return 0;
      return Array.from({length:15},(_,i)=>i).find(i=>mask[i]);
    }));
    const expected=active.map((r,i)=>r.a.step(actions[i]));
    const actual=candidate.stepBatch(active.map(r=>r.b),actions,{verifyNative:true});
    active.forEach((r,i)=>{assert.deepEqual(actual[i],expected[i]);compare(r.a,r.b,r.c.name+' step '+step);});
    if(step===0){
      assert(records[1].b.b.sides[0].active[0].transformed,'Transform must execute');
      assert.equal(records[2].b.b.sides[0].active[0].species.name,'Terapagos-Stellar');
      assert(records[3].b.b.log.some(l=>l.includes('|-hitcount|')),'Multi-hit must execute');
    }
  }
  assert(records[4].b.b.log.some(l=>l.includes('|move|')&&l.includes('Revival Blessing')),'Revival Blessing must execute');
  assert(records[4].b.b.log.some(l=>l.includes('|-heal|')&&l.includes('Revival Blessing')),'Revival target must heal');
  require('fs').writeFileSync('build/pokemon_gen9/continuation/mechanics.json',JSON.stringify({status:'passed',
    cases:cases.map(c=>c.name),checks:'Exact requests, masks, commitments, ordered logs, PRNG and both lossless compact/public observations; private-state independence and Illusion visibility.',
    kernel_source_ir_sha256:candidate.runtime.manifest.source_ir_sha256,schema_sha256:candidate.schema.schema_sha256,
    limits:'Seven targeted scenarios, at most twelve decision boundaries per scenario; no all-state coverage claim.'},null,2)+'\n');
  console.log('PASS: '+cases.map(c=>c.name).join(', ')+'; exact requests, logs, PRNG and compact/public observations');
}finally{for(const r of records){r.a.close();r.b.close();}}
