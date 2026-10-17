// Read the delivered archive and GLBs; never execute authoring scripts.
const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto'),zlib=require('zlib');
const root=path.resolve(__dirname,'..'),handoff=path.join(root,'art_handoffs/motel-door-hinges');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const zip=fs.readFileSync(path.join(handoff,'source/ROOM101_HINGE_SEATED_OPTION1_r01_candidate.zip'));
assert.equal(sha(zip),'9964a13c72bfb68b75824c2b2315f3a7977c227d7625e82245b8585728b50cb5');
const end=zip.lastIndexOf(Buffer.from([0x50,0x4b,0x05,0x06]));assert(end>=0);
let at=zip.readUInt32LE(end+16);const members=new Map();
for(let i=0;i<zip.readUInt16LE(end+10);i++) {
    assert.equal(zip.readUInt32LE(at),0x02014b50);
    const method=zip.readUInt16LE(at+10),compressed=zip.readUInt32LE(at+20),size=zip.readUInt32LE(at+24);
    const length=zip.readUInt16LE(at+28),extra=zip.readUInt16LE(at+30),comment=zip.readUInt16LE(at+32);
    const name=zip.subarray(at+46,at+46+length).toString(),offset=zip.readUInt32LE(at+42);
    assert(name.startsWith('seated_hinge_kit/') && !name.split('/').includes('..') && size<64*1024*1024);
    assert(!members.has(name.slice('seated_hinge_kit/'.length)));assert.equal(zip.readUInt32LE(offset),0x04034b50);
    const start=offset+30+zip.readUInt16LE(offset+26)+zip.readUInt16LE(offset+28);
    assert(method===0 || method===8);const bytes=zip.subarray(start,start+compressed);
    const data=method===8?zlib.inflateRawSync(bytes):bytes;assert.equal(data.length,size);
    members.set(name.slice('seated_hinge_kit/'.length),data);at+=46+length+extra+comment;
}
const contents=JSON.parse(members.get('candidate/PACKAGE_CONTENTS.json'));
assert.equal(members.size,81);assert.equal(contents.file_count,80);
for(const [name,hash] of Object.entries(contents.sha256))assert.equal(sha(members.get(name)),hash,name);
for(const name of ['room_door_leaf_candidate.glb','room_facade_4m_candidate.glb'])
    assert(fs.readFileSync(path.join(root,'assets/environment/motel_room101_v2',name)).equals(members.get('source/assets/'+name)));

