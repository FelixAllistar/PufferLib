// Full-playfield semantic CNN + player MLP + shared entity encoder.
// Included by src/ocean.cu after the trainer's tensor/GEMM helpers.
#include "policy_shape.h"

struct FptEncoderWeights {Prec w[FPT_ENCODER_LAYERS],b[FPT_ENCODER_LAYERS];int hidden;};
struct FptEncoderActivations {
    Prec terrain,image[2],image_grad[2],terrain_features,terrain_grad;
    Prec player_input,player,player_grad,entities[2],entity_grad[2];
    Prec joined,joined_grad,out,saved_input,dw[FPT_ENCODER_LAYERS],db[FPT_ENCODER_LAYERS];
    Float weight_partials;
    Int maxima,active_rows,active_count;
};
static Prec fpt_matrix(Prec t,int rows,int cols) {return Prec{.data=t.data,.shape={rows,cols}};}
template<int Layer> __global__ void fpt_conv_bias_relu(
        precision_t* __restrict__ output, const precision_t* __restrict__ input,
        const precision_t* __restrict__ weights, const precision_t* __restrict__ bias,
        int B) {
    constexpr auto c=FPT_CONVS[Layer];
    constexpr int K=c.k*c.k*c.ic, N=c.oh*c.ow, Channels=8, Groups=c.oc/Channels;
    // Convert each weight once per block. The transposed shared layout permits
    // broadcasts across spatial positions without channel-group bank conflicts.
    __shared__ float w[K*c.oc];
    for(int i=threadIdx.x;i<K*c.oc;i+=blockDim.x)
        w[i]=to_float(weights[(i%c.oc)*K+i/c.oc]);
    __syncthreads();
    int i=blockIdx.x*blockDim.x+threadIdx.x, row=i/Groups;
    if(row>=B*N) return;
    int channel=(i%Groups)*Channels, b=row/N;
    int oy=row%N/c.ow,ox=row%c.ow;
    constexpr int stride=c.ih*c.iw*c.ic;
    constexpr int offset=0;
    float sums[Channels]={};
    // Leave the outer loop rolled to keep instruction-cache pressure modest.
    #pragma unroll 1
    for(int ky=0;ky<c.k;ky++) {
        int iy=oy*c.stride-c.pad+ky;
        #pragma unroll
        for(int kx=0;kx<c.k;kx++) {
            int ix=ox*c.stride-c.pad+kx;
            bool inside=(iy>=0&&iy<c.ih&&ix>=0&&ix<c.iw);
            #pragma unroll
            for(int ic=0;ic<c.ic;ic++) {
                float x=inside?to_float(input[b*stride+offset+(iy*c.iw+ix)*c.ic+ic]):0.0f;
                int k=(ky*c.k+kx)*c.ic+ic;
                #pragma unroll
                for(int ch=0;ch<Channels;ch++)
                    sums[ch]=fmaf(x,w[k*c.oc+channel+ch],sums[ch]);
            }
        }
    }
    #pragma unroll
    for(int ch=0;ch<Channels;ch++) {
        // The old GEMM stores precision_t before the separate bias kernel.
        float rounded=to_float(from_float(sums[ch]));
        output[row*c.oc+channel+ch]=from_float(fmaxf(0.0f,rounded+to_float(bias[channel+ch])));
    }
}
template<int Layer> static void fpt_conv_forward_fused(precision_t* out,
        const precision_t* in,const precision_t* w,const precision_t* b,int B,cudaStream_t stream) {
    constexpr auto c=FPT_CONVS[Layer];
    fpt_conv_bias_relu<Layer><<<grid_size(B*c.oh*c.ow*(c.oc/8)),BLOCK_SIZE,0,stream>>>(out,in,w,b,B);
}

static constexpr int FPT_DW_SPLIT_ROWS=1024;

