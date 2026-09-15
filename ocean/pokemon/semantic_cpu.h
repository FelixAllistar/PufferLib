#pragma once
// CPU implementation of the exact semantic policy; included after MinGRU.
#include "semantic_contract.h"
static inline size_t pk_parameter_count(int hidden,int layers) {
    const int inputs[6]={PK_MOVE_IN,PK_MON_IN,PK_FUSION,PK_SPECIES_IN,PK_MOVE_IN,hidden};
    const int mids[6]={64,64,hidden,64,64,hidden};
    const int outs[6]={PK_EMBED,PK_EMBED,hidden,PK_EMBED,PK_EMBED,PK_QUERY_OUT};
    size_t n=0;
    for(int i=0;i<6;i++)n+=(size_t)mids[i]*PK_AUG(inputs[i])+(size_t)outs[i]*PK_AUG(mids[i]);
    return n+(size_t)layers*3*hidden*hidden;
}
typedef struct {
    int in, mid, out, rows, relu_out;
    float *w1, *w2, *hidden, *output;
} PKCpuMLP;
static inline float* pk_cpu_take(Weights* weights, int count, int alignment) {
    weights->idx = (weights->idx + alignment - 1) & ~(alignment - 1);
    if (count < 0 || weights->idx > weights->size - count) {
        fprintf(stderr, "Truncated Pokemon semantic weights\n"); exit(1);
    }
    float* result = weights->data + weights->idx;
    weights->idx += count;
    return result;
}
static inline PKCpuMLP pk_cpu_mlp_make(Weights* w, int rows, int in, int mid, int out,
        int relu_out, int alignment) {
    PKCpuMLP m = {in, mid, out, rows, relu_out, NULL, NULL, NULL, NULL};
    m.w1 = pk_cpu_take(w, mid * PK_AUG(in), alignment);
    m.w2 = pk_cpu_take(w, out * PK_AUG(mid), alignment);
    m.hidden = (float*)calloc((size_t)rows * mid, sizeof(float));
    m.output = (float*)calloc((size_t)rows * out, sizeof(float));
    return m;
}
static inline void pk_cpu_mlp(PKCpuMLP* m, const float* input, int stride) {
    for (int r = 0; r < m->rows; r++) {
        for (int h = 0; h < m->mid; h++) {
            const float* w = m->w1 + h * PK_AUG(m->in);
            float v = w[m->in];
            for (int i = 0; i < m->in; i++) v += w[i] * input[r * stride + i];
            m->hidden[r * m->mid + h] = fmaxf(0, v);
        }
        for (int o = 0; o < m->out; o++) {
            const float* w = m->w2 + o * PK_AUG(m->mid);
            float v = w[m->mid];
            for (int h = 0; h < m->mid; h++) v += w[h] * m->hidden[r * m->mid + h];
            m->output[r * m->out + o] = m->relu_out ? fmaxf(0, v) : v;
        }
    }
}
typedef struct {
    PKCpuMLP move,mon,fusion,dec_species,dec_move,query;
    MinGRU* mingru;
    int batch;
    float *mon_input,*fused,*output;
} PKCpuPolicy;
static inline void pk_cpu_static(PKCpuMLP* m,int move) {
    float* input=(float*)calloc((size_t)m->rows*m->in,sizeof(float));
    int ids=move?166:150,features=move?PK_MF:PK_SF;
    for(int r=1;r<ids;r++) {
        input[r*m->in+r]=1;
        memcpy(input+r*m->in+ids,move?pk_sem_move[r]:pk_sem_species[r],features*sizeof(float));
    }
    pk_cpu_mlp(m,input,m->in);free(input);
}
static inline void pk_cpu_refresh_tables(PKCpuPolicy* p) {
    pk_cpu_static(&p->move,1);pk_cpu_static(&p->dec_species,0);pk_cpu_static(&p->dec_move,1);
}
static inline PKCpuPolicy* pk_cpu_make(Weights* weights,int batch,int hidden,int layers) {
    size_t expected=pk_parameter_count(hidden,layers);
    if(batch<1 || hidden<8 || hidden%8 || layers<1 || weights->idx!=0 || (size_t)(weights->size-7)!=expected) {
        fprintf(stderr,"Pokemon semantic weights require %zu floats (H=%d L=%d); got %d\n",expected,hidden,layers,weights->size-7);exit(1);
    }
    PKCpuPolicy* p=(PKCpuPolicy*)calloc(1,sizeof(*p));p->batch=batch;
    p->move=pk_cpu_mlp_make(weights,168,PK_MOVE_IN,64,PK_EMBED,1,8);
    p->mon=pk_cpu_mlp_make(weights,batch*PK_MON_COUNT,PK_MON_IN,64,PK_EMBED,1,8);
    p->fusion=pk_cpu_mlp_make(weights,batch,PK_FUSION,hidden,hidden,1,8);
    p->dec_species=pk_cpu_mlp_make(weights,152,PK_SPECIES_IN,64,PK_EMBED,0,8);
    p->dec_move=pk_cpu_mlp_make(weights,168,PK_MOVE_IN,64,PK_EMBED,0,8);
    p->query=pk_cpu_mlp_make(weights,batch,hidden,hidden,PK_QUERY_OUT,0,8);
    p->mingru=make_mingru(weights,batch,hidden,layers);
    p->mon_input=(float*)calloc((size_t)batch*PK_MON_COUNT*PK_MON_IN,sizeof(float));
    p->fused=(float*)calloc((size_t)batch*PK_FUSION,sizeof(float));
    p->output=(float*)calloc((size_t)batch*169,sizeof(float));
    pk_cpu_refresh_tables(p);
    return p;
}
static inline float* pk_cpu_encode(PKCpuPolicy* p,const float* obs) {
    memset(p->mon_input,0,(size_t)p->batch*PK_MON_COUNT*PK_MON_IN*sizeof(float));
    for(int b=0;b<p->batch;b++)for(int e=0;e<PK_MON_COUNT;e++) {
        const float* o=obs+b*648;
        float* input=p->mon_input+(b*PK_MON_COUNT+e)*PK_MON_IN;
        int s=pk_entity_species(o,e);if(!s)continue;
        input[s]=1;memcpy(input+150,pk_sem_species[s],PK_SF*sizeof(float));
        int count=0;for(int m=0;m<4;m++)count+=pk_entity_move(o,e,m)!=0;
        for(int m=0;m<4;m++) {
            int move=pk_entity_move(o,e,m);if(!move)continue;
            for(int f=0;f<PK_EMBED;f++)input[PK_SPECIES_IN+f]+=p->move.output[move*PK_EMBED+f]/count;
        }
        for(int f=0;f<PK_DYN;f++)input[PK_SPECIES_IN+PK_EMBED+f]=pk_dynamic(o,e,f);
    }
    pk_cpu_mlp(&p->mon,p->mon_input,PK_MON_IN);
    for(int b=0;b<p->batch;b++) {
        float* fused=p->fused+b*PK_FUSION;
        memcpy(fused,p->mon.output+b*PK_MON_COUNT*PK_EMBED,PK_MON_COUNT*PK_EMBED*sizeof(float));
        for(int f=0;f<PK_GLOBAL;f++)fused[PK_MON_COUNT*PK_EMBED+f]=pk_global_feature(obs+b*648,f);
    }
    pk_cpu_mlp(&p->fusion,p->fused,PK_FUSION);
    return p->fusion.output;
}
static inline float* pk_cpu_decode(PKCpuPolicy* p,const float* obs,const float* hidden) {
    pk_cpu_mlp(&p->query,hidden,p->query.in);
    for(int b=0;b<p->batch;b++) {
        const float* o=obs+b*648;const float* q=p->query.output+b*PK_QUERY_OUT;
        for(int action=0;action<168;action++) {
            PKActionParts parts=pk_action_parts(o,action);float v=0;
            for(int k=0;k<parts.n;k++) {
                int c=parts.candidate[k];
                const float* emb=c<152?p->dec_species.output+c*PK_EMBED:p->dec_move.output+(c-152)*PK_EMBED;
                float score=0;for(int f=0;f<PK_EMBED;f++)score+=q[parts.query[k]*PK_EMBED+f]*emb[f];
                v+=parts.coefficient[k]*score/sqrtf((float)PK_EMBED);
            }
            for(int f=0;f<PK_DYN;f++)v+=pk_action_dynamic(o,action,f,&pk_sem_move[0][0],&pk_sem_chart[0][0])*q[PK_QUERIES*PK_EMBED+f];
            if(parts.scalar>=0)v+=q[PK_QUERIES*PK_EMBED+PK_DYN+parts.scalar];
            p->output[b*169+action]=v;
        }
        p->output[b*169+168]=q[PK_QUERY_OUT-1];
    }
    return p->output;
}
static inline float* pk_cpu_forward(PKCpuPolicy* p,const float* obs) {
    mingru(p->mingru,pk_cpu_encode(p,obs));
    return pk_cpu_decode(p,obs,p->mingru->output);
}
static inline void pk_cpu_free(PKCpuPolicy* p) {
    PKCpuMLP* mlps[]={&p->move,&p->mon,&p->fusion,&p->dec_species,&p->dec_move,&p->query};
    for(int i=0;i<6;i++) {free(mlps[i]->hidden);free(mlps[i]->output);}
    free_mingru(p->mingru);free(p->mon_input);free(p->fused);free(p->output);free(p);
}
