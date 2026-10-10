const fs=require('fs'),path=require('path'),assert=require('assert');
const root=process.argv[2] || path.join(__dirname,'packet/briar_court_windows_realistic_r1');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'component_manifest.json'))),parts=JSON.parse(fs.readFileSync(path.join(root,'physical_parts.json')));
const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
function multiply(a,b){return Array.from({length:16},(_,i)=>{let r=i%4,c=Math.floor(i/4),s=0;for(let k=0;k<4;k++)s+=a[r+4*k]*b[k+4*c];return s;});}
function local(n){if(n.matrix)return n.matrix;const [x,y,z,w]=n.rotation||[0,0,0,1],s=n.scale||[1,1,1],t=n.translation||[0,0,0];return [(1-2*y*y-2*z*z)*s[0],(2*x*y+2*z*w)*s[0],(2*x*z-2*y*w)*s[0],0,(2*x*y-2*z*w)*s[1],(1-2*x*x-2*z*z)*s[1],(2*y*z+2*x*w)*s[1],0,(2*x*z+2*y*w)*s[2],(2*y*z-2*x*w)*s[2],(1-2*x*x-2*y*y)*s[2],0,...t,1];}
let total=0,glass=0,report=[];
for(const [asset,file,expected] of [['room_window_insert','room_window_insert_realistic_r1.glb',5280],['lobby_glazed_facade','lobby_glazed_facade_realistic_r1.glb',4932]]){
 const bytes=fs.readFileSync(path.join(root,'assets',file));assert.equal(bytes.readUInt32LE(0),0x46546c67);assert.equal(bytes.readUInt32LE(8),bytes.length);
 const len=bytes.readUInt32LE(12),doc=JSON.parse(bytes.subarray(20,20+len).toString()),bin=bytes.subarray(28+len),parents={};doc.nodes.forEach((n,i)=>(n.children||[]).forEach(c=>parents[c]=i));
 function matrix(i){return multiply(parents[i]===undefined?identity:matrix(parents[i]),local(doc.nodes[i]));}
 function read(ai,index){const a=doc.accessors[ai],v=doc.bufferViews[a.bufferView],sizes={5126:4,5125:4,5123:2,5121:1},dim=a.type==='VEC3'?3:1,size=sizes[a.componentType],offset=(v.byteOffset||0)+(a.byteOffset||0)+index*(v.byteStride||size*dim);assert(offset+dim*size<=bin.length);return Array.from({length:dim},(_,k)=>a.componentType===5126?bin.readFloatLE(offset+k*size):size===4?bin.readUInt32LE(offset+k*size):size===2?bin.readUInt16LE(offset+k*size):bin.readUInt8(offset+k));}
 const coverage=new Map();let triangles=0,count=0,closed=0;
 for(const component of manifest.components.filter(c=>c.asset===asset)){
  const m=component.runtime_mapping,ni=doc.nodes.findIndex(n=>n.name===m.node_name);assert(ni>=0);const node=doc.nodes[ni],mesh=doc.meshes[node.mesh];assert.equal(mesh.name,m.mesh_name);const primitive=mesh.primitives[m.primitive_index],index=doc.accessors[primitive.indices];assert.equal(doc.materials[primitive.material].name,component.material);
  const key=`${node.mesh}:${m.primitive_index}`;if(!coverage.has(key))coverage.set(key,new Uint8Array(index.count/3));const seen=coverage.get(key),[start,end]=m.triangle_range_half_open;assert.equal(end-start,component.triangles);assert.deepEqual(m.index_range_half_open,[3*start,3*end]);
  const transform=matrix(ni),lo=[Infinity,Infinity,Infinity],hi=[-Infinity,-Infinity,-Infinity],edges=new Map();
  for(let t=start;t<end;t++){assert(t<seen.length && !seen[t]);seen[t]=1;const keys=[];for(let k=0;k<3;k++){
   const ix=read(primitive.indices,t*3+k)[0];assert(ix<doc.accessors[primitive.attributes.POSITION].count);const p=read(primitive.attributes.POSITION,ix),world=[0,1,2].map(a=>transform[a]*p[0]+transform[a+4]*p[1]+transform[a+8]*p[2]+transform[a+12]),source=[world[0],-world[2],world[1]];source.forEach((v,a)=>{assert(Number.isFinite(v));lo[a]=Math.min(lo[a],v);hi[a]=Math.max(hi[a],v);});keys.push(source.map(v=>Math.round(v*1e6)).join(','));
  }for(let k=0;k<3;k++){const a=keys[k],b=keys[(k+1)%3];assert.notEqual(a,b);const edge=a<b?`${a}|${b}`:`${b}|${a}`;edges.set(edge,(edges.get(edge)||0)+1);}}
  for(let a=0;a<3;a++){assert(Math.abs(lo[a]-component.aabb_source_m[0][a])<2e-6);assert(Math.abs(hi[a]-component.aabb_source_m[1][a])<2e-6);}
  assert([...edges.values()].every(n=>n===2),component.id+' open or nonmanifold');closed++;
  if(component.substrate==='glass'){assert(Math.abs(hi[1]-lo[1]-.0064)<2e-6);glass++;}
  const row=parts.rows.find(r=>r.component_id===component.id);assert(row && row.actual_triangles===component.triangles);assert.deepEqual(row.runtime,m);triangles+=end-start;count++;
 }
 for(const mesh of doc.meshes)mesh.primitives.forEach((p,i)=>{const seen=coverage.get(`${doc.meshes.indexOf(mesh)}:${i}`);assert(seen && seen.every(x=>x===1));});assert.equal(triangles,expected);total+=triangles;report.push({asset,components:count,triangles,closedComponents:closed,meshes:doc.meshes.length,materials:doc.materials.length});
}
assert.equal(total,10212);assert.equal(glass,6);assert.equal(manifest.components.length,349);console.log(JSON.stringify({pass:true,checks:'Full node transforms, actual vertex bounds, primitive materials and indices, exact nonoverlapping full triangle coverage, 349 welded closed component edge sets, six 6.4mm pane gauges; not a structural strength certification',total,glass,assets:report},null,2));
