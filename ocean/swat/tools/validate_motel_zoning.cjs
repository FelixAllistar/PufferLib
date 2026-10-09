// Read-only Phase A import contract. No Python, image conversion or geometry edits.
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict'),path=require('node:path');
const root=path.resolve(__dirname,'..'),runtime=path.join(root,'assets/environment/motel_zoning');
const mapping=JSON.parse(fs.readFileSync(path.join(runtime,'mapping.json')));
const provenance=JSON.parse(fs.readFileSync(path.join(root,'art_handoffs/motel-zoning/SCAN_PROVENANCE.json')));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const receipt=JSON.parse(fs.readFileSync(path.join(root,'art_handoffs/motel-zoning/INSTALLED_FILES.json')));
for(const file of receipt.files)assert.equal(hash(fs.readFileSync(path.join(root,file.path))),file.sha256);
assert.equal(mapping.supports.length,54);assert.equal(mapping.supports.filter(s=>s.eligible_top).length,12);
const header=fs.readFileSync(path.join(root,'motel_zoning.h'),'utf8');
assert.deepEqual([...header.matchAll(/case (\d+):/g)].map(m=>Number(m[1])),mapping.supports.filter(s=>s.eligible_top).map(s=>s.index));
const ground=fs.readFileSync(path.join(root,'assets/environment/motel_ground/connected_ground_render.glb'));
assert.equal(hash(ground),receipt.ground_render_sha256);
assert.equal(hash(fs.readFileSync(path.join(root,'assets/environment/motel_ground/connected_ground_collision.glb'))),receipt.ground_collision_sha256);
const jsonSize=ground.readUInt32LE(12),gltf=JSON.parse(ground.toString('utf8',20,20+jsonSize)),binStart=28+jsonSize;
const manifest=JSON.parse(fs.readFileSync(path.join(root,'assets/environment/motel_ground/placement_manifest.json')));
for(const s of mapping.supports) {
    assert.equal(s.owner_inherited,1119+s.index);
    assert.equal(s.semantic_name,manifest.support_matrix[s.index].semantic_name);
    assert.equal(gltf.nodes[s.index].name,s.semantic_name);
}
assert.equal(gltf.meshes.length,54);assert.equal(gltf.materials[0].extras.repeat_metres,8/3);
assert.deepEqual(gltf.materials[0].pbrMetallicRoughness.baseColorFactor,[.65,.65,.65,1]);
assert(Math.abs(gltf.materials[0].normalTexture.scale-.65)<1e-6);
const image=index=>{const v=gltf.bufferViews[gltf.images[gltf.textures[index].source].bufferView];return ground.subarray(binStart+v.byteOffset,binStart+v.byteOffset+v.byteLength);};
// Diffuse and normal remain the exact existing gravel images. Its packed G
// roughness image is intentionally not a byte copy of the standalone R map.
const gravel=gltf.materials[0];
for(const [channel,index]of [['Diffuse',gravel.pbrMetallicRoughness.baseColorTexture.index],['nor_gl',gravel.normalTexture.index]]) {
    const original=provenance.files.find(f=>f.asset==='gravel_ground_01'&&f.resolution==='1k'&&f.channel===channel);
    assert.equal(hash(image(index)),original.sha256);
}
for(const f of provenance.files.filter(f=>f.asset==='dirt'&&f.resolution==='1k')) {
    const b=fs.readFileSync(path.join(runtime,path.basename(f.path)));assert.equal(hash(b),f.sha256);
    assert.equal(b.readUInt32BE(16),1024);assert.equal(b.readUInt32BE(20),1024);
}
const mask=fs.readFileSync(path.join(runtime,'zoning_mask.png'));
assert.equal(mask.readUInt32BE(16),512);assert.equal(mask.readUInt32BE(20),512);assert.equal(mask[24],8);assert.equal(mask[25],0);
assert.equal(mapping.materials.dirt.repeat_m,2);assert.equal(mapping.materials.gravel.repeat_m,8/3);
assert.equal(mapping.materials.dirt.normal.scale,.65);assert.equal(mapping.materials.gravel.normal.scale,.65);
// The original authoring excerpt stays archival; validate its declared hashes
// and measured control points without executing the supplied generator.
const excerpt=path.join(root,'art_handoffs/motel-zoning/source_excerpt');
for(const line of fs.readFileSync(path.join(excerpt,'SHA256SUMS'),'utf8').trim().split('\n')) {
    const [digest,file]=line.split(/\s+/);assert.equal(hash(fs.readFileSync(path.join(excerpt,file))),digest);
}
const chunks=[];for(let at=8;at<mask.length;) {
    const length=mask.readUInt32BE(at);if(mask.toString('ascii',at+4,at+8)==='IDAT')chunks.push(mask.subarray(at+8,at+8+length));at+=12+length;
}
const raw=require('node:zlib').inflateSync(Buffer.concat(chunks)),pixels=Buffer.alloc(512*512);
function paeth(a,b,c){const p=a+b-c,da=Math.abs(p-a),db=Math.abs(p-b),dc=Math.abs(p-c);return da<=db&&da<=dc?a:db<=dc?b:c;}
for(let y=0;y<512;y++)for(let x=0;x<512;x++) {
    const filter=raw[y*513],a=x?pixels[y*512+x-1]:0,b=y?pixels[(y-1)*512+x]:0,c=x&&y?pixels[(y-1)*512+x-1]:0;
    assert(filter<=4);pixels[y*512+x]=(raw[y*513+1+x]+[0,a,b,Math.floor((a+b)/2),paeth(a,b,c)][filter])&255;
}
const points=JSON.parse(fs.readFileSync(path.join(excerpt,'qa/control_points.json')));
for(const point of points) {
    const [x,z]=point.world_xz,u=(x+64)/128,v=(z+48)/100;assert.deepEqual(point.uv_top_left,[u,v]);
    const px=Math.max(0,Math.min(511,u*512-.5)),pz=Math.max(0,Math.min(511,v*512-.5)),ix=Math.floor(px),iz=Math.floor(pz),a=px-ix,b=pz-iz;
    const sample=(xx,zz)=>pixels[Math.min(511,zz)*512+Math.min(511,xx)]/255;
    const weight=(sample(ix,iz)*(1-a)+sample(ix+1,iz)*a)*(1-b)+(sample(ix,iz+1)*(1-a)+sample(ix+1,iz+1)*a)*b;
    assert(Math.abs(weight-point.weight)<1e-12);
}
console.log(`PASS zoning source excerpt: original hashes and ${points.length} bilinear world/UV/weight controls; no Python executed.`);
console.log('PASS zoning import: original dirt/gravel hashes, 54 unchanged owners, 12 eligible tops, original metric factors and R8 mask; no duplicate gravel or reference geometry installed.');
