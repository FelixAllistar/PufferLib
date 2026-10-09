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
console.log('PASS zoning import: original dirt/gravel hashes, 54 unchanged owners, 12 eligible tops, original metric factors and R8 mask; no duplicate gravel or reference geometry installed.');
