// Aggregate local read-only snapshots; raw text stays under ignored build/.
const fs=require('fs'),crypto=require('crypto');
const directory='build/webnav/captures',texts=new Set(),pages=[];
for(const file of fs.readdirSync(directory).filter(x=>x.endsWith('.json')&&x!=='manifest.json')){
 const path=directory+'/'+file;let x;try{x=JSON.parse(fs.readFileSync(path))}catch{pages.push({file,accepted:false,reason:'failed or incomplete capture'});continue}
 const o=x.observation,accepted=!!o?.nodes?.length&&/^https?:/.test(x.url||'');
 pages.push({file,url:x.url,title:x.title,accepted,reason:accepted?'nonempty initial page; behavior and rights not established':'empty/error snapshot; excluded',nodes:o?.nodes?.length||0,omitted:o?.omitted||0,truncated:o?.truncated||0,sha256:crypto.createHash('sha256').update(fs.readFileSync(path)).digest('hex')});
 if(accepted)for(const node of o.nodes)for(const key of ['name','value','text'])if(node[key])texts.add(node[key]);
}
fs.writeFileSync('build/webnav/reference/site-text-corpus.json',JSON.stringify([...texts]));
fs.writeFileSync(directory+'/manifest.json',JSON.stringify({schema:2,pages,unique_strings:texts.size},null,2)+'\n');
console.log(`Corpus: ${texts.size} distinct strings from ${pages.filter(x=>x.accepted).length} accepted initial-page snapshots`);
