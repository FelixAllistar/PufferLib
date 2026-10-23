#pragma once
// CPU implementation of the exact semantic policy; included after MinGRU.
#include "semantic_contract.h"
// Memory network: MinGRU (enabled = 0), or the causal window Transformer of
// src/window_transformer.cuh with window length = the policy's num_layers.
typedef struct { int enabled, layers, width, heads; } PKWindow;
static const PKWindow pk_window_off = {0, 2, 128, 4};
static inline size_t pk_transformer_count(int layers,int D) {
    return (size_t)layers*(4*D*D+2*D*4*D+9*D+4*D)+2*D;
}
static inline size_t pk_parameter_count(int hidden,int layers,int embed,int mid,PKAttn attn,PKWindow window) {
    int D=attn.width;
    const int inputs[6]={PK_MOVE_IN,PK_MON_IN(embed),attn.layers?D:PK_FUSION(embed),PK_SPECIES_IN,PK_MOVE_IN,hidden};
    const int mids[6]={mid,mid,hidden,mid,mid,hidden};
    const int outs[6]={embed,embed,hidden,embed,embed,PK_QUERY_OUT(embed)};
    size_t n=0;
    for(int i=0;i<6;i++)n+=(size_t)mids[i]*PK_AUG(inputs[i])+(size_t)outs[i]*PK_AUG(mids[i]);
    if(attn.layers) {
        n+=(size_t)D*PK_AUG(PK_TOKEN_IN(embed))+(size_t)D*PK_AUG(D);
        n+=pk_transformer_count(attn.layers,D);
    }
    if(window.enabled) {
        int W=window.width;
        return n+(size_t)W*PK_AUG(hidden+layers+2)+pk_transformer_count(window.layers,W)+(size_t)hidden*PK_AUG(W);
    }
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
#ifdef PK_CPU_BENCHMARK_GEMM
    // Optional benchmark acceleration; normal viewer/inference keeps its
    // existing scalar implementation. Both paths use identical saved weights.
    extern void pk_benchmark_gemm(const float*,const float*,float*,int,int,int,int,int);
    pk_benchmark_gemm(input,m->w1,m->hidden,m->rows,stride,m->in,m->mid,PK_AUG(m->in));
    for(int r=0;r<m->rows;r++)for(int h=0;h<m->mid;h++)
        m->hidden[r*m->mid+h]=fmaxf(0,m->hidden[r*m->mid+h]+m->w1[h*PK_AUG(m->in)+m->in]);
    pk_benchmark_gemm(m->hidden,m->w2,m->output,m->rows,m->mid,m->mid,m->out,PK_AUG(m->mid));
    for(int r=0;r<m->rows;r++)for(int o=0;o<m->out;o++) {
        float v=m->output[r*m->out+o]+m->w2[o*PK_AUG(m->mid)+m->mid];
        m->output[r*m->out+o]=m->relu_out?fmaxf(0,v):v;
    }
    return;
#endif
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
// Pre-LN Transformer block, in puf_transformer::parameter_specs order.
typedef struct { float *ln1g,*ln1b,*qkvw,*qkvb,*outw,*outb,*ln2g,*ln2b,*ff1w,*ff1b,*ff2w,*ff2b; } PKCpuBlock;
// CPU version of src/native_transformer.cuh over batch x tokens rows of width D.
typedef struct {
    int layers, width, heads, tokens, batch;
    PKCpuBlock* blocks;
    float *final_g,*final_b;
    float *x,*z,*qkv,*context,*residual,*ff,*scores;
} PKCpuTransformer;
typedef struct {
    int hidden, length, width, batch;
    float *in_w, *out_w, *zero;
    PKCpuTransformer tf;
    float *state, *tokens, *pooled, *output;   // state: [batch][length][hidden], oldest first
} PKCpuWindow;
typedef struct {
    PKCpuMLP move,mon,token,fusion,dec_species,dec_move,query;
    MinGRU* mingru;
    PKCpuWindow* window;
    int batch, embed;
    PKAttn attn;
    PKCpuTransformer tf;
    float *mon_input,*fused,*output,*tokens;
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
static inline PKCpuTransformer pk_cpu_transformer_make(Weights* weights,int layers,int D,int heads,int tokens,int batch) {
    PKCpuTransformer t={layers,D,heads,tokens,batch,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    t.blocks=(PKCpuBlock*)calloc(layers,sizeof(PKCpuBlock));
    for(int l=0;l<layers;l++) {
        PKCpuBlock* b=t.blocks+l;
        b->ln1g=pk_cpu_take(weights,D,8);b->ln1b=pk_cpu_take(weights,D,8);
        b->qkvw=pk_cpu_take(weights,3*D*D,8);b->qkvb=pk_cpu_take(weights,3*D,8);
        b->outw=pk_cpu_take(weights,D*D,8);b->outb=pk_cpu_take(weights,D,8);
        b->ln2g=pk_cpu_take(weights,D,8);b->ln2b=pk_cpu_take(weights,D,8);
        b->ff1w=pk_cpu_take(weights,4*D*D,8);b->ff1b=pk_cpu_take(weights,4*D,8);
        b->ff2w=pk_cpu_take(weights,4*D*D,8);b->ff2b=pk_cpu_take(weights,D,8);
    }
    t.final_g=pk_cpu_take(weights,D,8);t.final_b=pk_cpu_take(weights,D,8);
    size_t T=(size_t)batch*tokens;
    t.x=(float*)calloc(T*D,sizeof(float));t.z=(float*)calloc(T*D,sizeof(float));
    t.qkv=(float*)calloc(T*3*D,sizeof(float));t.context=(float*)calloc(T*D,sizeof(float));
    t.residual=(float*)calloc(T*D,sizeof(float));t.ff=(float*)calloc(T*4*D,sizeof(float));
    t.scores=(float*)calloc(tokens,sizeof(float));
    return t;
}
static inline void pk_cpu_transformer_free(PKCpuTransformer* t) {
    free(t->blocks);free(t->x);free(t->z);free(t->qkv);free(t->context);free(t->residual);free(t->ff);free(t->scores);
}
static inline PKCpuWindow* pk_cpu_window_make(Weights* weights,int batch,int hidden,int length,PKWindow window) {
    PKCpuWindow* w=(PKCpuWindow*)calloc(1,sizeof(*w));
    int W=window.width,in=PK_AUG(hidden+length+2);
    w->hidden=hidden;w->length=length;w->width=W;w->batch=batch;
    w->in_w=pk_cpu_take(weights,W*in,8);
    w->tf=pk_cpu_transformer_make(weights,window.layers,W,window.heads,length+1,batch);
    w->out_w=pk_cpu_take(weights,hidden*PK_AUG(W),8);
    w->zero=(float*)calloc(in>hidden?in:hidden,sizeof(float));
    w->state=(float*)calloc((size_t)batch*length*hidden,sizeof(float));
    w->tokens=(float*)calloc((size_t)batch*(length+1)*in,sizeof(float));
    w->pooled=(float*)calloc((size_t)batch*PK_AUG(W),sizeof(float));
    w->output=(float*)calloc((size_t)batch*hidden,sizeof(float));
    return w;
}
static inline void pk_cpu_window_free(PKCpuWindow* w) {
    if(!w) return;
    pk_cpu_transformer_free(&w->tf);
    free(w->zero);free(w->state);free(w->tokens);free(w->pooled);free(w->output);free(w);
}
static inline PKCpuPolicy* pk_cpu_make(Weights* weights,int batch,int hidden,int layers,
        int embed,int mid,PKAttn attn,PKWindow window) {
    if(!pk_attn_valid(attn)) {fprintf(stderr,"Invalid Pokemon attention dimensions\n");exit(1);}
    if(window.enabled && (window.layers<1 || window.width<8 || window.width%8 || window.heads<1 || window.width%window.heads)) {
        fprintf(stderr,"Invalid window Transformer dimensions\n");exit(1);
    }
    size_t expected=pk_parameter_count(hidden,layers,embed,mid,attn,window);
    if(batch<1 || hidden<8 || hidden%8 || layers<1 || weights->idx!=0 || (size_t)(weights->size-7)!=expected) {
        fprintf(stderr,"Pokemon semantic weights require %zu floats (H=%d L=%d); got %d\n",expected,hidden,layers,weights->size-7);exit(1);
    }
    PKCpuPolicy* p=(PKCpuPolicy*)calloc(1,sizeof(*p));p->batch=batch;p->embed=embed;p->attn=attn;
    p->move=pk_cpu_mlp_make(weights,168,PK_MOVE_IN,mid,embed,1,8);
    p->mon=pk_cpu_mlp_make(weights,batch*PK_MON_COUNT,PK_MON_IN(embed),mid,embed,1,8);
    if(attn.layers) {
        int D=attn.width,T=batch*PK_TOKENS;
        p->token=pk_cpu_mlp_make(weights,T,PK_TOKEN_IN(embed),D,D,0,8);
        p->tf=pk_cpu_transformer_make(weights,attn.layers,D,attn.heads,PK_TOKENS,batch);
        p->tokens=(float*)calloc((size_t)T*PK_TOKEN_IN(embed),sizeof(float));
    }
    p->fusion=pk_cpu_mlp_make(weights,batch,attn.layers?attn.width:PK_FUSION(embed),hidden,hidden,1,8);
    p->dec_species=pk_cpu_mlp_make(weights,152,PK_SPECIES_IN,mid,embed,0,8);
    p->dec_move=pk_cpu_mlp_make(weights,168,PK_MOVE_IN,mid,embed,0,8);
    p->query=pk_cpu_mlp_make(weights,batch,hidden,hidden,PK_QUERY_OUT(embed),0,8);
    if(window.enabled) p->window=pk_cpu_window_make(weights,batch,hidden,layers,window);
    else p->mingru=make_mingru(weights,batch,hidden,layers);
    p->mon_input=(float*)calloc((size_t)batch*PK_MON_COUNT*PK_MON_IN(embed),sizeof(float));
    p->fused=(float*)calloc((size_t)batch*(attn.layers?attn.width:PK_FUSION(embed)),sizeof(float));
    p->output=(float*)calloc((size_t)batch*169,sizeof(float));
    pk_cpu_refresh_tables(p);
    return p;
}
// y[rows,out] = x[rows,in] w[out,in]^T + b
static inline void pk_cpu_linear(const float* x,const float* w,const float* b,float* y,int rows,int in,int out) {
    for(int r=0;r<rows;r++)for(int o=0;o<out;o++) {
        const float* wo=w+(size_t)o*in;const float* xr=x+(size_t)r*in;float v=b[o];
        for(int i=0;i<in;i++)v+=wo[i]*xr[i];
        y[(size_t)r*out+o]=v;
    }
}
static inline void pk_cpu_layernorm(const float* x,const float* g,const float* b,float* y,int rows,int d) {
    for(int r=0;r<rows;r++) {
        const float* xr=x+(size_t)r*d;float mean=0,var=0;
        for(int j=0;j<d;j++)mean+=xr[j];
        mean/=d;
        for(int j=0;j<d;j++)var+=(xr[j]-mean)*(xr[j]-mean);
        float inverse=1.0f/sqrtf(var/d+1e-5f);
        for(int j=0;j<d;j++)y[(size_t)r*d+j]=(xr[j]-mean)*inverse*g[j]+b[j];
    }
}
static inline float pk_cpu_gelu(float x) {
    return .5f*x*(1.f+tanhf(0.7978845608028654f*(x+.044715f*x*x*x)));
}
// Pre-LN dense self-attention over each batch row's tokens, as native_transformer.cuh.
// Runs the blocks on t->x in place; the final LN is applied by pk_cpu_transformer_pool.
static inline void pk_cpu_transformer_run(PKCpuTransformer* t) {
    int D=t->width,H=t->heads,dk=D/H,N=t->tokens,T=t->batch*N;
    float scale=1.0f/sqrtf((float)dk);
    for(int l=0;l<t->layers;l++) {
        PKCpuBlock* blk=t->blocks+l;
        pk_cpu_layernorm(t->x,blk->ln1g,blk->ln1b,t->z,T,D);
        pk_cpu_linear(t->z,blk->qkvw,blk->qkvb,t->qkv,T,D,3*D);
        for(int b=0;b<t->batch;b++)for(int h=0;h<H;h++)for(int i=0;i<N;i++) {
            const float* q=t->qkv+(size_t)(b*N+i)*3*D+h*dk;
            float maximum=-INFINITY,sum=0;
            for(int j=0;j<N;j++) {
                const float* k=t->qkv+(size_t)(b*N+j)*3*D+D+h*dk;
                float s=0;for(int c=0;c<dk;c++)s+=q[c]*k[c];
                t->scores[j]=s*scale;maximum=fmaxf(maximum,t->scores[j]);
            }
            for(int j=0;j<N;j++) {t->scores[j]=expf(t->scores[j]-maximum);sum+=t->scores[j];}
            float* out=t->context+(size_t)(b*N+i)*D+h*dk;
            for(int c=0;c<dk;c++)out[c]=0;
            for(int j=0;j<N;j++) {
                const float* v=t->qkv+(size_t)(b*N+j)*3*D+2*D+h*dk;
                for(int c=0;c<dk;c++)out[c]+=t->scores[j]/sum*v[c];
            }
        }
        pk_cpu_linear(t->context,blk->outw,blk->outb,t->residual,T,D,D);
        for(size_t i=0;i<(size_t)T*D;i++)t->residual[i]+=t->x[i];
        pk_cpu_layernorm(t->residual,blk->ln2g,blk->ln2b,t->z,T,D);
        pk_cpu_linear(t->z,blk->ff1w,blk->ff1b,t->ff,T,D,4*D);
        for(size_t i=0;i<(size_t)T*4*D;i++)t->ff[i]=pk_cpu_gelu(t->ff[i]);
        pk_cpu_linear(t->ff,blk->ff2w,blk->ff2b,t->x,T,4*D,D);
        for(size_t i=0;i<(size_t)T*D;i++)t->x[i]+=t->residual[i];
    }
}
// Final LN of one token per batch row, written with stride `stride`.
static inline void pk_cpu_transformer_pool(PKCpuTransformer* t,int token,float* out,int stride) {
    for(int b=0;b<t->batch;b++)
        pk_cpu_layernorm(t->x+(size_t)(b*t->tokens+token)*t->width,t->final_g,t->final_b,out+(size_t)b*stride,1,t->width);
}
// One step of the window Transformer: tokens are the window rows (oldest first) and the
// current input; all-zero rows are empty. Then the window shifts in the input.
static inline float* pk_cpu_window(PKCpuWindow* w,const float* x) {
    int H=w->hidden,K=w->length,T=K+1,W=w->width,in=PK_AUG(H+K+2),out_cols=PK_AUG(W);
    memset(w->tokens,0,(size_t)w->batch*T*in*sizeof(float));
    for(int b=0;b<w->batch;b++)for(int k=0;k<T;k++) {
        const float* src=k==K?x+(size_t)b*H:w->state+((size_t)b*K+k)*H;
        int valid=k==K;
        for(int j=0;j<H && !valid;j++) valid=src[j]!=0;
        float* row=w->tokens+((size_t)b*T+k)*in;
        if(valid) {memcpy(row,src,H*sizeof(float));row[H]=1;}
        row[H+1+k]=1;row[H+1+T]=1;
    }
    pk_cpu_linear(w->tokens,w->in_w,w->zero,w->tf.x,w->batch*T,in,W);
    pk_cpu_transformer_run(&w->tf);
    memset(w->pooled,0,(size_t)w->batch*out_cols*sizeof(float));
    pk_cpu_transformer_pool(&w->tf,K,w->pooled,out_cols);
    for(int b=0;b<w->batch;b++) w->pooled[(size_t)b*out_cols+W]=1;
    pk_cpu_linear(w->pooled,w->out_w,w->zero,w->output,w->batch,out_cols,H);
    for(int b=0;b<w->batch;b++) {
        for(int h=0;h<H;h++) w->output[(size_t)b*H+h]+=x[(size_t)b*H+h];
        float* st=w->state+(size_t)b*K*H;
        memmove(st,st+H,(size_t)(K-1)*H*sizeof(float));
        memcpy(st+(size_t)(K-1)*H,x+(size_t)b*H,H*sizeof(float));
    }
    return w->output;
}
// Clears recurrent memory (MinGRU state or window) between games.
static inline void pk_cpu_reset_memory(PKCpuPolicy* p) {
    if(p->window) memset(p->window->state,0,(size_t)p->window->batch*p->window->length*p->window->hidden*sizeof(float));
    if(p->mingru) {
        memset(p->mingru->state,0,(size_t)p->batch*p->mingru->hidden_size*p->mingru->num_layers*sizeof(float));
        memset(p->mingru->output,0,(size_t)p->batch*p->mingru->hidden_size*sizeof(float));
    }
}
static inline float* pk_cpu_encode(PKCpuPolicy* p,const float* obs) {
    int E=p->embed;
    memset(p->mon_input,0,(size_t)p->batch*PK_MON_COUNT*PK_MON_IN(E)*sizeof(float));
    for(int b=0;b<p->batch;b++)for(int e=0;e<PK_MON_COUNT;e++) {
        const float* o=obs+b*648;
        float* input=p->mon_input+(b*PK_MON_COUNT+e)*PK_MON_IN(E);
        int s=pk_entity_species(o,e);if(!s)continue;
        input[s]=1;memcpy(input+150,pk_sem_species[s],PK_SF*sizeof(float));
        int count=0;for(int m=0;m<4;m++)count+=pk_entity_move(o,e,m)!=0;
        for(int m=0;m<4;m++) {
            int move=pk_entity_move(o,e,m);if(!move)continue;
            for(int f=0;f<E;f++)input[PK_SPECIES_IN+f]+=p->move.output[move*E+f]/count;
        }
        for(int f=0;f<PK_DYN;f++)input[PK_SPECIES_IN+E+f]=pk_dynamic(o,e,f);
    }
    pk_cpu_mlp(&p->mon,p->mon_input,PK_MON_IN(E));
    if(p->attn.layers) {
        int TIN=PK_TOKEN_IN(E),D=p->attn.width;
        memset(p->tokens,0,(size_t)p->batch*PK_TOKENS*TIN*sizeof(float));
        for(int b=0;b<p->batch;b++) {
            for(int t=0;t<PK_MON_COUNT;t++)
                memcpy(p->tokens+(size_t)(b*PK_TOKENS+t)*TIN,p->mon.output+(size_t)(b*PK_MON_COUNT+t)*E,E*sizeof(float));
            float* global=p->tokens+(size_t)(b*PK_TOKENS+PK_MON_COUNT)*TIN;
            for(int f=0;f<PK_GLOBAL;f++)global[E+f]=pk_global_feature(obs+b*648,f);
            global[E+PK_GLOBAL]=1;
        }
        pk_cpu_mlp(&p->token,p->tokens,TIN);
        memcpy(p->tf.x,p->token.output,(size_t)p->batch*PK_TOKENS*D*sizeof(float));
        pk_cpu_transformer_run(&p->tf);
        // Only the pooled global token is used: final LN of token 20 into the fusion input.
        pk_cpu_transformer_pool(&p->tf,PK_MON_COUNT,p->fused,D);
        pk_cpu_mlp(&p->fusion,p->fused,D);
        return p->fusion.output;
    }
    for(int b=0;b<p->batch;b++) {
        float* fused=p->fused+b*PK_FUSION(E);
        memcpy(fused,p->mon.output+b*PK_MON_COUNT*E,PK_MON_COUNT*E*sizeof(float));
        for(int f=0;f<PK_GLOBAL;f++)fused[PK_MON_COUNT*E+f]=pk_global_feature(obs+b*648,f);
    }
    pk_cpu_mlp(&p->fusion,p->fused,PK_FUSION(E));
    return p->fusion.output;
}
static inline float* pk_cpu_decode(PKCpuPolicy* p,const float* obs,const float* hidden) {
    int E=p->embed;
    pk_cpu_mlp(&p->query,hidden,p->query.in);
    for(int b=0;b<p->batch;b++) {
        const float* o=obs+b*648;const float* q=p->query.output+b*PK_QUERY_OUT(E);
        for(int action=0;action<168;action++) {
            PKActionParts parts=pk_action_parts(o,action);float v=0;
            for(int k=0;k<parts.n;k++) {
                int c=parts.candidate[k];
                const float* emb=c<152?p->dec_species.output+c*E:p->dec_move.output+(c-152)*E;
                float score=0;for(int f=0;f<E;f++)score+=q[parts.query[k]*E+f]*emb[f];
                v+=parts.coefficient[k]*score/sqrtf((float)E);
            }
            for(int f=0;f<PK_DYN;f++)v+=pk_action_dynamic(o,action,f,&pk_sem_move[0][0],&pk_sem_chart[0][0])*q[PK_QUERIES*E+f];
            if(parts.scalar>=0)v+=q[PK_QUERIES*E+PK_DYN+parts.scalar];
            p->output[b*169+action]=v;
        }
        p->output[b*169+168]=q[PK_QUERY_OUT(E)-1];
    }
    return p->output;
}
static inline float* pk_cpu_forward(PKCpuPolicy* p,const float* obs) {
    float* encoded=pk_cpu_encode(p,obs);
    if(p->window) return pk_cpu_decode(p,obs,pk_cpu_window(p->window,encoded));
    mingru(p->mingru,encoded);
    return pk_cpu_decode(p,obs,p->mingru->output);
}
static inline void pk_cpu_free(PKCpuPolicy* p) {
    PKCpuMLP* mlps[]={&p->move,&p->mon,&p->token,&p->fusion,&p->dec_species,&p->dec_move,&p->query};
    for(int i=0;i<7;i++) {free(mlps[i]->hidden);free(mlps[i]->output);}
    if(p->attn.layers) pk_cpu_transformer_free(&p->tf);
    free(p->tokens);pk_cpu_window_free(p->window);
    if(p->mingru) free_mingru(p->mingru);
    free(p->mon_input);free(p->fused);free(p->output);free(p);
}
