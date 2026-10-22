// BF16 forward parity and finite/nonzero backward checks at sweep widths.
// Numerical derivatives are independently covered by test_encoder.cu in FP32.
#define ENV_HEADER "ocean/breakout/breakout.h"
#define PUFFER_ENV_NAME "breakout"
#include "../../../src/pufferl.cu"
#include "../pokemon_encoder.cu"
#include "encoder_layout.h"

static void upload(precision_t* dst, const float* src, int n) {
    std::vector<precision_t> packed(n);
    for (int i=0;i<n;i++) packed[i]=from_float(src[i]);
    assert(cudaMemcpy(dst,packed.data(),n*sizeof(precision_t),cudaMemcpyHostToDevice)==cudaSuccess);
}
static std::vector<float> download(precision_t* src,int n) {
    std::vector<precision_t> packed(n); std::vector<float> result(n);
    assert(cudaMemcpy(packed.data(),src,n*sizeof(precision_t),cudaMemcpyDeviceToHost)==cudaSuccess);
    for(int i=0;i<n;i++) result[i]=to_float(packed[i]);
    return result;
}
static void compare(Prec value,const float* expected,int n) {
    auto actual=download(value.data,n);
    double max_scaled=0;
    for(int i=0;i<n;i++) {
        assert(isfinite(actual[i]) && isfinite(expected[i]));
        double error=fabs(actual[i]-expected[i])/(1+fabs(expected[i]));
        max_scaled=fmax(max_scaled,error);
        assert(error<0.025);
    }
    printf("BF16 forward n=%d max_scaled_error=%g\n",n,max_scaled);
}
static void qualify(int H) {
    const int B=3;
    Encoder enc={.in_dim=648,.out_dim=H}; Decoder dec={.hidden_dim=H,.output_dim=168};
    create_pokemon_encoder(&enc); create_pokemon_decoder(&dec);
    void* ew=enc.create_weights(&enc); void* dw=dec.create_weights(&dec);
    PKEncoderActs ea={}; PKDecoderActs da={};
    Allocator params={},acts={},grads={};
    enc.reg_params(ew,&params); dec.reg_params(dw,&params);
    enc.reg_train(ew,&ea,&acts,&grads,B); dec.reg_train(dw,&da,&acts,&grads,B);
    Prec obs={.shape={B,648}},state={.shape={B,H}},ge={.shape={B,H}};
    Float gl={.shape={B,168}},gv={.shape={B}};
    alloc_register(&acts,&obs); alloc_register(&acts,&state); alloc_register(&acts,&ge);
    alloc_register(&acts,&gl); alloc_register(&acts,&gv);
    alloc_create(&params); alloc_create(&acts); alloc_create(&grads);
    std::vector<int> features=pk_test_features(H);
    const int regs=(int)features.size();
    assert(sizeof(precision_t)==2 && params.num_regs==regs && grads.num_regs==regs);
    std::vector<float> weights(params.total_elems+3*H*H+7),input(B*648),hidden(B*H),
        expected_enc(B*H),expected_dec(B*169),enc_grad(B*H),logits_grad(B*168),value_grad(B);
    int offset=0;
    for(int p=0;p<regs;p++) {
        int cols=pk_test_cols(params,p),rows=numel(params.regs[p].shape)/cols;
        for(int r=0;r<rows;r++) for(int c=0;c<cols;c++) {
            float v=pk_test_value(features[p],c,offset+r*cols+c,.03f);
            weights[offset+r*cols+c]=to_float(from_float(v));
        }
        upload((precision_t*)*params.regs[p].data_ptr,weights.data()+offset,rows*cols);
        offset+=rows*cols;
    }
    for(int i=0;i<B*H;i++) {
        hidden[i]=to_float(from_float(sinf(i*.31f)));
        enc_grad[i]=.1f*cosf(i*.73f);
    }
    for(int i=0;i<B*168;i++) logits_grad[i]=.01f*sinf(i*.37f);
    for(int i=0;i<B;i++) value_grad[i]=.01f;
    upload(state.data,hidden.data(),B*H); upload(ge.data,enc_grad.data(),B*H);
    assert(cudaMemcpy(gl.data,logits_grad.data(),B*168*sizeof(float),cudaMemcpyHostToDevice)==cudaSuccess);
    assert(cudaMemcpy(gv.data,value_grad.data(),B*sizeof(float),cudaMemcpyHostToDevice)==cudaSuccess);
    std::vector<double> magnitude(regs);
    for(int phase=0;phase<3;phase++) {
        for(int b=0;b<B;b++) {
            float* o=input.data()+b*648;
            for(int i=0;i<648;i++) o[i]=(i*17+b*13+phase*31)%150;
            o[0]=phase; o[4]=b+1; o[5]=b+2;
            o[401]=2; o[402]=3; o[433]=4; o[434]=5;
        }
        upload(obs.data,input.data(),B*648);
        pk_reference(weights.data(),B,H,pk_embed,pk_mid,pk_attn,input.data(),hidden.data(),expected_enc.data(),expected_dec.data());
        compare(enc.forward(ew,&ea,obs,0),expected_enc.data(),B*H);
        pk_decoder_bind(&da,obs);
        compare(dec.forward(dw,&da,state,0),expected_dec.data(),B*169);
        enc.backward(ew,&ea,ge,0);
        Prec dh=dec.backward(dw,&da,gl,{},gv,0);
        for(float v:download(dh.data,B*H)) assert(isfinite(v));
        for(int p=0;p<regs;p++) for(float v:download((precision_t*)*grads.regs[p].data_ptr,numel(grads.regs[p].shape))) {
            assert(isfinite(v)); magnitude[p]+=fabs(v);
        }
    }
    for(int p=0;p<regs;p++) assert(magnitude[p]>0);
    printf("PASS BF16 H%d CPU forward parity; all %d parameter tensors receive finite nonzero gradients\n",H,regs);
    // Each width runs in a fresh process; process teardown frees CUDA allocations.
}
int main(int argc,char** argv) {
    cublas_init_handle(); pk_test_sizes(); qualify(argc>1?atoi(argv[1]):256); return 0;
}
