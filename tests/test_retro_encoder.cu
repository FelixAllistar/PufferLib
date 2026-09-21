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
#include "../ocean/retro/tests/observation_reference.h"

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
static void random_tensor(PrecisionTensor t,unsigned seed,float scale,float offset=0) {
    std::vector<float> values(numel(t.shape));
    for(size_t i=0;i<values.size();i++) {
        unsigned x=(unsigned)i+seed;
        x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16;
        values[i]=offset+scale*((x>>8)*0x1.0p-23f-1);
    }
    put(t,values);
}
template<int Layer> static void convolution_parity(int B) {
    constexpr auto c=RETRO_CONVS[Layer];
    constexpr int K=c.k*c.k*c.ic,N=c.oh*c.ow;
    int splits=(B*N+RETRO_DW_SPLIT_ROWS-1)/RETRO_DW_SPLIT_ROWS;
    Allocator alloc;
    PrecisionTensor input={.shape={B,Layer?c.ih*c.iw*c.ic:RETRO_POLICY_INPUTS}};
    PrecisionTensor columns={.shape={B*N,K}},old_out={.shape={B*N,c.oc}},new_out=old_out;
    PrecisionTensor grad=old_out,old_dw={.shape={c.oc,K}},new_dw=old_dw;
    PrecisionTensor old_db={.shape={c.oc}},new_db=old_db;
    PrecisionTensor old_dx={.shape={B,c.ih*c.iw*c.ic}},new_dx=old_dx;
    RetroEncoderWeights ew={}; ew.w[Layer]=old_dw; ew.b[Layer]=old_db;
    FloatTensor partial={.shape={splits*c.oc*(K+1)}};
    for(auto* t:{&input,&columns,&old_out,&new_out,&grad,&old_dw,&new_dw,
                &old_db,&new_db,&ew.w[Layer],&ew.b[Layer]}) alloc_register(&alloc,t);
    if(Layer) { alloc_register(&alloc,&old_dx); alloc_register(&alloc,&new_dx); }
    alloc_register(&alloc,&partial); ck(alloc_create(&alloc));
    random_tensor(input,91,0.5f,0.5f); random_tensor(grad,712,0.03f);
    random_tensor(ew.w[Layer],432,0.04f); random_tensor(ew.b[Layer],107,0.03f);
    retro_im2col<<<grid_size(B*N*K),BLOCK_SIZE>>>(columns.data,input.data,c,B,
        Layer?c.ih*c.iw*c.ic:RETRO_POLICY_INPUTS,Layer?0:RETRO_RAM_INPUTS);
    puf_mm(&columns,&ew.w[Layer],&old_out,0);
    retro_bias_relu<<<grid_size(B*N*c.oc),BLOCK_SIZE>>>(old_out.data,ew.b[Layer].data,B*N*c.oc,c.oc);
    retro_conv_forward_fused<Layer>(new_out.data,input.data,ew.w[Layer].data,ew.b[Layer].data,B,0);
    puf_mm_tn(&grad,&columns,&old_dw,0);
    retro_bias_grad<<<c.oc,256>>>(old_db.data,grad.data,B*N,c.oc);
    if(Layer) {
        puf_mm_nn(&grad,&ew.w[Layer],&columns,0);
        retro_col2im<<<grid_size(B*c.ih*c.iw*c.ic),BLOCK_SIZE>>>(old_dx.data,columns.data,c,B);
    }
    RetroEncoderActivations a={}; a.image_grad[Layer]=grad;
    if constexpr (Layer>0) { a.image[Layer-1]=input; a.image_grad[Layer-1]=new_dx; }
    else a.saved_input=input;
    a.dw[Layer]=new_dw; a.db[Layer]=new_db; a.weight_partials=partial;
    retro_conv_backward_fused<Layer>(&ew,&a,B,0); ck(cudaDeviceSynchronize());
    auto compare=[&](PrecisionTensor ref,PrecisionTensor actual,const char* part,float atol,float rtol) {
        auto x=host(ref),y=host(actual); char label[128];
        snprintf(label,sizeof(label),"conv%d B=%d %s old/fused",Layer+1,B,part);
        near(x.data(),y.data(),x.size(),atol,rtol,label);
    };
    compare(old_out,new_out,"forward",USE_BF16?0.0005f:2e-6f,USE_BF16?0.01f:0.0002f);
    compare(old_dw,new_dw,"dW",USE_BF16?0.002f:0.0002f,USE_BF16?0.02f:0.0005f);
    compare(old_db,new_db,"db",USE_BF16?0.002f:0.0002f,USE_BF16?0.02f:0.0005f);
    if(Layer) compare(old_dx,new_dx,"dX",USE_BF16?0.0001f:2e-6f,USE_BF16?0.02f:0.0002f);
    ck(cudaFree(alloc.mem)); free(alloc.regs);
}

