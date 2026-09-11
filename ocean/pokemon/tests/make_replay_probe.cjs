const fs = require('node:fs');
const source = fs.readFileSync('src/algo.cu','utf8');
function kernel(name) {
    const start=source.indexOf('__global__ void '+name+'(');
    if(start<0) throw Error(name);
    let pos=source.indexOf('{',start), depth=1, end=pos+1;
    while(depth && end<source.length) {if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
    return source.slice(start,end);
}
let sample=kernel('multinomial_sample').replace('float u = curand_uniform(&rng_state);','float u = 1.0f; // force the legal cuRAND endpoint');
fs.writeFileSync('build/pokemon/replay_probe.cu', `
#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <cub/block/block_scan.cuh>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <vector>
#define PRIO_BLOCK_SIZE 256
#define PRIO_NUM_WARPS 8
#define PRIO_WARP_SIZE 32
#define PRIO_FULL_MASK 0xffffffff
${kernel('compute_prio_normalize')}
${kernel('build_cdf')}
${sample}
${kernel('multinomial_sample').replace('void multinomial_sample(', 'void multinomial_sample_real(')}
${kernel('compute_prio_imp_weights')}
${kernel('puf_epoch_sample')}
int main() {
    constexpr int B=4096, primary=3072;
    float host[B]; for(int i=0;i<B;i++)host[i]=i<primary?1.0f:0.0f;
    float *weights,*cdf,*importance; int* selected; int64_t* offset;
    cudaMalloc(&weights,B*sizeof(float));cudaMalloc(&cdf,B*sizeof(float));
    cudaMalloc(&importance,sizeof(float));cudaMalloc(&selected,sizeof(int));
    cudaMalloc(&offset,sizeof(int64_t));cudaMemset(offset,0,sizeof(int64_t));
    cudaMemcpy(weights,host,sizeof(host),cudaMemcpyHostToDevice);
    compute_prio_normalize<<<1,256>>>(weights,B);
    build_cdf<<<1,256>>>(cdf,weights,B);
    multinomial_sample<<<1,256>>>(selected,cdf,B,1,73,offset);
    compute_prio_imp_weights<<<1,256>>>(selected,weights,importance,B,0.0f,1);
    assert(cudaDeviceSynchronize()==cudaSuccess);
    int row;float weight,last,prob,primary_end;
    cudaMemcpy(&row,selected,sizeof(row),cudaMemcpyDeviceToHost);
    cudaMemcpy(&weight,importance,sizeof(weight),cudaMemcpyDeviceToHost);
    cudaMemcpy(&last,cdf+B-1,sizeof(last),cudaMemcpyDeviceToHost);
    cudaMemcpy(&prob,weights+row,sizeof(prob),cudaMemcpyDeviceToHost);
    cudaMemcpy(&primary_end,cdf+primary-1,sizeof(primary_end),cudaMemcpyDeviceToHost);
    printf("forced_draw=1 cdf_last=%.9g row=%d frozen=%d importance=%g finite=%d\\n",
        last,row,row>=primary,weight,(int)std::isfinite(weight));
    printf("selected_probability=%.9g primary_end_cdf=%.9g\\n",prob,primary_end);
    assert(row>=primary && !std::isfinite(weight));
    // Replay the actual seeded training-row RNG sequence, without training.
    constexpr int draws=4000000;
    int* all;cudaMalloc(&all,draws*sizeof(int));
    multinomial_sample_real<<<(draws+255)/256,256>>>(all,cdf,B,draws,73,offset);
    std::vector<int> rows(draws);
    assert(cudaMemcpy(rows.data(),all,draws*sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess);
    for(int i=0;i<draws;i++) if(rows[i]>=primary) {
        printf("first_real_frozen_draw=%d row=%d epoch_1based=%d rollout_steps=%lld\\n",
            i,rows[i],i/(64*48)+1,(long long)(i/(64*48)+1)*524288);
        break;
    }
    cudaFree(all);
    // The Pokemon configuration workaround uses the existing learner-only path.
    for(int buffers : {1,2}) {
        int count=primary*buffers;
        std::vector<int> permutation(count), picked(count);
        std::vector<float> importance_host(count);
        for(int i=0;i<count;i++) permutation[i]=count-1-i;
        int *perm,*out;float* iw;
        cudaMalloc(&perm,count*sizeof(int));cudaMalloc(&out,count*sizeof(int));
        cudaMalloc(&iw,count*sizeof(float));
        cudaMemcpy(perm,permutation.data(),count*sizeof(int),cudaMemcpyHostToDevice);
        puf_epoch_sample<<<(count+255)/256,256>>>(out,iw,perm,0,count,B,primary);
        assert(cudaMemcpy(picked.data(),out,count*sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess);
        cudaMemcpy(importance_host.data(),iw,count*sizeof(float),cudaMemcpyDeviceToHost);
        for(int i=0;i<count;i++) {
            int logical=permutation[i];
            assert(picked[i]==(logical/primary)*B+logical%primary);
            assert(picked[i]%B<primary && importance_host[i]==1.0f);
        }
        cudaFree(perm);cudaFree(out);cudaFree(iw);
    }
    puts("Learner-only epoch sampling passed for one and two buffers");
    cudaFree(weights);cudaFree(cdf);cudaFree(importance);cudaFree(selected);cudaFree(offset);
}
`);