// Split the long reduction over images/spatial positions into independent
// chunks. Each block multiplies a small implicit image-patch tile by dY in
// shared memory; no full im2col matrix is materialized. Float partials are
// reduced deterministically before the single final precision_t conversion.
template<int Layer> __global__ void fpt_conv_dw_partial(float* __restrict__ partial,
        const precision_t* __restrict__ input,const precision_t* __restrict__ grad,int B) {
    constexpr auto c=FPT_CONVS[Layer];
    constexpr int K=c.k*c.k*c.ic,N=c.oh*c.ow,Tile=32,Tiles=(K+Tile-1)/Tile;
    constexpr int Groups=c.oc/8,Width=c.oc*(K+1);
    constexpr int stride=c.ih*c.iw*c.ic;
    constexpr int offset=0;
    __shared__ float patch[Tile*Tile],dy[Tile*c.oc];
    int tile=blockIdx.x%Tiles,split=blockIdx.x/Tiles,t=threadIdx.x;
    int k=tile*Tile+t%Tile,ch=t/Tile;
    int begin=split*FPT_DW_SPLIT_ROWS,end=min(begin+FPT_DW_SPLIT_ROWS,B*N);
    float sums[Groups]={},bias_sum=0;
    for(int row0=begin;row0<end;row0+=Tile) {
        for(int j=t;j<Tile*Tile;j+=blockDim.x) {
            int r=row0+j/Tile,kk=tile*Tile+j%Tile;
            int b=r/N,oy=r%N/c.ow,ox=r%c.ow;
            int iy=oy*c.stride-c.pad+kk/(c.k*c.ic);
            int ix=ox*c.stride-c.pad+kk/c.ic%c.k;
            patch[j]=(r<B*N&&kk<K&&iy>=0&&iy<c.ih&&ix>=0&&ix<c.iw)
                ?to_float(input[b*stride+offset+(iy*c.iw+ix)*c.ic+kk%c.ic]):0.0f;
        }
        for(int j=t;j<Tile*c.oc;j+=blockDim.x) {
            int r=row0+j/c.oc;
            dy[j]=r<B*N?to_float(grad[r*c.oc+j%c.oc]):0.0f;
        }
        __syncthreads();
        #pragma unroll
        for(int r=0;r<Tile;r++) {
            float x=patch[r*Tile+t%Tile];
            #pragma unroll
            for(int group=0;group<Groups;group++)
                sums[group]=fmaf(x,dy[r*c.oc+ch+group*8],sums[group]);
        }
        if(tile==0&&t<c.oc) {
            #pragma unroll
            for(int r=0;r<Tile;r++) bias_sum+=dy[r*c.oc+t];
        }
        __syncthreads();
    }
    if(k<K) {
        #pragma unroll
        for(int group=0;group<Groups;group++)
            partial[split*Width+(ch+group*8)*K+k]=sums[group];
    }
    if(tile==0&&t<c.oc) partial[split*Width+c.oc*K+t]=bias_sum;
}

template<int Layer> __global__ void fpt_conv_dw_reduce(precision_t* dw,precision_t* db,
        const float* __restrict__ partial,int splits) {
    constexpr auto c=FPT_CONVS[Layer];
    constexpr int Weights=c.oc*c.k*c.k*c.ic,Width=Weights+c.oc;
    __shared__ float sums[256];
    int t=threadIdx.x,j=blockIdx.x*32+t%32,lane=t/32;
    float sum=0;
    if(j<Width) for(int s=lane;s<splits;s+=8) sum+=partial[s*Width+j];
    sums[t]=sum; __syncthreads();
    for(int stride=4;stride;stride/=2) {
        if(lane<stride) sums[t]+=sums[t+stride*32];
        __syncthreads();
    }
    if(lane==0&&j<Width) {
        if(j<Weights) dw[j]=from_float(sums[t]);
        else db[j-Weights]=from_float(sums[t]);
    }
}

