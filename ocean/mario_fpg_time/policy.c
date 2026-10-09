// Use the repository's ordinary FP32 CPU inference implementation for both
// sides of the transfer comparison. GPU BF16 sampling is a separate contract.
#include "../../src/puffercpu.c"
#include "policy.h"
#include "encoder_cpu.h"
typedef struct {Weights* weights;FptCpuEncoder encoder;MinGRU* recurrent;Linear* decoder;} FptPolicy;
void* fpt_policy_load(const char* path,int hidden,int layers) {
    if(hidden<32||hidden>1024||hidden%32||layers<1||layers>8)return NULL;
    struct stat st;if(stat(path,&st)||st.st_size%sizeof(float))return NULL;
    Weights* w=load_weights(path);int need=fpt_policy_weight_count(hidden,layers);
    if(!w||w->size-7!=need){free(w);return NULL;}
    for(int i=0;i<need;i++)if(!isfinite(w->data[i])){free(w);return NULL;}
    FptPolicy* p=calloc(1,sizeof(*p));if(!p){free(w);return NULL;}
    p->weights=w;w->idx=fpt_cpu_encoder_bind(&p->encoder,w->data,hidden);
    p->decoder=make_linear(w,1,hidden,65);p->recurrent=make_mingru(w,1,hidden,layers);return p;
}
void fpt_policy_reset(void* handle) {
    FptPolicy* p=handle;MinGRU* r=p->recurrent;memset(r->state,0,r->hidden_size*r->num_layers*sizeof(float));
}
const float* fpt_policy_logits(void* handle,const float* obs) {
    FptPolicy* p=handle;fpt_cpu_encode(&p->encoder,obs);
    mingru(p->recurrent,p->encoder.out);linear(p->decoder,p->recurrent->output);return p->decoder->output;
}
void fpt_policy_free(void* handle) {
    if(!handle)return;FptPolicy* p=handle;free_mingru(p->recurrent);free(p->decoder);free(p->weights);free(p);
}
