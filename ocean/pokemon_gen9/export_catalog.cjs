#!/usr/bin/env node
'use strict';
// Build-time import of resolved Gen 9 records, never a runtime dependency.
const fs=require('fs'),path=require('path'),vm=require('vm'),crypto=require('crypto');
const {Dex,root,pin}=require('./reference.cjs');
const dex=Dex.mod('gen9');
const sets=JSON.parse(fs.readFileSync(path.join(root,'build/pokemon_gen9/showdown/data/random-battles/gen9/sets.json')));
const source=fs.readFileSync(path.join(root,'build/pokemon_gen9/showdown/data/random-battles/gen9/teams.ts'),'utf8');
const toID=s=>String(s).toLowerCase().replace(/[^a-z0-9]/g,'');
function mapping(names,normalizer=toID) {
  const ids=[...new Set(names.map(normalizer))].filter(Boolean).sort();
  return {names:['',...ids],map:Object.fromEntries(ids.map((x,i)=>[x,i+1]))};
}
const speciesRecords=dex.species.all().filter(s=>s.types.every(t=>dex.types.get(t).exists));
const displayNames=new Set(speciesRecords.map(s=>s.name));
for(const s of speciesRecords)
  for(const name of [...(s.cosmeticFormes||[]),...(s.otherFormes||[])]) displayNames.add(name);
for(const name of ['Pikachu','Pikachu-Original','Pikachu-Hoenn','Pikachu-Sinnoh','Pikachu-Unova',
  'Pikachu-Kalos','Pikachu-Alola','Pikachu-Partner','Pikachu-World']) displayNames.add(name);
const species=mapping([...displayNames]),moves=mapping(dex.moves.all().map(x=>x.id));
const abilities=mapping(dex.abilities.all().map(x=>x.id)),items=mapping(dex.items.all().map(x=>x.id));
const types=mapping(dex.types.names());
const roles=mapping(Object.values(sets).flatMap(s=>s.sets.map(x=>x.role)));
const targets=mapping(dex.moves.all().map(m=>m.target));
const conditionID=name=>name.startsWith('item:')?'item:'+toID(name.slice(5)):
  name.startsWith('ability:')?'ability:'+toID(name.slice(8)):toID(name);
const conditions=mapping([...Object.keys(dex.data.Conditions),...Object.keys(dex.data.Rulesets),
  ...dex.moves.all().filter(x=>x.condition).map(x=>x.id),
  ...dex.abilities.all().filter(x=>x.condition).map(x=>x.id),
  ...dex.items.all().filter(x=>x.condition).map(x=>x.id),
  ...abilities.names.slice(1).map(x=>'ability:'+x),...items.names.slice(1).map(x=>'item:'+x),
  'recoil','drain'],conditionID);
const conditionRecords=conditions.names.slice(1).map(x=>dex.conditions.getByID(x));
// Item type-name fields are also scalar callbacks: Multitype/RKS System and
// Techno Blast use runEvent('Plate'/'Memory'/'Drive'). Keep both representations.
const itemTypeKeys=new Set(['onPlate','onDrive','onMemory']);
const isCallback=key=>
  ((key.startsWith('on')&&!key.endsWith('Order')&&!key.endsWith('Priority'))||key.endsWith('Callback'));
function callbackKeys(effect) {
  return [...new Set(Object.keys(effect).flatMap(key=>isCallback(key)||functionCallbacks.has(key)?[key]:
    key.startsWith('on')&&/(SubOrder|Priority|Order)$/.test(key)?
      [key.replace(/(SubOrder|Priority|Order)$/,'')]:[]))];
}
const effects=[...dex.moves.all(),...dex.abilities.all(),...dex.items.all(),...conditionRecords,...speciesRecords];
const functionCallbacks=new Set(effects.flatMap(effect=>Object.keys(effect).filter(key=>
  typeof effect[key]==='function'&&(key.startsWith('on')||key.endsWith('Callback')))));
