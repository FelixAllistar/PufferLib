// Shared semantic move -> Pokemon encoders, recurrent fusion, and candidate
// decoder. No per-catalog-set logits or ordinal-ID neural inputs.
#include "semantic_contract.h"
#include "semantic_mlp.cuh"
#define PK_MOVE_ROWS 168
#define PK_SPECIES_ROWS 152
#define PK_CANDIDATES (PK_MOVE_ROWS+PK_SPECIES_ROWS)
__device__ static const float pk_gpu_move[166][PK_MF]={
#include "move_features.inc"
};
__device__ static const float pk_gpu_species[150][PK_SF]={
#include "species_features.inc"
};
__device__ static const float pk_gpu_chart[15][15]={
#include "type_chart.inc"
};

__global__ static void pk_static_input(precision_t* dst,int rows,int features,bool move) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=rows*PK_AUG(features))return;
    int r=i/PK_AUG(features),f=i%PK_AUG(features),ids=move?166:150;
    float v=0;
    if(f==features)v=1;
    else if(r<ids && r>0 && f<features) v=f<ids?(float)(f==r):
        (move?pk_gpu_move[r][f-ids]:pk_gpu_species[r][f-ids]);
    dst[i]=from_float(v);
}
static void pk_static_forward(PKMLPWeights* w,PKMLPActs* a,bool move,cudaStream_t stream) {
    pk_static_input<<<grid_size(a->out.shape[0]*PK_AUG(w->in)),BLOCK_SIZE,0,stream>>>(
        a->input_aug.data,a->out.shape[0],w->in,move);
    pk_nn_mlp_forward(w,a,stream);
}

