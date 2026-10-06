'use strict';
// Lossless schema for the existing player-only encoder. Singular categories
// are IDs (0 = all-zero block; n+1 = original one-hot n). Sets use 16-bit words
// stored as exact float32 integers. Numeric features are copied unchanged.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const {root,pin}=require('./reference.cjs');
const source=require('./worker_core.cjs');
const ts=require(path.join(root,'build/pokemon_gen9/batch-deps/node_modules/typescript'));
const f=ts.factory,blocks=[];
let dense=0,compact=0;
function block(name,width,kind){
  const compact_width=kind===0?width:kind===1?1:Math.ceil(width/16);
  blocks.push({name,dense,dense_width:width,compact,compact_width,kind});dense+=width;compact+=compact_width;
}
function schema(){
  const s=source.sizes;
  block('global',32,0);
  for(const name of ['field_conditions','own_side_conditions','opponent_side_conditions'])block(name,s.conditions,2);
  for(let seat=0;seat<2;seat++)for(let mon=0;mon<6;mon++){
    const name='seat'+seat+'.mon'+mon;
    block(name+'.numeric',32,0);
    for(const kind of ['species','abilities','items'])block(name+'.'+kind,s[kind],1);
    block(name+'.types',s.types,2);block(name+'.tera_type',s.types,1);block(name+'.moves',s.moves,2);
  }
  for(let seat=0;seat<2;seat++){
    block('seat'+seat+'.active_numeric',8,0);block('seat'+seat+'.active_conditions',s.conditions,2);
  }
  for(let move=0;move<4;move++){block('candidate'+move+'.numeric',16,0);block('candidate'+move+'.id',s.moves,1);}
  if(dense!==source.OBS)throw Error('Dense/source encoder layout mismatch');
  return {revision:pin,abi:2,dense_observations:dense,observations:compact,actions:source.ACTIONS,blocks,
    contract:'Lossless existing public encoder; exact float32 numeric values, one-hot IDs and complete category sets. Policy IDs are lookup selectors, not ordinal numeric features. Requires float32 precision.'};
}
function encodeSource(report){
  const text=fs.readFileSync(path.join(root,'ocean/pokemon_gen9/worker_core.cjs'),'utf8');
  const file=ts.createSourceFile('worker_core.cjs',text,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
  const selected=file.statements.filter(n=>ts.isFunctionDeclaration(n)&&['one','keys','encode'].includes(n.name.text));
  if(selected.length!==3)throw Error('Expected original encoder and its two writing helpers');
  const result=ts.transform(f.updateSourceFile(file,selected),[context=>{
    function visit(n){
      if(ts.isBinaryExpression(n)&&n.operatorToken.kind===ts.SyntaxKind.EqualsToken&&
         ts.isElementAccessExpression(n.left)&&ts.isIdentifier(n.left.expression)&&['a','out'].includes(n.left.expression.text))
        return f.createCallExpression(f.createIdentifier('put'),undefined,[n.left.expression,
          ts.visitNode(n.left.argumentExpression,visit),ts.visitNode(n.right,visit)]);
      if(ts.isParameter(n)&&n.name.getText(file)==='out'&&n.initializer)
        return f.updateParameterDeclaration(n,n.modifiers,n.dotDotDotToken,n.name,n.questionToken,n.type,
          f.createNewExpression(f.createIdentifier('Float32Array'),undefined,[f.createIdentifier('COMPACT_OBS')]));
      return ts.visitEachChild(n,visit,context);
    }
    return node=>ts.visitNode(node,visit);
  }]);
  const generated=ts.createPrinter().printFile(result.transformed[0]);result.dispose();
  return `'use strict';
const source=require(${JSON.stringify(path.join(root,'ocean/pokemon_gen9/worker_core.cjs'))});
const {cat,norm,stats,boosts,status,Dex}=source.encoderBindings;
const {MON,ACTIVE,MOVE}=source;
const sizes=source.sizes,OBS=source.OBS,COMPACT_OBS=${report.observations};
const schema=require('./schema.json');
const index=new Uint16Array(OBS),kind=new Uint8Array(OBS),code=new Uint16Array(OBS);
for(const b of schema.blocks)for(let i=0;i<b.dense_width;i++){
  const d=b.dense+i;kind[d]=b.kind;
  index[d]=b.compact+(b.kind===0?i:b.kind===2?i>>4:0);
  code[d]=b.kind===1?i+1:b.kind===2?1<<(i&15):0;
}
function put(out,dense,value){
  const at=index[dense];
  if(kind[dense]===0){out[at]=value;return value;}
  if(value!==0&&value!==1)throw Error('Categorical source encoder value is not binary');
  if(kind[dense]===1){if(value)out[at]=code[dense];else if(out[at]===code[dense])out[at]=0;}
  else out[at]=value?out[at]|code[dense]:out[at]&~code[dense];
  return value;
}
${generated}
function unpack(input,out=new Float32Array(OBS)){
  if(input.length!==COMPACT_OBS||out.length!==OBS)throw Error('Compact/dense observation length mismatch');
  for(let i=0;i<OBS;i++)out[i]=kind[i]===0?input[index[i]]:
    kind[i]===1?+(input[index[i]]===code[i]):+!!(input[index[i]]&code[i]);
  return out;
}
module.exports={encode,unpack,schema,OBS:COMPACT_OBS,DENSE_OBS:OBS,ABI:schema.abi,ACTIONS:source.ACTIONS};
`;
}
function build(){
  if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
  const report=schema(),generated=encodeSource(report);
  report.source_encoder_sha256=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'ocean/pokemon_gen9/worker_core.cjs'))).digest('hex');
  report.generated_encoder_sha256=crypto.createHash('sha256').update(generated).digest('hex');
  report.schema_sha256=crypto.createHash('sha256').update(JSON.stringify(report)).digest('hex');
  const out=path.join(root,'build/pokemon_gen9/compact');fs.mkdirSync(out,{recursive:true});
  fs.writeFileSync(path.join(out,'schema.json'),JSON.stringify(report,null,2)+'\n');
  fs.writeFileSync(path.join(out,'encode.cjs'),generated);
  const table=blocks.map(b=>'{'+[b.dense,b.dense_width,b.compact,b.compact_width,b.kind].join(',')+'}').join(',');
  fs.writeFileSync(path.join(root,'ocean/pokemon_gen9/compact_layout.h'),`#pragma once
/* Lossless ABI 2 generated from the original public encoder. */
#define PG9_WORKER_ABI 2
#define PG9_OBS ${report.observations}
#define PG9_DENSE_OBS ${report.dense_observations}
#define PG9_ACTIONS ${report.actions}
#define PG9_COMPACT_BLOCK_COUNT ${blocks.length}
#define PG9_COMPACT_BLOCKS_DATA {${table}}
#define PG9_COMPACT_SCHEMA_HASH "${report.schema_sha256}"
`);
  console.log(JSON.stringify({abi:2,dense_observations:dense,compact_observations:compact,blocks:blocks.length,
    observation_bytes_ratio:dense/compact,schema_sha256:report.schema_sha256}));return report;
}
module.exports={build};if(require.main===module)build();