// Gather input gradients directly. Preserve the old GEMM -> precision_t ->
// col2im rounding at each contributing patch before summing patch overlaps.
template<int Layer> __global__ void fpt_conv_dx(precision_t* __restrict__ dx,
        const precision_t* __restrict__ grad,const precision_t* __restrict__ weights,int B) {
    constexpr auto c=FPT_CONVS[Layer];
    constexpr int K=c.k*c.k*c.ic;
    __shared__ float w[c.oc*K];
    for(int j=threadIdx.x;j<c.oc*K;j+=blockDim.x) w[j]=to_float(weights[j]);
    __syncthreads();
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=B*c.ih*c.iw*c.ic) return;
    int ic=i%c.ic,ix=i/c.ic%c.iw,iy=i/c.ic/c.iw%c.ih,b=i/(c.ic*c.iw*c.ih);
    float sum=0;
    #pragma unroll
    for(int ky=0;ky<c.k;ky++) {
        int sy=iy+c.pad-ky;
        if(sy<0||sy%c.stride||sy/c.stride>=c.oh) continue;
        #pragma unroll
        for(int kx=0;kx<c.k;kx++) {
            int sx=ix+c.pad-kx;
            if(sx<0||sx%c.stride||sx/c.stride>=c.ow) continue;
            int r=(b*c.oh+sy/c.stride)*c.ow+sx/c.stride,k=(ky*c.k+kx)*c.ic+ic;
            float dot=0;
            #pragma unroll
            for(int oc=0;oc<c.oc;oc++) dot=fmaf(to_float(grad[r*c.oc+oc]),w[oc*K+k],dot);
            sum+=to_float(from_float(dot));
        }
    }
    dx[i]=from_float(sum);
}

template<int Layer> static void fpt_conv_backward_fused(FptEncoderWeights* ew,
        FptEncoderActivations* a,int B,cudaStream_t stream) {
    constexpr auto c=FPT_CONVS[Layer];
    constexpr int K=c.k*c.k*c.ic;
    int splits=(B*c.oh*c.ow+FPT_DW_SPLIT_ROWS-1)/FPT_DW_SPLIT_ROWS;
    fpt_conv_dw_partial<Layer><<<((K+31)/32)*splits,BLOCK_SIZE,0,stream>>>(
        a->weight_partials.data,Layer?a->image[Layer-1].data:a->terrain.data,
        a->image_grad[Layer].data,B);
    fpt_conv_dw_reduce<Layer><<<(c.oc*(K+1)+31)/32,BLOCK_SIZE,0,stream>>>(
        a->dw[Layer].data,a->db[Layer].data,a->weight_partials.data,splits);
    if constexpr (Layer>0)
        fpt_conv_dx<Layer><<<grid_size(B*c.ih*c.iw*c.ic),BLOCK_SIZE,0,stream>>>(
            a->image_grad[Layer-1].data,a->image_grad[Layer].data,ew->w[Layer].data,B);
}
__global__ void fpt_bias_relu(precision_t* x,const precision_t* bias,int n,int C) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n) x[i]=from_float(fmaxf(0.0f,to_float(x[i])+to_float(bias[i%C])));
}
__global__ void fpt_relu_grad(precision_t* g,const precision_t* out,int n) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n&&to_float(out[i])<=0) g[i]=from_float(0.0f);
}
// Parallel reduction over rows; accumulate in float even with bf16 storage.
__global__ void fpt_bias_grad(precision_t* db,const precision_t* g,int rows,int C) {
    __shared__ float sums[256];
    int ch=blockIdx.x,t=threadIdx.x; float v=0;
    for(int r=t;r<rows;r+=blockDim.x) v+=to_float(g[r*C+ch]);
    sums[t]=v; __syncthreads();
    for(int n=128;n;n/=2) { if(t<n) sums[t]+=sums[t+n]; __syncthreads(); }
    if(t==0) db[ch]=from_float(sums[0]);
}

__global__ void fpt_prepare(precision_t* terrain,precision_t* player,const precision_t* obs,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<B*FPT_PLAYER_FEATURES)player[i]=obs[(i/FPT_PLAYER_FEATURES)*FPT_OBS+i%FPT_PLAYER_FEATURES];
    int size=FPT_GRID_CELLS*FPT_TERRAIN_CHANNELS;
    if(i>=B*size)return;
    int b=i/size,cell=i/FPT_TERRAIN_CHANNELS%FPT_GRID_CELLS,ch=i%FPT_TERRAIN_CHANNELS;
    const precision_t* o=obs+b*FPT_OBS;
    terrain[i]=from_float(fpt_terrain_feature((int)to_float(o[FPT_TERRAIN_OFFSET+cell]),ch,cell,
        to_float(o[FPT_P_GRID_DX]),to_float(o[FPT_P_ANCHOR_Y]),to_float(o[FPT_P_SCROLL_FRACTION])));
}

