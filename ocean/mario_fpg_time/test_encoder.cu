#include <cuda_runtime.h>
#ifndef FPT_TEST_BF16
#define PRECISION_FLOAT
#endif
#define SMB_HEADLESS
#define ENV_HEADER "../ocean/mario_fpg_time/mario_fpg_time.cu"
#define PUFFER_MARIO_FPG_TIME
#define PUFFER_ENV_NAME "mario_fpg_time"
#include "../../src/pufferl.cu"
#include "encoder_cpu.h"
#include "policy.h"
#include <fstream>
#include <algorithm>

static void require(bool ok,const char* message) {if(!ok){fprintf(stderr,"encoder test: %s\n",message);exit(1);}}
static void ck(cudaError_t error) {require(error==cudaSuccess,cudaGetErrorString(error));}
static std::vector<float> host(Prec t) {
    std::vector<precision_t> a(numel(t.shape));ck(cudaMemcpy(a.data(),t.data,a.size()*sizeof(precision_t),cudaMemcpyDeviceToHost));
    std::vector<float> out(a.size());for(size_t i=0;i<a.size();i++)out[i]=to_float(a[i]);return out;
}
static void put(Prec t,const std::vector<float>& values) {
    require(values.size()==(size_t)numel(t.shape),"tensor shape");std::vector<precision_t> packed(values.size());
    for(size_t i=0;i<values.size();i++)packed[i]=from_float(values[i]);
    ck(cudaMemcpy(t.data,packed.data(),packed.size()*sizeof(precision_t),cudaMemcpyHostToDevice));
}
static void near(const float* a,const float* b,size_t n,float atol,float rtol,const char* label) {
    for(size_t i=0;i<n;i++)if(!std::isfinite(a[i])||!std::isfinite(b[i])||fabsf(a[i]-b[i])>atol+rtol*fmaxf(fabsf(a[i]),fabsf(b[i]))) {
        fprintf(stderr,"%s[%zu] reference=%g actual=%g\n",label,i,a[i],b[i]);exit(1);
    }
}
static std::vector<float> observations(int B) {
    SmbBank bank("build/mario_fpg_time/curriculum/bank.bin");std::vector<float> out(B*FPT_OBS);
    for(int b=0;b<B;b++) {
        FptObservationHistory h={};fpt_observe(&bank.entries[(b*137)%bank.entries.size()].scene.initial,
            bank.worlds.data(),&h,out.data()+b*FPT_OBS);
        float* e=out.data()+b*FPT_OBS+FPT_ENTITY_OFFSET;
        std::fill(e,e+FPT_ENTITY_COUNT*FPT_ENTITY_FEATURES,0);
        int count=b%3==0?0:b%3==1?3:FPT_ENTITY_COUNT;
        for(int k=0;k<count;k++) {
            e[k*FPT_ENTITY_FEATURES]=(float)(1+(k*13)%71);
            for(int f=1;f<FPT_ENTITY_FEATURES;f++)e[k*FPT_ENTITY_FEATURES+f]=0.4f*sinf((k+1)*0.17f+f*0.7f);
        }
    }
    return out;
}
static void randomize(Prec t,int seed,float scale,float center=0) {
    std::vector<float> data(numel(t.shape));uint32_t rng=(uint32_t)seed;
    for(float& x:data){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;x=center+scale*((rng>>8)*(1.0f/8388608)-1);}
    put(t,data);
}
int main(int argc,char** argv) {
    bool bench=argc>1&&!strcmp(argv[1],"--bench");int B=bench?4096:3,H=64,L=2;
    cublas_init_handle();Allocator params={},acts={},grads={},rollout_alloc={};
    Arch arch=build_arch(FPT_OBS,H,L,64,false,32);Weights weights=weights_create(&arch,&params);alloc_create(&params);
    ulong seed=137;weights_init(&arch,weights,&seed,0);
    auto* ew=(FptEncoderWeights*)weights.encoder;
    for(int l=0;l<FPT_ENCODER_LAYERS;l++){randomize(ew->w[l],73+l,0.06f);randomize(ew->b[l],100+l,0.005f,0.035f);}
    FptEncoderActivations a={};arch.encoder.reg_train(weights.encoder,&a,&acts,&grads,B);
    Prec input={.shape={B,FPT_OBS}},grad={.shape={B,H}};alloc_register(&acts,&input);alloc_register(&acts,&grad);
    alloc_create(&acts);alloc_create(&grads);
    Prec flat={.data=(precision_t*)params.mem,.shape={params.total_elems}};
    require(params.total_elems==fpt_policy_weight_count(H,L),"CPU/CUDA parameter layout mismatch");
    auto obs=observations(B);put(input,obs);obs=host(input);std::vector<float> g(B*H);
    for(int i=0;i<B*H;i++)g[i]=0.1f*cosf(i*0.173f);put(grad,g);
    auto actual=host(arch.encoder.forward(weights.encoder,&a,input,0));
    if(bench) {
        cudaEvent_t start,end;ck(cudaEventCreate(&start));ck(cudaEventCreate(&end));
        ck(cudaEventRecord(start));for(int i=0;i<16;i++)arch.encoder.forward(weights.encoder,&a,input,0);
        ck(cudaEventRecord(end));ck(cudaEventSynchronize(end));float ms;ck(cudaEventElapsedTime(&ms,start,end));
        printf("encoder batch=%d forward_ms=%.3f agent_steps_per_second=%.0f training_activation_MiB=%.1f precision=%s\n",
            B,ms/16,B*16000/ms,acts.total_bytes/1048576.0,USE_BF16?"bf16":"fp32");
        for(int l=0;l<2;l++) {
            ck(cudaEventRecord(start));for(int i=0;i<16;i++) {
                if(l==0)fpt_conv_forward_fused<0>(a.image[0].data,a.terrain.data,ew->w[0].data,ew->b[0].data,B,0);
                else fpt_conv_forward_fused<1>(a.image[1].data,a.image[0].data,ew->w[1].data,ew->b[1].data,B,0);
            }
            ck(cudaEventRecord(end));ck(cudaEventSynchronize(end));ck(cudaEventElapsedTime(&ms,start,end));printf("conv%d batch_ms=%.3f\n",l,ms/16);
        }
        return 0;
    }
    auto serialized=host(flat);FptCpuEncoder cpu={};fpt_cpu_encoder_bind(&cpu,serialized.data(),H);
    float atol=USE_BF16?0.008f:3e-6f,rtol=USE_BF16?0.04f:0.0002f;
    for(int b=0;b<B;b++) {
        fpt_cpu_encode(&cpu,obs.data()+b*FPT_OBS);near(cpu.out,actual.data()+b*H,H,atol,rtol,"CPU/CUDA encoder");
        for(int l=0;l<2;l++){auto x=host(a.image[l]);near(cpu.image[l],x.data()+b*FPT_CNN_FLAT,FPT_CNN_FLAT,atol,rtol,"terrain CNN");}
    }
    puts("PASS CPU/CUDA semantic encoder, both CNN layers, empty/partial/full entity sets");

    std::vector<int> covered(FPT_MAP_CELLS,1);
    for(int l=1;l>=0;l--) {
        auto c=FPT_CONVS[l];std::vector<int> input_coverage(c.ih*c.iw,0);
        for(int y=0;y<c.oh;y++)for(int x=0;x<c.ow;x++)if(covered[y*c.ow+x])
            for(int ky=0;ky<c.k;ky++)for(int kx=0;kx<c.k;kx++) {
                int iy=y*c.stride-c.pad+ky,ix=x*c.stride-c.pad+kx;
                if(iy>=0&&iy<c.ih&&ix>=0&&ix<c.iw)input_coverage[iy*c.iw+ix]=1;
            }
        covered=input_coverage;
    }
    require(std::all_of(covered.begin(),covered.end(),[](int n){return n!=0;}),"CNN discards visible terrain cells");
    puts("PASS CNN receptive fields cover every gameplay tile, including all partial borders");

    // Every branch and each visible corner must influence the actual network.
    for(int branch=0;branch<3;branch++) {
        auto changed=obs;
        for(int b=0;b<B;b++) {
            float* o=changed.data()+b*FPT_OBS;
            if(branch==0)std::fill(o,o+FPT_PLAYER_FEATURES,0);
            if(branch==1)for(int cell:{0,16,12*17,12*17+16})o[FPT_TERRAIN_OFFSET+cell]=0xc0;
            if(branch==2)std::fill(o+FPT_ENTITY_OFFSET,o+FPT_OBS,0);
        }
        put(input,changed);auto x=host(arch.encoder.forward(weights.encoder,&a,input,0));float diff=0;
        for(size_t i=0;i<x.size();i++)diff+=fabsf(x[i]-actual[i]);require(diff>1e-5,"disconnected input branch or terrain corners");
    }
    auto permuted=obs;
    for(int b=0;b<B;b++)for(int k=0;k<FPT_ENTITY_COUNT;k++)for(int f=0;f<FPT_ENTITY_FEATURES;f++)
        permuted[b*FPT_OBS+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES+f]=
            obs[b*FPT_OBS+FPT_ENTITY_OFFSET+(FPT_ENTITY_COUNT-1-k)*FPT_ENTITY_FEATURES+f];
    put(input,permuted);auto reordered=host(arch.encoder.forward(weights.encoder,&a,input,0));
    near(actual.data(),reordered.data(),actual.size(),USE_BF16?0.002f:2e-6f,0.0001f,"entity permutation invariance");
    puts("PASS all three branches influence policy; entity slot order does not");

    put(input,obs);arch.encoder.forward(weights.encoder,&a,input,0);put(grad,g);
    arch.encoder.backward(weights.encoder,&a,grad,0);ck(cudaDeviceSynchronize());
    for(int l=0;l<FPT_ENCODER_LAYERS;l++) {
        auto dw=host(a.dw[l]);double norm=0;for(float x:dw){require(std::isfinite(x),"finite gradients");norm+=fabs(x);}
        require(norm>1e-8,"gradient does not reach a branch");
    }
#ifndef FPT_TEST_BF16
    auto loss=[&](){auto x=host(arch.encoder.forward(weights.encoder,&a,input,0));double sum=0;for(int i=0;i<B*H;i++)sum+=(double)x[i]*g[i];return sum;};
    int checks=0;
    for(int l=0;l<FPT_ENCODER_LAYERS;l++)for(int bias=0;bias<2;bias++) {
        Prec t=bias?ew->b[l]:ew->w[l];auto values=host(t),analytic=host(bias?a.db[l]:a.dw[l]);
        int largest=0;for(size_t i=1;i<analytic.size();i++)if(fabsf(analytic[i])>fabsf(analytic[largest]))largest=i;
        for(int idx:{0,(int)values.size()/2,(int)values.size()-1,largest}) {
            float original=values[idx],eps=0.0002f;
            values[idx]=original+eps;put(t,values);double plus=loss();
            values[idx]=original-eps;put(t,values);double minus=loss();values[idx]=original;put(t,values);
            double numeric=(plus-minus)/(2*eps),delta=fabs(numeric-analytic[idx]);
            if(delta>0.00025+0.03*std::max(fabs(numeric),(double)fabsf(analytic[idx]))) {
                fprintf(stderr,"gradient layer=%d bias=%d index=%d analytic=%g numeric=%g\n",l,bias,idx,analytic[idx],numeric);return 1;
            }
            checks++;
        }
    }
    printf("PASS %d finite-difference weight/bias gradients across all seven encoder layers\n",checks);
#endif
    // A captured train graph must also see fresh minibatch data and gradients.
    cudaStream_t stream;ck(cudaStreamCreate(&stream));cudaGraph_t graph;cudaGraphExec_t executable;
    put(input,obs);put(grad,g);arch.encoder.forward(weights.encoder,&a,input,stream);arch.encoder.backward(weights.encoder,&a,grad,stream);ck(cudaStreamSynchronize(stream));
    ck(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
    arch.encoder.forward(weights.encoder,&a,input,stream);arch.encoder.backward(weights.encoder,&a,grad,stream);
    ck(cudaStreamEndCapture(stream,&graph));ck(cudaGraphInstantiate(&executable,graph,0,0,0));
    for(int i=0;i<2;i++){put(input,i?permuted:obs);put(grad,g);ck(cudaGraphLaunch(executable,stream));ck(cudaStreamSynchronize(stream));}
    auto captured=host(a.out);near(actual.data(),captured.data(),actual.size(),USE_BF16?0.002f:2e-6f,0.0001f,"captured forward/backward");
    puts("PASS CUDA graph replay with changed entity order");

    // Exercise the actual serialized policy loader, recurrent carry and reset.
    const char* path=USE_BF16?"build/mario_fpg_time/encoder_test_bf16.bin":"build/mario_fpg_time/encoder_test_fp32.bin";
    {std::ofstream f(path,std::ios::binary);f.write((const char*)serialized.data(),serialized.size()*sizeof(float));}
    auto rollout=arch_reg_rollout(&arch,weights,&rollout_alloc,B);Prec state={.shape={L,B,H}};
    alloc_register(&rollout_alloc,&state);alloc_create(&rollout_alloc);void* policies[3];
    for(int b=0;b<B;b++){policies[b]=fpt_policy_load(path,H,L);require(policies[b],"CPU policy load");}
    require(!fpt_policy_load("checkpoints/mario_fpg_time/1791475084903/0000000268435456.bin",H,L),"old RAM checkpoint accepted");
    put(input,obs);
    for(int step=0;step<8;step++) {
        if(step==4){ck(cudaMemset(state.data,0,L*B*H*sizeof(precision_t)));for(void* p:policies)fpt_policy_reset(p);}
        auto logits=host(arch_forward(&arch,weights,rollout,input,state,0));
        for(int b=0;b<B;b++)near(fpt_policy_logits(policies[b],obs.data()+b*FPT_OBS),logits.data()+b*65,65,
            USE_BF16?0.02f:1e-5f,USE_BF16?0.05f:0.0002f,"recurrent policy logits/value");
    }
    for(void* p:policies)fpt_policy_free(p);
    puts("PASS serialized CPU/CUDA policy, recurrent carry/reset, incompatible RAM checkpoint rejection");
    printf("encoder parameters=%d total_parameters=%d observations=%d precision=%s\n",
        fpt_encoder_weight_count(H),fpt_policy_weight_count(H,L),FPT_OBS,USE_BF16?"bf16":"fp32");return 0;
}