struct PKEncoderWeights { PKMLPWeights move,mon,fusion; };
struct PKEncoderActs {
    PKMLPActs move,mon,fusion;
    PrecisionTensor incidence,mean_moves,grad_mean_moves,mean_projection;
};
static void* pk_encoder_weights(void* self) {
    Encoder* enc=(Encoder*)self;
    if(enc->in_dim!=648 || enc->out_dim<8 || enc->out_dim%8) {
        fprintf(stderr,"Pokemon semantic policy requires ABI 3 / hidden_size divisible by 8\n");exit(1);
    }
    PKEncoderWeights* w=(PKEncoderWeights*)calloc(1,sizeof(*w));
    w->move={PK_MOVE_IN,64,PK_EMBED,true,sqrtf(2.0f),{}, {}};
    w->mon={PK_MON_IN,64,PK_EMBED,true,sqrtf(2.0f),{}, {}};
    w->fusion={PK_FUSION,enc->out_dim,enc->out_dim,true,sqrtf(2.0f),{}, {}};
    return w;
}
static void pk_encoder_params(void* weights,Allocator* alloc) {
    PKEncoderWeights* w=(PKEncoderWeights*)weights;
    pk_nn_mlp_params(&w->move,alloc);pk_nn_mlp_params(&w->mon,alloc);pk_nn_mlp_params(&w->fusion,alloc);
}
static void pk_encoder_init(void* weights,ulong* seed,cudaStream_t stream) {
    PKEncoderWeights* w=(PKEncoderWeights*)weights;
    pk_nn_mlp_init(&w->move,seed,stream);pk_nn_mlp_init(&w->mon,seed,stream);pk_nn_mlp_init(&w->fusion,seed,stream);
}
static void pk_encoder_acts(void* weights,void* activations,Allocator* acts,Allocator* grads,int rows) {
    PKEncoderWeights* w=(PKEncoderWeights*)weights; PKEncoderActs* a=(PKEncoderActs*)activations;
    pk_nn_mlp_acts(&w->move,&a->move,acts,grads,PK_MOVE_ROWS,false);
    // Only the move-embedding slice needs an input gradient. Avoid materializing
    // hundreds of MB of gradients for immutable identity/stat features.
    pk_nn_mlp_acts(&w->mon,&a->mon,acts,grads,rows*PK_MON_COUNT,false);
    pk_nn_mlp_acts(&w->fusion,&a->fusion,acts,grads,rows,true);
    a->incidence={.shape={rows*PK_MON_COUNT,PK_MOVE_ROWS}};alloc_register(acts,&a->incidence);
    a->mean_moves={.shape={rows*PK_MON_COUNT,PK_EMBED}};alloc_register(acts,&a->mean_moves);
    if(grads) {
        a->grad_mean_moves={.shape={rows*PK_MON_COUNT,PK_EMBED}};alloc_register(acts,&a->grad_mean_moves);
        a->mean_projection={.shape={64,PK_EMBED}};alloc_register(acts,&a->mean_projection);
    }
}
static void pk_encoder_rollout(void* w,void* a,Allocator* alloc,int rows) {pk_encoder_acts(w,a,alloc,nullptr,rows);}
__global__ static void pk_incidence(precision_t* dst,const precision_t* obs,int rows) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=rows*PK_MON_COUNT*PK_MOVE_ROWS)return;
    int r=i/PK_MOVE_ROWS,e=r%PK_MON_COUNT,move=i%PK_MOVE_ROWS;
    const precision_t* o=obs+(r/PK_MON_COUNT)*648;
    int count=0,found=0;
    for(int m=0;m<4;m++) {int id=pk_entity_move(o,e,m);count+=id!=0;found+=id!=0 && id==move;}
    dst[i]=from_float(count?(float)found/count:0);
}
__global__ static void pk_mon_input(precision_t* dst,const precision_t* obs,const precision_t* means,int rows) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=rows*PK_MON_COUNT*PK_AUG(PK_MON_IN))return;
    int r=i/PK_AUG(PK_MON_IN),f=i%PK_AUG(PK_MON_IN),e=r%PK_MON_COUNT;
    const precision_t* o=obs+(r/PK_MON_COUNT)*648;
    int species=pk_entity_species(o,e);float v=0;
    if(f==PK_MON_IN)v=1;
    else if(species && f<PK_MON_IN) {
        if(f<150)v=f==species;
        else if(f<PK_SPECIES_IN)v=pk_gpu_species[species][f-150];
        else if(f<PK_SPECIES_IN+PK_EMBED)v=to_float(means[r*PK_EMBED+f-PK_SPECIES_IN]);
        else v=pk_dynamic(o,e,f-PK_SPECIES_IN-PK_EMBED);
    }
    dst[i]=from_float(v);
}
__global__ static void pk_fuse(precision_t* dst,const precision_t* obs,const precision_t* mons,int rows) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=rows*PK_AUG(PK_FUSION))return;
    int r=i/PK_AUG(PK_FUSION),f=i%PK_AUG(PK_FUSION);float v=0;
    if(f<PK_MON_COUNT*PK_EMBED)v=to_float(mons[r*PK_MON_COUNT*PK_EMBED+f]);
    else if(f<PK_FUSION)v=pk_global_feature(obs+r*648,f-PK_MON_COUNT*PK_EMBED);
    else if(f==PK_FUSION)v=1;
    dst[i]=from_float(v);
}
static PrecisionTensor pk_encoder_forward(void* weights,void* activations,PrecisionTensor obs,cudaStream_t stream) {
    PKEncoderWeights* w=(PKEncoderWeights*)weights;PKEncoderActs* a=(PKEncoderActs*)activations;
    int rows=a->fusion.out.shape[0];
    pk_static_forward(&w->move,&a->move,true,stream);
    pk_incidence<<<grid_size(rows*PK_MON_COUNT*PK_MOVE_ROWS),BLOCK_SIZE,0,stream>>>(a->incidence.data,obs.data,rows);
    puf_mm_nn(&a->incidence,&a->move.out,&a->mean_moves,stream);
    pk_mon_input<<<grid_size(rows*PK_MON_COUNT*PK_AUG(PK_MON_IN)),BLOCK_SIZE,0,stream>>>(
        a->mon.input_aug.data,obs.data,a->mean_moves.data,rows);
    pk_nn_mlp_forward(&w->mon,&a->mon,stream);
    pk_fuse<<<grid_size(rows*PK_AUG(PK_FUSION)),BLOCK_SIZE,0,stream>>>(a->fusion.input_aug.data,obs.data,a->mon.out.data,rows);
    return pk_nn_mlp_forward(&w->fusion,&a->fusion,stream);
}
__global__ static void pk_extract_gradient(precision_t* dst,const precision_t* src,int rows,int width,int src_width,int offset) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<rows*width)dst[i]=src[(i/width)*src_width+offset+i%width];
}
static void pk_encoder_backward(void* weights,void* activations,PrecisionTensor grad,cudaStream_t stream) {
    PKEncoderWeights* w=(PKEncoderWeights*)weights;PKEncoderActs* a=(PKEncoderActs*)activations;
    int rows=a->fusion.out.shape[0];
    PrecisionTensor fused=pk_nn_mlp_backward(&w->fusion,&a->fusion,grad,stream);
    pk_extract_gradient<<<grid_size(rows*PK_MON_COUNT*PK_EMBED),BLOCK_SIZE,0,stream>>>(
        a->mon.grad_out.data,fused.data,rows,PK_MON_COUNT*PK_EMBED,PK_FUSION,0);
    pk_nn_mlp_backward(&w->mon,&a->mon,a->mon.grad_out,stream);
    pk_extract_gradient<<<grid_size(64*PK_EMBED),BLOCK_SIZE,0,stream>>>(
        a->mean_projection.data,w->mon.w1.data,64,PK_EMBED,PK_AUG(PK_MON_IN),PK_SPECIES_IN);
    puf_mm_nn(&a->mon.grad_mid,&a->mean_projection,&a->grad_mean_moves,stream);
    // Deterministic reduction over all occurrences, with no atomic embedding updates.
    puf_mm_tn(&a->incidence,&a->grad_mean_moves,&a->move.grad_out,stream);
    pk_nn_mlp_backward(&w->move,&a->move,a->move.grad_out,stream);
}

