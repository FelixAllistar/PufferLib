// Reproduce the runtime's conservative envelope from the unchanged rigid GLB.
// Usage: node import_rifle_collision.cjs INPUT.glb OUTPUT.h [--check]
const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto');
const [input,output,mode]=process.argv.slice(2);
assert(input && output && (!mode || mode==='--check'),'Expected INPUT.glb OUTPUT.h [--check]');
assert.notEqual(path.resolve(input),path.resolve(output),'The source GLB is read-only');
const file=fs.readFileSync(input),sha=crypto.createHash('sha256').update(file).digest('hex');
assert.equal(sha,'011f758561ead2519e7cf37f6b1379479e232dbf493d374e3ae06ef01b33133a','Expected accepted original rigid-rifle bytes');
assert.equal(file.readUInt32LE(0),0x46546c67);assert.equal(file.readUInt32LE(4),2);assert.equal(file.readUInt32LE(8),file.length);
let json,bin;
for(let off=12;off<file.length;){const n=file.readUInt32LE(off),type=file.readUInt32LE(off+4),data=file.subarray(off+8,off+8+n);assert.equal(data.length,n);if(type===0x4e4f534a)json=JSON.parse(data.toString());if(type===0x004e4942)bin=data;off+=n+8;}
assert(json && bin && json.buffers.length===1 && !json.buffers[0].uri);
function local(n){
    if(n.matrix)return n.matrix;
    const [x,y,z,w]=n.rotation??[0,0,0,1],s=n.scale??[1,1,1],p=n.translation??[0,0,0];
    return[(1-2*(y*y+z*z))*s[0],2*(x*y+z*w)*s[0],2*(x*z-y*w)*s[0],0,
        2*(x*y-z*w)*s[1],(1-2*(x*x+z*z))*s[1],2*(y*z+x*w)*s[1],0,
        2*(x*z+y*w)*s[2],2*(y*z-x*w)*s[2],(1-2*(x*x+y*y))*s[2],0,...p,1];
}
function multiply(a,b){const c=Array(16).fill(0);for(let j=0;j<4;j++)for(let i=0;i<4;i++)for(let k=0;k<4;k++)c[j*4+i]+=a[k*4+i]*b[j*4+k];return c;}
function transform(p,m){return[0,1,2].map(c=>m[c]*p[0]+m[4+c]*p[1]+m[8+c]*p[2]+m[12+c]);}
const parents=Array(json.nodes.length).fill(-1),worlds=[];
for(let i=0;i<json.nodes.length;i++)for(const child of json.nodes[i].children??[]){assert.equal(parents[child],-1);parents[child]=i;}
function world(i){return worlds[i]??=(parents[i]<0?local(json.nodes[i]):multiply(world(parents[i]),local(json.nodes[i])));}
function accessor(id,index){
    const a=json.accessors[id],v=json.bufferViews[a.bufferView],width={SCALAR:1,VEC3:3}[a.type];
    const read={5121:['readUInt8',1],5123:['readUInt16LE',2],5125:['readUInt32LE',4],5126:['readFloatLE',4]}[a.componentType];
    assert(width && read && !a.sparse && !a.normalized && index<a.count && v.buffer===0);
    const at=(v.byteOffset??0)+(a.byteOffset??0)+index*(v.byteStride??width*read[1]);
    return Array.from({length:width},(_,k)=>bin[read[0]](at+k*read[1]));
}
const primitives=[];
for(let node=0;node<json.nodes.length;node++){
    const n=json.nodes[node];assert(n.skin===undefined,'Expected a rigid original rifle');if(n.mesh===undefined)continue;
    for(const p of json.meshes[n.mesh].primitives){
        assert(p.mode===undefined || p.mode===4);
        const positions=Array.from({length:json.accessors[p.attributes.POSITION].count},(_,v)=>transform(accessor(p.attributes.POSITION,v),world(node)));
        const indices=p.indices===undefined?positions.map((_,i)=>i):Array.from({length:json.accessors[p.indices].count},(_,i)=>accessor(p.indices,i)[0]);
        assert.equal(indices.length%3,0);for(const i of indices)assert(i<positions.length);
        primitives.push({positions,indices});
    }
}
const triangles=primitives.reduce((n,p)=>n+p.indices.length/3,0);assert.equal(primitives.length,2);assert.equal(triangles,14762);
function clip(poly,x,sign){const out=[];for(let i=0;i<poly.length;i++){const a=poly[i],b=poly[(i+1)%poly.length],da=sign*(a[0]-x),db=sign*(b[0]-x);if(da>=0)out.push(a);if((da<0)!=(db<0)){const t=da/(da-db);out.push(a.map((n,k)=>n+t*(b[k]-n)));}}return out;}
const ranges=[[-.00001,.20],[.20,.40],[.40,.60],[.60,.90001]];
const boxes=ranges.map(([lo,hi])=>{
    const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];let faces=0;
    for(const p of primitives)for(let t=0;t<p.indices.length;t+=3){
        let poly=[0,1,2].map(k=>p.positions[p.indices[t+k]]);poly=clip(clip(poly,lo,1),hi,-1);if(!poly.length)continue;faces++;
        for(const v of poly)for(let k=0;k<3;k++){min[k]=Math.min(min[k],v[k]);max[k]=Math.max(max[k],v[k]);}
    }
    assert(faces && min.every(Number.isFinite) && max.every(Number.isFinite));return{min,max};
});
const literal=n=>n.toFixed(9)+'f';
const header='static const float rifle_boxes[4][2][3]={\n'+boxes.map(b=>'  {{'+b.min.map(literal).join(',')+'},{'+b.max.map(literal).join(',')+'}}').join(',\n')+'\n};\n';
if(mode==='--check')assert.equal(fs.readFileSync(output,'utf8'),header,'Generated envelope differs');else fs.writeFileSync(output,header);
console.log(JSON.stringify({source_sha256:sha,triangles,sections:boxes.length,mode:mode??'write',header_sha256:crypto.createHash('sha256').update(header).digest('hex')}));
