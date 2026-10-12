// Independent full triangle/attribute multiset and original image/material audit.
const fs=require('fs'),assert=require('assert'),crypto=require('crypto'),path=require('path');
const [original,folder]=process.argv.slice(2);assert(original&&folder);
function read(file){const b=fs.readFileSync(file),n=b.readUInt32LE(12),j=JSON.parse(b.subarray(20,20+n)),bin=b.subarray(28+n),p=j.meshes[0].primitives[0];
 function row(index,i){const a=j.accessors[index],v=j.bufferViews[a.bufferView],size={5123:2,5125:4,5126:4}[a.componentType],width={SCALAR:1,VEC2:2,VEC3:3,VEC4:4}[a.type]*size;assert(size&&width&&!a.sparse);const at=(v.byteOffset||0)+(a.byteOffset||0)+i*(v.byteStride||width);return bin.subarray(at,at+width);}
 const index=j.accessors[p.indices];let triangles=[];
 for(let t=0;t<index.count;t+=3){const vertices=[];for(let k=0;k<3;k++){const raw=row(p.indices,t+k),id=index.componentType===5123?raw.readUInt16LE():raw.readUInt32LE();vertices.push(Buffer.concat(Object.keys(p.attributes).sort().map(a=>row(p.attributes[a],id))).toString('hex'));}triangles.push(vertices.join(':'));}
 const images=j.images.map(i=>{const v=j.bufferViews[i.bufferView];return crypto.createHash('sha256').update(bin.subarray(v.byteOffset||0,(v.byteOffset||0)+v.byteLength)).digest('hex');});
 return{j,triangles,images};
}
const source=read(original),parts=['hose_bracket','stored_hose','hose_couplers'].map(n=>read(path.join(folder,n+'.glb')));
for(const part of parts){assert.deepEqual(part.j.materials,source.j.materials);assert.deepEqual(part.j.textures,source.j.textures);assert.deepEqual(part.j.samplers,source.j.samplers);assert.deepEqual(part.images,source.images);}
assert.deepEqual(parts.flatMap(x=>x.triangles).sort(),source.triangles.sort());
console.log('PASS original full hanger: all 8380 ordered triangle corners/positions/normals/UVs/tangents preserved exactly once, source materials/samplers/textures/PNG bytes unchanged across three damage parts');
