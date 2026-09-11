// Generate a diagnostic translation unit, never edit shared trainer sources.
const fs=require('node:fs');
const root='build/pokemon';
fs.mkdirSync(root,{recursive:true});
let source=fs.readFileSync('src/pufferl.cu','utf8');
const needle='                    cumsum += expf(l - logsumexp);';
if(source.split(needle).length!==2) throw Error('Sampler changed; review probe');
const marker='\n            float sampled_logit = use_cache ? cache[sampled]';
if(source.split(marker).length!==2) throw Error('Sampler tail changed; review probe');
source=source.replace('                float rand_val = curand_uniform(&state);',
    '                float rand_val = curand_uniform(&state);\n                bool selected_by_cdf = false;');
source=source.replace('                    if (rand_val < cumsum) {\n                        sampled = a;',
    '                    if (rand_val < cumsum) {\n                        selected_by_cdf = true;\n                        sampled = a;');
const tail='                }\n            }\n\n            float sampled_logit';
const diagnostic=`                }
                if (!selected_by_cdf) {
                    int bad=0;
                    for(int a=0;a<A;a++) bad += !isfinite(to_float(logits[logits_base+logits_offset+a]));
                    printf("POKEMON_SAMPLER_PROBE row=%d rand=%.9g cdf=%.9g max=%.9g lse=%.9g fallback=%d legal=%d bad_logits=%d\\n",
                        idx,rand_val,cumsum,max_val,logsumexp,sampled,
                        (int)action_mask[mask_base+logits_offset+sampled],bad);
                }
            }

            float sampled_logit`;
if(source.split(tail).length!==2) throw Error('Sampler branch changed; review probe');
source=source.replace(tail,diagnostic);
// Production now has the legal-tail fix; --candidate is retained as an alias.
fs.writeFileSync(root+'/pufferl_sampler_probe.cu',source);
const build=fs.readFileSync('build.sh','utf8').replaceAll('src/pufferl.cu',root+'/pufferl_sampler_probe.cu');
fs.writeFileSync(root+'/build_sampler_probe.sh',build);
console.log('Generated isolated probe; shared sources unchanged');