// One warp per object, skip absent objects, and consume the categorical type
// by selecting its weight column. No one-hot tensor is needed during rollout.
__global__ void fpt_entities_forward(precision_t* first,precision_t* second,
        const precision_t* obs,const precision_t* w0,const precision_t* b0,
        const precision_t* w1,const precision_t* b1,int B) {
    int t=blockIdx.x*blockDim.x+threadIdx.x,row=t/32,ch=t%32;
    if(row>=B*FPT_ENTITY_COUNT)return;
    const precision_t* e=obs+(row/FPT_ENTITY_COUNT)*FPT_OBS+FPT_ENTITY_OFFSET+
        (row%FPT_ENTITY_COUNT)*FPT_ENTITY_FEATURES;
    int type=(int)to_float(e[0]);int at=row*32+ch;
    if(type<=0||type>=FPT_ENTITY_TYPES){first[at]=second[at]=from_float(0);return;}
    float sum=to_float(w0[ch*FPT_ENTITY_INPUTS+FPT_ENTITY_FEATURES-1+type]);
    #pragma unroll
    for(int f=0;f<FPT_ENTITY_FEATURES-1;f++)
        sum=fmaf(to_float(e[1+f]),to_float(w0[ch*FPT_ENTITY_INPUTS+f]),sum);
    float h=to_float(from_float(fmaxf(0,to_float(from_float(sum))+to_float(b0[ch]))));
    first[at]=from_float(h);sum=0;
    #pragma unroll
    for(int f=0;f<32;f++)sum=fmaf(__shfl_sync(0xffffffff,h,f),to_float(w1[ch*32+f]),sum);
    second[at]=from_float(fmaxf(0,to_float(from_float(sum))+to_float(b1[ch])));
}
__global__ void fpt_join(precision_t* joined,int* maxima,const precision_t* terrain,
        const precision_t* player,const precision_t* entities,const precision_t* obs,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=B*FPT_JOINED)return;
    int b=i/FPT_JOINED,f=i%FPT_JOINED;
    if(f<FPT_TERRAIN_FEATURES){joined[i]=terrain[b*FPT_TERRAIN_FEATURES+f];return;}
    f-=FPT_TERRAIN_FEATURES;
    if(f<FPT_PLAYER_HIDDEN){joined[i]=player[b*FPT_PLAYER_HIDDEN+f];return;}
    f-=FPT_PLAYER_HIDDEN;int ch=f%FPT_ENTITY_HIDDEN,count=0,best=-1;float sum=0,maximum=0;
    for(int k=0;k<FPT_ENTITY_COUNT;k++) {
        int type=(int)to_float(obs[b*FPT_OBS+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES]);
        if(type<=0||type>=FPT_ENTITY_TYPES)continue;
        float v=to_float(entities[(b*FPT_ENTITY_COUNT+k)*FPT_ENTITY_HIDDEN+ch]);count++;sum+=v;
        if(best<0||v>maximum){maximum=v;best=k;}
    }
    if(f<FPT_ENTITY_HIDDEN){joined[i]=from_float(maximum);maxima[b*FPT_ENTITY_HIDDEN+ch]=best;}
    else if(f<2*FPT_ENTITY_HIDDEN)joined[i]=from_float(count?sum/count:0);
    else joined[i]=from_float(count*(1.0f/128));
}
__global__ void fpt_split(precision_t* terrain,precision_t* player,const precision_t* joined,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<B*FPT_TERRAIN_FEATURES)terrain[i]=joined[(i/FPT_TERRAIN_FEATURES)*FPT_JOINED+i%FPT_TERRAIN_FEATURES];
    if(i<B*FPT_PLAYER_HIDDEN)player[i]=joined[(i/FPT_PLAYER_HIDDEN)*FPT_JOINED+FPT_TERRAIN_FEATURES+i%FPT_PLAYER_HIDDEN];
}
__global__ void fpt_entity_unpool(precision_t* g,const precision_t* joined_grad,
        const precision_t* joined,const precision_t* obs,const precision_t* second,
        const int* maxima,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=B*FPT_ENTITY_COUNT*FPT_ENTITY_HIDDEN)return;
    int ch=i%32,k=i/32%FPT_ENTITY_COUNT,b=i/32/FPT_ENTITY_COUNT;
    int type=(int)to_float(obs[b*FPT_OBS+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES]);
    if(type<=0||type>=FPT_ENTITY_TYPES||to_float(second[i])<=0){g[i]=from_float(0);return;}
    int count=(int)(to_float(joined[b*FPT_JOINED+FPT_JOINED-1])*128+0.5f);
    int start=b*FPT_JOINED+FPT_TERRAIN_FEATURES+FPT_PLAYER_HIDDEN;
    float v=count?to_float(joined_grad[start+32+ch])/count:0;
    if(maxima[b*32+ch]==k)v+=to_float(joined_grad[start+ch]);
    g[i]=from_float(v);
}
// Compact only indices, on the device. Sparse scenes must not pay for dense
// matrix products over all padded slots (especially the empty near-flag band).
__global__ void fpt_entity_compact(int* rows,int* count,const precision_t* obs,int B) {
    int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=B*FPT_ENTITY_COUNT)return;
    int type=(int)to_float(obs[(row/FPT_ENTITY_COUNT)*FPT_OBS+FPT_ENTITY_OFFSET+(row%FPT_ENTITY_COUNT)*FPT_ENTITY_FEATURES]);
    if(type>0&&type<FPT_ENTITY_TYPES)rows[atomicAdd(count,1)]=row;
}
__global__ void fpt_entity_first_grad(precision_t* grad,const precision_t* second_grad,
        const precision_t* first,const precision_t* weights,const int* rows,const int* count) {
    int lane=threadIdx.x%32,warp=(blockIdx.x*blockDim.x+threadIdx.x)/32;
    for(int r=warp;r<*count;r+=gridDim.x*blockDim.x/32) {
        int row=rows[r];float dy=to_float(second_grad[row*32+lane]),sum=0;
        #pragma unroll
        for(int ch=0;ch<32;ch++)sum=fmaf(__shfl_sync(0xffffffff,dy,ch),to_float(weights[ch*32+lane]),sum);
        grad[row*32+lane]=from_float(to_float(first[row*32+lane])>0?sum:0);
    }
}
static constexpr int FPT_ENTITY_DW_ROWS=256;
template<int Layer> __global__ void fpt_entity_dw_partial(float* partial,
        const precision_t* obs,const precision_t* first,const precision_t* grad,
        const int* rows,const int* count) {
    constexpr int K=Layer==4?FPT_ENTITY_INPUTS:FPT_ENTITY_HIDDEN,Width=32*(K+1);
    int j=blockIdx.x*blockDim.x+threadIdx.x,begin=blockIdx.y*FPT_ENTITY_DW_ROWS;
    int end=min(begin+FPT_ENTITY_DW_ROWS,*count);if(j>=Width||begin>=end)return;
    int ch=j/(K+1),f=j%(K+1);float sum=0;
    for(int r=begin;r<end;r++) {
        int row=rows[r];float x=1;
        if(f<K) {
            if constexpr (Layer==4) {
                const precision_t* e=obs+(row/FPT_ENTITY_COUNT)*FPT_OBS+FPT_ENTITY_OFFSET+(row%FPT_ENTITY_COUNT)*FPT_ENTITY_FEATURES;
                if(f<FPT_ENTITY_FEATURES-1)x=to_float(e[f+1]);
                else {if((int)to_float(e[0])!=f-(FPT_ENTITY_FEATURES-1))continue;}
            } else x=to_float(first[row*32+f]);
        }
        sum=fmaf(x,to_float(grad[row*32+ch]),sum);
    }
    partial[blockIdx.y*Width+j]=sum;
}
template<int Layer> __global__ void fpt_entity_dw_reduce(precision_t* dw,precision_t* db,
        const float* partial,const int* count) {
    constexpr int K=Layer==4?FPT_ENTITY_INPUTS:FPT_ENTITY_HIDDEN,Width=32*(K+1);
    int j=blockIdx.x*blockDim.x+threadIdx.x;if(j>=Width)return;
    int splits=(*count+FPT_ENTITY_DW_ROWS-1)/FPT_ENTITY_DW_ROWS;float sum=0;
    for(int s=0;s<splits;s++)sum+=partial[s*Width+j];
    int ch=j/(K+1),f=j%(K+1);
    if(f<K)dw[ch*K+f]=from_float(sum);else db[ch]=from_float(sum);
}
template<int Layer> static void fpt_entity_weight_grad(FptEncoderWeights* ew,FptEncoderActivations* a,int B,cudaStream_t stream) {
    constexpr int K=Layer==4?FPT_ENTITY_INPUTS:FPT_ENTITY_HIDDEN,Width=32*(K+1);
    int splits=(B*FPT_ENTITY_COUNT+FPT_ENTITY_DW_ROWS-1)/FPT_ENTITY_DW_ROWS;
    fpt_entity_dw_partial<Layer><<<dim3(grid_size(Width),splits),BLOCK_SIZE,0,stream>>>(
        a->weight_partials.data,a->saved_input.data,a->entities[0].data,a->entity_grad[Layer-4].data,
        a->active_rows.data,a->active_count.data);
    fpt_entity_dw_reduce<Layer><<<grid_size(Width),BLOCK_SIZE,0,stream>>>(
        a->dw[Layer].data,a->db[Layer].data,a->weight_partials.data,a->active_count.data);
}
static void fpt_linear_forward(FptEncoderWeights* ew,int layer,Prec input,Prec out,cudaStream_t stream) {
    puf_mm(&input,&ew->w[layer],&out,stream);
    fpt_bias_relu<<<grid_size(numel(out.shape)),BLOCK_SIZE,0,stream>>>(out.data,ew->b[layer].data,
        numel(out.shape),out.shape[1]);
}
static Prec fpt_encoder_forward(void* w,void* activations,Prec input,cudaStream_t stream) {
    auto* ew=(FptEncoderWeights*)w;auto* a=(FptEncoderActivations*)activations;int B=input.shape[0];
    a->saved_input=input;
    fpt_prepare<<<grid_size(B*FPT_GRID_CELLS*FPT_TERRAIN_CHANNELS),BLOCK_SIZE,0,stream>>>(
        a->terrain.data,a->player_input.data,input.data,B);
    fpt_conv_forward_fused<0>(a->image[0].data,a->terrain.data,ew->w[0].data,ew->b[0].data,B,stream);
    fpt_conv_forward_fused<1>(a->image[1].data,a->image[0].data,ew->w[1].data,ew->b[1].data,B,stream);
    fpt_linear_forward(ew,2,a->image[1],a->terrain_features,stream);
    fpt_linear_forward(ew,3,a->player_input,a->player,stream);
    fpt_entities_forward<<<grid_size(B*FPT_ENTITY_COUNT*32),BLOCK_SIZE,0,stream>>>(
        a->entities[0].data,a->entities[1].data,input.data,ew->w[4].data,ew->b[4].data,ew->w[5].data,ew->b[5].data,B);
    fpt_join<<<grid_size(B*FPT_JOINED),BLOCK_SIZE,0,stream>>>(a->joined.data,a->maxima.data,
        a->terrain_features.data,a->player.data,a->entities[1].data,input.data,B);
    fpt_linear_forward(ew,6,a->joined,a->out,stream);return a->out;
}
static void fpt_linear_backward(FptEncoderWeights* ew,FptEncoderActivations* a,int layer,
        Prec input,Prec grad,Prec* input_grad,cudaStream_t stream) {
    puf_mm_tn(&grad,&input,&a->dw[layer],stream);
    fpt_bias_grad<<<grad.shape[1],256,0,stream>>>(a->db[layer].data,grad.data,grad.shape[0],grad.shape[1]);
    if(input_grad)puf_mm_nn(&grad,&ew->w[layer],input_grad,stream);
}
static void fpt_encoder_backward(void* w,void* activations,Prec grad,cudaStream_t stream) {
    auto* ew=(FptEncoderWeights*)w;auto* a=(FptEncoderActivations*)activations;int B=grad.shape[0];
    fpt_relu_grad<<<grid_size(B*ew->hidden),BLOCK_SIZE,0,stream>>>(grad.data,a->out.data,B*ew->hidden);
    fpt_linear_backward(ew,a,6,a->joined,grad,&a->joined_grad,stream);
    fpt_split<<<grid_size(B*FPT_TERRAIN_FEATURES),BLOCK_SIZE,0,stream>>>(
        a->terrain_grad.data,a->player_grad.data,a->joined_grad.data,B);
    fpt_relu_grad<<<grid_size(B*FPT_TERRAIN_FEATURES),BLOCK_SIZE,0,stream>>>(
        a->terrain_grad.data,a->terrain_features.data,B*FPT_TERRAIN_FEATURES);
    fpt_linear_backward(ew,a,2,a->image[1],a->terrain_grad,&a->image_grad[1],stream);
    fpt_relu_grad<<<grid_size(B*FPT_PLAYER_HIDDEN),BLOCK_SIZE,0,stream>>>(
        a->player_grad.data,a->player.data,B*FPT_PLAYER_HIDDEN);
    fpt_linear_backward(ew,a,3,a->player_input,a->player_grad,nullptr,stream);
    fpt_entity_unpool<<<grid_size(B*FPT_ENTITY_COUNT*32),BLOCK_SIZE,0,stream>>>(
        a->entity_grad[1].data,a->joined_grad.data,a->joined.data,a->saved_input.data,
        a->entities[1].data,a->maxima.data,B);
    cudaMemsetAsync(a->active_count.data,0,sizeof(int),stream);
    fpt_entity_compact<<<grid_size(B*FPT_ENTITY_COUNT),BLOCK_SIZE,0,stream>>>(a->active_rows.data,a->active_count.data,a->saved_input.data,B);
    fpt_entity_first_grad<<<128,BLOCK_SIZE,0,stream>>>(a->entity_grad[0].data,a->entity_grad[1].data,
        a->entities[0].data,ew->w[5].data,a->active_rows.data,a->active_count.data);
    fpt_entity_weight_grad<5>(ew,a,B,stream);fpt_entity_weight_grad<4>(ew,a,B,stream);
    for(int l=1;l>=0;l--) {
        fpt_relu_grad<<<grid_size(B*FPT_CNN_FLAT),BLOCK_SIZE,0,stream>>>(
            a->image_grad[l].data,a->image[l].data,B*FPT_CNN_FLAT);
        if(l)fpt_conv_backward_fused<1>(ew,a,B,stream);else fpt_conv_backward_fused<0>(ew,a,B,stream);
    }
}
static void fpt_encoder_init(void* w,ulong* seed,cudaStream_t stream) {
    auto* ew=(FptEncoderWeights*)w;
    for(int l=0;l<FPT_ENCODER_LAYERS;l++) {
        puf_kaiming_init(&ew->w[l],sqrtf(2.0f),(*seed)++,stream);
        cudaMemsetAsync(ew->b[l].data,0,numel(ew->b[l].shape)*sizeof(precision_t),stream);
    }
}
static void fpt_encoder_params(void* w,Allocator* alloc) {
    auto* ew=(FptEncoderWeights*)w;
    for(int l=0;l<FPT_ENCODER_LAYERS;l++){alloc_register(alloc,&ew->w[l]);alloc_register(alloc,&ew->b[l]);}
}
static void fpt_encoder_buffers(FptEncoderWeights* ew,FptEncoderActivations* a,Allocator* acts,int B,bool train) {
    *a={};
    a->terrain={.shape={B,FPT_GRID_CELLS*FPT_TERRAIN_CHANNELS}};alloc_register(acts,&a->terrain);
    for(int l=0;l<2;l++){
        a->image[l]={.shape={B,FPT_CNN_FLAT}};alloc_register(acts,&a->image[l]);
        a->entities[l]={.shape={B*FPT_ENTITY_COUNT,FPT_ENTITY_HIDDEN}};alloc_register(acts,&a->entities[l]);
        if(train){
            a->image_grad[l]=a->image[l];alloc_register(acts,&a->image_grad[l]);
            a->entity_grad[l]=a->entities[l];alloc_register(acts,&a->entity_grad[l]);
        }
    }
    a->terrain_features={.shape={B,FPT_TERRAIN_FEATURES}};
    a->player_input={.shape={B,FPT_PLAYER_FEATURES}};a->player={.shape={B,FPT_PLAYER_HIDDEN}};
    a->joined={.shape={B,FPT_JOINED}};a->out={.shape={B,ew->hidden}};
    a->maxima={.shape={B,FPT_ENTITY_HIDDEN}};
    for(auto* t:{&a->terrain_features,&a->player_input,&a->player,&a->joined,&a->out})alloc_register(acts,t);
    alloc_register(acts,&a->maxima);
    if(train) {
        a->terrain_grad=a->terrain_features;a->player_grad=a->player;a->joined_grad=a->joined;
        for(auto* t:{&a->terrain_grad,&a->player_grad,&a->joined_grad})alloc_register(acts,t);
        a->active_rows={.shape={B*FPT_ENTITY_COUNT}};a->active_count={.shape={1}};
        alloc_register(acts,&a->active_rows);alloc_register(acts,&a->active_count);
        int splits=(B*FPT_MAP_CELLS+FPT_DW_SPLIT_ROWS-1)/FPT_DW_SPLIT_ROWS;
        int conv_size=splits*FPT_CONV_CHANNELS*(9*FPT_TERRAIN_CHANNELS+1);
        int entity_size=((B*FPT_ENTITY_COUNT+FPT_ENTITY_DW_ROWS-1)/FPT_ENTITY_DW_ROWS)*32*(FPT_ENTITY_INPUTS+1);
        a->weight_partials={.shape={std::max(conv_size,entity_size)}};
        alloc_register(acts,&a->weight_partials);
    }
}
static void fpt_encoder_train(void* w,void* activations,Allocator* acts,Allocator* grads,int B) {
    auto* ew=(FptEncoderWeights*)w;auto* a=(FptEncoderActivations*)activations;
    fpt_encoder_buffers(ew,a,acts,B,true);
    for(int l=0;l<FPT_ENCODER_LAYERS;l++){
        a->dw[l]=ew->w[l];a->dw[l].data=nullptr;a->db[l]=ew->b[l];a->db[l].data=nullptr;
        alloc_register(grads,&a->dw[l]);alloc_register(grads,&a->db[l]);
    }
}
static void fpt_encoder_rollout(void* w,void* activations,Allocator* alloc,int B) {
    fpt_encoder_buffers((FptEncoderWeights*)w,(FptEncoderActivations*)activations,alloc,B,false);
}
static void* fpt_encoder_create(void* self) {
    auto* e=(Encoder*)self;
    if(e->in_dim!=FPT_OBS||e->out_dim<32||e->out_dim%32){
        fprintf(stderr,"FPG semantic encoder v%d requires %d observations and hidden_size divisible by 32\n",
            FPT_OBSERVATION_VERSION,FPT_OBS);exit(1);
    }
    auto* ew=(FptEncoderWeights*)calloc(1,sizeof(FptEncoderWeights));ew->hidden=e->out_dim;
    for(int l=0;l<FPT_ENCODER_LAYERS;l++){
        ew->w[l]={.shape={fpt_encoder_rows(l,ew->hidden),fpt_encoder_cols(l)}};
        ew->b[l]={.shape={fpt_encoder_rows(l,ew->hidden)}};
    }
    return ew;
}
static void create_fpt_encoder(Encoder* e) {
    *e=Encoder{.forward=fpt_encoder_forward,.backward=fpt_encoder_backward,
        .init_weights=fpt_encoder_init,.reg_params=fpt_encoder_params,.reg_train=fpt_encoder_train,
        .reg_rollout=fpt_encoder_rollout,.create_weights=fpt_encoder_create,.in_dim=e->in_dim,
        .out_dim=e->out_dim,.activation_size=sizeof(FptEncoderActivations)};
}