// Original CUDA encoder, retained only in this test to compare a learned
// checkpoint through the complete recurrent policy on identical ROM frames.
static PrecisionTensor reference_encoder_forward(void* w,void* activations,
        PrecisionTensor input,cudaStream_t stream) {
    auto* ew=(RetroEncoderWeights*)w; auto* a=(RetroEncoderActivations*)activations;
    int B=input.shape[0];
    for(int l=0;l<3;l++) {
        auto c=RETRO_CONVS[l]; int N=c.oh*c.ow,K=c.k*c.k*c.ic;
        auto col=retro_matrix(a->columns,B*N,K),out=retro_matrix(a->image[l],B*N,c.oc);
        retro_im2col<<<grid_size(B*N*K),BLOCK_SIZE,0,stream>>>(col.data,
            l?a->image[l-1].data:input.data,c,B,
            l?c.ih*c.iw*c.ic:RETRO_POLICY_INPUTS,l?0:RETRO_RAM_INPUTS);
        puf_mm(&col,&ew->w[l],&out,stream);
        retro_bias_relu<<<grid_size(B*N*c.oc),BLOCK_SIZE,0,stream>>>(out.data,ew->b[l].data,B*N*c.oc,c.oc);
    }
    retro_ram_gather<<<grid_size(B*RETRO_RAM_INPUTS),BLOCK_SIZE,0,stream>>>(a->ram_input.data,input.data,B);
    puf_mm(&a->ram_input,&ew->w[3],&a->ram,stream);
    retro_bias_relu<<<grid_size(B*RETRO_RAM_HIDDEN),BLOCK_SIZE,0,stream>>>(a->ram.data,ew->b[3].data,B*RETRO_RAM_HIDDEN,RETRO_RAM_HIDDEN);
    retro_join<<<grid_size(B*RETRO_FUSED_FEATURES),BLOCK_SIZE,0,stream>>>(a->joined.data,a->image[2].data,a->ram.data,B);
    puf_mm(&a->joined,&ew->w[4],&a->out,stream);
    retro_bias_relu<<<grid_size(B*ew->hidden),BLOCK_SIZE,0,stream>>>(a->out.data,ew->b[4].data,B*ew->hidden,ew->hidden);
    return a->out;
}
// Compare borrowed input with an explicitly owned copy, including pointer
// rebinding and graph replay after overwriting the next minibatch in place.
static void borrowed_input_parity(Policy& policy,PolicyWeights& weights,int B) {
    Allocator acts,grads,owned_acts,owned_grads;
    RetroEncoderActivations borrowed={},owned={};
    policy.encoder.reg_train(weights.encoder,&borrowed,&acts,&grads,B);
    policy.encoder.reg_train(weights.encoder,&owned,&owned_acts,&owned_grads,B);
    PrecisionTensor inputs[2]={};
    for(auto& input:inputs) { input={.shape={B,RETRO_POLICY_INPUTS}}; alloc_register(&acts,&input); }
    PrecisionTensor copy={.shape={B,RETRO_POLICY_INPUTS}};
    PrecisionTensor seed_grad={.shape={B,policy.encoder.out_dim}},grad=seed_grad,owned_grad=seed_grad;
    alloc_register(&acts,&seed_grad); alloc_register(&acts,&grad);
    alloc_register(&owned_acts,&copy); alloc_register(&owned_acts,&owned_grad);
    for(auto* alloc:{&acts,&grads,&owned_acts,&owned_grads}) ck(alloc_create(alloc));
    require(!borrowed.saved_input.data&&!owned.saved_input.data,"encoder still allocates an observation copy");
    random_tensor(seed_grad,17,0.03f);
    cudaStream_t stream; ck(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    auto run=[&](PrecisionTensor input) {
        puf_copy(&copy,&input,stream);
        policy.encoder.forward(weights.encoder,&owned,copy,stream);
        puf_copy(&owned_grad,&seed_grad,stream);
        policy.encoder.backward(weights.encoder,&owned,owned_grad,stream);
        policy.encoder.forward(weights.encoder,&borrowed,input,stream);
        require(borrowed.saved_input.data==input.data,"encoder did not rebind borrowed input");
        puf_copy(&grad,&seed_grad,stream);
        policy.encoder.backward(weights.encoder,&borrowed,grad,stream);
    };
    auto compare=[&](PrecisionTensor input,const std::vector<float>& before) {
        ck(cudaStreamSynchronize(stream));
        require(host(borrowed.out)==host(owned.out),"borrowed/copied outputs differ");
        for(int l=0;l<5;l++) {
            require(host(borrowed.dw[l])==host(owned.dw[l]),"borrowed/copied weight gradients differ");
            require(host(borrowed.db[l])==host(owned.db[l]),"borrowed/copied bias gradients differ");
        }
        require(host(input)==before,"encoder modified borrowed observations");
    };
    for(int pass=0;pass<2;pass++) {
        random_tensor(inputs[pass],73+pass,0.5f,0.5f);
        auto before=host(inputs[pass]); run(inputs[pass]); compare(inputs[pass],before);
    }
    cudaGraph_t graph; cudaGraphExec_t executable;
    ck(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
    run(inputs[0]);
    ck(cudaStreamEndCapture(stream,&graph));
    ck(cudaGraphInstantiate(&executable,graph,nullptr,nullptr,0));
    for(int pass=0;pass<3;pass++) {
        random_tensor(inputs[0],401+pass,0.5f,0.5f);
        auto before=host(inputs[0]);
        ck(cudaGraphLaunch(executable,stream)); compare(inputs[0],before);
    }
    ck(cudaGraphExecDestroy(executable)); ck(cudaGraphDestroy(graph)); ck(cudaStreamDestroy(stream));
    for(auto* alloc:{&acts,&grads,&owned_acts,&owned_grads}) { ck(cudaFree(alloc->mem)); free(alloc->regs); }
    printf("PASS borrowed/copied encoder inputs: B=%d, exact outputs/all gradients, rebinding + 3 graph replays\n",B);
}
static void checkpoint_parity(const char* path,Policy policy,PolicyWeights weights,PrecisionTensor flat) {
    std::vector<float> saved(numel(flat.shape));
    FILE* f=fopen(path,"rb"); require(f,"open learned checkpoint");
    require(fread(saved.data(),sizeof(float),saved.size(),f)==saved.size(),"complete learned checkpoint");
    require(fgetc(f)==EOF,"checkpoint architecture unchanged"); fclose(f); put(flat,saved);
    constexpr int B=3,H=128,L=2;
    Policy reference=policy; reference.encoder.forward=reference_encoder_forward;
    Allocator fast_alloc,ref_alloc;
    auto fast=policy_reg_rollout(&policy,weights,&fast_alloc,B);
    auto old=policy_reg_rollout(&reference,weights,&ref_alloc,B);
    auto* old_encoder=(RetroEncoderActivations*)old.encoder;
    if(RETRO_CNN_FUSED) {
        old_encoder->columns={.shape={B,RETRO_COL_ELEMENTS}};
        alloc_register(&ref_alloc,&old_encoder->columns);
    }
    PrecisionTensor fast_state={.shape={L,B,H}},old_state=fast_state,input={.shape={B,RETRO_POLICY_INPUTS}};
    alloc_register(&fast_alloc,&fast_state); alloc_register(&fast_alloc,&input);
    alloc_register(&ref_alloc,&old_state); ck(alloc_create(&fast_alloc)); ck(alloc_create(&ref_alloc));
    std::vector<obs_t> packed_observations(B*RETRO_POLICY_INPUTS);
    std::vector<float> observations(B*RETRO_POLICY_INPUTS),actions(B),rewards(B),terminals(B);
    Env env[B]={}; Dict cfg={};
    dict_set_str(&cfg,"spawn_levels","1-1,1-2,8-4"); dict_set_str(&cfg,"cpu_backend","reference");
    dict_set(&cfg,"max_frames",48); dict_set(&cfg,"frameskip",1);
    for(int b=0;b<B;b++) {
        env[b].rng=73+b; puf_init(&env[b],&cfg);
        env[b].agents[0]={packed_observations.data()+b*RETRO_POLICY_INPUTS,&actions[b],&rewards[b],&terminals[b],nullptr,0};
        env[b].spawn_pin=1; env[b].cur_spawn=b; puf_reset(&env[b]);
        require(retro_observation_matches_reference(&env[b],packed_observations.data()+b*RETRO_POLICY_INPUTS),
            "production-precision reset observation differs from staged reference");
    }
    float max_error=0,max_probability_error=0; int resets=0;
    require(retro_observation_synthetic_parity(&env[0]),"production-precision synthetic palette mismatch");
    for(int step=0;step<128;step++) {
        for(int b=0;b<B;b++) if(terminals[b]) {
            resets++;
            for(int l=0;l<L;l++) {
                ck(cudaMemset(fast_state.data+(l*B+b)*H,0,H*sizeof(precision_t)));
                ck(cudaMemset(old_state.data+(l*B+b)*H,0,H*sizeof(precision_t)));
            }
        }
        for(size_t i=0;i<observations.size();i++) observations[i]=to_float(packed_observations[i]);
        put(input,observations);
        auto actual=host(policy_forward(&policy,weights,fast,input,fast_state,0));
        auto expected=host(policy_forward(&reference,weights,old,input,old_state,0));
        for(int b=0;b<B;b++) {
            float max_old=-INFINITY,max_new=-INFINITY;
            for(int j=0;j<65;j++) {
                int i=b*65+j; float error=fabsf(actual[i]-expected[i]);
                max_error=std::max(max_error,error);
                float tolerance=USE_BF16?0.03f+0.01f*fabsf(expected[i]):0.0001f+0.0001f*fabsf(expected[i]);
                require(std::isfinite(actual[i])&&error<=tolerance,"learned checkpoint logits/value old/fused");
                if(j<64) { max_old=std::max(max_old,expected[i]); max_new=std::max(max_new,actual[i]); }
            }
            double old_z=0,new_z=0;
            for(int j=0;j<64;j++) {
                old_z+=exp(expected[b*65+j]-max_old); new_z+=exp(actual[b*65+j]-max_new);
            }
            for(int j=0;j<64;j++) {
                float error=fabs(exp(expected[b*65+j]-max_old)/old_z-exp(actual[b*65+j]-max_new)/new_z);
                max_probability_error=std::max(max_probability_error,error);
            }
            actions[b]=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|((step%40<24)?RETRO_BTN_A:0));
            puf_step(&env[b]);
            require(retro_observation_matches_reference(&env[b],packed_observations.data()+b*RETRO_POLICY_INPUTS),
                "production-precision observation differs from staged reference");
        }
    }
    require(resets>=6,"learned checkpoint recurrent resets exercised");
    require(max_probability_error<=(USE_BF16?0.01f:0.00001f),"learned checkpoint action distribution old/fused");
    printf("PASS learned checkpoint: 384 ROM decisions, %d resets, max logit/value error=%g probability error=%g\n",
        resets,max_error,max_probability_error);
    puts("PASS 384 production-precision ROM observations bit-identical to staged reference");
    for(auto& e:env) puf_close(&e); dict_clear(&cfg);
    ck(cudaFree(fast_alloc.mem)); ck(cudaFree(ref_alloc.mem)); free(fast_alloc.regs); free(ref_alloc.regs);
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr);
    bool large=false; const char* checkpoint=nullptr; const char* write_path=nullptr;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--large")) large=true;
        else if(!strcmp(argv[i],"--checkpoint")&&i+1<argc) checkpoint=argv[++i];
        else { require(argv[i][0]!='-'&&!write_path,"usage: test_retro_encoder [--large] [--checkpoint FILE] [OUTPUT]"); write_path=argv[i]; }
    }
    cublas_init_handle();
    for(int batch:{1,3,17,33}) {
        convolution_parity<0>(batch); convolution_parity<1>(batch); convolution_parity<2>(batch);
    }
    if(large) { convolution_parity<0>(2048); convolution_parity<1>(2048); convolution_parity<2>(2048); }
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
    std::vector<int> live(RETRO_CONVS[2].oh*RETRO_CONVS[2].ow,1);
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
    if(write_path) {
        FILE* f=fopen(write_path,"wb"); require(f,"open test checkpoint");
        require(fwrite(serialized.data(),sizeof(float),serialized.size(),f)==serialized.size(),"write test checkpoint");
        require(fclose(f)==0,"close test checkpoint");
    }
    for(auto* p:cpu) free_retro_policy(p);
    printf("PASS Retro CNN (%s): %zu parameters, CPU/GPU encoder and recurrent policy agree\n",USE_BF16?"bf16":"float32",serialized.size());
    borrowed_input_parity(policy,weights,3);
    if(large) borrowed_input_parity(policy,weights,2048);
    if(checkpoint) checkpoint_parity(checkpoint,policy,weights,flat);
}
