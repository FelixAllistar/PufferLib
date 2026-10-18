// Audit approved static candidates without running Blender/Python recipes.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),assert=require('assert');
const root=path.resolve(__dirname,'..'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
for(const [folder,asset,expectedTriangles,expectedPrimitives,expectedMaterials,expectedPayloads,indexHash] of [
 ['motel-sedan','motel_sedan',13124,12,4,64,'369896317f74ab01e70e4023a3dfa12b65b73a1b4b6612b289dd558483c5a23d'],
 ['motel-shelter','motel_shelter',7868,3,3,54,'2c9257a12f46c982e9a3a3775219a908cec81a88650ce687e9147df8baaf207d']
]) {
 const handoff=path.join(root,'art_handoffs',folder),json=name=>JSON.parse(fs.readFileSync(path.join(handoff,name)));
 const manifest=json('MANIFEST.json'),indexBytes=fs.readFileSync(path.join(handoff,'SHA256SUMS.json'));
 assert.equal(sha(indexBytes),indexHash);const index=JSON.parse(indexBytes);
 const runtime=path.join(root,'assets/environment',asset,path.basename(manifest.runtime.file));
 assert.equal(Object.keys(index).length,expectedPayloads);
 for(const [name,entry] of Object.entries(index)) {
  assert(!path.isAbsolute(name) && !name.split('/').includes('..'));
  const b=fs.readFileSync(name===manifest.runtime.file?runtime:path.join(handoff,name));
  assert.equal(sha(b),typeof entry==='string'?entry:entry.sha256,name);
  if(entry.bytes!==undefined)assert.equal(b.length,entry.bytes,name);
 }
 const bytes=fs.readFileSync(runtime);assert.equal(sha(bytes),manifest.runtime.sha256);assert.equal(bytes.length,manifest.runtime.bytes);
 assert.equal(bytes.readUInt32LE(0),0x46546c67);assert.equal(bytes.readUInt32LE(4),2);assert.equal(bytes.readUInt32LE(8),bytes.length);
 assert.equal(bytes.readUInt32LE(16),0x4e4f534a);
 const end=20+bytes.readUInt32LE(12),g=JSON.parse(bytes.subarray(20,end)),bin=bytes.subarray(end+8);
 assert.equal(bytes.readUInt32LE(end+4),0x004e4942);assert.equal(end+8+bytes.readUInt32LE(end),bytes.length);
 assert.equal(g.buffers.length,1);assert(!g.buffers[0].uri && g.buffers[0].byteLength<=bin.length);
 assert(!(g.animations||[]).length && !(g.skins||[]).length);
 function view(id) {
  const v=g.bufferViews[id];assert(v && v.buffer===0);const start=v.byteOffset??0;
  assert(start>=0 && v.byteLength>0 && start+v.byteLength<=g.buffers[0].byteLength);
  return bin.subarray(start,start+v.byteLength);
 }
 function data(id) {
  const a=g.accessors[id];assert(a && !a.sparse);const v=g.bufferViews[a.bufferView],b=view(a.bufferView);
  const n={SCALAR:1,VEC2:2,VEC3:3}[a.type],size={5121:1,5123:2,5125:4,5126:4}[a.componentType];assert(n && size && a.count>0);
  const stride=v.byteStride??n*size,start=a.byteOffset??0;assert(stride>=n*size && start>=0 && start+(a.count-1)*stride+n*size<=b.length);
  return Array.from({length:a.count},(_,i)=>Array.from({length:n},(_,j)=>{
   const at=start+i*stride+j*size,x=a.componentType===5126?b.readFloatLE(at):size===1?b.readUInt8(at):size===2?b.readUInt16LE(at):b.readUInt32LE(at);
   assert(Number.isFinite(x));return x;
  }));
 }
 let triangles=0,primitives=0,meshes=0;const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
 for(const node of g.nodes) {
  assert(!node.matrix && !node.rotation && !node.scale && node.skin===undefined);
  if(node.mesh===undefined){assert(!node.translation || node.translation.every(Number.isFinite));continue;}
  assert(!node.translation);meshes++;
  for(const p of g.meshes[node.mesh].primitives) {
   assert((p.mode??4)===4 && p.material>=0 && p.material<g.materials.length);
   const positions=data(p.attributes.POSITION),normals=data(p.attributes.NORMAL),uv=data(p.attributes.TEXCOORD_0),indices=data(p.indices).flat();
   assert.equal(positions.length,normals.length);assert.equal(positions.length,uv.length);
   assert(indices.length%3===0 && indices.every(i=>i>=0 && i<positions.length));
   for(const v of positions)for(let axis=0;axis<3;axis++){min[axis]=Math.min(min[axis],v[axis]);max[axis]=Math.max(max[axis],v[axis]);}
   triangles+=indices.length/3;primitives++;
  }
 }
 assert.equal(triangles,expectedTriangles);assert.equal(primitives,expectedPrimitives);assert.equal(g.materials.length,expectedMaterials);
 assert.equal(meshes,asset==='motel_sedan'?6:3);assert.equal(g.images.length,5);
 assert(g.materials.every(m=>(m.alphaMode??'OPAQUE')==='OPAQUE'));
 for(const image of g.images){assert(!image.uri && image.mimeType==='image/png');assert.equal(view(image.bufferView).subarray(0,8).toString('hex'),'89504e470d0a1a0a');}
 for(let axis=0;axis<3;axis++)assert(Math.abs(max[axis]-min[axis]-manifest.runtime.dimensions_gltf_xyz_m[axis])<1e-6);
 assert(Math.abs(min[1])<1e-6);
 console.log(`PASS ${manifest.asset_id} intake: ${expectedPayloads} unchanged indexed payloads, ${triangles} triangles, ${primitives} primitives, ${expectedMaterials} opaque materials, finite indexed geometry/normals/UVs, embedded PNGs and grounded metre bounds; engine acceptance remains open`);
}
