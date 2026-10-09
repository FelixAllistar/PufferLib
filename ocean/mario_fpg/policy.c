#include "fpg_task.h"
#include "../../src/puffercpu.c"
#include "policy.h"
typedef struct {Weights* weights;PufferNet* net;} FpgPolicy;
void* fpg_policy_load(const char* path,int hidden,int layers) {
    if(hidden<32||hidden>512||hidden%32||layers<1||layers>8)return NULL;
    Weights* weights=load_weights(path);int need=FPG_OBS*hidden+13*hidden+layers*3*hidden*hidden;
    if(!weights||weights->size-7!=need){free(weights);return NULL;}
    for(int i=0;i<need;i++)if(!isfinite(weights->data[i])){free(weights);return NULL;}
    FpgPolicy* p=(FpgPolicy*)calloc(1,sizeof(*p));if(!p){free(weights);return NULL;}
    int sizes[]={12};p->weights=weights;p->net=make_puffernet(weights,1,FPG_OBS,hidden,layers,sizes,1);return p;
}
void fpg_policy_reset(void* handle) {
    FpgPolicy* p=(FpgPolicy*)handle;MinGRU* r=p->net->mingru;memset(r->state,0,r->hidden_size*r->num_layers*sizeof(float));
}
int fpg_policy_action(void* handle,const float* obs,int deterministic) {
    FpgPolicy* p=(FpgPolicy*)handle;PufferNet* net=p->net;
    linear(net->encoder,(float*)obs);mingru(net->mingru,net->encoder->output);linear(net->decoder,net->mingru->output);
    float action;multidiscrete(net->multidiscrete,net->decoder->output,&action,deterministic,NULL);return (int)action;
}
void fpg_policy_logits(void* handle,const float* obs,float* logits) {
    FpgPolicy* p=(FpgPolicy*)handle;PufferNet* net=p->net;
    linear(net->encoder,(float*)obs);mingru(net->mingru,net->encoder->output);linear(net->decoder,net->mingru->output);
    memcpy(logits,net->decoder->output,12*sizeof(float));
}
void fpg_policy_free(void* handle) {
    if(!handle)return;FpgPolicy* p=(FpgPolicy*)handle;free_puffernet(p->net);free(p->weights);free(p);
}
