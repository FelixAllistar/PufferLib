// Bake original CC0 fragment geometry and sampled base colour into one small
// immediate-mode batch. No runtime image, material, model or physics allocation.
// Usage: node ocean/swat/bake_breach_fragments.js UNPACKED_PACKETS OUTPUT_HEADER OUTPUT_RECEIPT
const fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib'),crypto=require('node:crypto');
const [root,header,receipt]=process.argv.slice(2);
if(!receipt)throw Error('Expected unpacked packet root, output header and JSON receipt');
const inputs=[
 ['timber_splinters_visual_candidate_v1/exports/timber_splinter_01.glb','1uTiW91mfgZo7HtK05tZjJsYACBWjcpmx'],
 ['timber_splinters_visual_candidate_v1/exports/timber_splinter_02.glb','1uTiW91mfgZo7HtK05tZjJsYACBWjcpmx'],
 ['gypsum_fragments_shared_v1/gypsum_wedge_64mm.glb','1rj1HhsWznOAOLJOutwv17ne6iZaRZG5x'],
 ['gypsum_fragments_shared_v1/gypsum_peeled_84mm.glb','1rj1HhsWznOAOLJOutwv17ne6iZaRZG5x'],
 ['original_fired_clay_fragments_v1/models/Clay_chip_30mm.glb','19iWzdllk9hHb4G6B9nfDO2bGgGMKq8Q9'],
 ['original_fired_clay_fragments_v1/models/Clay_corner_85mm.glb','19iWzdllk9hHb4G6B9nfDO2bGgGMKq8Q9']
];
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function png(bytes) {
 if(bytes.readUInt32BE(0)!==0x89504e47)throw Error('Expected PNG');
 let width,height,channels,parts=[];
 for(let at=8;at<bytes.length;) {
  const n=bytes.readUInt32BE(at),type=bytes.toString('ascii',at+4,at+8),data=bytes.subarray(at+8,at+8+n);at+=n+12;
  if(type==='IHDR') {width=data.readUInt32BE(0);height=data.readUInt32BE(4);channels=data[9]===6?4:data[9]===2?3:0;
   if(data[8]!==8 || !channels || data[12]!==0 || width>4096 || height>4096)throw Error('Unsupported PNG format');}
  if(type==='IDAT')parts.push(data);
 }
 const row=width*channels,raw=zlib.inflateSync(Buffer.concat(parts)),pixels=Buffer.alloc(row*height);
 if(raw.length!==(row+1)*height)throw Error('Wrong PNG scanline length');
 for(let y=0;y<height;y++)for(let x=0;x<row;x++) {
  const f=raw[y*(row+1)],a=x>=channels?pixels[y*row+x-channels]:0,b=y?pixels[(y-1)*row+x]:0,c=y&&x>=channels?pixels[(y-1)*row+x-channels]:0;
  let prediction=0;
  if(f===1)prediction=a;else if(f===2)prediction=b;else if(f===3)prediction=(a+b)>>1;
  else if(f===4){const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);prediction=pa<=pb&&pa<=pc?a:pb<=pc?b:c;}
  else if(f!==0)throw Error('Invalid PNG filter');
  pixels[y*row+x]=(raw[y*(row+1)+x+1]+prediction)&255;
 }
 return {width,height,channels,pixels};
}
function glb(bytes) {
 if(bytes.readUInt32LE(0)!==0x46546c67 || bytes.readUInt32LE(4)!==2 || bytes.readUInt32LE(8)!==bytes.length)throw Error('Invalid GLB');
 let json,bin;
 for(let at=12;at<bytes.length;) {const size=bytes.readUInt32LE(at),type=bytes.readUInt32LE(at+4),chunk=bytes.subarray(at+8,at+8+size);at+=8+size;
  if(type===0x4e4f534a)json=JSON.parse(chunk.toString());else if(type===0x004e4942)bin=chunk;}
 if(!json || !bin || json.nodes.some(n=>n.matrix||n.translation||n.rotation||n.scale))throw Error('Expected baked identity node transforms');
 function accessor(id) {
  const a=json.accessors[id],v=json.bufferViews[a.bufferView],components={SCALAR:1,VEC2:2,VEC3:3,VEC4:4}[a.type];
  const bytes={5123:2,5125:4,5126:4}[a.componentType];
  if(!components||!bytes||a.sparse||a.normalized)throw Error('Unsupported accessor');
  const stride=v.byteStride||components*bytes,start=(v.byteOffset||0)+(a.byteOffset||0),result=[];
  for(let i=0;i<a.count;i++) {let values=[];for(let j=0;j<components;j++) {
    const at=start+i*stride+j*bytes,value=a.componentType===5126?bin.readFloatLE(at):bytes===2?bin.readUInt16LE(at):bin.readUInt32LE(at);
    if(!Number.isFinite(value))throw Error('Nonfinite attribute');values.push(value);}
   result.push(components===1?values[0]:values);}
  return result;
 }
 const imageCache=new Map(),imageHashes=[];
 function image(index) {
  const source=json.textures[index].source;
  if(!imageCache.has(source)) {const img=json.images[source],v=json.bufferViews[img.bufferView],data=bin.subarray(v.byteOffset||0,(v.byteOffset||0)+v.byteLength);
   if(img.mimeType!=='image/png')throw Error('Expected embedded PNG');imageHashes.push({name:img.name,sha256:hash(data)});imageCache.set(source,png(data));}
  return imageCache.get(source);
 }
 const vertices=[],indices=[];
 for(const mesh of json.meshes)for(const primitive of mesh.primitives) {
  if(primitive.mode!==undefined&&primitive.mode!==4)throw Error('Expected triangles');
  const p=accessor(primitive.attributes.POSITION),n=accessor(primitive.attributes.NORMAL),uv=accessor(primitive.attributes.TEXCOORD_0),idx=accessor(primitive.indices);
  const material=json.materials[primitive.material],m=material.pbrMetallicRoughness,texture=image(m.baseColorTexture.index),factor=m.baseColorFactor||[1,1,1,1];
  if(material.alphaMode && material.alphaMode!=='OPAQUE')throw Error('Expected opaque fragments');
  const base=vertices.length;
  for(let i=0;i<p.length;i++) {
   if(Math.abs(Math.hypot(...n[i])-1)>.002)throw Error('Invalid normal');
   const u=((uv[i][0]%1)+1)%1,v=((uv[i][1]%1)+1)%1;
   const x=Math.min(texture.width-1,Math.floor(u*texture.width)),y=Math.min(texture.height-1,Math.floor(v*texture.height));
   const at=(y*texture.width+x)*texture.channels;
   const color=[0,1,2].map(k=>{
    const s=texture.pixels[at+k]/255,linear=(s<=.04045?s/12.92:((s+.055)/1.055)**2.4)*factor[k];
    return Math.round(255*Math.max(0,Math.min(1,linear<=.0031308?12.92*linear:1.055*linear**(1/2.4)-.055)));
   });
   vertices.push({p:p[i],n:n[i],color:[...color,255]});
  }
  for(const i of idx){if(i>=p.length)throw Error('Index outside mesh');indices.push(base+i);}
 }
 if(indices.length%3 || vertices.length>65535)throw Error('Invalid triangle count');
 let degenerate=0;
 for(let i=0;i<indices.length;i+=3) {
  const p=indices.slice(i,i+3).map(x=>vertices[x].p),a=p[1].map((x,j)=>x-p[0][j]),b=p[2].map((x,j)=>x-p[0][j]);
  if(Math.hypot(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])<1e-12)degenerate++;
 }
 if(degenerate)throw Error('Degenerate triangle in selected source');
 return {vertices,indices,min:[0,1,2].map(k=>Math.min(...vertices.map(v=>v.p[k]))),max:[0,1,2].map(k=>Math.max(...vertices.map(v=>v.p[k]))),imageHashes};
}
const f=x=>{const s=Number(x.toPrecision(9)).toString();return s+(s.includes('.')||s.includes('e')?'f':'.0f');};
let lines=['// Generated by bake_breach_fragments.js; original CC0 geometry, sampled colour.','#ifndef SWAT_BREACH_FRAGMENT_DATA_H','#define SWAT_BREACH_FRAGMENT_DATA_H','#include <stdint.h>','typedef struct SwatFragmentVertex {float p[3],n[3];uint8_t color[4];} SwatFragmentVertex;','typedef struct SwatFragmentMesh {const SwatFragmentVertex* vertices;const uint16_t* indices;int vertex_count,triangle_count;float min[3],max[3];} SwatFragmentMesh;'];
const records=[],models=[];
inputs.forEach(([file,id],i)=>{
 const bytes=fs.readFileSync(path.join(root,file)),model=glb(bytes);models.push(model);
 lines.push(`static const SwatFragmentVertex swat_fragment_vertices_${i}[]={`);
 for(const v of model.vertices)lines.push(`{{${v.p.map(f)}},{${v.n.map(f)}},{${v.color}}},`);
 lines.push('};',`static const uint16_t swat_fragment_indices_${i}[]={`);
 for(let at=0;at<model.indices.length;at+=36)lines.push(model.indices.slice(at,at+36).join(',')+',');
 lines.push('};');
 records.push({file,source_packet:`https://drive.google.com/file/d/${id}/view`,sha256:hash(bytes),triangles:model.indices.length/3,vertices:model.vertices.length,min:model.min,max:model.max,sampled_images:model.imageHashes});
});
lines.push('static const SwatFragmentMesh swat_fragment_meshes[]={');
models.forEach((m,i)=>lines.push(`{swat_fragment_vertices_${i},swat_fragment_indices_${i},${m.vertices.length},${m.indices.length/3},{${m.min.map(f)}},{${m.max.map(f)}}},`));
lines.push('};','#endif','');fs.writeFileSync(header,lines.join('\n'));
const report={license:'CC0-1.0',units:'metres',geometry:'Original positions, normals, indices and triangle order retained; base-color images sampled into opaque vertex colours. Normal/roughness textures are omitted for these small cosmetic fragments.',runtime_textures:0,runtime_materials:0,geometry_attribute_bytes:models.reduce((s,m)=>s+m.vertices.length*28+m.indices.length*2,0),header_sha256:hash(fs.readFileSync(header)),models:records};
fs.writeFileSync(receipt,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({models:models.length,triangles:records.reduce((s,m)=>s+m.triangles,0),attribute_bytes:report.geometry_attribute_bytes,header_sha256:report.header_sha256}));
