// Verify actual runtime GLBs independently of the supplied exporter receipts.
// Run from the repository root: node ocean/swat/tools/verify_motel_seating.cjs
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../assets/environment/motel_props');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function read(file) {
    const bytes = fs.readFileSync(path.join(root, file));
    assert.equal(bytes.readUInt32LE(0), 0x46546c67);
    assert.equal(bytes.readUInt32LE(4), 2);
    assert.equal(bytes.readUInt32LE(8), bytes.length);
    const length = bytes.readUInt32LE(12);
    assert.equal(bytes.readUInt32LE(16), 0x4e4f534a);
    const json = JSON.parse(bytes.subarray(20, 20 + length).toString());
    const offset = 20 + length;
    assert.equal(bytes.readUInt32LE(offset + 4), 0x004e4942);
    return {json, bin:bytes.subarray(offset + 8), hash:hash(bytes)};
}
const reports = [];
for (const name of ['modern_arm_chair_01', 'vintage_day_bed']) {
    const original = read(`${name}_floor_centered_2k.glb`);
    const candidate = read(`${name}_floor_centered_1k_candidate.glb`);
    for (const key of ['asset', 'scene', 'scenes', 'nodes', 'meshes', 'accessors', 'materials', 'textures', 'samplers'])
        assert.deepEqual(candidate.json[key], original.json[key], `${name}: ${key}`);
    assert.equal(candidate.json.bufferViews.length, original.json.bufferViews.length);
    assert.equal(candidate.json.images.length, original.json.images.length);
    const imageViews = new Set(original.json.images.map(image => image.bufferView));
    const geometry = [];
    for (let i = 0; i < original.json.bufferViews.length; i++) {
        if (imageViews.has(i)) continue;
        const a = original.json.bufferViews[i], b = candidate.json.bufferViews[i];
        assert.deepEqual(a, b, `${name}: geometry buffer ${i}`);
        const bytes = original.bin.subarray(a.byteOffset || 0, (a.byteOffset || 0) + a.byteLength);
        assert.deepEqual(candidate.bin.subarray(b.byteOffset || 0, (b.byteOffset || 0) + b.byteLength), bytes);
        geometry.push({bufferView:i, bytes:bytes.length, sha256:hash(bytes)});
    }
    const images = original.json.images.map((image, i) => {
        const derived = candidate.json.images[i];
        assert.equal(derived.bufferView, image.bufferView);
        assert.equal(derived.mimeType, 'image/png');
        const a = original.json.bufferViews[image.bufferView], b = candidate.json.bufferViews[derived.bufferView];
        const source = original.bin.subarray(a.byteOffset, a.byteOffset + a.byteLength);
        const bytes = candidate.bin.subarray(b.byteOffset, b.byteOffset + b.byteLength);
        assert.equal(bytes.subarray(0, 8).toString('hex'), '89504e470d0a1a0a');
        assert.equal(bytes.readUInt32BE(16), 1024);
        assert.equal(bytes.readUInt32BE(20), 1024);
        return {image:i, sourceSha256:hash(source), derivedSha256:hash(bytes), width:1024, height:1024};
    });
    reports.push({name, originalSha256:original.hash, candidateSha256:candidate.hash, geometry, images});
}
console.log(JSON.stringify({result:'PASS: geometry, hierarchy, accessors, materials and texture routing unchanged; separate 1024px PNG maps', reports}, null, 2));
