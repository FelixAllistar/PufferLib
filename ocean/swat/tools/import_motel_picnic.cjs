// Rebuild source-exact collision/selection data from the accepted immutable GLB.
// Usage: node tools/import_motel_picnic.cjs [INPUT.glb] [OUTPUT.h]
const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto');
const input=process.argv[2]||path.resolve(__dirname,'../assets/environment/motel_picnic/BC_PT01_picnic_table.glb');
const output=process.argv[3]||path.resolve(__dirname,'../motel_picnic_data.h');
const spec=JSON.parse(fs.readFileSync(path.join(__dirname,'motel_picnic_import.json'))),bytes=fs.readFileSync(input),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
assert.equal(sha(bytes),spec.source_sha256,'Only the accepted original source bytes may be imported');assert.notEqual(path.resolve(input),path.resolve(output));
const sourcePath=fs.realpathSync(input),targetPath=path.join(fs.realpathSync(path.dirname(output)),path.basename(output));assert.notEqual(sourcePath,targetPath);
if(fs.existsSync(output)){const from=fs.statSync(input),to=fs.statSync(output);assert(from.dev!==to.dev||from.ino!==to.ino,"Output aliases the protected source");}
function describe(bytes){
const end=20+bytes.readUInt32LE(12),g=JSON.parse(bytes.subarray(20,end)),bin=bytes.subarray(end+8),audit={expected_pair_checks:spec.contacts};
const ownership={source:input,source_sha256:spec.source_sha256,parts:spec.components.map(c=>{const p=g.meshes[c.mesh].primitives[c.primitive],vertices=data(p.attributes.POSITION),indices=data(p.indices),source_triangles=Array.from({length:c.triangles},(_,i)=>c.first_triangle+i),points=source_triangles.flatMap(t=>[0,1,2].map(k=>vertices.slice(indices[t*3+k]*3,indices[t*3+k]*3+3)));assert(source_triangles.every(t=>t>=0&&t<indices.length/3));const lo=[0,1,2].map(k=>Math.min(...points.map(p=>p[k]))),hi=[0,1,2].map(k=>Math.max(...points.map(p=>p[k])));return {...c,lo,hi,source_triangles};})};
let retained=0;for(let m=0;m<g.meshes.length;m++)for(let p=0;p<g.meshes[m].primitives.length;p++){const primitive=g.meshes[m].primitives[p],indices=data(primitive.indices),selected=new Set();for(const c of ownership.parts.filter(c=>c.mesh===m&&c.primitive===p)){assert.equal(c.material,primitive.material);for(const t of c.source_triangles){assert(!selected.has(t));selected.add(t);}}assert.equal(selected.size,indices.length/3);retained+=selected.size;}assert.equal(retained,5752);assert.equal(ownership.parts.length,80);
assert(!g.skins&&!g.animations);for(const n of g.nodes)assert(!n.matrix&&!n.translation&&!n.rotation&&!n.scale);
function data(id){const a=g.accessors[id],v=g.bufferViews[a.bufferView],n={SCALAR:1,VEC3:3}[a.type],s={5123:2,5125:4,5126:4}[a.componentType];assert(n&&s&&!a.sparse);return Array.from({length:a.count*n},(_,i)=>{let p=(v.byteOffset||0)+(a.byteOffset||0)+Math.floor(i/n)*(v.byteStride||n*s)+(i%n)*s;return a.componentType===5126?bin.readFloatLE(p):s===2?bin.readUInt16LE(p):bin.readUInt32LE(p);});}
const sub=(a,b)=>a.map((x,i)=>x-b[i]),add=(a,b)=>a.map((x,i)=>x+b[i]),mul=(a,s)=>a.map(x=>x*s),dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0),cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],sq=a=>dot(a,a),clamp=x=>Math.max(0,Math.min(1,x));
function pointTriangle(p,[a,b,c]){const ab=sub(b,a),ac=sub(c,a),ap=sub(p,a),d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return a;const bp=sub(p,b),d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return b;const vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return add(a,mul(ab,d1/(d1-d3)));const cp=sub(p,c),d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return c;const vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return add(a,mul(ac,d2/(d2-d6)));const va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return add(b,mul(sub(c,b),(d4-d3)/(d4-d3+d5-d6)));const inv=1/(va+vb+vc);return add(a,add(mul(ab,vb*inv),mul(ac,vc*inv)));}
function segmentTriangle(p,q,[a,b,c]){const d=sub(q,p),e1=sub(b,a),e2=sub(c,a),h=cross(d,e2),det=dot(e1,h);if(Math.abs(det)<1e-16)return null;const inv=1/det,s=sub(p,a),u=inv*dot(s,h);if(u<0||u>1)return null;const r=cross(s,e1),v=inv*dot(d,r);if(v<0||u+v>1)return null;const t=inv*dot(e2,r);return t>=0&&t<=1?add(p,mul(d,t)):null;}
function segmentPair(p1,q1,p2,q2){const d1=sub(q1,p1),d2=sub(q2,p2),r=sub(p1,p2),a=sq(d1),e=sq(d2),f=dot(d2,r);let s=0,t=0;if(a<=1e-24&&e<=1e-24)return[p1,p2];if(a<=1e-24)t=clamp(f/e);else{const c=dot(d1,r);if(e<=1e-24)s=clamp(-c/a);else{const b=dot(d1,d2),den=a*e-b*b;s=den>1e-24?clamp((b*f-c*e)/den):0;t=(b*s+f)/e;if(t<0){t=0;s=clamp(-c/a);}else if(t>1){t=1;s=clamp((b-c)/a);}}}return[add(p1,mul(d1,s)),add(p2,mul(d2,t))];}
function bounds(t){return [Array.from({length:3},(_,k)=>Math.min(...t.map(p=>p[k]))),Array.from({length:3},(_,k)=>Math.max(...t.map(p=>p[k])))];}
function aabbGap(a,b){return Math.sqrt([0,1,2].reduce((s,k)=>s+Math.max(0,a[0][k]-b[1][k],b[0][k]-a[1][k])**2,0));}
const named=[];
for(let m=0;m<g.meshes.length;m++){
 const node=g.nodes.find(n=>n.mesh===m),names=node.extras.source_parts,parts=ownership.parts.filter(p=>p.mesh===m);assert.equal(parts.length,names.length);
 for(let i=0;i<parts.length;i++){
  const p=parts[i],name=names[i];assert.equal(name,spec.components.find(c=>c.id===p.id).name);
  const primitive=g.meshes[m].primitives[p.primitive],vertices=data(primitive.attributes.POSITION),indices=data(primitive.indices),triangles=p.source_triangles.map(id=>{const points=[0,1,2].map(c=>vertices.slice(3*indices[3*id+c],3*indices[3*id+c]+3));return{id,points,bounds:bounds(points)};});named.push({...p,name,triangles});
 }
}
const byName=new Map(named.map(p=>[p.name,p])),tolerance=2e-6;
function witness(a,b){
 assert(a&&b);assert(aabbGap([a.lo,a.hi],[b.lo,b.hi])<tolerance,a.name+' / '+b.name+' bounds');
 for(const ta of a.triangles)for(const tb of b.triangles){if(aabbGap(ta.bounds,tb.bounds)>tolerance)continue;
  const hits=[];for(let i=0;i<3;i++)for(const [p,q]of [[ta,tb],[tb,ta]]){const hit=segmentTriangle(p.points[i],p.points[(i+1)%3],q.points);if(hit)hits.push(hit);}
  if(hits.length){let pair=[hits[0],hits[0]],span=0;for(const a of hits)for(const b of hits)if(sq(sub(a,b))>span){span=sq(sub(a,b));pair=[a,b];}return{method:'source triangle intersection segment midpoint',point:mul(add(...pair),.5),a_triangle:ta.id,b_triangle:tb.id,distance_m:0,intersection_span_m:Math.sqrt(span)};}
  let best=Infinity,pair;for(const [from,to]of [[ta.points,tb.points],[tb.points,ta.points]])for(const p of from){const q=pointTriangle(p,to),d=sq(sub(p,q));if(d<best){best=d;pair=[p,q];}}
  for(let i=0;i<3;i++)for(let j=0;j<3;j++){const q=segmentPair(ta.points[i],ta.points[(i+1)%3],tb.points[j],tb.points[(j+1)%3]),d=sq(sub(...q));if(d<best){best=d;pair=q;}}
  if(best<=tolerance*tolerance)return{method:'source triangle closest points',point:mul(add(...pair),.5),a_triangle:ta.id,b_triangle:tb.id,distance_m:Math.sqrt(best)};
 }
 throw Error('No actual runtime triangle contact: '+a.name+' / '+b.name);
}
const contacts=audit.expected_pair_checks.map(p=>({a:p.a,b:p.b,...witness(byName.get(p.a),byName.get(p.b))}));assert.equal(contacts.length,93);
const groups=new Map();for(const p of named){const key=p.kind==='wood'?p.name:p.name.replace(/_(Washer|HexHead|HexNut)$/,'');if(!groups.has(key))groups.set(key,{name:key,kind:p.kind,components:[],parents:[]});groups.get(key).components.push(p.id);}
assert.equal(groups.size,50);const pairSet=new Set(contacts.map(p=>[p.a,p.b].sort().join('|')));
for(const group of groups.values()){
 const n=group.name;let parents=[];
 if(group.kind==='wood'){
  let match;if(n.includes('_Leg_'))parents=[];
  else if(match=n.match(/^Frame_(Left|Right)_(Seat|Top)Bearer$/))parents=['Front','Rear'].map(side=>'Frame_'+match[1]+'_Leg_'+side);
  else if(n==='Frame_LongitudinalSpine')parents=['Left','Right'].map(s=>'Frame_'+s+'_TopBearer');
  else if(match=n.match(/^Frame_LongitudinalBrace_(Left|Right)$/))parents=['Frame_'+match[1]+'_SeatBearer','Frame_LongitudinalSpine'];
  else if(n.startsWith('Bench_'))parents=['Frame_Left_SeatBearer','Frame_Right_SeatBearer'];
  else if(n.startsWith('Tabletop_'))parents=['Frame_Left_TopBearer','Frame_Right_TopBearer',...(n.endsWith('_03')?['Frame_LongitudinalSpine']:[])];
  else throw Error(n);
 }else{
  assert.equal(group.components.length,2);let match;
  if(match=n.match(/^Tabletop_Bolt_(\d)_/))parents=['Tabletop_Plank_'+match[1].padStart(2,'0')];
  else if(match=n.match(/^Bench_(Front|Rear)_Bolt_(\d)_/))parents=['Bench_'+match[1]+'_Plank_'+match[2].padStart(2,'0')];
  else if(match=n.match(/^Frame_(Left|Right)_(Front|Rear)_(Seat|Top)$/))parents=['Frame_'+match[1]+'_'+match[3]+'Bearer'];
  else if(match=n.match(/^Frame_Brace_(Left|Right)(Lower|Upper)$/))parents=['Frame_LongitudinalBrace_'+match[1]];
  else throw Error(n);
  const [a,b]=group.components.map(id=>named.find(p=>p.id===id));assert(pairSet.has([a.name,b.name].sort().join('|')),n+' fastener pair');
 }
 for(const p of parents){const parent=groups.get(p);assert(parent,p);assert(group.components.some(a=>parent.components.some(b=>pairSet.has([named.find(p=>p.id===a).name,named.find(p=>p.id===b).name].sort().join('|')))),n+' / '+p);}
 group.parents=parents;
}
const ordered=[];while(ordered.length<groups.size){const ready=[...groups.values()].filter(g=>!ordered.includes(g)&&g.parents.every(p=>ordered.some(g=>g.name===p)));assert(ready.length,'support cycle');ordered.push(...ready);}
for(let i=0;i<ordered.length;i++){const p=ordered[i];p.id=i;p.parents=p.parents.map(n=>ordered.findIndex(g=>g.name===n));assert(p.parents.every(j=>j<i));p.triangles=p.components.reduce((s,j)=>s+named.find(p=>p.id===j).source_triangles.length,0);}
const ground=ordered.filter(p=>!p.parents.length);assert.equal(ground.length,4);
for(const p of ground){const component=named.find(q=>q.id===p.components[0]);let area=0,count=0;for(const t of component.triangles)if(t.points.every(v=>Math.abs(v[1])<1e-7)){area+=Math.sqrt(sq(cross(sub(t.points[1],t.points[0]),sub(t.points[2],t.points[0]))))*.5;count++;}assert(area>.006);p.ground_face_area_m2=area;p.ground_triangle_count=count;}
const report={source:ownership.source,source_sha256:ownership.source_sha256,method:'Actual GLB triangle edge/face intersections or closest source points within2µm; names bound to GLB extras, original source triangle counts and Y-up converted bounds. Source audit used only to identify intended pairs, never as the contact result.',source_components:80,proposed_game_owners:50,wood_owners:20,fastener_pair_owners:30,original_triangles:5752,required_source_contact_pairs:93,contacts,owners:ordered,component_names:named.map(p=>({id:p.id,name:p.name})),limits:['Game support graph is proposed engine ownership, not authored physics, measured strength or dynamic joint validation.','All source fastener triangles remain; no invisible bolts or replacement primitives.','World placement, player contacts, destruction, replication and native context raster remain open.']};return {meta:report,ownership};
}
function emit(meta,own,bytes){const end=20+bytes.readUInt32LE(12),g=JSON.parse(bytes.subarray(20,end)),bin=bytes.subarray(end+8);
function data(id){const a=g.accessors[id],v=g.bufferViews[a.bufferView],n={SCALAR:1,VEC3:3}[a.type],s={5123:2,5125:4,5126:4}[a.componentType];return Array.from({length:a.count*n},(_,i)=>{let p=(v.byteOffset||0)+(a.byteOffset||0)+Math.floor(i/n)*(v.byteStride||n*s)+(i%n)*s;return a.componentType===5126?bin.readFloatLE(p):s===2?bin.readUInt16LE(p):bin.readUInt32LE(p);});}
const f=x=>((s=>s.includes('.')||s.includes('e')?s:s+'.0')(Number(x).toPrecision(10)))+'f',vector=v=>'{'+v.map(f).join(',')+'}';let out='// Generated from unchanged BC_PT01 GLB; original triangles/materials only.\n';const defs=[];
for(const owner of meta.owners){
 const components=owner.components.map(i=>own.parts.find(p=>p.id===i)),lo=[0,1,2].map(k=>Math.min(...components.map(p=>p.lo[k]))),hi=[0,1,2].map(k=>Math.max(...components.map(p=>p.hi[k]))),center=lo.map((x,k)=>Math.fround((x+hi[k])/2)),half=lo.map((x,k)=>Math.fround((hi[k]-x)/2)),vertices=[],anchors=[];
 for(const part of components){const p=g.meshes[part.mesh].primitives[part.primitive],v=data(p.attributes.POSITION),idx=data(p.indices);for(const t of part.source_triangles)for(let k=0;k<3;k++){const source=v.slice(idx[t*3+k]*3,idx[t*3+k]*3+3);vertices.push(source.map((x,j)=>Math.fround(x-center[j])));if(!owner.parents.length&&Math.abs(source[1])<1e-7&&!anchors.some(p=>p.every((x,j)=>x===source[j])))anchors.push(source);}}
 if(owner.parents.length){for(const parent of owner.parents){const p=meta.owners[parent],a=owner.components.map(id=>meta.component_names.find(c=>c.id===id).name),b=p.components.map(id=>meta.component_names.find(c=>c.id===id).name),contact=meta.contacts.find(c=>(a.includes(c.a)&&b.includes(c.b))||(a.includes(c.b)&&b.includes(c.a)));assert(contact);anchors.push(contact.point);}}
 assert.equal(vertices.length,owner.triangles*3);out+=`static b3Vec3 picnic_vertices_${owner.id}[]={\n${vertices.map(vector).join(',\n')}\n};\nstatic int32_t picnic_indices_${owner.id}[]={${vertices.map((_,i)=>i).join(',')}};\nstatic const b3Vec3 picnic_anchors_${owner.id}[]={${anchors.map(vector).join(',')}};\n`;
 const section=owner.kind==='steel'?.006:owner.name.startsWith('Tabletop')?.05:owner.name.startsWith('Bench')?.045:Math.min(...half)*2;
 defs.push('{"'+owner.name+'",'+vector(center)+','+vector(half)+',picnic_vertices_'+owner.id+','+vertices.length+',picnic_indices_'+owner.id+','+owner.triangles+','+(owner.kind==='wood'?'SWAT_WOOD':'SWAT_STEEL')+','+f(section)+',{'+[...owner.parents,...Array(5-owner.parents.length).fill(-1)].join(',')+'},picnic_anchors_'+owner.id+','+anchors.length+'}');
}
out+='static const SwatRoadsidePart picnic_parts[]={\n'+defs.join(',\n')+'\n};\n';

for(const p of own.parts)out+='static const int picnic_component_triangles_'+p.id+'[]={'+p.source_triangles.join(',')+'};\n';
out+='static const SwatPicnicComponent picnic_components[]={\n'+own.parts.map(p=>'{'+meta.owners.find(o=>o.components.includes(p.id)).id+','+(p.mesh*2+p.primitive)+','+p.triangles+',picnic_component_triangles_'+p.id+'}').join(',\n')+'\n};\n';
return out;
}
const verified=describe(bytes),header=emit(verified.meta,verified.ownership,bytes);const stage=output+".picnic-stage-"+process.pid;fs.writeFileSync(stage,header,{flag:"wx"});fs.renameSync(stage,output);assert.equal(sha(fs.readFileSync(input)),spec.source_sha256);console.log("PASS80 original components/50 game owners/93 source contacts/5752 triangles; source GLB unchanged; wrote "+output);
