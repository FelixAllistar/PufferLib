'use strict';
const assert=require('assert'),vm=require('vm');
const compiler=require('./continuation_compile.cjs'),runtime=require('./continuation_runtime.cjs');
if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
const source=`
const effects={handler(leaf){return leaf('handler');},__proto__(leaf){return leaf('proto');}};
class Fixture {
  branch(flag,leaf) { if(flag)return leaf('branch'); return 17; }
  reassigned(flag,leaf) { flag=!flag; if(flag)leaf('reassigned'); return flag; }
  shadow(flag,leaf) { { const flag=true; if(flag)leaf('shadow'); } return flag; }
  destructured(flag,leaf) { [flag]=[!flag]; if(flag)leaf('destructured'); return flag; }
  compare(a,b) { return a-b; }
  withComparator(comparator=this.compare) { return comparator(6,1); }
  spreadBranch(a,flag,...rest) { if(flag)return rest[1]('spread'); return 0; }
  restFlag(leaf,...flag) { if(flag)return leaf('rest'); return 0; }
  exercise(leaf) {
    const events=[], mark=x=>(events.push(x),leaf(x));
    const object={get method(){events.push('get');return function(x){events.push(this===object?'this':'bad');return x;}}};
    const result=object.method(mark('argument'));
    const nil=null;
    const a=nil?.method(mark('skipped'));
    const b=object.missing?.(mark('skipped2'));
    const key=()=>mark('key');
    const c=nil?.[key()](mark('skipped3'));
    const d=object?.method(mark('optional'));
    const sparse=[1,,3];
    const mapped=sparse.map((x,i)=>{mark('map'+i);if(i===0)sparse.push(4);return leaf(x*2);});
    const found=[,2].find((x,i)=>{mark('find'+i);return x===undefined;});
    const reduced=[,2,3].reduce((a,x)=>leaf(a+x));
    const arrayLike={length:2,0:'zero',1:'one',[Symbol.iterator](){throw Error('iterator used');}};
    const apply=function(a,b){return [this.value,a,b];};
    const applied=apply.apply({value:7},arrayLike);
    const bound=apply.bind({value:9},'first');
    const boundResult=bound('second');
    const short=false&&mark('short');
    const special=[this.branch(false,leaf),this.reassigned(false,leaf),this.shadow(false,leaf),this.destructured(false,leaf),
      this.withComparator(),this.spreadBranch(...[false,true],false,leaf),this.restFlag(leaf,false),effects.handler(leaf),effects.__proto__(leaf)];
    const metadata=[effects.handler.name,Reflect.ownKeys(effects.handler),Object.hasOwn(effects,'__proto__')];
    try { new effects.handler(leaf); metadata.push('constructible'); } catch(e) { metadata.push(e.name); }
    return {events,result,a,b,c,d,mapped,found,reduced,applied,boundResult,short,special,metadata};
  }
  failure(leaf) { return (null).method(leaf('never')); }
  badApply(leaf) { return leaf.apply(null,7); }
}
module.exports=Fixture;
`;
function load(text){const module={exports:{}};vm.runInThisContext('(function(module,require){'+text+'\n})')(module,require);return module.exports;}
compiler.analyze([['semantic-fixture.js',source]]);
const Original=load(source),Transformed=load(compiler.transform(source,'semantic-fixture.js'));
function leaf(x){return x;}
runtime.register(leaf,function*(x){return x;},'fixture-leaf');
runtime.resetMetrics();
const expected=new Original().exercise(leaf);
const actual=runtime.run([runtime.invoke(new Transformed().exercise,new Transformed(),[leaf])],{profile:true})[0];
assert.deepEqual(actual,expected);
assert(runtime.snapshot().source_calls>10,'Fixtures must exercise generated continuations');
const calls=runtime.snapshot().calls_by_source;
assert(!Object.keys(calls).some(k=>k.endsWith(':branch')),'Known false branch should stay scalar');
assert(!Object.keys(calls).some(k=>k.endsWith(':withComparator')),'Known pure comparator should stay scalar');
for(const name of ['reassigned','shadow','destructured','spreadBranch','restFlag'])assert(Object.keys(calls).some(k=>k.endsWith(':'+name)),name+' must retain its continuation');
for(const name of ['failure','badApply']){
  let expectedError,actualError;
  try{new Original()[name](leaf);}catch(e){expectedError=e;}
  try{const target=new Transformed();runtime.run([runtime.invoke(target[name],target,[leaf])]);}catch(e){actualError=e;}
  assert.equal(actualError?.name,expectedError?.name,name);
}
assert.equal(Object.getOwnPropertyNames(Transformed.prototype).some(x=>x.startsWith('__pg9_g_')),false);
console.log('PASS: source continuation call order, optional chains, sparse callbacks, apply/bind and errors');