const callbacks=mapping(effects.flatMap(callbackKeys).filter(key=>!itemTypeKeys.has(key)));
const originalCallbacks=Object.fromEntries(effects.flatMap(callbackKeys).map(key=>[toID(key),key]));
// Events with no base-dex listener still need an identity for the engine's
// control flow/context. Append these IDs so all existing handler IDs remain
// stable. EntryHazard is also named by the shared battle comparator.
const engineCallbackKeys=new Set(['onEntryHazard',...itemTypeKeys]);
for(const file of ['sim/battle.ts','sim/battle-actions.ts','sim/pokemon.ts','sim/side.ts','sim/field.ts',
  'data/scripts.ts','data/abilities.ts','data/items.ts','data/moves.ts','data/conditions.ts','data/rulesets.ts',
  'config/formats.ts']) {
  const text=fs.readFileSync(path.join(root,'build/pokemon_gen9/showdown',file),'utf8');
  // Match complete literal arguments, not prefixes such as 'Modify' + stat.
  // Prefixes can collide with real callbacks ending in Priority or Order.
  for(const match of text.matchAll(/\b(?:singleEvent|runEvent|eachEvent|residualEvent|priorityEvent)\(\s*['"]([A-Za-z][A-Za-z0-9]*)['"]\s*(?=[,)])/g))
    engineCallbackKeys.add('on'+match[1]);
}
for(const key of [...engineCallbackKeys].sort()) {
  const name=toID(key);originalCallbacks[name] ||= key;
  if(callbacks.map[name]===undefined){callbacks.map[name]=callbacks.names.length;callbacks.names.push(name);}
}
callbacks.original=callbacks.names.map(name=>originalCallbacks[name]||'');
const strings=mapping(effects.flatMap(x=>Object.keys(x).filter(key=>isCallback(key)&&!itemTypeKeys.has(key))
  .map(k=>x[k]).filter(v=>typeof v==='string')),x=>x);
const typeStrings=[...new Set(effects.flatMap(effect=>[...itemTypeKeys].map(key=>effect[key])
  .filter(value=>typeof value==='string')))].sort();
for(const value of typeStrings)if(strings.map[value]===undefined){strings.map[value]=strings.names.length;strings.names.push(value);}
// Append effect identity/display strings after existing literal IDs. Text
// values retain exact spelling; canonical effect IDs are not species-table IDs.
const format=dex.formats.get('gen9randombattle');
if(Object.keys(format).some(key=>isCallback(key)&&format[key]!==undefined))
  throw Error('Pinned default format gained callback properties; port its battle handler collection');
const identityTexts=[...new Set([...effects.flatMap(effect=>[effect.id,effect.name]),
  ...species.names.slice(1).flatMap(name=>{const effect=dex.species.get(name);return [effect.id,effect.name];}),
  format.id,format.name].filter(value=>typeof value==='string'&&value))].sort();
strings.map['']=0;
for(const value of identityTexts)if(strings.map[value]===undefined){
  strings.map[value]=strings.names.length;strings.names.push(value);
}
// Type-query values preserve the source spelling. Append after identities so
// no existing literal/identity ID changes when these helpers are introduced.
const queryTypeTexts=[...new Set([...dex.types.names(),'Normal','Flying','Rock','Ice','Stellar','???','Bird'])].sort();
for(const name of species.names.slice(1)){
  const added=dex.species.get(name).addedType||'';
  if(typeof added!=='string')throw Error('Non-string species addedType: '+name);
  if(strings.map[added]===undefined){strings.map[added]=strings.names.length;strings.names.push(added);}
}
for(const value of queryTypeTexts)if(strings.map[value]===undefined){
  strings.map[value]=strings.names.length;strings.names.push(value);
}
// Slot objects preserve exact target and hidden-disable strings. Append to
// retain every previous primitive identity, then export reverse numeric views.
for(const value of [...new Set(dex.moves.all().map(move=>move.target))].sort().concat(['hidden','Hidden Power '])){
  if(strings.map[value]===undefined){strings.map[value]=strings.names.length;strings.names.push(value);}
}
const ignoredRuleHooks=new Set(['onBegin','onTeamPreview','onBattleStart','onValidateRule',
  'onValidateTeam','onChangeSet','onValidateSet']);
const constructorRules=[...dex.formats.getRuleTable(format).keys()].filter(rule=>
  !'+*-!'.includes(rule.charAt(0))).filter(rule=>{
    const effect=dex.formats.get(rule);
    return effect.exists&&Object.keys(effect).some(key=>key.startsWith('on')&&!ignoredRuleHooks.has(key));
  });
if(JSON.stringify(constructorRules)!==JSON.stringify(['sleepclausemod']))
  throw Error('Pinned default constructor rules changed: '+JSON.stringify(constructorRules));
const lookup=(m,n)=>{const id=m.map[toID(n)];if(id===undefined)throw Error('Missing catalog ID: '+n);return id;};
const words=new Uint32Array(524288);
const mutableWords=16384;
let cursor=mutableWords;
const regions={};
function allocate(name,count,stride=1) {
  const start=cursor;cursor+=count*stride;
  if(cursor>words.length)throw Error('Catalog capacity exceeded');
  regions[name]={start,count,stride};return start;
}
const speciesAt=allocate('species',species.names.length,64);
const movesAt=allocate('moves',moves.names.length,24);
const abilitiesAt=allocate('abilities',abilities.names.length,8);
const itemsAt=allocate('items',items.names.length,8);
const templateCount=Object.values(sets).reduce((n,s)=>n+s.sets.length,0);
const templatesAt=allocate('templates',templateCount+1,8);
const effectivenessAt=allocate('effectiveness',types.names.length*types.names.length);
const immunityAt=allocate('immunity',types.names.length*types.names.length);
const conditionsAt=allocate('conditions',conditions.names.length,16);
const arenaStart=cursor;
function list(values) {
  const start=cursor;cursor+=values.length;
  if(cursor>words.length)throw Error('Catalog arena capacity exceeded');
  words.set(values,start);return start;
}
function handlers(effect) {
  const values=[];
  for(const key of callbackKeys(effect)) {
    // Dex Item instances own undefined onPlate/onDrive/onMemory fields. They
    // have no callback or ordering semantics. Keep genuine metadata-only
    // entries (needed for SwitchIn aliases), without allocating absent fields.
    if(effect[key]===undefined&&effect[key+'Order']===undefined&&
      effect[key+'Priority']===undefined&&effect[key+'SubOrder']===undefined)continue;
    const handler=effect[key];
    let tag,value=0;
    if(handler===undefined)tag=4;
    else if(typeof handler==='function')tag=0;
    else if(typeof handler==='boolean'){tag=1;value=+handler;}
    else if(typeof handler==='number'&&Number.isInteger(handler)&&handler>=0&&handler<=4294967295){tag=2;value=handler;}
    else if(typeof handler==='number'&&Number.isInteger(handler*10)&&handler*10>=-32768&&handler*10<=32767){tag=5;value=handler*10+32768;}
    else if(typeof handler==='string'){tag=3;value=strings.map[handler];}
    else throw Error('Unsupported callback value '+effect.id+'.'+key+': '+typeof handler);
    const priority=(effect[key+'Priority']||0)*10+32768,sub=(effect[key+'SubOrder']||0)+32768;
    const order=Number(effect[key+'Order']||0);
    if(!Number.isInteger(priority)||!Number.isInteger(sub)||!Number.isInteger(order)||
       priority<0||priority>65535||sub<0||sub>65535||order<0||order>4294967295)
      throw Error('Unrepresentable ordering metadata '+effect.id+'.'+key);
    values.push(lookup(callbacks,key),tag,order,priority,sub,value,0,0);
  }
  return [values.length/8,list(values)];
}
let templateID=1;
const templateMeta=[null];
const stats=['hp','atk','def','spa','spd','spe'];
function formPool(s) {
  if(!sets[s.id])return {sample:false,names:[s.name]};
  if(typeof s.battleOnly==='string')return {sample:false,names:[s.battleOnly]};
  if(s.cosmeticFormes)return {sample:true,names:[s.name,...s.cosmeticFormes]};
  if(s.name.endsWith('-Gmax'))return {sample:false,names:[s.name.slice(0,-5)]};
  if(['Dudunsparce','Maushold','Polteageist','Sinistcha','Zarude'].includes(s.baseSpecies))
    return {sample:true,names:[s.name,...s.otherFormes]};
  if(s.baseSpecies==='Basculin')return {sample:true,names:['Basculin','Basculin-Blue-Striped']};
  if(s.baseSpecies==='Magearna')return {sample:true,names:['Magearna','Magearna-Original']};
  if(s.baseSpecies==='Pikachu')return {sample:true,names:['Pikachu','Pikachu-Original','Pikachu-Hoenn',
    'Pikachu-Sinnoh','Pikachu-Unova','Pikachu-Kalos','Pikachu-Alola','Pikachu-Partner','Pikachu-World']};
  return {sample:false,names:[s.name]};
}
const speciesMeta=[null];
const typeArrayIDs=new WeakMap(),typeArrays=[null];
const typeCacheStart=4480,typeCacheEnd=8192;
for(let id=1;id<species.names.length;id++) {
  const s=dex.species.get(species.names[id]);
  if(!s.exists)throw Error('Unresolved species '+species.names[id]);
  const at=speciesAt+id*64;
  const form=formPool(s), required=s.requiredItems||[], available=Object.values(s.abilities);
  words[at]=id;words[at+1]=lookup(species,s.baseSpecies);
  words[at+2]=lookup(types,s.types[0]);words[at+3]=s.types[1]?lookup(types,s.types[1]):0;
  stats.forEach((stat,i)=>words[at+4+i]=s.baseStats[stat]);
  for(const stat of stats)if(!Number.isInteger(s.baseStats[stat])||s.baseStats[stat]<0||s.baseStats[stat]>10000)
    throw Error('Species base stat domain: '+s.id+'.'+stat);
  words[at+10]={M:1,F:2,N:3}[s.gender]||0;words[at+11]=Number(s.nfe);
  words[at+12]=s.requiredMove?lookup(moves,s.requiredMove):0;
  words[at+13]=required.length;words[at+14]=list(required.map(x=>lookup(items,x)));
  words[at+15]=form.names.length;words[at+16]=list(form.names.map(x=>lookup(species,x)));
  words[at+17]=sets[s.id]?.level||80;
  const setIDs=[];
  if(sets[s.id])for(const set of sets[s.id].sets) {
    const tid=templateID++,ts=templatesAt+tid*8;setIDs.push(tid);
    words[ts]=lookup(roles,set.role);
    words[ts+1]=set.movepool.length;words[ts+2]=list(set.movepool.map(x=>lookup(moves,x)));
    words[ts+3]=set.abilities.length;words[ts+4]=list(set.abilities.map(x=>lookup(abilities,x)));
    words[ts+5]=set.teraTypes.length;words[ts+6]=list(set.teraTypes.map(x=>lookup(types,x)));
    templateMeta[tid]={species:s.id,...set};
  }
  words[at+18]=setIDs.length;words[at+19]=list(setIDs);
  words[at+20]=available.length;words[at+21]=list(available.map(x=>lookup(abilities,x)));
  words[at+22]=Number(form.sample);words[at+23]=Number(!!s.isMega);
  words[at+24]=s.weighthg;words[at+25]=s.maxHP||0;
  words[at+26]=s.requiredTeraType?lookup(types,s.requiredTeraType):0;
  words[at+27]=lookup(species,s.id);
  [words[at+28],words[at+29]]=handlers(s);
  if(!typeArrayIDs.has(s.types)){
    if(!Array.isArray(s.types)||!s.types.length||!Object.isFrozen(s.types))
      throw Error('Unexpected mutable/empty resolved species type array: '+s.id);
    typeArrayIDs.set(s.types,typeArrays.length);
    typeArrays.push(s.types.map(text=>lookup(types,text)));
  }
  words[at+30]=typeArrayIDs.get(s.types);
  if(!Number.isInteger(s.num)||s.num<-(2**31)||s.num>=2**31)throw Error('Species number domain: '+s.id);
  words[at+51]=s.num>>>0;
  words[at+52]=strings.map[s.addedType||''];
  for(let t=1;t<types.names.length;t++)
    words[at+31+t]=dex.getEffectiveness(dex.types.get(types.names[t]).name,s)+8;
  speciesMeta[id]={id:s.id,name:s.name,baseSpecies:s.baseSpecies,forms:form.names,type_array:words[at+30],num:s.num,addedType:s.addedType||''};
}
if(typeCacheStart+typeArrays.length>typeCacheEnd)
  throw Error('Species type identity cache exceeds reserved private region');
const typeArraysAt=allocate('speciesTypeArrays',typeArrays.length,2);
for(let group=1;group<typeArrays.length;group++){
  words[typeArraysAt+group*2]=typeArrays[group].length;
  words[typeArraysAt+group*2+1]=list(typeArrays[group]);
}
if(templateID!==templateCount+1)throw Error('Template count mismatch: '+templateID);
for(let id=1;id<moves.names.length;id++) {
  const m=dex.moves.get(moves.names[id]),at=movesAt+id*24;
  const flags=[m.damage,m.basePowerCallback,m.damageCallback,m.recoil,m.hasCrashDamage,m.drain,
    Array.isArray(m.multihit)&&m.multihit[1]===5,m.flags.bite,m.flags.punch,m.flags.sound,
    m.secondary,m.hasSheerForceBoost];
  words[at]={Physical:1,Special:2,Status:3}[m.category];words[at+1]=lookup(types,m.type);
  words[at+2]=m.basePower;words[at+3]=m.accuracy===true?0:m.accuracy;
  words[at+4]=m.priority+16;words[at+5]=m.pp;
  words[at+6]=flags.reduce((v,f,i)=>v|(f?1<<i:0),0);
  words[at+7]=Number(!!m.noPPBoosts);
  words[at+8]=m.recoil?.[0]||0;words[at+9]=m.recoil?.[1]||0;
  words[at+10]=m.drain?.[0]||0;words[at+11]=m.drain?.[1]||0;
  [words[at+12],words[at+13]]=handlers(m);
  words[at+14]=lookup(targets,m.target);
}
for(let t=1;t<types.names.length;t++)for(let u=1;u<types.names.length;u++)
 {
  const sourceType=dex.types.get(types.names[t]).name,targetType=dex.types.get(types.names[u]).name;
  words[effectivenessAt+t*types.names.length+u]=dex.getEffectiveness(sourceType,targetType)+8;
  words[immunityAt+t*types.names.length+u]=Number(dex.getImmunity(sourceType,targetType));
 }
for(let id=1;id<abilities.names.length;id++) {
  const a=dex.abilities.get(abilities.names[id]),at=abilitiesAt+id*8;
  words[at]=Number(!!a.flags.breakable);words[at+1]=Number(!!a.flags.failroleplay);
  words[at+2]=Number(!!a.flags.notrace);words[at+3]=Number(!!a.flags.cantsuppress);
  words[at+4]=Number(!!a.flags.notransform);
  [words[at+5],words[at+6]]=handlers(a);words[at+7]=Number(a.exists);
}
for(let id=1;id<items.names.length;id++) {
  const item=dex.items.get(items.names[id]),at=itemsAt+id*8;
  words[at]=Number(!!item.isBerry);words[at+1]=Number(!!item.isGem);
  words[at+2]=Number(!!item.isPrimalOrb);words[at+3]=Number(!!item.ignoreKlutz);
  words[at+4]=(item.onPlate||item.onDrive||item.onMemory)?lookup(types,item.onPlate||item.onDrive||item.onMemory):0;
  [words[at+5],words[at+6]]=handlers(item);words[at+7]=Number(item.exists);
}
for(let index=0;index<conditionRecords.length;index++) {
  const c=conditionRecords[index],id=index+1,at=conditionsAt+id*16;
  words[at]=({Condition:0,Ability:1,Item:2,Format:3,Weather:4,Rule:5,Ruleset:6})[c.effectType]??7;
  const name=conditions.names[id];
  words[at+1]=name.startsWith('ability:')?lookup(abilities,name.slice(8)):0;
  words[at+2]=name.startsWith('item:')?lookup(items,name.slice(5)):0;
  words[at+3]=moves.map[name]||0;words[at+4]=c.duration||0;
  words[at+5]=Number(!!c.durationCallback);
  words[at+6]=Number(!!c.affectsFainted)|(Number(!!c.noCopy)<<1);
  words[at+7]=Number(c.exists);
  words[at+8]=Number(c.onStart!==undefined);words[at+9]=Number(c.onRestart!==undefined);
  words[at+10]=Number(c.onEnd!==undefined);words[at+11]=Number(!!c.onAnySwitchIn);
  [words[at+12],words[at+13]]=handlers(c);
  words[at+14]=Number(c.effectType==='Status');
  words[at+15]=Number(c.effectType==='Terrain');
}
function arrayConstant(name,context={}) {
  const escaped=name.replace(/[.*+?^$(){}|[\]\\]/g,'\\$&');
  const match=source.match(new RegExp('const '+escaped+' = (\\[[\\s\\S]*?\\n\\s*\\]);'));
  if(!match)throw Error('Source array not found: '+name);
  return vm.runInNewContext(match[1],context,{timeout:1000});
}
const ruleNames=['RECOVERY_MOVES','CONTRARY_MOVES','PHYSICAL_SETUP','SPECIAL_SETUP','MIXED_SETUP','SPEED_SETUP',
 'SETUP','SPEED_CONTROL','NO_STAB','HAZARDS','PROTECT_MOVES','PIVOT_MOVES','MOVE_PAIRS',
 'PRIORITY_POKEMON','NO_LEAD_POKEMON','DEFENSIVE_TERA_BLAST_USERS'];
const rules=Object.fromEntries(ruleNames.map(name=>[name,arrayConstant(name)]));
const statusMoves=dex.moves.all().filter(x=>x.category==='Status').map(x=>x.id);
rules.INCOMPATIBLE_PAIRS=arrayConstant('incompatiblePairs',{...rules,statusMoves});
const grouped=new Map();
for(const name of Object.keys(sets)) {
  const s=dex.species.get(name);
  if(!grouped.has(s.baseSpecies))grouped.set(s.baseSpecies,[]);
  grouped.get(s.baseSpecies).push(lookup(species,name));
}
const basePool=[];
const formPoolTable=new Uint32Array(species.names.length*2);
for(const [base,forms] of grouped) {
  const id=lookup(species,base),weight=base==='Squawkabilly'?1:Math.min(Math.ceil(forms.length/3),3);
  for(let i=0;i<weight;i++)basePool.push(id);
  formPoolTable[id*2]=forms.length;formPoolTable[id*2+1]=list(forms);
}
regions.basePool={start:list(basePool),count:basePool.length,stride:1};
regions.formPools={start:list(Array.from(formPoolTable)),count:species.names.length,stride:2};
// Callback-name relationships used by native bubbling and resolvePriority.
// Zero means that the prefixed property has no listener anywhere in the pin.
const callbackInfo=allocate('callbackInfo',callbacks.names.length,8);
for(let id=1;id<callbacks.names.length;id++) {
  const key=callbacks.original[id],at=callbackInfo+id*8;
  words[at]=Number(key.endsWith('SwitchIn')) | (Number(key.endsWith('RedirectTarget'))<<1) |
    (Number(key==='onAllyTryHitSide')<<2);
  if(key.startsWith('on'))for(const [index,prefix] of ['onAlly','onAny','onFoe','onSource'].entries())
    words[at+1+index]=callbacks.map[toID(prefix+key.slice(2))]||0;
}
const textFamilies={species,moves,abilities,items,conditions};
for(const [family,ids]of Object.entries(textFamilies)){
  const at=allocate(family+'Text',ids.names.length,2);
  for(let id=1;id<ids.names.length;id++){
    const effect=family==='conditions'?dex.conditions.getByID(ids.names[id]):dex[family].get(ids.names[id]);
    for(const [offset,key]of ['id','name'].entries()){
      const value=effect[key]||'';
      if(strings.map[value]===undefined)throw Error('Missing identity text: '+family+'.'+ids.names[id]+'.'+key);
      words[at+id*2+offset]=strings.map[value];
    }
  }
}
// Exact JS string code units, including unpaired surrogates. Hash buckets
// are only an index: equality always compares complete UTF-16 contents.
const textUnitsAt=allocate('textUnits',strings.names.length,2);
const textNextAt=allocate('textNext',strings.names.length);
const textBucketsAt=allocate('textBuckets',16384);
for(let id=0;id<strings.names.length;id++){
  const text=strings.names[id],units=Array.from({length:text.length},(_,i)=>text.charCodeAt(i));
  words[textUnitsAt+id*2]=units.length;
  words[textUnitsAt+id*2+1]=list(units);
  if(id){
    let hash=0;for(const unit of units)hash=(Math.imul(hash,31)+unit)>>>0;
    const bucket=textBucketsAt+(hash%16384);
    words[textNextAt+id]=words[bucket];words[bucket]=id;
  }
}
const targetTexts=allocate('targetTexts',targets.names.length);
const moveByText=allocate('moveByText',strings.names.length);
const targetByText=allocate('targetByText',strings.names.length);
for(let id=1;id<moves.names.length;id++){
  const text=dex.moves.get(moves.names[id]).id,tid=strings.map[text];
  if(tid===undefined||words[moveByText+tid])throw Error('Ambiguous resolved move ID text: '+text);
  words[moveByText+tid]=id;
}
for(const move of dex.moves.all()){
  const id=targets.map[toID(move.target)],tid=strings.map[move.target];
  if(words[targetTexts+id]&&words[targetTexts+id]!==tid)throw Error('Ambiguous target text');
  words[targetTexts+id]=tid;words[targetByText+tid]=id;
}
regions.arena={start:arenaStart,count:cursor-arenaStart,stride:1};
const catalog={schema_version:12,revision:pin,word_count:words.length,mutable_words:mutableWords,used_words:cursor,regions,
 ids:{species,moves,abilities,items,types,roles,conditions,callbacks,strings,targets},rules,species:speciesMeta,templates:templateMeta,
 type_order:dex.types.names().map(x=>lookup(types,x)),constructor_rules:constructorRules,
 species_type_arrays:typeArrays,species_type_cache:{start:typeCacheStart,end:typeCacheEnd},
 resolved:{moves:dex.moves.all(),abilities:dex.abilities.all(),items:dex.items.all(),
   conditions:dex.data.Conditions, species:speciesRecords}};
const out=path.join(root,'build/pokemon_gen9/catalog');fs.mkdirSync(out,{recursive:true});
const bytes=Buffer.alloc(words.length*4);for(let i=0;i<words.length;i++)bytes.writeUInt32LE(words[i],i*4);
catalog.binary_sha256=crypto.createHash('sha256').update(bytes).digest('hex');
fs.writeFileSync(path.join(out,'catalog.bin'),bytes);
fs.writeFileSync(path.join(out,'catalog.json'),JSON.stringify(catalog,null,2)+'\n');
const generated=path.join(__dirname,'data');fs.mkdirSync(generated,{recursive:true});
for(const [kind,ids] of Object.entries(catalog.ids)) {
  const defs=ids.names.slice(1).map((name,index)=>'def '+kind+'_'+
    (kind==='strings'?'v'+(index+1):name.replace(/:/g,'_'))+'() -> U32:\n  '+(index+1)+'\n');
  fs.writeFileSync(path.join(generated,kind+'.bend'),'# Generated stable IDs from Showdown '+pin+'\nimport Base\n\n'+defs.join('\n'));
}
const typeTextDefs=['# Generated exact type text IDs from Showdown '+pin,'import Base',''];
for(const [name,value]of Object.entries({normal:'Normal',flying:'Flying',rock:'Rock',ice:'Ice',stellar:'Stellar',unknown:'???',bird:'Bird'}))
  typeTextDefs.push('def '+name+'() -> U32:\n  '+strings.map[value]+'\n');
typeTextDefs.push('def type_text(kind: Nat) -> U32:\n  match kind:');
for(let index=1;index<types.names.length;index++)
  typeTextDefs.push('    case '+index+'n: '+strings.map[dex.types.get(types.names[index]).name]);
typeTextDefs.push('    case _: 0','');
fs.writeFileSync(path.join(generated,'TypeTexts.bend'),typeTextDefs.join('\n'));
const layout=['# Generated from Showdown '+pin,'import Base',''];
layout.push('def mutable_words() -> U32:\n  '+mutableWords+'\n');
layout.push('def species_type_cache_start() -> U32:\n  '+typeCacheStart+'\n');
layout.push('def species_type_cache_end() -> U32:\n  '+typeCacheEnd+'\n');
layout.push('def slot_hidden_text() -> U32:\n  '+strings.map.hidden+'\n');
layout.push('def slot_hidden_power_prefix_text() -> U32:\n  '+strings.map['Hidden Power ']+'\n');
layout.push('def format_id_text() -> U32:\n  '+strings.map[format.id]+'\n');
layout.push('def format_name_text() -> U32:\n  '+strings.map[format.name]+'\n');
for(const [name,region] of Object.entries(regions))
  for(const field of ['start','count','stride'])
    layout.push('def '+name+'_'+field+'() -> U32:\n  '+region[field]+'\n');
for(const [name,ids] of Object.entries({species,moves,abilities,items,types,roles,conditions,callbacks,strings,targets}))
  if(!regions[name])layout.push('def '+name+'_count() -> U32:\n  '+ids.names.length+'\n');
fs.writeFileSync(path.join(generated,'Layout.bend'),layout.join('\n'));
fs.writeFileSync(path.join(generated,'layout.h'),
  '#ifndef PG9_LAYOUT_H\n#define PG9_LAYOUT_H\n'+
  '#define PG9_WORD_COUNT '+words.length+'u\n#define PG9_MUTABLE_WORDS '+mutableWords+'u\n'+
  '#define PG9_ARRAY_BITS '+Math.log2(words.length)+'\n#endif\n');
const nativeRules=['# Ordered source constants from Showdown '+pin,'import Base','import ../GenData.bend as D',''];
function idList(names,mapping_) {return '['+names.map(x=>lookup(mapping_,x)).join(', ')+']';}
for(const [name,value] of Object.entries(rules)) {
  if(name==='MOVE_PAIRS'||name==='INCOMPATIBLE_PAIRS') {
    nativeRules.push('def '+name.toLowerCase()+'() -> +List<D.MovePair>:\n  ['+
      value.map(pair=>'D.Pair{'+idList(Array.isArray(pair[0])?pair[0]:[pair[0]],moves)+', '+
        idList(Array.isArray(pair[1])?pair[1]:[pair[1]],moves)+'}').join(', ')+']\n');
  } else {
    const mapping_=['PRIORITY_POKEMON','NO_LEAD_POKEMON','DEFENSIVE_TERA_BLAST_USERS'].includes(name)?species:moves;
    nativeRules.push('def '+name.toLowerCase()+'() -> +List<U32>:\n  '+idList(value,mapping_)+'\n');
  }
}
fs.writeFileSync(path.join(generated,'Rules.bend'),nativeRules.join('\n'));
fs.copyFileSync(path.join(root,'build/pokemon_gen9/showdown/LICENSE'),path.join(__dirname,'SHOWDOWN_LICENSE'));
console.log('Exported '+(species.names.length-1)+' species/forms, '+templateCount+' templates; '+cursor+'/'+words.length+' words');
