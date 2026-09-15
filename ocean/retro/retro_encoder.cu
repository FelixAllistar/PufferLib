// CUDA CNN/RAM encoder. Included by src/ocean.cu, after GEMM/allocator helpers.
#include "retro_policy_shape.h"

struct RetroEncoderWeights { PrecisionTensor w[5],b[5]; int hidden; };
struct RetroEncoderActivations {
    PrecisionTensor image[3], image_grad[3], ram_input, ram, ram_grad;
    PrecisionTensor joined, joined_grad, out, saved_input, columns;
    PrecisionTensor dw[5],db[5];
};
static PrecisionTensor retro_matrix(PrecisionTensor t,int rows,int cols) {
    return PrecisionTensor{.data=t.data,.shape={rows,cols}};
}
__global__ void retro_im2col(precision_t* col,const precision_t* input,
        RetroConvShape c,int B,int batch_stride,int offset) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    int K=c.k*c.k*c.ic,N=c.oh*c.ow;
    if(i>=B*N*K) return;
    int kernel=i%K,row=i/K,ch=kernel%c.ic;
    int ky=kernel/c.ic/c.k,kx=kernel/c.ic%c.k;
    int y=row/N,oy=row%N/c.ow,ox=row%c.ow;
    int iy=oy*c.stride-c.pad+ky,ix=ox*c.stride-c.pad+kx;
    col[i]=(iy>=0&&iy<c.ih&&ix>=0&&ix<c.iw)
        ?input[y*batch_stride+offset+(iy*c.iw+ix)*c.ic+ch]:from_float(0.0f);
}
__global__ void retro_col2im(precision_t* dx,const precision_t* col,
        RetroConvShape c,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=B*c.ih*c.iw*c.ic) return;
    int ch=i%c.ic,ix=i/c.ic%c.iw,iy=i/c.ic/c.iw%c.ih,b=i/(c.ic*c.iw*c.ih);
    int K=c.k*c.k*c.ic; float sum=0;
    for(int ky=0;ky<c.k;ky++) for(int kx=0;kx<c.k;kx++) {
        int sy=iy+c.pad-ky,sx=ix+c.pad-kx;
        if(sy<0||sx<0||sy%c.stride||sx%c.stride) continue;
        int oy=sy/c.stride,ox=sx/c.stride;
        if(oy<c.oh&&ox<c.ow)
            sum+=to_float(col[((b*c.oh+oy)*c.ow+ox)*K+(ky*c.k+kx)*c.ic+ch]);
    }
    dx[i]=from_float(sum);
}
__global__ void retro_bias_relu(precision_t* x,const precision_t* bias,int n,int C) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n) x[i]=from_float(fmaxf(0.0f,to_float(x[i])+to_float(bias[i%C])));
}
__global__ void retro_relu_grad(precision_t* g,const precision_t* out,int n) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n&&to_float(out[i])<=0) g[i]=from_float(0.0f);
}
// Parallel reduction over rows; accumulate in float even with bf16 storage.
__global__ void retro_bias_grad(precision_t* db,const precision_t* g,int rows,int C) {
    __shared__ float sums[256];
    int ch=blockIdx.x,t=threadIdx.x; float v=0;
    for(int r=t;r<rows;r+=blockDim.x) v+=to_float(g[r*C+ch]);
    sums[t]=v; __syncthreads();
    for(int n=128;n;n/=2) { if(t<n) sums[t]+=sums[t+n]; __syncthreads(); }
    if(t==0) db[ch]=from_float(sums[0]);
}
__global__ void retro_ram_gather(precision_t* ram,const precision_t* obs,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<B*RETRO_RAM_INPUTS)
        ram[i]=obs[(i/RETRO_RAM_INPUTS)*RETRO_POLICY_INPUTS+i%RETRO_RAM_INPUTS];
}
__global__ void retro_join(precision_t* joined,const precision_t* image,
        const precision_t* ram,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=B*RETRO_FUSED_FEATURES) return;
    int b=i/RETRO_FUSED_FEATURES,f=i%RETRO_FUSED_FEATURES;
    joined[i]=f<RETRO_CNN_FEATURES?image[b*RETRO_CNN_FEATURES+f]
        :ram[b*RETRO_RAM_HIDDEN+f-RETRO_CNN_FEATURES];
}
__global__ void retro_split(precision_t* image,precision_t* ram,
        const precision_t* joined,int B) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=B*RETRO_FUSED_FEATURES) return;
    int b=i/RETRO_FUSED_FEATURES,f=i%RETRO_FUSED_FEATURES;
    if(f<RETRO_CNN_FEATURES) image[b*RETRO_CNN_FEATURES+f]=joined[i];
    else ram[b*RETRO_RAM_HIDDEN+f-RETRO_CNN_FEATURES]=joined[i];
}
static PrecisionTensor retro_encoder_forward(void* w,void* activations,
        PrecisionTensor input,cudaStream_t stream) {
    auto* ew=(RetroEncoderWeights*)w; auto* a=(RetroEncoderActivations*)activations;
    int B=input.shape[0];
    if(a->saved_input.data) puf_copy(&a->saved_input,&input,stream);
    for(int l=0;l<3;l++) {
        auto c=RETRO_CONVS[l]; int N=c.oh*c.ow,K=c.k*c.k*c.ic;
        auto col=retro_matrix(a->columns,B*N,K);
        auto out=retro_matrix(a->image[l],B*N,c.oc);
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
static void retro_encoder_backward(void* w,void* activations,
        PrecisionTensor grad,cudaStream_t stream) {
    auto* ew=(RetroEncoderWeights*)w; auto* a=(RetroEncoderActivations*)activations;
    int B=grad.shape[0];
    retro_relu_grad<<<grid_size(B*ew->hidden),BLOCK_SIZE,0,stream>>>(grad.data,a->out.data,B*ew->hidden);
    puf_mm_tn(&grad,&a->joined,&a->dw[4],stream);
    retro_bias_grad<<<ew->hidden,256,0,stream>>>(a->db[4].data,grad.data,B,ew->hidden);
    puf_mm_nn(&grad,&ew->w[4],&a->joined_grad,stream);
    retro_split<<<grid_size(B*RETRO_FUSED_FEATURES),BLOCK_SIZE,0,stream>>>(a->image_grad[2].data,a->ram_grad.data,a->joined_grad.data,B);
    retro_relu_grad<<<grid_size(B*RETRO_RAM_HIDDEN),BLOCK_SIZE,0,stream>>>(a->ram_grad.data,a->ram.data,B*RETRO_RAM_HIDDEN);
    puf_mm_tn(&a->ram_grad,&a->ram_input,&a->dw[3],stream);
    retro_bias_grad<<<RETRO_RAM_HIDDEN,256,0,stream>>>(a->db[3].data,a->ram_grad.data,B,RETRO_RAM_HIDDEN);
    for(int l=2;l>=0;l--) {
        auto c=RETRO_CONVS[l]; int N=c.oh*c.ow,K=c.k*c.k*c.ic;
        auto g=retro_matrix(a->image_grad[l],B*N,c.oc);
        auto col=retro_matrix(a->columns,B*N,K);
        retro_relu_grad<<<grid_size(B*N*c.oc),BLOCK_SIZE,0,stream>>>(g.data,a->image[l].data,B*N*c.oc);
        retro_im2col<<<grid_size(B*N*K),BLOCK_SIZE,0,stream>>>(col.data,
            l?a->image[l-1].data:a->saved_input.data,c,B,
            l?c.ih*c.iw*c.ic:RETRO_POLICY_INPUTS,l?0:RETRO_RAM_INPUTS);
        puf_mm_tn(&g,&col,&a->dw[l],stream);
        retro_bias_grad<<<c.oc,256,0,stream>>>(a->db[l].data,g.data,B*N,c.oc);
        if(l) {
            // Reuse im2col storage only after the weight gradient consumed it.
            puf_mm_nn(&g,&ew->w[l],&col,stream);
            retro_col2im<<<grid_size(B*c.ih*c.iw*c.ic),BLOCK_SIZE,0,stream>>>(a->image_grad[l-1].data,col.data,c,B);
        }
    }
}
static void retro_encoder_init(void* w,ulong* seed,cudaStream_t stream) {
    auto* ew=(RetroEncoderWeights*)w;
    for(int l=0;l<5;l++) {
        puf_kaiming_init(&ew->w[l],sqrtf(2.0f),(*seed)++,stream);
        cudaMemsetAsync(ew->b[l].data,0,numel(ew->b[l].shape)*sizeof(precision_t),stream);
    }
}
static void retro_encoder_params(void* w,Allocator* alloc) {
    auto* ew=(RetroEncoderWeights*)w;
    for(int l=0;l<5;l++) { alloc_register(alloc,&ew->w[l]); alloc_register(alloc,&ew->b[l]); }
}
static void retro_encoder_buffers(RetroEncoderWeights* ew,RetroEncoderActivations* a,
        Allocator* acts,int B,bool training) {
    *a={};
    for(int l=0;l<3;l++) {
        auto c=RETRO_CONVS[l];
        a->image[l]={.shape={B,c.oh*c.ow*c.oc}}; alloc_register(acts,&a->image[l]);
        if(training) { a->image_grad[l]={.shape={B,c.oh*c.ow*c.oc}}; alloc_register(acts,&a->image_grad[l]); }
    }
    a->columns={.shape={B,RETRO_COL_ELEMENTS}};
    a->ram_input={.shape={B,RETRO_RAM_INPUTS}}; a->ram={.shape={B,RETRO_RAM_HIDDEN}};
    a->joined={.shape={B,RETRO_FUSED_FEATURES}}; a->out={.shape={B,ew->hidden}};
    alloc_register(acts,&a->columns); alloc_register(acts,&a->ram_input);
    alloc_register(acts,&a->ram); alloc_register(acts,&a->joined); alloc_register(acts,&a->out);
    if(training) {
        a->saved_input={.shape={B,RETRO_POLICY_INPUTS}};
        a->joined_grad={.shape={B,RETRO_FUSED_FEATURES}}; a->ram_grad={.shape={B,RETRO_RAM_HIDDEN}};
        alloc_register(acts,&a->saved_input); alloc_register(acts,&a->joined_grad); alloc_register(acts,&a->ram_grad);
    }
}
static void retro_encoder_train(void* w,void* activations,Allocator* acts,Allocator* grads,int B) {
    auto* ew=(RetroEncoderWeights*)w; auto* a=(RetroEncoderActivations*)activations;
    retro_encoder_buffers(ew,a,acts,B,true);
    for(int l=0;l<5;l++) {
        a->dw[l]=ew->w[l]; a->dw[l].data=nullptr; a->db[l]=ew->b[l]; a->db[l].data=nullptr;
        alloc_register(grads,&a->dw[l]); alloc_register(grads,&a->db[l]);
    }
}
static void retro_encoder_rollout(void* w,void* activations,Allocator* alloc,int B) {
    retro_encoder_buffers((RetroEncoderWeights*)w,(RetroEncoderActivations*)activations,alloc,B,false);
}
static void* retro_encoder_create(void* self) {
    auto* e=(Encoder*)self;
    if(e->in_dim!=RETRO_POLICY_INPUTS||e->out_dim<8||e->out_dim%8) {
        fprintf(stderr,"retro CNN requires 15472 inputs and a positive multiple-of-8 hidden size\n"); exit(1);
    }
    auto* ew=new RetroEncoderWeights{}; ew->hidden=e->out_dim;
    for(int l=0;l<3;l++) {
        auto c=RETRO_CONVS[l]; ew->w[l]={.shape={c.oc,c.k*c.k*c.ic}}; ew->b[l]={.shape={c.oc}};
    }
    ew->w[3]={.shape={RETRO_RAM_HIDDEN,RETRO_RAM_INPUTS}}; ew->b[3]={.shape={RETRO_RAM_HIDDEN}};
    ew->w[4]={.shape={e->out_dim,RETRO_FUSED_FEATURES}}; ew->b[4]={.shape={e->out_dim}};
    return ew;
}
static void create_retro_encoder(Encoder* e) {
    *e=Encoder{.forward=retro_encoder_forward,.backward=retro_encoder_backward,
        .init_weights=retro_encoder_init,.reg_params=retro_encoder_params,
        .reg_train=retro_encoder_train,.reg_rollout=retro_encoder_rollout,
        .create_weights=retro_encoder_create,.in_dim=e->in_dim,.out_dim=e->out_dim,
        .activation_size=sizeof(RetroEncoderActivations)};
}
