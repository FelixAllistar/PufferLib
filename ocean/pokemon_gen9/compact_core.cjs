'use strict';
const path=require('path'),fs=require('fs'),crypto=require('crypto'),assert=require('assert');
const {root}=require('./reference.cjs'),core=require('./continuation_core.cjs');
const compact=require(path.join(root,'build/pokemon_gen9/compact/encode.cjs'));
const sourceHash=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'ocean/pokemon_gen9/worker_core.cjs'))).digest('hex');
assert.equal(sourceHash,compact.schema.source_encoder_sha256,'Public encoder changed; regenerate compact schema');
class Game extends core.Game{
  observe(s,out){return compact.encode(this.views[s],this.requests[s],s,this.pending[s],out);}
}
module.exports={...core,...compact,Game};
