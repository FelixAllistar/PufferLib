'use strict';
// Restricted source-to-kernel compiler. Unsupported syntax is an error, never
// an implicit scalar fallback. The manifest describes every admitted field.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const {root,pin}=require('./reference.cjs');
const ts=require(path.join(root,'build/pokemon_gen9/batch-deps/node_modules/typescript'));
const sourceRoot=path.join(root,'build/pokemon_gen9/showdown');
const hash=s=>crypto.createHash('sha256').update(s).digest('hex');
const strings=Object.assign(Object.create(null),{'':0});
function intern(s){return strings[s]??(strings[s]=Object.keys(strings).length);}
const parsed=new Map();
function source(file){
  if(!parsed.has(file)){
    const text=fs.readFileSync(path.join(sourceRoot,file),'utf8');
    parsed.set(file,ts.createSourceFile(file,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS));
  }
  return parsed.get(file);
}
function method(file,name){
  let found;function visit(n){if(ts.isMethodDeclaration(n)&&n.name.getText()===name)found=n;ts.forEachChild(n,visit);}
  visit(source(file));if(!found)throw Error('Missing source method '+file+':'+name);return found;
}
function abilities(){
  const result=[];function visit(n){
    if(ts.isVariableDeclaration(n)&&n.name.getText()==='Abilities'){
      for(const owner of n.initializer.properties)if(ts.isPropertyAssignment(owner)&&ts.isObjectLiteralExpression(owner.initializer))
        for(const fn of owner.initializer.properties)if(ts.isMethodDeclaration(fn))result.push({owner:owner.name.getText(),fn});
    }ts.forEachChild(n,visit);
  }visit(source('data/abilities.ts'));return result;
}
function propertyPath(n){
  if(ts.isParenthesizedExpression(n)||ts.isAsExpression(n)||ts.isNonNullExpression(n))return propertyPath(n.expression);
  if(ts.isIdentifier(n))return n.text;
  if(n.kind===ts.SyntaxKind.ThisKeyword)return 'this';
  if(ts.isPropertyAccessExpression(n))return propertyPath(n.expression)+'.'+n.name.text;
  if(ts.isElementAccessExpression(n)&&ts.isStringLiteral(n.argumentExpression))
    return propertyPath(n.expression)+'.'+n.argumentExpression.text;
  throw Error('Not a statically named field');
}
function fieldType(name){
  const parts=name.split('.'),last=parts.at(-1);
  if(parts.includes('flags')){
    if(['contact','bite','punch','sound','pulse','bullet','slicing','wind','healing','charge','protect','recharge'].includes(last))return 'flag';
    throw Error('Unmodeled flag '+name);
  }
  if(['hp','maxhp','level','activeTurns','timesAttacked','weightkg','heightm','hit','basePower','priority','num'].includes(last))return 'number';
  if(['status','type','category','gender'].includes(last))return 'string';
  throw Error('Unmodeled field '+name);
}
const kernels=[],rejected=[];
const helpers={};
const value=(code,type='number',extra={})=>({code,type,...extra});
class Lower {
  constructor(spec){
    this.spec=spec;this.vars=new Map();this.fields=[];this.slots=[];this.lines=[];this.mutates_modifier=false;
    const params=spec.node.parameters.filter(p=>p.name.getText()!=='this');
    this.params=params;
    for(let i=0;i<params.length;i++){
      const name=params[i].name.getText(),shape=spec.shapes[i]||'object';
      if(shape==='object'){this.vars.set(name,{type:'object'});continue;}
      if(shape==='tuple'){
        const items=[0,1].map(index=>{
          const slot=this.slots.length;this.slots.push({kind:'argument',parameter:i,index});
          return value('pg_values['+slot+']');
        });
        this.vars.set(name,{type:'tuple',items});this.lines.push('double pg_var_'+name+' = NAN;');
      }else{
        const slot=this.slots.length;this.slots.push({kind:'argument',parameter:i,
          default:params[i].initializer?Number(params[i].initializer.getText()):null});
        this.lines.push('double pg_var_'+name+' = pg_values['+slot+'];');this.vars.set(name,value('pg_var_'+name));
      }
    }
  }
  fail(node,reason){throw Error(reason+' at '+this.spec.file+':'+(source(this.spec.file).getLineAndCharacterOfPosition(node.getStart()).line+1));}
  field(node){
    const name=propertyPath(node);
    if(name==='this.event.modifier')return value('(*pg_ctx_modifier)');
    if(name==='this.gen')return value('9');
    if(name.startsWith('this.'))this.fail(node,'Unmodeled battle context '+name);
    const rootName=name.split('.')[0];if(this.vars.get(rootName)?.type!=='object')this.fail(node,'Non-object field receiver');
    const type=fieldType(name);let index=this.fields.findIndex(f=>f.path===name);
    if(index<0){index=this.fields.length;this.fields.push({path:name,type,slot:this.slots.length});this.slots.push({kind:'field',path:name,type});}
    return value('pg_values['+this.fields[index].slot+']',type);
  }
  expr(n,booleanContext=false){
    if(ts.isParenthesizedExpression(n)||ts.isAsExpression(n)||ts.isNonNullExpression(n))return this.expr(n.expression,booleanContext);
    if(ts.isNumericLiteral(n))return value(n.text);
    if(ts.isStringLiteral(n))return value(String(intern(n.text)),'string');
    if(n.kind===ts.SyntaxKind.TrueKeyword||n.kind===ts.SyntaxKind.FalseKeyword)return value(n.kind===ts.SyntaxKind.TrueKeyword?'1':'0','boolean');
    if(n.kind===ts.SyntaxKind.NullKeyword)return value('0','null');
    if(ts.isIdentifier(n)){
      if(n.text==='undefined')return value('0','undefined');
      const item=this.vars.get(n.text);if(!item)this.fail(n,'Unbound value '+n.text);return item;
    }
    if(ts.isArrayLiteralExpression(n)){
      if(n.elements.length!==2)this.fail(n,'Only numeric ratio tuples are admitted');
      const items=n.elements.map(e=>this.expr(e));if(items.some(x=>x.type!=='number'))this.fail(n,'Non-numeric ratio tuple');
      return {type:'tuple',items};
    }
    if(ts.isPropertyAccessExpression(n))return this.field(n);
    if(ts.isElementAccessExpression(n)){
      if(ts.isStringLiteral(n.argumentExpression))return this.field(n);
      const receiver=this.expr(n.expression);
      if(receiver.type==='tuple'&&ts.isNumericLiteral(n.argumentExpression)){
        const i=Number(n.argumentExpression.text);if(i!==0&&i!==1)this.fail(n,'Ratio index');return receiver.items[i];
      }
      this.fail(n,'Dynamic index');
    }
    if(ts.isPrefixUnaryExpression(n)){
      const x=this.expr(n.operand),op=n.operator;
      if(op===ts.SyntaxKind.ExclamationToken){if(!x.code)this.fail(n,'Object truth requires a tagged value');return value('!pg_truth('+x.code+')','boolean');}
      if(x.type!=='number')this.fail(n,'Numeric unary operator on '+x.type);
      if(op===ts.SyntaxKind.MinusToken)return value('(-('+x.code+'))');
      if(op===ts.SyntaxKind.PlusToken)return x;
      this.fail(n,'Unary operator');
    }
    if(ts.isConditionalExpression(n)){
      const c=this.expr(n.condition,true),a=this.expr(n.whenTrue),b=this.expr(n.whenFalse);
      if(a.type!==b.type||!a.code||!b.code)this.fail(n,'Mixed conditional value');
      return value('(pg_truth('+c.code+') ? '+a.code+' : '+b.code+')',a.type);
    }
    if(ts.isBinaryExpression(n)){
      const op=n.operatorToken.kind;
      const logical=op===ts.SyntaxKind.AmpersandAmpersandToken||op===ts.SyntaxKind.BarBarToken;
      const a=this.expr(n.left,logical),b=this.expr(n.right,logical);
      if(logical){
        if(!a.code||!b.code)this.fail(n,'Object logical operand requires a tagged value');
        if(!booleanContext&&(a.type!=='boolean'||b.type!=='boolean'))this.fail(n,'Value-producing scalar logical operator');
        return value('(pg_truth('+a.code+') '+(op===ts.SyntaxKind.AmpersandAmpersandToken?'&&':'||')+' pg_truth('+b.code+'))','boolean');
      }
      const compare=new Map([[ts.SyntaxKind.EqualsEqualsEqualsToken,'=='],[ts.SyntaxKind.ExclamationEqualsEqualsToken,'!='],
        [ts.SyntaxKind.LessThanToken,'<'],[ts.SyntaxKind.LessThanEqualsToken,'<='],[ts.SyntaxKind.GreaterThanToken,'>'],[ts.SyntaxKind.GreaterThanEqualsToken,'>=']]);
      if(compare.has(op)){
        if(!a.code||!b.code)this.fail(n,'Object comparison requires identity semantics');
        if(a.type!==b.type)this.fail(n,'Mixed-type comparison');
        if((a.type==='string'||a.type==='boolean')&&!['==','!='].includes(compare.get(op)))this.fail(n,'Ordered nonnumeric comparison');
        return value('('+a.code+' '+compare.get(op)+' '+b.code+')','boolean');
      }
      if(a.type!=='number'||b.type!=='number')this.fail(n,'Arithmetic on nonnumeric value');
      const arithmetic=new Map([[ts.SyntaxKind.PlusToken,'+'],[ts.SyntaxKind.MinusToken,'-'],[ts.SyntaxKind.AsteriskToken,'*'],[ts.SyntaxKind.SlashToken,'/']]);
      if(arithmetic.has(op))return value('('+a.code+' '+arithmetic.get(op)+' '+b.code+')');
      if(op===ts.SyntaxKind.PercentToken)return value('fmod('+a.code+','+b.code+')');
      if(op===ts.SyntaxKind.AsteriskAsteriskToken)return value('pow('+a.code+','+b.code+')');
      if(op===ts.SyntaxKind.GreaterThanGreaterThanGreaterThanToken)return value('pg_ushr('+a.code+','+b.code+')');
      if(op===ts.SyntaxKind.GreaterThanGreaterThanToken)return value('pg_shr('+a.code+','+b.code+')');
      this.fail(n,'Unsupported binary operator');
    }
    if(ts.isCallExpression(n)){
      const name=ts.isIdentifier(n.expression)?n.expression.text:propertyPath(n.expression);
      if(name==='Math.floor'){
        if(n.arguments.length!==1)this.fail(n,'Math.floor arity');
        const x=this.expr(n.arguments[0]);if(x.type!=='number')this.fail(n,'Math.floor primitive type');
        return value('floor('+x.code+')');
      }
      if(name==='Array.isArray'){
        const x=this.expr(n.arguments[0]);return value(x.type==='tuple'?'1':'0','boolean',{constant:x.type==='tuple'});
      }
      if(name==='this.debug'){
        // Explicit frozen contract: debugMode=false. These callbacks otherwise
        // have no public logging effects, but debug-enabled kernels are rejected.
        for(const arg of n.arguments){
          const scan=node=>{
            if(ts.isCallExpression(node)||ts.isBinaryExpression(node)&&node.operatorToken.kind===ts.SyntaxKind.EqualsToken||
               ts.isPostfixUnaryExpression(node))this.fail(node,'Potential side effect in debug argument');
            ts.forEachChild(node,scan);
          };scan(arg);
        }
        return value('0','undefined',{noEmit:true});
      }
      const alias=this.vars.get(name);
      const helper=alias?.type==='alias'?alias.helper:name.startsWith('this.')?name.slice(5):null;
      if(helper&&helpers[helper]){
        const args=n.arguments.map(a=>this.expr(a));
        const target=helpers[helper].find(h=>h.shapes.every((shape,i)=>i>=args.length||shape===args[i].type));
        if(!target)this.fail(n,'Unsupported helper signature '+helper);
        if(target.mutates_modifier)this.mutates_modifier=true;
        const flat=[];
        for(let i=0;i<target.params.length;i++){
          const arg=args[i]||value(String(target.params[i].default));
          if(arg.type==='tuple')flat.push(...arg.items.map(x=>x.code));else flat.push(arg.code);
        }
        const call='pg_fn_'+target.id+'(pg_ctx_modifier,pg_ctx_written,(double[]){'+flat.join(',')+'})';
        return target.name==='chainModify'?value(call,'undefined',{callResult:true}):value(call+'.value');
      }
      this.fail(n,'Unlowered call '+name);
    }
    this.fail(n,'Unsupported expression '+ts.SyntaxKind[n.kind]);
  }
  output(x){
    if(x.type==='undefined')return x.callResult?'return '+x.code+';':'return (PgOut){0,0};';
    const tags={number:1,boolean:2,null:3};if(tags[x.type]===undefined)throw Error('Unsupported return type '+x.type);
    return 'return (PgOut){'+tags[x.type]+','+x.code+'};';
  }
  statement(n){
    if(ts.isBlock(n)){for(const s of n.statements)this.statement(s);return;}
    if(ts.isVariableStatement(n)){
      for(const d of n.declarationList.declarations){
        const name=d.name.getText();if(!ts.isIdentifier(d.name)||!d.initializer)this.fail(d,'Variable declaration');
        if(ts.isPropertyAccessExpression(d.initializer)&&propertyPath(d.initializer)==='this.trunc'){
          this.vars.set(name,{type:'alias',helper:'trunc'});continue;
        }
        const x=this.expr(d.initializer);if(x.type==='tuple'){this.vars.set(name,x);continue;}
        if(!['number','string','boolean'].includes(x.type))this.fail(d,'Non-scalar local');
        this.lines.push('double pg_var_'+name+' = '+x.code+';');this.vars.set(name,value('pg_var_'+name,x.type));
      }return;
    }
    if(ts.isIfStatement(n)){
      const c=this.expr(n.expression,true);
      if(c.constant!==undefined){if(c.constant)this.statement(n.thenStatement);else if(n.elseStatement)this.statement(n.elseStatement);return;}
      this.lines.push('if (pg_truth('+c.code+')) {');this.statement(n.thenStatement);this.lines.push('}');
      if(n.elseStatement){this.lines.push('else {');this.statement(n.elseStatement);this.lines.push('}');}return;
    }
    if(ts.isReturnStatement(n)){this.lines.push(this.output(n.expression?this.expr(n.expression):value('0','undefined')));return;}
    if(ts.isExpressionStatement(n)){
      const e=n.expression;
      if(ts.isBinaryExpression(e)&&e.operatorToken.kind===ts.SyntaxKind.EqualsToken){
        const x=this.expr(e.right);
        if(x.type!=='number')this.fail(e,'Only numeric assignments admitted');
        let left;
        if(ts.isIdentifier(e.left)){
          const old=this.vars.get(e.left.text);if(!old||!['number','tuple'].includes(old.type))this.fail(e,'Assignment receiver');
          left='pg_var_'+e.left.text;this.vars.set(e.left.text,value(left));
        }else if(propertyPath(e.left)==='this.event.modifier'){left='(*pg_ctx_modifier)';this.mutates_modifier=true;}
        else this.fail(e,'Unmodeled mutation');
        this.lines.push(left+' = '+x.code+';');if(left==='(*pg_ctx_modifier)')this.lines.push('(*pg_ctx_written) = 1;');return;
      }
      const x=this.expr(e);if(!x.noEmit){if(!x.callResult)this.fail(e,'Only void helper calls admitted as statements');this.lines.push('(void)'+x.code+';');}return;
    }
    this.fail(n,'Unsupported statement '+ts.SyntaxKind[n.kind]);
  }
  compile(){
    this.statement(this.spec.node.body);this.lines.push('return (PgOut){0,0};');
    return {slots:this.slots,fields:this.fields,mutates_modifier:this.mutates_modifier,body:this.lines.join('\n')};
  }
}
function admit(spec){
  const id=kernels.length,lower=new Lower(spec),compiled=lower.compile();
  const params=spec.node.parameters.filter(p=>p.name.getText()!=='this').map(p=>({name:p.name.getText(),default:p.initializer?Number(p.initializer.getText()):null}));
  const item={id,key:spec.key,file:spec.file,name:spec.node.name.getText(),owner:spec.owner||null,
    kind:spec.kind||(!spec.owner?'helper':'ability'),expression:spec.expression||null,
    shapes:spec.shapes,params,...compiled,line:source(spec.file).getLineAndCharacterOfPosition(spec.node.getStart()).line+1,
    source_sha256:hash(spec.node.getText())};
  kernels.push(item);return item;
}
// Whitespace, TypeScript casts and redundant parentheses do not distinguish
// the clean source expression from esbuild's copy of the same expression.
function expressionShape(node){
  if(ts.isParenthesizedExpression(node)||ts.isAsExpression(node)||ts.isNonNullExpression(node))return expressionShape(node.expression);
  if(ts.isIdentifier(node))return ['identifier',node.text];
  if(ts.isNumericLiteral(node))return ['number',Number(node.text)];
  if(ts.isStringLiteral(node))return ['string',node.text];
  const children=[];ts.forEachChild(node,n=>{children.push(expressionShape(n));});return [node.kind,...children];
}
function expressionHash(node){return hash(JSON.stringify(expressionShape(node)));}
function expressions(){
  const pure=new Set(['trunc','modify','chain']);
  for(const file of ['sim/battle.ts','sim/battle-actions.ts','sim/pokemon.ts','sim/side.ts','sim/field.ts','sim/battle-queue.ts']){
    const tree=source(file),methods=[];
    function collect(n){if(ts.isMethodDeclaration(n)&&n.body)methods.push(n);ts.forEachChild(n,collect);}collect(tree);
    for(const fn of methods){
      if(pure.has(fn.name.getText())||fn.name.getText()==='chainModify')continue;
      const aliases=new Map();
      function alias(n){
        if(ts.isFunctionLike(n)&&n!==fn)return;
        if(ts.isVariableDeclaration(n)&&ts.isIdentifier(n.name)&&n.initializer){
          try{
            const p=propertyPath(n.initializer),parts=p.split('.'),helper=parts.pop(),receiver=parts.join('.');
            if(pure.has(helper)&&['this','this.battle'].includes(receiver))aliases.set(n.name.text,{helper,receiver});
          }catch{}
        }ts.forEachChild(n,alias);
      }alias(fn.body);
      function tryExpression(node){
        const free=new Map(),usedAliases=new Map(),directHelpers=new Set(),receivers=new Set();let calls=0,invalid=false;
        function helper(call){
          if(ts.isIdentifier(call.expression)&&aliases.has(call.expression.text))return {...aliases.get(call.expression.text),alias:call.expression.text};
          try{
            const p=propertyPath(call.expression),parts=p.split('.'),name=parts.pop(),receiver=parts.join('.');
            if(pure.has(name)&&['this','this.battle'].includes(receiver))return {helper:name,receiver};
          }catch{}return null;
        }
        function scan(n){
          if(ts.isFunctionLike(n)){invalid=true;return;}
          if(ts.isCallExpression(n)){
            const h=helper(n);
            if(h){calls++;receivers.add(h.receiver);if(h.alias)usedAliases.set(h.alias,h);else directHelpers.add(h.helper);}
            else {try{if(propertyPath(n.expression)!=='Math.floor')invalid=true;}catch{invalid=true;}}
            for(const a of n.arguments)scan(a);return;
          }
          if(ts.isPropertyAccessExpression(n)){
            let p;try{p=propertyPath(n);}catch{invalid=true;return;}
            const rootName=p.split('.')[0];
            if(rootName!=='this'&&rootName!=='Math')free.set(rootName,'object');
            return;
          }
          if(ts.isIdentifier(n)){
            if(n.text!=='undefined'&&!usedAliases.has(n.text)&&!aliases.has(n.text))
              if(!free.has(n.text))free.set(n.text,'number');
            return;
          }
          ts.forEachChild(n,scan);
        }scan(node);
        if(invalid||calls<2||receivers.size!==1)return false;
        const receiver=[...receivers][0],originHash=expressionHash(node),key='expression.'+file+'.'+originHash;
        if(kernels.some(k=>k.key===key))return true;
        let invalidThis=false;
        const normalized=ts.transform(node,[context=>{
          function visit(n){
            if(ts.isPropertyAccessExpression(n)){
              try{
                const p=propertyPath(n);
                if(p.startsWith('this.')&&receiver==='this.battle'){
                  if(!p.startsWith('this.battle.'))invalidThis=true;
                  else if(p==='this.battle')return ts.factory.createThis();
                }
              }catch{}
              if(receiver==='this.battle'&&ts.isPropertyAccessExpression(n.expression)&&
                 n.expression.expression.kind===ts.SyntaxKind.ThisKeyword&&n.expression.name.text==='battle')
                return ts.factory.createPropertyAccessExpression(ts.factory.createThis(),n.name);
            }
            return ts.visitEachChild(n,visit,context);
          }return n=>ts.visitNode(n,visit);
        }]);
        let expression=ts.createPrinter().printNode(ts.EmitHint.Expression,normalized.transformed[0],tree);normalized.dispose();
        if(invalidThis)return false;
        expression=ts.transpileModule('const result='+expression+';',{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText;
        const js=ts.createSourceFile('expression.js',expression,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
        expression=js.statements[0].declarationList.declarations[0].initializer.getText(js);
        const name='__pg9_expression_'+kernels.length,parameters=[...free.keys()];
        const aliasText=[...usedAliases].map(([name,h])=>'const '+name+'=this.'+h.helper+';').join('\n');
        const body=aliasText+'\nreturn '+expression+';';
        const virtual=file.replace('.ts','')+'.expressions/'+name+'.ts';
        const text='class Expression { '+name+'('+parameters.join(',')+'){'+body+'} }';
        const ast=ts.createSourceFile(virtual,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);parsed.set(virtual,ast);
        const spec={kind:'expression',key,file:virtual,node:ast.statements[0].members[0],shapes:[...free.values()],
          expression:{file,line:tree.getLineAndCharacterOfPosition(node.getStart()).line+1,method:fn.name.getText(),
            source_sha256:hash(node.getText(tree)),shape_sha256:originHash,receiver,parameters,
            aliases:[...usedAliases].map(([name,h])=>({name,helper:h.helper})),direct_helpers:[...directHelpers],
            reference_source:'function('+parameters.join(',')+'){'+body+'}'}};
        try{admit(spec);return true;}
        catch(error){rejected.push({key,reason:error.message});return false;}
      }
      function walk(n){
        if(ts.isFunctionLike(n))return;
        if((ts.isCallExpression(n)||ts.isBinaryExpression(n)||ts.isConditionalExpression(n)||ts.isPrefixUnaryExpression(n))&&tryExpression(n))return;
        ts.forEachChild(n,walk);
      }walk(fn.body);
    }
  }
}
function build(){
  if(cp.execFileSync('git',['-C',sourceRoot,'rev-parse','HEAD'],{encoding:'utf8'}).trim()!==pin||
     cp.execFileSync('git',['-C',sourceRoot,'status','--porcelain','--untracked-files=no'],{encoding:'utf8'}).trim())
    throw Error('Batch compiler requires the clean pinned source');
  for(const [name,file,variants] of [
    ['trunc','sim/dex.ts',[['number','number']]],
    ['chain','sim/battle.ts',[['number','number'],['number','tuple'],['tuple','number'],['tuple','tuple']]],
    ['chainModify','sim/battle.ts',[['number','number'],['tuple','number']]],
    ['modify','sim/battle.ts',[['number','number','number'],['number','tuple','number']]],
  ]){
    helpers[name]=[];for(const shapes of variants)helpers[name].push(admit({key:'helper.'+name+'.'+shapes.join('-'),name,file,node:method(file,name),shapes}));
  }
  const events=new Set(['onModifyAtk','onModifySpA','onModifyDef','onModifySpD','onModifySpe','onModifyWeight','onBasePower','onModifyDamage']);
  for(const {owner,fn} of abilities()){
    const name=fn.name.getText(),key='ability.'+owner+'.'+name;
    if(!events.has(name)){rejected.push({key,reason:'Outside numeric-event prototype'});continue;}
    const shapes=fn.parameters.map((_,i)=>i===0?'number':'object');
    try{admit({key,owner,file:'data/abilities.ts',node:fn,shapes});}
    catch(error){rejected.push({key,reason:error.message});}
  }
  expressions();
  const prelude=`#include <stdint.h>\n#include <stddef.h>\n#include <math.h>\n#include <assert.h>\n
typedef struct {uint32_t tag; double value;} PgOut;
static inline int pg_truth(double x){return x!=0&&!isnan(x);}
static inline uint32_t pg_u32(double x){if(x>=-4294967296.0&&x<4294967296.0)return (uint32_t)(int64_t)x;if(!isfinite(x)||x==0)return 0;double y=fmod(trunc(x),4294967296.0);if(y<0)y+=4294967296.0;return (uint32_t)y;}
static inline double pg_ushr(double x,double bits){return (double)(pg_u32(x)>>(pg_u32(bits)&31));}
static inline double pg_shr(double x,double bits){uint32_t u=pg_u32(x);double signedX=u>=2147483648u?(double)u-4294967296.0:(double)u;return floor(signedX/ldexp(1.0,pg_u32(bits)&31));}
`;
  const compilerHash=hash(fs.readFileSync(__filename));
  const irHash=hash(JSON.stringify({abi:2,compiler_sha256:compilerHash,prelude,strings,kernels}));
  let c=prelude+'const char *pg9_kernel_source_hash(void){return "'+irHash+'";}\n';
  for(const k of kernels)c+='static inline PgOut pg_fn_'+k.id+'(double *pg_ctx_modifier,unsigned *pg_ctx_written,const double *pg_values){\n'+k.body+'\n}\n';
  c+='int pg9_kernel_slots(unsigned op){switch(op){\n'+kernels.map(k=>'case '+k.id+':return '+k.slots.length+';').join('\n')+'\ndefault:return -1;}}\n';
  c+='static void pg9_kernel_main(unsigned op,size_t count,const double *const *inputs,double *restrict modifier,uint32_t *restrict tags,double *restrict output,int checked){\nswitch(op){\n';
  for(const k of kernels){
    c+='case '+k.id+': for(size_t lane=0;lane<count;lane++){double v['+Math.max(1,k.slots.length)+']={'+k.slots.map((_,i)=>'inputs['+i+'][lane]').join(',')+'};unsigned written=0;PgOut r=pg_fn_'+k.id+'(modifier+lane,&written,v);tags[lane]=r.tag|(checked&&written?256u:0u);output[lane]=r.value;}break;\n';
  }
  c+='default:assert(0);}}\n';
  for(const [name,checked] of [['pg9_kernel',0],['pg9_kernel_checked',1]])
    c+='void '+name+'(unsigned op,size_t count,const double *const *inputs,double *restrict modifier,uint32_t *restrict tags,double *restrict output){pg9_kernel_main(op,count,inputs,modifier,tags,output,'+checked+');}\n';
  const dir=path.join(root,'build/pokemon_gen9/batch');fs.mkdirSync(dir,{recursive:true});
  fs.writeFileSync(path.join(dir,'kernels.c'),c);
  const manifest={revision:pin,compiler_typescript:ts.version,contract:'Gen 9, debugMode=false, explicit primitive field schema. This is a numeric-event lowering prototype; unsupported handlers fail closed.',
    abi:2,compiler_sha256:compilerHash,
    strings,kernels:kernels.map(({body,...k})=>k),rejected,source_ir_sha256:irHash,generated_c_sha256:hash(c)};
  fs.writeFileSync(path.join(dir,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
  console.log(JSON.stringify({kernels:kernels.length,ability_handlers:kernels.filter(k=>k.owner).length,
    numeric_expression_kernels:kernels.filter(k=>k.kind==='expression').length,rejected:rejected.length,revision:pin}));
  return manifest;
}
module.exports={build,expressionHash};
if(require.main===module){if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');build();}
