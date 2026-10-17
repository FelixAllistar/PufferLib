// Audit the approved static art without executing Blender authoring scripts.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),assert=require('assert');
const root=path.resolve(__dirname,'..'),handoff=path.join(root,'art_handoffs/motel-dumpster');
const manifest=JSON.parse(fs.readFileSync(path.join(handoff,'MANIFEST.json')));
const provenance=JSON.parse(fs.readFileSync(path.join(handoff,'PROVENANCE.json')));
const bytes=fs.readFileSync(path.join(root,'assets/environment/motel_dumpster/BC_DUMP01_closed.glb'));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
assert.equal(sha(bytes),manifest.runtime.sha256);assert.equal(bytes.length,manifest.runtime.bytes);
assert.equal(bytes.readUInt32LE(0),0x46546c67);assert.equal(bytes.readUInt32LE(4),2);assert.equal(bytes.readUInt32LE(8),bytes.length);
assert.equal(bytes.readUInt32LE(16),0x4e4f534a);
const end=20+bytes.readUInt32LE(12),g=JSON.parse(bytes.subarray(20,end)),bin=bytes.subarray(end+8);
assert.equal(bytes.readUInt32LE(end+4),0x004e4942);assert.equal(end+8+bytes.readUInt32LE(end),bytes.length);
assert.equal(g.buffers.length,1);assert(!g.buffers[0].uri && g.buffers[0].byteLength<=bin.length);
assert(!g.animations && !g.skins);assert.equal(g.nodes.length,4);assert.equal(g.meshes.length,3);assert.equal(g.materials.length,4);
assert.deepEqual(g.scenes[g.scene??0].nodes,[3]);assert.deepEqual(g.nodes[3].children,[0,1,2]);
function view(id) {
    const v=g.bufferViews[id];assert(v && v.buffer===0);
    const start=v.byteOffset??0;assert(start>=0 && v.byteLength>0 && start+v.byteLength<=g.buffers[0].byteLength);
    return bin.subarray(start,start+v.byteLength);
}
function data(id) {
    const a=g.accessors[id];assert(a && !a.sparse);
    const v=g.bufferViews[a.bufferView],b=view(a.bufferView),n={SCALAR:1,VEC2:2,VEC3:3}[a.type],size={5123:2,5125:4,5126:4}[a.componentType];
    assert(n && size && a.count>0);
    const stride=v.byteStride??n*size,start=a.byteOffset??0;
    assert(stride>=n*size && start>=0 && start+(a.count-1)*stride+n*size<=b.length);
    return Array.from({length:a.count},(_,i)=>Array.from({length:n},(_,j)=>{
        const at=start+i*stride+j*size;
        const x=a.componentType===5126?b.readFloatLE(at):size===2?b.readUInt16LE(at):b.readUInt32LE(at);
        assert(Number.isFinite(x));return x;
    }));
}
const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];let triangles=0,primitives=0,solids=0;
for(const node of g.nodes) {
    assert(!node.matrix && !node.translation && !node.rotation && !node.scale && node.skin===undefined);
    if(node.mesh===undefined)continue;
    assert(node.extras.render_only);solids+=node.extras.source_parts.length;
    for(const p of g.meshes[node.mesh].primitives) {
        assert(p.mode===undefined || p.mode===4);assert(p.material>=0 && p.material<g.materials.length);
        const positions=data(p.attributes.POSITION),normals=data(p.attributes.NORMAL),uv=data(p.attributes.TEXCOORD_0),indices=data(p.indices).flat();
        assert.equal(normals.length,positions.length);assert.equal(uv.length,positions.length);
        assert(indices.length%3===0 && indices.every(i=>i>=0 && i<positions.length));
        for(const v of positions)for(let a=0;a<3;a++){min[a]=Math.min(min[a],v[a]);max[a]=Math.max(max[a],v[a]);}
        triangles+=indices.length/3;primitives++;
    }
}
assert.equal(triangles,2908);assert.equal(primitives,5);assert.equal(solids,64);
for(let a=0;a<3;a++)assert(Math.abs(max[a]-min[a]-manifest.runtime.dimensions_gltf_xyz_m[a])<1e-6);
assert(Math.abs(min[1])<1e-7);
const mapHashes=new Set(provenance.textures.map(t=>{
    const b=fs.readFileSync(path.join(handoff,t.file));assert.equal(sha(b),t.sha256);return t.sha256;
}));
assert.equal(g.images.length,4);
for(const image of g.images){assert(!image.uri && image.mimeType==='image/png');assert(mapHashes.has(sha(view(image.bufferView))));}
assert.equal(sha(fs.readFileSync(path.join(handoff,manifest.editable_source.file))),manifest.editable_source.sha256);
console.log('PASS BC_DUMP01 intake: original runtime/source/map hashes, 2908 triangles, 5 primitives, 64 source solids, finite indexed geometry/UVs, embedded PNGs, identity nodes and ground-centred metre bounds; native placement/collision remain open');
