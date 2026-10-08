// Import original, hash-checked closed supports. Never execute art scripts.
const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto');
const root=path.resolve(__dirname,'..'),dir=path.join(root,'assets/environment/motel_ground');
const covered=JSON.parse(fs.readFileSync(path.join(root,'art_handoffs/motel-ground/COVERED_FILES.json'))).files;
function glb(name){const bytes=fs.readFileSync(path.join(dir,name+'.glb'));assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),covered.find(f=>f.path==='runtime/'+name+'.glb').sha256);assert.equal(bytes.readUInt32LE(0),0x46546c67);assert.equal(bytes.readUInt32LE(4),2);const end=20+bytes.readUInt32LE(12);return {json:JSON.parse(bytes.subarray(20,end)),bin:bytes.subarray(end+8)};}
function data(g,id){const a=g.json.accessors[id],v=g.json.bufferViews[a.bufferView],n={SCALAR:1,VEC3:3}[a.type],size={5123:2,5125:4,5126:4}[a.componentType];assert(n&&size&&!a.sparse);return Array.from({length:a.count},(_,i)=>Array.from({length:n},(_,j)=>{const at=(v.byteOffset||0)+(a.byteOffset||0)+i*(v.byteStride||n*size)+j*size;return a.componentType===5126?g.bin.readFloatLE(at):size===2?g.bin.readUInt16LE(at):g.bin.readUInt32LE(at);}));}
const collision=glb('connected_ground_collision'),render=glb('connected_ground_render');
const manifestBytes=fs.readFileSync(path.join(dir,'placement_manifest.json'));
assert.equal(crypto.createHash('sha256').update(manifestBytes).digest('hex'),covered.find(f=>f.path==='runtime/placement_manifest.json').sha256);
const manifest=JSON.parse(manifestBytes);
assert.deepEqual(manifest.envelope_xz,[-64,64,-48,52]);assert.equal(manifest.support_matrix.length,54);
function point(v){return v.map(x=>Math.round(x*1e6)).join(',');}
function geometry(g,i){const node=g.json.nodes[i];assert(!node.matrix&&!node.translation&&!node.rotation&&!node.scale&&!node.children);assert.equal(node.mesh,i);assert.equal(node.name,manifest.support_matrix[i].semantic_name);const p=g.json.meshes[i].primitives;assert.equal(p.length,1);assert(p[0].mode===undefined||p[0].mode===4);return {vertices:data(g,p[0].attributes.POSITION),indices:data(g,p[0].indices).flat()};}
function triangles(g){return g.indices.reduce((a,_,i)=>{if(i%3===0)a.push(g.indices.slice(i,i+3).map(j=>point(g.vertices[j])).sort().join(';'));return a;},[]).sort();}
const parts=manifest.support_matrix.map((m,i)=>{const g=geometry(collision,i);assert.equal(g.indices.length,36);assert.deepEqual(triangles(g),triangles(geometry(render,i)));const edges=new Map();for(let t=0;t<g.indices.length;t+=3)for(let e=0;e<3;e++){const a=point(g.vertices[g.indices[t+e]]),b=point(g.vertices[g.indices[t+(e+1)%3]]),key=[a,b].sort().join('|'),q=edges.get(key)||{count:0,balance:0};q.count++;q.balance+=a<b?1:-1;edges.set(key,q);}for(const e of edges.values())assert(e.count===2&&e.balance===0);g.min=[0,1,2].map(a=>Math.min(...g.vertices.map(v=>v[a])));g.max=[0,1,2].map(a=>Math.max(...g.vertices.map(v=>v[a])));g.center=g.min.map((v,j)=>(v+g.max[j])/2);g.half=g.min.map((v,j)=>(g.max[j]-v)/2);return g;});
const f=x=>{let s=(Math.abs(x)<1e-12?0:x).toPrecision(9);return(s.includes('.')||s.includes('e')?s:s+'.0')+'f';};
let out='// Generated from original hash-checked GLBs by tools/import_motel_ground.cjs.\n';
for(let k=0;k<parts.length;k++){const p=parts[k];out+=`static b3Vec3 ground_vertices_${k}[]={\n`+p.vertices.map(v=>'{'+v.map((x,i)=>f(x-p.center[i])).join(',')+'}').join(',\n')+'};\n';out+=`static int32_t ground_indices_${k}[]={${p.indices.join(',')}};\n`;}
out+='static const SwatMotelAsset ground_parts[]={\n'+parts.map((p,k)=>`{NULL,{${p.center.map(f)}},{${p.half.map(f)}},ground_vertices_${k},${p.vertices.length},ground_indices_${k},12,NULL,0}`).join(',\n')+'};\n';
// Asphalt and gravel have different ballistic/acoustic authority.
out+='static const SwatMaterial ground_materials[]={'+manifest.support_matrix.map(m=>m.material==='asphalt'?'SWAT_CONCRETE':'SWAT_SOIL').join(',')+'};\n';
const output=path.join(root,'motel_ground_data.h');
if(!fs.existsSync(output)||fs.readFileSync(output,'utf8')!==out)fs.writeFileSync(output,out);
console.log('PASS ground import: 54 closed 12-triangle supports; render/collision triangles exact; identity world placement; runtime hashes verified');
