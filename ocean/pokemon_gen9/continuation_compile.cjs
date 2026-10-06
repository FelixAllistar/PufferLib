'use strict';
// Keep original scalar methods for constructors/external compatibility and
// generate resumable counterparts from their actual JavaScript AST. A method
// call evaluates its receiver and function before its arguments, just as JS
// does; optional chains retain their argument short-circuiting.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const {root,oracle,pin}=require('./reference.cjs');
const ts=require(path.join(root,'build/pokemon_gen9/batch-deps/node_modules/typescript'));
const f=ts.factory;
const {expressionHash}=require('./batch_compile.cjs');
const runtimeFile=path.join(root,'ocean/pokemon_gen9/continuation_runtime.cjs');
const out=path.join(root,'build/pokemon_gen9/continuation');
const hash=s=>crypto.createHash('sha256').update(s).digest('hex');
const rt=name=>f.createPropertyAccessExpression(f.createIdentifier('__pg9rt'),name);
const call=(name,args)=>f.createCallExpression(rt(name),undefined,args);
const arr=xs=>f.createArrayLiteralExpression(xs);
const str=s=>f.createStringLiteral(s);
const undef=()=>f.createVoidZero();
const par=n=>f.createParenthesizedExpression(n);
const bin=(a,token,b)=>f.createBinaryExpression(a,f.createToken(token),b);
const comma=(a,b)=>par(bin(a,ts.SyntaxKind.CommaToken,b));
const assign=(a,b)=>bin(a,ts.SyntaxKind.EqualsToken,b);
const yieldCall=args=>par(f.createYieldExpression(f.createToken(ts.SyntaxKind.AsteriskToken),call('invoke',args)));
const entries=[],rejected=[];
const suspendNames=new Set(),callbackArrays=new Set(['map','filter','flatMap','some','every','find','findIndex','findLast','findLastIndex','forEach','reduce','reduceRight']);
const methodBodies=new Map(),specializing=new Set(),nativeNames=new Set(),inspectingFunctions=new Set();
const scalarGlobals=new Set(['Array','Object','String','Number','Boolean','parseInt','parseFloat','isNaN','isFinite']);
const expressionKernels=new Map();
const expressionFiles=new Set();
let sourceFile,context,fileName;
function id(node){return fileName+':'+(sourceFile.getLineAndCharacterOfPosition(node.getStart()).line+1)+':'+(node.name?.getText(sourceFile)||'closure');}
function params(node){return node.parameters.map(p=>f.updateParameterDeclaration(p,p.modifiers,p.dotDotDotToken,p.name,p.questionToken,undefined,p.initializer));}
function memberName(expression){
  if(ts.isPropertyAccessExpression(expression))return expression.name.text;
  if(ts.isElementAccessExpression(expression)&&ts.isStringLiteral(expression.argumentExpression))return expression.argumentExpression.text;
  return null;
}
function sourceImport(expression){
  while(ts.isParenthesizedExpression(expression))expression=expression.expression;
  if(ts.isBinaryExpression(expression)&&expression.operatorToken.kind===ts.SyntaxKind.CommaToken)
    return sourceImport(expression.right);
  // Imported free functions are outside the continuation registration set.
  // Simulator class methods are resolved separately by their method names.
  return ts.isPropertyAccessExpression(expression)&&ts.isIdentifier(expression.expression)&&/^import_/.test(expression.expression.text);
}
function booleanValue(node,bindings){
  if(node.kind===ts.SyntaxKind.TrueKeyword)return true;
  if(node.kind===ts.SyntaxKind.FalseKeyword)return false;
  if(ts.isIdentifier(node)){const value=bindings.get(node.text);return typeof value==='boolean'?value:undefined;}
  if(ts.isParenthesizedExpression(node))return booleanValue(node.expression,bindings);
  if(ts.isPrefixUnaryExpression(node)&&node.operator===ts.SyntaxKind.ExclamationToken){
    const value=booleanValue(node.operand,bindings);return value===undefined?undefined:!value;
  }
  return undefined;
}
function specializedCall(n,name){
  if(nativeNames.has(name)||specializing.has(name))return undefined;
  if(n.arguments.some(a=>ts.isSpreadElement(a)))return undefined;
  const definitions=methodBodies.get(name);
  if(definitions?.length!==1)return undefined;
  const method=definitions[0],bindings=new Map(),scalarCalls=new Set();
  function pureFunction(expression){
    if(sourceImport(expression))return true;
    if(ts.isArrowFunction(expression)||ts.isFunctionExpression(expression)){
      if(inspectingFunctions.has(expression))return false;
      inspectingFunctions.add(expression);
      try{return !bodyCanSuspend(expression.body);}finally{inspectingFunctions.delete(expression);}
    }
    const referenceName=memberName(expression),reference=methodBodies.get(referenceName);
    return reference?.length===1&&!nativeNames.has(referenceName)&&!suspendNames.has(referenceName)&&
      !specializing.has(referenceName);
  }
  for(let i=0;i<method.parameters.length;i++){
    const parameter=method.parameters[i];let argument=n.arguments[i];
    if(parameter.dotDotDotToken||!ts.isIdentifier(parameter.name))return undefined;
    if(!argument&&parameter.initializer){
      if(!pureFunction(parameter.initializer))return undefined;
      argument=parameter.initializer;
    }
    if(argument&&(argument.kind===ts.SyntaxKind.TrueKeyword||argument.kind===ts.SyntaxKind.FalseKeyword))
      bindings.set(parameter.name.text,argument.kind===ts.SyntaxKind.TrueKeyword);
    else if(argument&&pureFunction(argument))scalarCalls.add(parameter.name.text);
  }
  if(!bindings.size&&!scalarCalls.size)return undefined;
  const boundNames=new Set([...bindings.keys(),...scalarCalls]);
  let unsafe=false;
  function writes(node){
    if(ts.isIdentifier(node))return boundNames.has(node.text);
    if(ts.isArrayLiteralExpression(node))return node.elements.some(e=>ts.isSpreadElement(e)?writes(e.expression):writes(e));
    if(ts.isObjectLiteralExpression(node))return node.properties.some(p=>
      ts.isShorthandPropertyAssignment(p)?writes(p.name):ts.isPropertyAssignment(p)?writes(p.initializer):false);
    return false;
  }
  function check(node){
    if(ts.isBinaryExpression(node)&&node.operatorToken.kind>=ts.SyntaxKind.FirstAssignment&&
       node.operatorToken.kind<=ts.SyntaxKind.LastAssignment&&writes(node.left))unsafe=true;
    if((ts.isForOfStatement(node)||ts.isForInStatement(node))&&writes(node.initializer))unsafe=true;
    if((ts.isPrefixUnaryExpression(node)||ts.isPostfixUnaryExpression(node))&&
       [ts.SyntaxKind.PlusPlusToken,ts.SyntaxKind.MinusMinusToken].includes(node.operator)&&
       ts.isIdentifier(node.operand)&&boundNames.has(node.operand.text))unsafe=true;
    if((ts.isVariableDeclaration(node)||ts.isParameter(node)||ts.isBindingElement(node))&&
       ts.isIdentifier(node.name)&&boundNames.has(node.name.text))unsafe=true;
    ts.forEachChild(node,check);
  }check(method.body);
  if(unsafe)return undefined;
  specializing.add(name);
  try{return bodyCanSuspend(method.body,bindings,scalarCalls);}finally{specializing.delete(name);}
}
function canSuspend(n,scalarCalls=new Set()){
  if(expressionKernel(n))return true;
  if(!ts.isCallExpression(n))return false;
  const expression=n.expression,name=memberName(expression);
  if(ts.isIdentifier(expression)&&scalarCalls.has(expression.text))return false;
  if(name==='call'||name==='apply'||name==='bind')return true;
  if(sourceImport(expression))return false;
  if(name){
    const specialized=specializedCall(n,name);if(specialized!==undefined)return specialized;
    if(suspendNames.has(name))return true;
    if(callbackArrays.has(name))return n.arguments.some(argument=>{
      if(sourceImport(argument))return false;
      if(!ts.isArrowFunction(argument)&&!ts.isFunctionExpression(argument))return true;
      let possible=false;function visit(node){if(canSuspend(node))possible=true;ts.forEachChild(node,visit);}visit(argument.body);return possible;
    });
    return false;
  }
  if(ts.isIdentifier(expression))return !scalarGlobals.has(expression.text);
  return true;
}
function expressionKernel(n){
  if(!expressionFiles.has(fileName)||!(ts.isCallExpression(n)||ts.isBinaryExpression(n)||ts.isConditionalExpression(n)||ts.isPrefixUnaryExpression(n)))return undefined;
  return expressionKernels.get(fileName+'|'+expressionHash(n));
}
function bodyCanSuspend(body,bindings=new Map(),scalarCalls=new Set()){
  let possible=false;
  function visit(node){
    if(possible)return;
    if(ts.isIfStatement(node)){
      visit(node.expression);const value=booleanValue(node.expression,bindings);
      if(value!==false)visit(node.thenStatement);
      if(value!==true&&node.elseStatement)visit(node.elseStatement);
      return;
    }
    if(ts.isFunctionLike(node)&&(bindings.size||scalarCalls.size)){if(bodyCanSuspend(node.body))possible=true;return;}
    if(canSuspend(node,scalarCalls))possible=true;ts.forEachChild(node,visit);
  }visit(body);return possible;
}
function analyze(files){
  const methods=[];
  for(const [file,text] of files){
    // The supported default Gen 9 format uses the base scripts (gen: 9).
    // Other generations/mods must not add false dependencies to that graph.
    if(file.startsWith('data/mods/'))continue;
    const tree=ts.createSourceFile(file,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
    function visit(node){
      if(ts.isMethodDeclaration(node)&&node.body&&
         (ts.isIdentifier(node.name)||ts.isStringLiteral(node.name))){
        methods.push({name:node.name.text,body:node.body});
        const definitions=methodBodies.get(node.name.text)||[];definitions.push(node);methodBodies.set(node.name.text,definitions);
      }
      ts.forEachChild(node,visit);
    }visit(tree);
  }
  const manifest=JSON.parse(fs.readFileSync(path.join(root,'build/pokemon_gen9/batch/manifest.json')));
  for(const kernel of manifest.kernels){
    if(kernel.kind==='expression'){
      const file=kernel.expression.file.replace(/\.ts$/,'.js');expressionFiles.add(file);
      expressionKernels.set(file+'|'+kernel.expression.shape_sha256,kernel);
    }
    else {suspendNames.add(kernel.name);nativeNames.add(kernel.name);}
  }
  let changed=true;while(changed){
    changed=false;for(const method of methods)if(!suspendNames.has(method.name)&&bodyCanSuspend(method.body)){
      suspendNames.add(method.name);changed=true;
    }
  }
}
function forbidden(node){
  let reason;function visit(n){
    if(n.kind===ts.SyntaxKind.SuperKeyword)reason='super requires a home-object continuation';
    if(ts.isCallExpression(n)&&ts.isIdentifier(n.expression)&&n.expression.text==='eval')reason='direct eval';
    if(n.asteriskToken||n.modifiers?.some(m=>m.kind===ts.SyntaxKind.AsyncKeyword))reason='existing generator/async function';
    ts.forEachChild(n,visit);
  }visit(node.body);return reason;
}
function methodProperty(property,generated){
  // Extract a real concise method so its synchronous function keeps the
  // source's nonconstructibility and own-property shape. Super methods are
  // excluded by forbidden(), so the temporary home object is unobservable.
  const key=ts.isNumericLiteral(property.name)?f.createNumericLiteral(Number(property.name.text)):str(property.name.text);
  const original=f.createElementAccessExpression(par(f.createObjectLiteralExpression([property])),key);
  const generator=f.createFunctionExpression(undefined,f.createToken(ts.SyntaxKind.AsteriskToken),undefined,undefined,params(property),undefined,generated);
  // A computed constant key also preserves an own method named __proto__.
  return f.createPropertyAssignment(f.createComputedPropertyName(key),call('dual',[original,generator,str(id(property))]));
}
function computedMethod(property){
  if(!ts.isComputedPropertyName(property.name))return false;
  if(bodyCanSuspend(property.body))rejected.push({id:id(property),reason:'computed method name retains its original synchronous contract'});
  return true;
}
function continuation(node,state){
  if(!bodyCanSuspend(node.body))return null;
  const reason=forbidden(node);
  if(reason){rejected.push({id:id(node),reason});return null;}
  const temps=[];let counter=0;
  function temp(){const n=f.createIdentifier('__pg9_r'+counter++);temps.push(n);return n;}
  function conditional(value,yes,no=undef()){
    return f.createConditionalExpression(value,undefined,yes,undefined,no);
  }
  function invokeCall(n){
    if(n.flags&ts.NodeFlags.OptionalChain)return optionalChain(n);
    const args=arr(n.arguments.map(a=>ts.visitNode(a,visit))),expr=n.expression;
    if(ts.isPropertyAccessExpression(expr)||ts.isElementAccessExpression(expr)){
      const receiver=temp(),left=assign(receiver,ts.visitNode(expr.expression,visit));
      const member=ts.isPropertyAccessExpression(expr)?f.createPropertyAccessExpression(par(left),expr.name):
        f.createElementAccessExpression(par(left),ts.visitNode(expr.argumentExpression,visit));
      return yieldCall([member,receiver,args]);
    }
    return yieldCall([ts.visitNode(expr,visit),undef(),args]);
  }
  function optionalChain(n){
    const segments=[];let base=n;
    while(ts.isCallExpression(base)||ts.isPropertyAccessExpression(base)||ts.isElementAccessExpression(base)){
      segments.unshift(base);base=base.expression;
    }
    function next(index,value,receiver){
      if(index===segments.length)return value;
      const segment=segments[index],saved=temp();
      let tail;
      if(ts.isCallExpression(segment)){
        tail=next(index+1,yieldCall([saved,receiver||undef(),arr(segment.arguments.map(a=>ts.visitNode(a,visit)))]),null);
      }else{
        const member=ts.isPropertyAccessExpression(segment)?f.createPropertyAccessExpression(saved,segment.name):
          f.createElementAccessExpression(saved,ts.visitNode(segment.argumentExpression,visit));
        tail=next(index+1,member,saved);
      }
      if(segment.questionDotToken)tail=conditional(bin(saved,ts.SyntaxKind.EqualsEqualsToken,f.createNull()),undef(),tail);
      return comma(assign(saved,value),tail);
    }
    return next(0,ts.visitNode(base,visit),null);
  }
  function closure(n){
    if(!bodyCanSuspend(n.body))return n;
    // Named function expressions have a separate self binding. Preserve their
    // original execution until that binding is explicitly lowered.
    if(ts.isFunctionExpression(n)&&n.name){rejected.push({id:id(n),reason:'named function-expression self binding'});return n;}
    const generated=continuation(n,state);if(!generated)return n;
    let generator=f.createFunctionExpression(undefined,f.createToken(ts.SyntaxKind.AsteriskToken),undefined,undefined,params(n),undefined,generated);
    if(ts.isArrowFunction(n)){
      // The original arrow preserves lexical this, arguments and constructibility.
      // The companion generator receives the same lexical this via bind.
      let usesArguments=false;function scan(x){
        if(ts.isIdentifier(x)&&x.text==='arguments')usesArguments=true;ts.forEachChild(x,scan);
      }scan(n.body);
      if(usesArguments){rejected.push({id:id(n),reason:'arrow lexical arguments'});return n;}
      generator=f.createCallExpression(f.createPropertyAccessExpression(par(generator),'bind'),undefined,[f.createThis()]);
    }
    const display=n.parent&&ts.isVariableDeclaration(n.parent)&&ts.isIdentifier(n.parent.name)?n.parent.name.text:undefined;
    return call('dual',[n,generator,str(id(n)),...(display===undefined?[]:[str(display)])]);
  }
  function visit(n){
    const kernel=expressionKernel(n);
    if(kernel){
      const spec=kernel.expression;
      let receiver=f.createThis();if(spec.receiver==='this.battle')receiver=f.createPropertyAccessExpression(receiver,'battle');
      const dependencies=[...spec.aliases.map(x=>f.createIdentifier(x.name)),
        ...spec.direct_helpers.map(name=>f.createPropertyAccessExpression(receiver,name))];
      return par(f.createYieldExpression(f.createToken(ts.SyntaxKind.AsteriskToken),call('expression',[
        str(kernel.key),receiver,arr(spec.parameters.map(name=>f.createIdentifier(name))),arr(dependencies),
        f.createArrowFunction(undefined,undefined,[],undefined,f.createToken(ts.SyntaxKind.EqualsGreaterThanToken),n)])));
    }
    if(ts.isArrowFunction(n)||ts.isFunctionExpression(n))return closure(n);
    if(ts.isFunctionDeclaration(n)||ts.isClassDeclaration(n)||ts.isClassExpression(n))return n;
    if(ts.isObjectLiteralExpression(n))return f.updateObjectLiteralExpression(n,n.properties.map(property=>{
      if(ts.isMethodDeclaration(property)&&property.body&&!property.asteriskToken){
        if(computedMethod(property))return property;
        const generated=continuation(property,state);if(!generated)return property;
        return methodProperty(property,generated);
      }
      return ts.visitEachChild(property,visit,context);
    }));
    if(ts.isCallExpression(n)&&canSuspend(n))return invokeCall(n);
    return ts.visitEachChild(n,visit,context);
  }
  const body=ts.isBlock(node.body)?ts.visitEachChild(node.body,visit,context):
    f.createBlock([f.createReturnStatement(ts.visitNode(node.body,visit))],true);
  const declarations=temps.length?[f.createVariableStatement(undefined,f.createVariableDeclarationList(
    temps.map(n=>f.createVariableDeclaration(n)),ts.NodeFlags.Let))]:[];
  entries.push({id:id(node),source_sha256:hash(node.getText(sourceFile)),calls_temporaries:temps.length});
  return f.updateBlock(body,[...declarations,...body.statements]);
}
function transform(text,file){
  fileName=file;sourceFile=ts.createSourceFile(file,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
  const result=ts.transform(sourceFile,[ctx=>{
    context=ctx;
    function visitor(n){
      if(ts.isClassDeclaration(n)&&n.name){
        const methods=[],registrations=[];
        for(const method of n.members){
          methods.push(method);
          if(!ts.isMethodDeclaration(method)||!method.body||method.asteriskToken)continue;
          if(!ts.isIdentifier(method.name)&&!ts.isStringLiteral(method.name))continue;
          const generated=continuation(method);if(!generated)continue;
          const name=method.name.text,hidden='__pg9_g_'+name;
          methods.push(f.createMethodDeclaration(method.modifiers,f.createToken(ts.SyntaxKind.AsteriskToken),hidden,
            undefined,undefined,params(method),undefined,generated));
          const isStatic=method.modifiers?.some(m=>m.kind===ts.SyntaxKind.StaticKeyword);
          const owner=isStatic?n.name:f.createPropertyAccessExpression(n.name,'prototype');
          registrations.push(f.createExpressionStatement(call('register',[
            f.createElementAccessExpression(owner,str(name)),f.createElementAccessExpression(owner,str(hidden)),str(id(method))])));
          registrations.push(f.createExpressionStatement(f.createDeleteExpression(f.createElementAccessExpression(owner,str(hidden)))));
        }
        return [f.updateClassDeclaration(n,n.modifiers,n.name,n.typeParameters,n.heritageClauses,methods),...registrations];
      }
      if(ts.isObjectLiteralExpression(n)){
        const properties=n.properties.map(property=>{
          if(ts.isMethodDeclaration(property)&&property.body&&!property.asteriskToken){
            if(computedMethod(property))return property;
            const generated=continuation(property);if(!generated)return property;
            return methodProperty(property,generated);
          }
          return ts.visitEachChild(property,visitor,ctx);
        });
        return f.updateObjectLiteralExpression(n,properties);
      }
      // Module initialization, getters, constructors and export helpers retain
      // their original synchronous contract. Their source is copied intact.
      if(ts.isFunctionLike(n))return n;
      return ts.visitEachChild(n,visitor,ctx);
    }
    return node=>ts.visitNode(node,visitor);
  }]);
  const code=ts.createPrinter({newLine:ts.NewLineKind.LineFeed}).printFile(result.transformed[0]);result.dispose();
  return '"use strict";\nconst __pg9rt=require('+JSON.stringify(runtimeFile)+');\n'+code;
}
function build(){
  if(process.env.WEBNAV_BUILD_CONFINED!=='1')throw Error('Use guarded build.cjs');
  const clean=path.join(root,'build/pokemon_gen9/showdown');
  if(cp.execFileSync('git',['-C',clean,'rev-parse','HEAD'],{encoding:'utf8'}).trim()!==pin||
     cp.execFileSync('git',['-C',clean,'status','--porcelain','--untracked-files=no'],{encoding:'utf8'}).trim())throw Error('Dirty/unpinned source');
  const runtime=path.join(out,'runtime');
  fs.mkdirSync(out,{recursive:true});fs.cpSync(oracle,runtime,{recursive:true,force:true,
    filter:source=>!path.relative(oracle,source).split(path.sep).includes('node_modules')});
  if(!fs.existsSync(path.join(runtime,'node_modules')))
    fs.symlinkSync(path.join(oracle,'node_modules'),path.join(runtime,'node_modules'),'dir');
  const files=['sim/battle.js','sim/battle-actions.js','sim/pokemon.js','sim/side.js','sim/field.js','sim/battle-queue.js'];
  // All data handlers, including nested conditions and format/mod overrides,
  // get source continuations. Team generation stays an explicit reset phase.
  for(const file of fs.readdirSync(path.join(oracle,'dist/data'),{recursive:true}))
    if(file.endsWith('.js')&&!file.includes('random-battles/'))files.push('data/'+file);
  const sources=[];
  const inputs=files.map(file=>[file,fs.readFileSync(path.join(oracle,'dist',file),'utf8')]);
  analyze(inputs);
  for(const [file,text] of inputs){
    const generated=transform(text,file);
    fs.writeFileSync(path.join(runtime,'dist',file),generated);
    sources.push({file,input_sha256:hash(text),output_sha256:hash(generated)});
  }
  const report={revision:pin,typescript:ts.version,files:sources,continuations:entries,rejected,suspend_names:[...suspendNames].sort(),
    frontend_sha256:hash(fs.readFileSync(__filename)),runtime_sha256:hash(fs.readFileSync(runtimeFile)),
    bridge_sha256:hash(fs.readFileSync(path.join(root,'ocean/pokemon_gen9/batch_addon.c'))),
    kernel_source_ir_sha256:JSON.parse(fs.readFileSync(path.join(root,'build/pokemon_gen9/batch/manifest.json'))).source_ir_sha256,
    contract:'Default base-script Gen 9 only. Source JS object semantics; resumable simulator/data methods; manifest-admitted C numeric kernels. Imported free functions and statically non-suspending literal-boolean branches stay scalar. Constructors, getters, reset generation and unregistered external functions stay scalar.'};
  report.source_ir_sha256=hash(JSON.stringify(report));
  fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify({source_files:sources.length,continuations:entries.length,rejected:rejected.length,source_ir_sha256:report.source_ir_sha256}));
  return report;
}
module.exports={build,transform,analyze};
if(require.main===module)build();
