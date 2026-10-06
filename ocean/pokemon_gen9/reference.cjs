'use strict';
const fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../..');
const oracle=path.join(root,'build/pokemon_gen9/oracle');
const pin='9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e';
if (JSON.parse(fs.readFileSync(path.join(oracle,'revision.json'))).revision!==pin)
  throw Error('Unpinned oracle');
const {Dex}=require(path.join(oracle,'dist/sim/dex'));
const {Teams}=require(path.join(oracle,'dist/sim/teams'));
const {PRNG,Gen5RNG}=require(path.join(oracle,'dist/sim/prng'));
module.exports={Dex,Teams,PRNG,Gen5RNG,root,oracle,pin};
