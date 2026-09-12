// Exact CPU/GPU architecture, branch/border coverage, and parameter gradients.
#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <curand.h>
#include <curand_kernel.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#ifndef RETRO_TEST_BF16
#define PRECISION_FLOAT
#endif
#ifndef PUFFER_RETRO_CNN
#define PUFFER_RETRO_CNN
#endif
#define ENV_HEADER "ocean/retro/retro.h"
#include "../src/pufferl_preamble.h"
#include "../src/algo.cu"
#include "../ocean/retro/retro_policy_cpu.h"

static void ck(cudaError_t e) {
    if(e!=cudaSuccess) { fprintf(stderr,"CUDA: %s\n",cudaGetErrorString(e)); exit(1); }
}
static void require(bool ok,const char* text) {
    if(!ok) { fprintf(stderr,"FAIL: %s\n",text); exit(1); }
}
static std::vector<float> host(PrecisionTensor t) {
    std::vector<precision_t> data(numel(t.shape));
    ck(cudaMemcpy(data.data(),t.data,data.size()*sizeof(precision_t),cudaMemcpyDeviceToHost));
    std::vector<float> out(data.size());
    for(size_t i=0;i<data.size();i++) out[i]=to_float(data[i]);
    return out;
}
static void put(PrecisionTensor t,const std::vector<float>& data) {
    require(data.size()==numel(t.shape),"copy shape");
    std::vector<precision_t> packed(data.size());
    for(size_t i=0;i<data.size();i++) packed[i]=from_float(data[i]);
    ck(cudaMemcpy(t.data,packed.data(),packed.size()*sizeof(precision_t),cudaMemcpyHostToDevice));
}
static void near(const float* a,const float* b,int n,float atol,float rtol,const char* label) {
    float worst=0;
    for(int i=0;i<n;i++) {
        float err=fabsf(a[i]-b[i]); worst=std::max(worst,err);
        if(!std::isfinite(a[i])||!std::isfinite(b[i])||err>atol+rtol*std::max(fabsf(a[i]),fabsf(b[i]))) {
            fprintf(stderr,"%s[%d]: CPU=%g GPU=%g error=%g\n",label,i,a[i],b[i],err); exit(1);
        }
    }
    printf("PASS %s max_error=%g\n",label,worst);
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr);
    cublas_init_handle();
    const int B=3,H=128,L=2;
    Policy policy=build_policy("retro",RETRO_POLICY_INPUTS,H,L,64,false,4);
    Allocator params,acts,grads,rollout_alloc;
    auto weights=policy_weights_create(&policy,&params); ck(alloc_create(&params));
    ulong seed=73; policy_init_weights(&policy,weights,&seed,0); ck(cudaDeviceSynchronize());
    auto* ew=(RetroEncoderWeights*)weights.encoder;
    // Avoid exact ReLU kinks for centered numerical derivatives: zero-bias
    // convolutions on an all-zero preceding patch sit precisely at zero.
    // This changes test parameters only, not production initialization.
    for(int l=0;l<5;l++) {
        std::vector<float> bias(numel(ew->b[l].shape));
        for(int i=0;i<(int)bias.size();i++) bias[i]=0.13f+0.007f*(i%11);
        put(ew->b[l],bias);
    }
    RetroEncoderActivations a={};
    policy.encoder.reg_train(weights.encoder,&a,&acts,&grads,B);
    PrecisionTensor input={.shape={B,RETRO_POLICY_INPUTS}},grad={.shape={B,H}};
    alloc_register(&acts,&input); alloc_register(&acts,&grad);
    ck(alloc_create(&acts)); ck(alloc_create(&grads));
    PrecisionTensor flat={.data=(precision_t*)params.mem,.shape={params.total_bytes/(long)sizeof(precision_t)}};
    auto serialized=host(flat);
    require(serialized.size()==retro_policy_weights(H,L),"serialized CNN parameter count");
    require(params.total_bytes==grads.total_bytes+(65*H+L*3*H*H)*(long)sizeof(precision_t),"encoder gradient registration order/size");
    std::vector<RetroPolicy*> cpu(B);
    for(int b=0;b<B;b++) {
        Weights cursor={serialized.data(),(int)serialized.size(),0};
        cpu[b]=make_retro_policy(&cursor,H,L);
        require(cursor.idx==serialized.size(),"CPU consumed complete native layout");
    }
    std::vector<float> obs(B*RETRO_POLICY_INPUTS),g(B*H);
    for(int i=0;i<(int)obs.size();i++) obs[i]=0.5f+0.45f*sinf(i*0.139f);
    for(int i=0;i<B*H;i++) g[i]=0.03f*sinf(i*0.271f);
    put(input,obs); obs=host(input); put(grad,g);
    auto gpu=host(policy.encoder.forward(weights.encoder,&a,input,0));
    float atol=USE_BF16?0.004f:3e-6f,rtol=USE_BF16?0.04f:1e-4f;
    for(int b=0;b<B;b++) {
        auto* ref=retro_policy_encode(cpu[b],obs.data()+b*RETRO_POLICY_INPUTS);
        near(ref,gpu.data()+b*H,H,atol,rtol,"CNN/RAM/fusion encoder");
        for(int l=0;l<3;l++) {
            auto image=host(a.image[l]); int n=cpu[b]->image[l].size();
            near(cpu[b]->image[l].data(),image.data()+b*n,n,atol,rtol,"convolution feature map");
        }
    }
    // Build the dependency mask backwards: no image border was discarded by
    // striding/padding, including the bottom-right input pixel.
    std::vector<int> live(7*8,1);
    for(int l=2;l>=0;l--) {
        auto c=RETRO_CONVS[l]; std::vector<int> before(c.ih*c.iw,0);
        for(int y=0;y<c.oh;y++) for(int x=0;x<c.ow;x++) if(live[y*c.ow+x])
            for(int ky=0;ky<c.k;ky++) for(int kx=0;kx<c.k;kx++) {
                int iy=y*c.stride-c.pad+ky,ix=x*c.stride-c.pad+kx;
                if(iy>=0&&iy<c.ih&&ix>=0&&ix<c.iw) before[iy*c.iw+ix]=1;
            }
        live=before;
    }
    require(std::all_of(live.begin(),live.end(),[](int x){return x==1;}),"CNN covers every image pixel");
    printf("PASS complete image coverage, including all four edges\n");
    // Both branches must influence the actual encoder, not just exist in its
    // parameter list. Change RAM alone, then pixels alone.
    for(int branch=0;branch<2;branch++) {
        auto changed=obs;
        for(int b=0;b<B;b++) for(int i=0;i<RETRO_POLICY_INPUTS;i++)
            if((i<RETRO_RAM_INPUTS)==(branch==0)) changed[b*RETRO_POLICY_INPUTS+i]=0;
        put(input,changed); auto other=host(policy.encoder.forward(weights.encoder,&a,input,0));
        float diff=0; for(int i=0;i<B*H;i++) diff+=fabsf(other[i]-gpu[i]);
        require(diff>0.001f,"disconnected observation branch");
    }
    put(input,obs); policy.encoder.forward(weights.encoder,&a,input,0); put(grad,g);
    policy.encoder.backward(weights.encoder,&a,grad,0); ck(cudaDeviceSynchronize());
    for(int l=0;l<5;l++) {
        auto dw=host(a.dw[l]); auto db=host(a.db[l]);
        double norm=0; for(float x:dw) { require(std::isfinite(x),"finite weight gradients"); norm+=fabs(x); }
        for(float x:db) require(std::isfinite(x),"finite bias gradients");
        require(norm>1e-7,"gradient reaches every branch/layer");
    }