function decode(bytes) {
    assert.equal(bytes.readUInt32LE(0),0x46546c67);assert.equal(bytes.readUInt32LE(4),2);assert.equal(bytes.readUInt32LE(8),bytes.length);
    const end=20+bytes.readUInt32LE(12),g=JSON.parse(bytes.subarray(20,end)),bin=bytes.subarray(end+8);
    const read=id=>{
        const a=g.accessors[id],v=g.bufferViews[a.bufferView],n={SCALAR:1,VEC2:2,VEC3:3,VEC4:4}[a.type],size={5123:2,5125:4,5126:4}[a.componentType];
        assert(n && size && !a.sparse);
        return Array.from({length:a.count},(_,i)=>Array.from({length:n},(_,j)=>{
            const at=(v.byteOffset||0)+(a.byteOffset||0)+i*(v.byteStride||n*size)+j*size;
            return a.componentType===5126?bin.readFloatLE(at):size===2?bin.readUInt16LE(at):bin.readUInt32LE(at);
        }));
    };
    return {g,bin,read};
}
function glb(bytes) {
    const {g,bin,read}=decode(bytes);
    const nodes=g.nodes.filter(n=>n.mesh!==undefined).map(n=>{
        assert(!n.matrix && !n.rotation && !n.scale && !n.children && !n.skin);
        const primitives=g.meshes[n.mesh].primitives;assert.equal(primitives.length,1);
        const p=primitives[0];assert(p.mode===undefined || p.mode===4);
        const positions=read(p.attributes.POSITION).map(v=>v.map((x,a)=>x+(n.translation?.[a]||0)));
        assert(positions.flat().every(Number.isFinite));
        const indices=read(p.indices).flat();assert(indices.every(i=>i>=0 && i<positions.length));
        return {name:n.name,positions,indices};
    });
    return {g,bin,nodes};
}
const names=['room101_facade_option1.glb','room101_leaf_option1.glb','room101_hardware_option1.glb','room101_failure_remnants.glb'];
const files=names.map(name=>{
    const bytes=fs.readFileSync(path.join(root,'assets/environment/motel_room101_hinges',name));
    assert.equal(sha(bytes),contents.sha256['candidate/runtime/'+name]);return glb(bytes);
});
const preservation=[];
const key=(name,p,uv)=>JSON.stringify([name,...p,...uv].map(v=>typeof v==='number'?Math.round(v*1e6):v));
function imageHashes(file) {
    return (file.g.images||[]).map(image=>{
        assert(image.bufferView!==undefined && !image.uri);
        const view=file.g.bufferViews[image.bufferView];
        return sha(file.bin.subarray(view.byteOffset||0,(view.byteOffset||0)+view.byteLength));
    }).sort();
}
for(const [oldName,newName,casing] of [['room_door_leaf_candidate.glb','room101_leaf_option1.glb',false],['room_facade_4m_candidate.glb','room101_facade_option1.glb',true]]) {
    const old=decode(members.get('source/assets/'+oldName));
    const next=decode(fs.readFileSync(path.join(root,'assets/environment/motel_room101_hinges',newName)));
    assert.deepEqual(imageHashes(next),imageHashes(old));
    const pool=new Map();
    for(const node of old.g.nodes) {
        assert(!node.matrix && !node.translation && !node.rotation && !node.scale && !node.children);
        if(node.mesh===undefined)continue;
        for(const primitive of old.g.meshes[node.mesh].primitives) {
            const positions=old.read(primitive.attributes.POSITION),uvs=old.read(primitive.attributes.TEXCOORD_0),normals=old.read(primitive.attributes.NORMAL);
            positions.forEach((p,i)=>{const k=key(old.g.materials[primitive.material].name,p,uvs[i]);if(!pool.has(k))pool.set(k,[]);pool.get(k).push(normals[i]);});
        }
    }
    let unchanged=0,relief=0,retreated=0,maxNormal=0;
    for(const node of next.g.nodes) {
        assert(!node.matrix && !node.translation && !node.rotation && !node.scale && !node.children);
        for(const primitive of next.g.meshes[node.mesh].primitives) {
            const positions=next.read(primitive.attributes.POSITION),uvs=next.read(primitive.attributes.TEXCOORD_0),normals=next.read(primitive.attributes.NORMAL);
            positions.forEach((p,i)=>{
                const material=next.g.materials[primitive.material].name;
                let matches=pool.get(key(material,p,uvs[i]));
                if(!matches && casing && node.name==='R101_FACADE_03') {
                    matches=pool.get(key(material,[p[0]+.006,p[1],p[2]],uvs[i]));
                    if(matches){retreated++;return;}
                }
                const inRelief=!casing && node.name==='R101_LEAF_00' && p[0]>=-.000001 && p[0]<=.007001 &&
                    Math.abs(p[2])<=.007001 && [.23,1.10,1.92].some(y=>Math.abs(p[1]-y)<=.050001);
                if(inRelief){relief++;return;}
                assert(matches,newName+' changed protected vertex/UV '+JSON.stringify(p));unchanged++;
                if(casing && node.name==='R101_FACADE_03')return;
                const n=normals[i],length=Math.hypot(...n);
                const angle=Math.min(...matches.map(m=>Math.acos(Math.max(-1,Math.min(1,m.reduce((d,v,a)=>d+v*n[a],0)/(Math.hypot(...m)*length))))*180/Math.PI));
                maxNormal=Math.max(maxNormal,angle);
            });
        }
    }
    assert(maxNormal<.01 && unchanged>100);
    if(casing)assert.equal(retreated,48);else assert(relief>100);
    preservation.push({file:newName,unchanged_position_uv_vertices:unchanged,approved_relief_vertices:relief,approved_6mm_casing_vertices:retreated,
        position_uv_match_precision_m:1e-6,max_unchanged_normal_angle_degrees:maxNormal,embedded_png_hashes_unchanged:true});
}
assert.deepEqual(files.map(f=>f.nodes.length),[7,8,9,24]);
assert.equal(files[2].nodes.reduce((n,m)=>n+m.indices.length/3,0),3198);
assert.equal(files[3].nodes.reduce((n,m)=>n+m.indices.length/3,0),504);
const clearances=[];
for(let h=0;h<3;h++) {
    const prefix=`R101_H0${h+1}_`,height=[.23,1.10,1.92][h];
    const [leaf,frame,pin]=files[2].nodes.slice(3*h,3*h+3);
    assert.deepEqual([leaf.name,frame.name,pin.name],[prefix+'LEAF_METAL',prefix+'FIXED_JAMB_METAL',prefix+'PIN']);
    const pinRadius=Math.max(...pin.positions.map(v=>Math.hypot(v[0],v[2])));
    assert(Math.abs(pinRadius-.003)<1e-8);
    assert(Math.abs((Math.min(...pin.positions.map(v=>v[1]))+Math.max(...pin.positions.map(v=>v[1])))/2-height)<1e-7);
    let bore=Infinity,faces=0;
    for(const node of [leaf,frame])for(let t=0;t<node.indices.length;t+=3) {
        const v=node.indices.slice(t,t+3).map(i=>node.positions[i]);
        if(!v.every(p=>Math.abs(Math.hypot(p[0],p[2])-.0033)<1e-8))continue;
        for(let k=0;k<3;k++) {
            const a=v[k],b=v[(k+1)%3],dx=b[0]-a[0],dz=b[2]-a[2],length=dx*dx+dz*dz;
            const u=length?Math.max(0,Math.min(1,-(a[0]*dx+a[2]*dz)/length)):0;
            bore=Math.min(bore,Math.hypot(a[0]+u*dx,a[2]+u*dz));
        }
        faces++;
    }
    assert(faces>100 && bore-pinRadius>.00028);
    const central=leaf.positions.filter(v=>Math.hypot(v[0],v[2])<.0061).map(v=>Math.abs(v[1]-height));
    const outer=frame.positions.filter(v=>Math.hypot(v[0],v[2])<.0061).map(v=>Math.abs(v[1]-height));
    const axial=Math.min(...outer)-Math.max(...central);assert(axial>.000999);
    clearances.push({hinge:h+1,pin_radius_m:pinRadius,minimum_bore_chord_radius_m:bore,radial_clearance_m:bore-pinRadius,axial_gap_m:axial});
}
const report={archive_sha256:sha(zip),files:members.size,verified_payload_hashes:80,frozen_inputs_match_local:true,
    runtime_sha256:Object.fromEntries(names.map(n=>[n,contents.sha256['candidate/runtime/'+n]])),
    clearances,preservation,scope:'Independent byte/hash, protected position/UV, embedded-map, normal, node-transform, triangle-count and actual faceted pin/bore/axial-gap checks. Native renderer and physics acceptance are separate. No authoring script execution.'};
fs.writeFileSync(path.join(handoff,'engine/source-check.json'),JSON.stringify(report,null,2)+'\n');
console.log('PASS Room 101 kit: 80 payload hashes, frozen local inputs, 4 unchanged runtime GLBs, all 3 faceted pin/bore clearances and axial gaps');