static void create_pokemon_encoder(Encoder* enc) {
    enc->forward=pk_encoder_forward;enc->backward=pk_encoder_backward;
    enc->reg_train=pk_encoder_acts;enc->reg_rollout=pk_encoder_rollout;
    enc->reg_params=pk_encoder_params;enc->init_weights=pk_encoder_init;
    enc->create_weights=pk_encoder_weights;enc->activation_size=sizeof(PKEncoderActs);
}

struct PKDecoderWeights { PKMLPWeights species,move,query; int hidden; };
struct PKDecoderActs {
    PKMLPActs species,move,query;
    PrecisionTensor obs,table,queries,scores,out,grad_scores,grad_queries,grad_table;
};
static void* pk_decoder_weights(void* self) {
    Decoder* dec=(Decoder*)self;
    if(dec->continuous || dec->output_dim!=168 || dec->hidden_dim<8 || dec->hidden_dim%8) {
        fprintf(stderr,"Invalid Pokemon semantic decoder dimensions\n");exit(1);
    }
    PKDecoderWeights* w=(PKDecoderWeights*)calloc(1,sizeof(*w));w->hidden=dec->hidden_dim;
    w->species={PK_SPECIES_IN,64,PK_EMBED,false,1.0f,{}, {}};
    w->move={PK_MOVE_IN,64,PK_EMBED,false,1.0f,{}, {}};
    w->query={w->hidden,w->hidden,PK_QUERY_OUT,false,0.01f,{}, {}};
    return w;
}
static void pk_decoder_params(void* weights,Allocator* alloc) {
    PKDecoderWeights* w=(PKDecoderWeights*)weights;
    pk_nn_mlp_params(&w->species,alloc);pk_nn_mlp_params(&w->move,alloc);pk_nn_mlp_params(&w->query,alloc);
}
static void pk_decoder_init(void* weights,ulong* seed,cudaStream_t stream) {
    PKDecoderWeights* w=(PKDecoderWeights*)weights;
    pk_nn_mlp_init(&w->species,seed,stream);pk_nn_mlp_init(&w->move,seed,stream);pk_nn_mlp_init(&w->query,seed,stream);
}
static void pk_decoder_acts(void* weights,void* activations,Allocator* acts,Allocator* grads,int rows) {
    PKDecoderWeights* w=(PKDecoderWeights*)weights;PKDecoderActs* a=(PKDecoderActs*)activations;
    pk_nn_mlp_acts(&w->species,&a->species,acts,grads,PK_SPECIES_ROWS,false);
    pk_nn_mlp_acts(&w->move,&a->move,acts,grads,PK_MOVE_ROWS,false);
    pk_nn_mlp_acts(&w->query,&a->query,acts,grads,rows,true);
    a->table={.shape={PK_CANDIDATES,PK_EMBED}};alloc_register(acts,&a->table);
    a->queries={.shape={rows*PK_QUERIES,PK_EMBED}};alloc_register(acts,&a->queries);
    a->scores={.shape={rows*PK_QUERIES,PK_CANDIDATES}};alloc_register(acts,&a->scores);
    a->out={.shape={rows,169}};alloc_register(acts,&a->out);
    if(grads) {
        a->grad_scores={.shape={rows*PK_QUERIES,PK_CANDIDATES}};alloc_register(acts,&a->grad_scores);
        a->grad_queries={.shape={rows*PK_QUERIES,PK_EMBED}};alloc_register(acts,&a->grad_queries);
        a->grad_table={.shape={PK_CANDIDATES,PK_EMBED}};alloc_register(acts,&a->grad_table);
    }
}
static void pk_decoder_rollout(void* w,void* a,Allocator* alloc,int rows) {pk_decoder_acts(w,a,alloc,nullptr,rows);}
static void pk_decoder_bind(void* acts,PrecisionTensor obs) {((PKDecoderActs*)acts)->obs=obs;}
__global__ static void pk_merge_tables(precision_t* dst,const precision_t* species,const precision_t* moves) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<PK_CANDIDATES*PK_EMBED)dst[i]=i<PK_SPECIES_ROWS*PK_EMBED?species[i]:moves[i-PK_SPECIES_ROWS*PK_EMBED];
}
__global__ static void pk_decode_actions(precision_t* dst,const precision_t* obs,const precision_t* scores,const precision_t* query,int rows) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=rows*169)return;
    int r=i/169,action=i%169;
    const precision_t* q=query+r*PK_QUERY_OUT;
    if(action==168) {dst[i]=q[PK_QUERY_OUT-1];return;}
    const precision_t* o=obs+r*648;
    PKActionParts p=pk_action_parts(o,action);float v=0;
    for(int k=0;k<p.n;k++) v+=p.coefficient[k]*to_float(scores[(r*PK_QUERIES+p.query[k])*PK_CANDIDATES+p.candidate[k]])/sqrtf((float)PK_EMBED);
    for(int f=0;f<PK_DYN;f++) v+=pk_action_dynamic(o,action,f,&pk_gpu_move[0][0],&pk_gpu_chart[0][0])*to_float(q[PK_QUERIES*PK_EMBED+f]);
    if(p.scalar>=0)v+=to_float(q[PK_QUERIES*PK_EMBED+PK_DYN+p.scalar]);
    dst[i]=from_float(v);
}
static PrecisionTensor pk_decoder_forward(void* weights,void* activations,PrecisionTensor input,cudaStream_t stream) {
    PKDecoderWeights* w=(PKDecoderWeights*)weights;PKDecoderActs* a=(PKDecoderActs*)activations;
    int rows=a->out.shape[0];
    pk_static_forward(&w->species,&a->species,false,stream);pk_static_forward(&w->move,&a->move,true,stream);
    pk_nn_pack_aug<<<grid_size(rows*PK_AUG(w->hidden)),BLOCK_SIZE,0,stream>>>(a->query.input_aug.data,input.data,rows,w->hidden,false);
    pk_nn_mlp_forward(&w->query,&a->query,stream);
    pk_merge_tables<<<grid_size(PK_CANDIDATES*PK_EMBED),BLOCK_SIZE,0,stream>>>(a->table.data,a->species.out.data,a->move.out.data);
    pk_extract_gradient<<<grid_size(rows*PK_QUERIES*PK_EMBED),BLOCK_SIZE,0,stream>>>(
        a->queries.data,a->query.out.data,rows,PK_QUERIES*PK_EMBED,PK_QUERY_OUT,0);
    puf_mm(&a->queries,&a->table,&a->scores,stream);
    pk_decode_actions<<<grid_size(rows*169),BLOCK_SIZE,0,stream>>>(a->out.data,a->obs.data,a->scores.data,a->query.out.data,rows);
    return a->out;
}
__global__ static void pk_decode_gradient(precision_t* score_grad,precision_t* query_grad,const precision_t* obs,
        const float* logits,const float* value,int rows) {
    // One thread owns an observation row, so duplicate candidate contributions
    // (e.g. shared moves across switch targets) accumulate without atomics.
    int r=blockIdx.x*blockDim.x+threadIdx.x;
    if(r>=rows)return;
    const precision_t* o=obs+r*648;
    precision_t* sg=score_grad+r*PK_QUERIES*PK_CANDIDATES;
    precision_t* qg=query_grad+r*PK_QUERY_OUT;
    float dyn[PK_DYN]={0},scalar[3]={0};
    for(int action=0;action<168;action++) {
        float g=logits[r*168+action];if(g==0)continue;
        PKActionParts p=pk_action_parts(o,action);
        for(int k=0;k<p.n;k++) {
            int c=p.query[k]*PK_CANDIDATES+p.candidate[k];
            sg[c]=from_float(to_float(sg[c])+g*p.coefficient[k]/sqrtf((float)PK_EMBED));
        }
        for(int f=0;f<PK_DYN;f++)dyn[f]+=g*pk_action_dynamic(o,action,f,&pk_gpu_move[0][0],&pk_gpu_chart[0][0]);
        if(p.scalar>=0)scalar[p.scalar]+=g;
    }
    for(int f=0;f<PK_DYN;f++)qg[PK_QUERIES*PK_EMBED+f]=from_float(dyn[f]);
    for(int f=0;f<3;f++)qg[PK_QUERIES*PK_EMBED+PK_DYN+f]=from_float(scalar[f]);
    qg[PK_QUERY_OUT-1]=from_float(value[r]);
}
__global__ static void pk_restore_query_gradient(precision_t* dst,const precision_t* grad,int rows) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<rows*PK_QUERIES*PK_EMBED)dst[(i/(PK_QUERIES*PK_EMBED))*PK_QUERY_OUT+i%(PK_QUERIES*PK_EMBED)]=grad[i];
}
static PrecisionTensor pk_decoder_backward(void* weights,void* activations,FloatTensor logits,FloatTensor logstd,FloatTensor value,cudaStream_t stream) {
    (void)logstd;
    PKDecoderWeights* w=(PKDecoderWeights*)weights;PKDecoderActs* a=(PKDecoderActs*)activations;
    int rows=a->out.shape[0];
    cudaMemsetAsync(a->grad_scores.data,0,(size_t)rows*PK_QUERIES*PK_CANDIDATES*sizeof(precision_t),stream);
    pk_decode_gradient<<<grid_size(rows),BLOCK_SIZE,0,stream>>>(a->grad_scores.data,a->query.grad_out.data,a->obs.data,logits.data,value.data,rows);
    puf_mm_nn(&a->grad_scores,&a->table,&a->grad_queries,stream);
    puf_mm_tn(&a->grad_scores,&a->queries,&a->grad_table,stream);
    pk_restore_query_gradient<<<grid_size(rows*PK_QUERIES*PK_EMBED),BLOCK_SIZE,0,stream>>>(a->query.grad_out.data,a->grad_queries.data,rows);
    pk_extract_gradient<<<grid_size(PK_SPECIES_ROWS*PK_EMBED),BLOCK_SIZE,0,stream>>>(
        a->species.grad_out.data,a->grad_table.data,1,PK_SPECIES_ROWS*PK_EMBED,PK_CANDIDATES*PK_EMBED,0);
    pk_extract_gradient<<<grid_size(PK_MOVE_ROWS*PK_EMBED),BLOCK_SIZE,0,stream>>>(
        a->move.grad_out.data,a->grad_table.data,1,PK_MOVE_ROWS*PK_EMBED,PK_CANDIDATES*PK_EMBED,PK_SPECIES_ROWS*PK_EMBED);
    pk_nn_mlp_backward(&w->species,&a->species,a->species.grad_out,stream);
    pk_nn_mlp_backward(&w->move,&a->move,a->move.grad_out,stream);
    return pk_nn_mlp_backward(&w->query,&a->query,a->query.grad_out,stream);
}
static void create_pokemon_decoder(Decoder* dec) {
    dec->forward=pk_decoder_forward;dec->backward=pk_decoder_backward;
    dec->reg_train=pk_decoder_acts;dec->reg_rollout=pk_decoder_rollout;
    dec->reg_params=pk_decoder_params;dec->init_weights=pk_decoder_init;
    dec->create_weights=pk_decoder_weights;dec->activation_size=sizeof(PKDecoderActs);
    dec->bind_observation=pk_decoder_bind;
}
