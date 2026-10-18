import assert from 'node:assert/strict';
import Core from './Controller.bend';
let checks=0;
for(const done of [false,true])for(const actor of [0,1,255])for(const kind of [0n,1n,2n,3n])for(const call of [0,50,100])for(const raising of [0,1])for(const upper of [150,300,20000])for(const target of [0,100,149,150,199,200,299,300,399,400,20000,20001]) {
  const allowed=Core.allowed(done,actor,0,kind,call,raising,100,200,upper,target);
  const expected=!done&&actor===0&&(kind===0n?call>0:kind===1n?true:kind===2n?!!raising&&target>100&&target<=upper&&(target>=200||target===upper):false);
  assert.equal(allowed,Number(expected));checks++;
}
assert.deepEqual(Core.record(0,150,Core.record(200,0,Core.empty())),{$:'S',hands:2,wins:1,won:200,lost:150});
for(const upper of [50,100,300,20000])for(const target of [0,99,100,250,100000]) {
  const actual=Core.raise_target(target,200,upper);
  assert.ok(actual<=upper);assert.ok(actual>=Math.min(200,upper));checks++;
}
console.log(`PASS: ${checks} Bend browser control boundary checks and session accounting`);
