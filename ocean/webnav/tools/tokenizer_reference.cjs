// Reference generation only: pinned Hugging Face Rust bindings via Node; no Python.
const fs=require('fs');
const dependency='../../../build/webnav/reference/tokenizers/node_modules/tokenizers';
if(require(dependency+'/package.json').version!=='0.23.2')throw Error('Reference requires tokenizers 0.23.2');
const {Tokenizer}=require(dependency);
(async()=>{
 const tokenizer=Tokenizer.fromFile('build/webnav/reference/potion-tokenizer.json');
 const weights=fs.readFileSync('build/webnav/reference/potion-model.safetensors');
 const offset=8+Number(weights.readBigUInt64LE()),dim=256;
 const cases=JSON.parse(fs.readFileSync('ocean/webnav/tests/text_cases.json')).flatMap(x=>[x.query,...x.options]);
 cases.push('', '   ', '\t\n\r', 'café résumé naïve', 'İstanbul ΣΟΣ', '東京 中文 한국어', 'Привет мир', 'العربية', 'हिन्दी', 'שלום', '👩‍💻🛒', '[CLS] Hi [MASK] [SEP] [UNK] [PAD]', 'hello\0world', 'a\u200db\u200cc', 'a\u0085b\u00a0c', '𝔸ﬃ Straße', 'a'.repeat(101), 'hello '.repeat(600), 'é'.repeat(100), 'e\u0301', 'x\ufffdy', '[cls] [mask]', 'foo[CLS]bar', 'a—b…c！d');
 let seed=731;const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return seed>>>0;};
 const pieces=['Add','cart','Résumé','東京','English','French','to','from','not','23.50','XL','🛒','İ','ΣΟΣ','\u200d','\0','\t','[CLS]','[UNK]','—','हिन्दी','مرحبا'];
 for(let n=0;n<1000;n++){let s='';for(let k=0;k<1+random()%16;k++)s+=pieces[random()%pieces.length]+(random()%2?' ':'');cases.push(s);}
 const rows=[];
 for(const text of cases){const encoded=await tokenizer.encode(text,null,{addSpecialTokens:false});const all=encoded.getIds(),ids=all.slice(0,512);const vector=Array(dim).fill(0);for(const id of ids){if(id===1)continue;for(let d=0;d<dim;d++)vector[d]=Math.fround(vector[d]+weights.readFloatLE(offset+(id*dim+d)*4));}const norm=Math.sqrt(vector.reduce((s,x)=>s+x*x,0));if(norm)for(let d=0;d<dim;d++)vector[d]=Math.fround(vector[d]*Math.fround(1/norm));rows.push({hex:Buffer.from(text).toString('hex'),ids,total:all.length,unknown:all.filter(x=>x===1).length,vector});}
 fs.writeFileSync('build/webnav/reference/tokenizer-golden.json',JSON.stringify({reference:'Hugging Face tokenizers Rust Node binding 0.23.2; independent JS normalized vector sum',cases:rows}));console.log(`${rows.length} reference cases`);
})().catch(e=>{console.error(e);process.exit(1)});
