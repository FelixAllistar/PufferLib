// Split original triangles by the author's atlas regions and component bounds.
// Geometry, normals, UVs, tangents, source materials and embedded PNG bytes stay exact.
const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto');
const [input,output]=process.argv.slice(2);assert(input&&output);
const bytes=fs.readFileSync(input),hash=crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(hash,'c9a0bca288bfd4a3c61725e13f21d8b68e6d8eaf8f1b613933cb64034da1b0b5');
const length=bytes.readUInt32LE(12),original=JSON.parse(bytes.subarray(20,20+length)),bin=bytes.subarray(28+length);
assert.equal(original.meshes.length,1);assert.equal(original.meshes[0].primitives.length,1);
assert(!original.skins&&!original.animations);const primitive=original.meshes[0].primitives[0];
function accessor(index){const a=original.accessors[index],v=original.bufferViews[a.bufferView],n={SCALAR:1,VEC2:2,VEC3:3,VEC4:4}[a.type],size={5123:2,5125:4,5126:4}[a.componentType];assert(n&&size&&!a.sparse);return{a,n,size,row:i=>bin.subarray((v.byteOffset||0)+(a.byteOffset||0)+i*(v.byteStride||n*size),(v.byteOffset||0)+(a.byteOffset||0)+i*(v.byteStride||n*size)+n*size)};}
const p=accessor(primitive.attributes.POSITION),uv=accessor(primitive.attributes.TEXCOORD_0),indices=accessor(primitive.indices);
const scalar=i=>indices.size===2?indices.row(i).readUInt16LE():indices.row(i).readUInt32LE();
const position=i=>[0,1,2].map(k=>p.row(i).readFloatLE(k*4));
assert.equal(indices.a.count,8380*3);const groups=[[],[],[]];
for(let t=0;t<indices.a.count;t+=3){const ids=[0,1,2].map(k=>scalar(t+k)),u=ids.map(i=>uv.row(i).readFloatLE()),y=ids.map(i=>position(i)[1]);
 const hose=u.every(x=>x>=.01499&&x<=.48001);assert(hose||u.every(x=>x>=.51499&&x<=.97501));
 const group=hose?1:y.every(x=>x<-.4)?2:0;
 if(group===0)assert(y.every(x=>Math.abs(x)<.106));if(group===2)assert(y.every(x=>x<-.48));
 groups[group].push(...ids);
}
assert.deepEqual(groups.map(x=>x.length/3),[712,5676,1992]);
fs.mkdirSync(output,{recursive:true});const names=['hose_bracket','stored_hose','hose_couplers'],receipt=[];
for(let part=0;part<3;part++){
 const j=structuredClone(original),ids=[...new Set(groups[part])],remap=new Map(ids.map((v,i)=>[v,i]));
 const chunks=[];let size=0;j.bufferViews=[];j.accessors=[];
 function append(data,extra={}){const offset=size;chunks.push(data);size+=data.length;const padding=(4-size%4)%4;if(padding){chunks.push(Buffer.alloc(padding));size+=padding;}j.bufferViews.push({buffer:0,byteOffset:offset,byteLength:data.length,...extra});return j.bufferViews.length-1;}
 const attributes={};for(const [name,index] of Object.entries(primitive.attributes)){
  const a=accessor(index);assert.equal(a.a.count,p.a.count);const data=Buffer.concat(ids.map(a.row));
  const next={...a.a,bufferView:append(data,{target:34962}),byteOffset:0,count:ids.length};delete next.min;delete next.max;
  if(name==='POSITION'){next.min=[0,1,2].map(k=>Math.min(...ids.map(i=>position(i)[k])));next.max=[0,1,2].map(k=>Math.max(...ids.map(i=>position(i)[k])));}
  attributes[name]=j.accessors.length;j.accessors.push(next);
 }
 const indexData=Buffer.alloc(groups[part].length*4);groups[part].forEach((v,i)=>indexData.writeUInt32LE(remap.get(v),i*4));
 const index=j.accessors.length;j.accessors.push({bufferView:append(indexData,{target:34963}),componentType:5125,count:groups[part].length,type:'SCALAR'});
 j.meshes[0].primitives=[{...primitive,attributes,indices:index}];
 for(const image of j.images){const v=original.bufferViews[image.bufferView];image.bufferView=append(bin.subarray(v.byteOffset||0,(v.byteOffset||0)+v.byteLength));}
 for(const node of j.nodes)if(node.mesh===0)node.name=names[part];
 j.buffers=[{byteLength:size}];j.extras={...j.extras,engine_source_sha256:hash,engine_damage_part:names[part]};
 const json=Buffer.from(JSON.stringify(j)),jsonSize=(json.length+3)&~3,header=Buffer.alloc(20),binaryHeader=Buffer.alloc(8);
 header.writeUInt32LE(0x46546c67,0);header.writeUInt32LE(2,4);header.writeUInt32LE(28+jsonSize+size,8);header.writeUInt32LE(jsonSize,12);header.writeUInt32LE(0x4e4f534a,16);
 binaryHeader.writeUInt32LE(size,0);binaryHeader.writeUInt32LE(0x004e4942,4);
 const result=Buffer.concat([header,json,Buffer.alloc(jsonSize-json.length,32),binaryHeader,...chunks]);
 fs.writeFileSync(path.join(output,names[part]+'.glb'),result);
 const bounds=j.accessors[attributes.POSITION];receipt.push({file:names[part]+'.glb',triangles:groups[part].length/3,vertices:ids.length,min:bounds.min,max:bounds.max,sha256:crypto.createHash('sha256').update(result).digest('hex')});
}
console.log(JSON.stringify({source_sha256:hash,source_triangles:8380,parts:receipt},null,2));
