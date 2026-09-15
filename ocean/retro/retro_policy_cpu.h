#pragma once
#include "puffercpu.h"
#include "retro_policy_shape.h"
#include <vector>
#include <stdexcept>
#include <algorithm>

// CPU reference and live viewer use the exact CUDA parameter order/layout.
// The existing CPU MinGRU and action/value head are unchanged.
struct RetroPolicy {
    const float* conv_w[3]; const float* conv_b[3];
    const float* ram_b; const float* fusion_b;
    Linear* ram; Linear* fusion; Linear* decoder;
    MinGRU* mingru; Multidiscrete* multidiscrete;
    std::vector<float> image[3],joined;
};
static RetroPolicy* make_retro_policy(Weights* weights,int hidden,int layers) {
    if(hidden<8||hidden%8||layers<1||layers>8)
        throw std::runtime_error("invalid Retro CNN hidden size/layer count");
    auto* p=new RetroPolicy{};
    for(int l=0;l<3;l++) {
        auto c=RETRO_CONVS[l];
        p->conv_w[l]=get_weights_aligned(weights,c.oc*c.k*c.k*c.ic);
        p->conv_b[l]=get_weights_aligned(weights,c.oc);
        p->image[l].resize(c.oh*c.ow*c.oc);
    }
    p->ram=make_linear(weights,1,RETRO_RAM_INPUTS,RETRO_RAM_HIDDEN);
    p->ram_b=get_weights_aligned(weights,RETRO_RAM_HIDDEN);
    p->fusion=make_linear(weights,1,RETRO_FUSED_FEATURES,hidden);
    p->fusion_b=get_weights_aligned(weights,hidden);
    p->decoder=make_linear(weights,1,hidden,65);
    p->mingru=make_mingru(weights,1,hidden,layers);
    int sizes[]={64}; p->multidiscrete=make_multidiscrete(1,sizes,1);
    p->joined.resize(RETRO_FUSED_FEATURES);
    return p;
}
static const float* retro_policy_encode(RetroPolicy* p,const float* obs) {
    for(int l=0;l<3;l++) {
        auto c=RETRO_CONVS[l]; int K=c.k*c.k*c.ic;
        const float* input=l?p->image[l-1].data():obs+RETRO_RAM_INPUTS;
        for(int y=0;y<c.oh;y++) for(int x=0;x<c.ow;x++) for(int ch=0;ch<c.oc;ch++) {
            float sum=p->conv_b[l][ch];
            for(int ky=0;ky<c.k;ky++) for(int kx=0;kx<c.k;kx++) {
                int iy=y*c.stride-c.pad+ky,ix=x*c.stride-c.pad+kx;
                if(iy<0||iy>=c.ih||ix<0||ix>=c.iw) continue;
                for(int ic=0;ic<c.ic;ic++)
                    sum+=input[(iy*c.iw+ix)*c.ic+ic]*p->conv_w[l][ch*K+(ky*c.k+kx)*c.ic+ic];
            }
            p->image[l][(y*c.ow+x)*c.oc+ch]=std::max(0.0f,sum);
        }
    }
    linear(p->ram,const_cast<float*>(obs));
    std::copy(p->image[2].begin(),p->image[2].end(),p->joined.begin());
    for(int i=0;i<RETRO_RAM_HIDDEN;i++)
        p->joined[RETRO_CNN_FEATURES+i]=std::max(0.0f,p->ram->output[i]+p->ram_b[i]);
    linear(p->fusion,p->joined.data());
    for(int i=0;i<p->fusion->output_dim;i++)
        p->fusion->output[i]=std::max(0.0f,p->fusion->output[i]+p->fusion_b[i]);
    return p->fusion->output;
}
static const float* retro_policy_logits(RetroPolicy* p,const float* obs) {
    auto* encoded=retro_policy_encode(p,obs);
    mingru(p->mingru,const_cast<float*>(encoded)); linear(p->decoder,p->mingru->output);
    return p->decoder->output;
}
static void retro_policy_act(RetroPolicy* p,float* obs,float* action,bool deterministic) {
    retro_policy_logits(p,obs);
    if(deterministic) argmax_multidiscrete(p->multidiscrete,p->decoder->output,action);
    else softmax_multidiscrete(p->multidiscrete,p->decoder->output,action);
}
static void free_retro_policy(RetroPolicy* p) {
    if(!p) return;
    free(p->ram); free(p->fusion); free(p->decoder); free_mingru(p->mingru);
    free(p->multidiscrete->logit_sizes); free(p->multidiscrete); delete p;
}