#ifndef RETRO_TEST_BF16
    // Centered finite differences for every convolution, RAM and fusion tensor.
    auto loss=[&]() {
        auto out=host(policy.encoder.forward(weights.encoder,&a,input,0));
        double v=0; for(int i=0;i<B*H;i++) v+=out[i]*g[i]; return v;
    };
    for(int l=0;l<5;l++) for(int bias=0;bias<2;bias++) {
        PrecisionTensor t=bias?ew->b[l]:ew->w[l];
        auto values=host(t),analytic=host(bias?a.db[l]:a.dw[l]);
        // Include the largest derivative, plus distributed tensor positions.
        int largest=0; for(int i=1;i<(int)analytic.size();i++) if(fabsf(analytic[i])>fabsf(analytic[largest])) largest=i;
        for(int idx: {0,(int)values.size()/2,(int)values.size()-1,largest}) {
            // Small enough not to cross many ReLU kinks in the first layer.
            float original=values[idx],eps=0.0001f;
            values[idx]=original+eps; put(t,values); double plus=loss();
            values[idx]=original-eps; put(t,values); double minus=loss();
            values[idx]=original; put(t,values);
            float numeric=(plus-minus)/(2*eps),actual=analytic[idx];
            if(fabsf(numeric-actual)>2e-4f+0.03f*std::max(fabsf(actual),fabsf(numeric))) {
                fprintf(stderr,"gradient layer=%d bias=%d index=%d analytical=%g numeric=%g\n",l,bias,idx,actual,numeric); return 1;
            }
        }
    }
    printf("PASS 40 numerical weight/bias gradients across all five encoder layers\n");
#endif
    auto rollout=policy_reg_rollout(&policy,weights,&rollout_alloc,B);
    PrecisionTensor state={.shape={L,B,H}}; alloc_register(&rollout_alloc,&state);
    ck(alloc_create(&rollout_alloc));
    for(int step=0;step<16;step++) {
        if(step==7) {
            ck(cudaMemset(state.data,0,L*B*H*sizeof(precision_t)));
            for(auto* p:cpu) memset(p->mingru->state,0,L*H*sizeof(float));
        }
        for(int i=0;i<(int)obs.size();i++) obs[i]=0.5f+0.45f*sinf(i*0.139f+step*0.1f);
        put(input,obs); obs=host(input);
        auto out=host(policy_forward(&policy,weights,rollout,input,state,0));
        for(int b=0;b<B;b++) {
            const float* ref=retro_policy_logits(cpu[b],obs.data()+b*RETRO_POLICY_INPUTS);
            near(ref,out.data()+b*65,65,USE_BF16?0.008f:1e-5f,USE_BF16?0.08f:1e-4f,"recurrent logits/value + reset");
        }
    }
    if(argc>1) {
        FILE* f=fopen(argv[1],"wb"); require(f,"open test checkpoint");
        require(fwrite(serialized.data(),sizeof(float),serialized.size(),f)==serialized.size(),"write test checkpoint");
        require(fclose(f)==0,"close test checkpoint");
    }
    for(auto* p:cpu) free_retro_policy(p);
    printf("PASS Retro CNN (%s): %zu parameters, CPU/GPU encoder and recurrent policy agree\n",USE_BF16?"bf16":"float32",serialized.size());
}
