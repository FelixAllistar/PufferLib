// Lossless compact actor observations -> categorical weight lookups. The
// weight table has the original dense feature columns; IDs/bitsets never enter
// the policy as ordinal numeric inputs. PPO expands only its update minibatch.
#ifndef PRECISION_FLOAT
#error "Lossless Gen 9 compact categorical words require --float"
#endif
struct PG9Block {int dense,width,compact,compact_width,kind;};
__device__ __constant__ static PG9Block pg9_blocks[PG9_COMPACT_BLOCK_COUNT]=PG9_COMPACT_BLOCKS_DATA;
struct PG9EncoderActs {EncoderActivations linear;Prec expanded;};
__device__ static float pg9_unpack_feature(const precision_t* input,int feature){
    int left=0,right=PG9_COMPACT_BLOCK_COUNT;
    while(left+1<right){int mid=(left+right)/2;if(pg9_blocks[mid].dense<=feature)left=mid;else right=mid;}
    PG9Block b=pg9_blocks[left];int i=feature-b.dense;
    if(b.kind==0)return input[b.compact+i];
    if(b.kind==1)return input[b.compact]==i+1;
    return (((unsigned)input[b.compact+(i>>4)])>>(i&15))&1u;
}
__global__ static void pg9_expand(precision_t* dst,const precision_t* src,int rows){
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<rows*PG9_DENSE_OBS)dst[i]=pg9_unpack_feature(src+(i/PG9_DENSE_OBS)*PG9_OBS,i%PG9_DENSE_OBS);
}
__global__ static void pg9_sparse_forward(precision_t* dst,const precision_t* src,const precision_t* weights,int rows,int hidden){
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=rows*hidden)return;
    const precision_t* input=src+(i/hidden)*PG9_OBS;
    const precision_t* w=weights+(i%hidden)*PG9_DENSE_OBS;float sum=0;
    for(int j=0;j<PG9_COMPACT_BLOCK_COUNT;j++){
        PG9Block b=pg9_blocks[j];
        if(b.kind==0){for(int k=0;k<b.width;k++)sum+=input[b.compact+k]*w[b.dense+k];}
        else if(b.kind==1){int id=(int)input[b.compact];if(id)sum+=w[b.dense+id-1];}
        else for(int word=0;word<b.compact_width;word++){
            unsigned bits=(unsigned)input[b.compact+word];
            while(bits){int bit=__ffs(bits)-1;int feature=16*word+bit;if(feature<b.width)sum+=w[b.dense+feature];bits&=bits-1;}
        }
    }
    dst[i]=sum;
}
static void* pg9_encoder_weights(void* self){
    Encoder dense=*(Encoder*)self;
    if(dense.in_dim!=PG9_OBS){fprintf(stderr,"Gen 9 compact encoder schema mismatch\n");exit(1);}
    dense.in_dim=PG9_DENSE_OBS;return encoder_create_weights(&dense);
}
static void pg9_encoder_train(void* weights,void* activations,Allocator* acts,Allocator* grads,int rows){
    PG9EncoderActs* a=(PG9EncoderActs*)activations;*a={};
    encoder_reg_train(weights,&a->linear,acts,grads,rows);
    // Reuse the saved-input allocation for expansion and backward; no second
    // dense minibatch copy, and no dense rollout/history buffer.
    a->expanded=a->linear.saved_input;
}
static void pg9_encoder_rollout(void* weights,void* activations,Allocator* acts,int rows){
    PG9EncoderActs* a=(PG9EncoderActs*)activations;*a={};
    encoder_reg_rollout(weights,&a->linear,acts,rows);
}
static Prec pg9_encoder_forward(void* weights,void* activations,Prec input,cudaStream_t stream){
    PG9EncoderActs* a=(PG9EncoderActs*)activations;EncoderWeights* w=(EncoderWeights*)weights;
    int rows=a->linear.out.shape[0];
    // alloc_create fills registered views after reg_train. Bind the alias only
    // after those device pointers exist, including during graph capture.
    a->expanded=a->linear.saved_input;
    if(a->expanded.data){
        pg9_expand<<<grid_size(rows*PG9_DENSE_OBS),BLOCK_SIZE,0,stream>>>(a->expanded.data,input.data,rows);
        puf_mm(&a->expanded,&w->weight,&a->linear.out,stream);
    }else{
        pg9_sparse_forward<<<grid_size(rows*w->out_dim),BLOCK_SIZE,0,stream>>>(a->linear.out.data,input.data,w->weight.data,rows,w->out_dim);
    }
    return a->linear.out;
}
static void pg9_encoder_backward(void* weights,void* activations,Prec gradient,cudaStream_t stream){
    encoder_backward(weights,&((PG9EncoderActs*)activations)->linear,gradient,stream);
}
static void create_pg9_batch_encoder(Encoder* encoder){
    encoder->create_weights=pg9_encoder_weights;encoder->reg_train=pg9_encoder_train;
    encoder->reg_rollout=pg9_encoder_rollout;encoder->forward=pg9_encoder_forward;
    encoder->backward=pg9_encoder_backward;encoder->activation_size=sizeof(PG9EncoderActs);
}
