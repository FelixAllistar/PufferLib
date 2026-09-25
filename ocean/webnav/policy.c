#include "puffercpu.c"
#include "observation.h"
#include "policy.h"
#include <sys/stat.h>

typedef struct { Weights *weights; PufferNet *net; } WebPolicy;
void *webnav_policy_load(const char *path,int hidden,int layers) {
    struct stat st;
    long long expected=4LL*(WEBNAV_FEATURES*hidden+(WEBNAV_ACTIONS+1)*hidden+3LL*layers*hidden*hidden);
    if(hidden<8||hidden>4096||hidden%8||layers<1||layers>16||stat(path,&st)||st.st_size!=expected) {
        fprintf(stderr,"webnav checkpoint size/architecture mismatch: %s\n",path);return NULL;
    }
    WebPolicy *p=calloc(1,sizeof *p);if(!p)return NULL;
    p->weights=load_weights(path);if(!p->weights){free(p);return NULL;}
    /* All pilot tensors have sizes divisible by 8 floats, so float and
     * bf16 checkpoint tensor alignment agree for this architecture. */
    int sizes[]={WEBNAV_ACTIONS};
    p->net=make_puffernet(p->weights,1,WEBNAV_FEATURES,hidden,layers,sizes,1);
    return p;
}
void webnav_policy_reset(void *opaque) {
    WebPolicy *p=opaque;
    memset(p->net->mingru->state,0,p->net->mingru->num_layers*p->net->mingru->hidden_size*sizeof(float));
}
int webnav_policy_action(void *opaque,const uint32_t *obs,const uint32_t *mask) {
    WebPolicy *p=opaque;
    float features[WEBNAV_FEATURES];webnav_features(obs,features);
    linear(p->net->encoder,features);
    mingru(p->net->mingru,p->net->encoder->output);
    linear(p->net->decoder,p->net->mingru->output);
    int best=-1;float value=-INFINITY;
    for(int i=0;i<WEBNAV_ACTIONS;i++)if(mask[i]) {
        float x=p->net->decoder->output[i];
        if(!isfinite(x)){fprintf(stderr,"nonfinite policy output\n");abort();}
        if(best<0||x>value){best=i;value=x;}
    }
    if(best<0)abort();return best;
}
void webnav_policy_free(void *opaque) {
    WebPolicy *p=opaque;if(!p)return;free_puffernet(p->net);free(p->weights);free(p);
}
const float *webnav_policy_logits(void *opaque) {
    return ((WebPolicy*)opaque)->net->decoder->output;
}
