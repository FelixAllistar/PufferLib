#include "puffercpu.c"
#include "training_policy.h"
#include "training_contract.h"
#include <sys/stat.h>
struct WTPolicy{Weights *weights;PufferNet *net;WTFeatures *features;};
WTPolicy *wt_policy_load(const char *path){int potion,hidden,layers;if(wt_contract_read(path,&potion,&hidden,&layers)){fprintf(stderr,"Missing/incompatible .webnav.json checkpoint contract: %s\n",path);return NULL;}struct stat st;long long expected=4LL*(WT_FEATURES*hidden+(WT_ACTIONS+1)*hidden+3LL*layers*hidden*hidden);if(stat(path,&st)||st.st_size!=expected){fprintf(stderr,"checkpoint architecture/size mismatch\n");return NULL;}WTPolicy *p=calloc(1,sizeof *p);if(!p)return NULL;p->features=wt_features_new(potion);if(!p->features){free(p);return NULL;}p->weights=load_weights(path);if(!p->weights){wt_features_free(p->features);free(p);return NULL;}int sizes[]={WT_ACTIONS};p->net=make_puffernet(p->weights,1,WT_FEATURES,hidden,layers,sizes,1);return p;}
void wt_policy_reset(WTPolicy *p){memset(p->net->mingru->state,0,(size_t)p->net->mingru->num_layers*p->net->mingru->hidden_size*sizeof(float));}
int wt_policy_action(WTPolicy *p,const WTView *v){wt_features(p->features,v,p->net->obs);linear(p->net->encoder,p->net->obs);mingru(p->net->mingru,p->net->encoder->output);linear(p->net->decoder,p->net->mingru->output);unsigned char mask[WT_ACTIONS];wt_mask(v,mask);int best=0;float val=-INFINITY;for(unsigned i=0;i<WT_ACTIONS;i++)if(mask[i]){float x=p->net->decoder->output[i];if(!isfinite(x))abort();if(x>val){best=i;val=x;}}return best;}
const float *wt_policy_logits(WTPolicy *p){return p->net->decoder->output;}
void wt_policy_free(WTPolicy *p){if(p){wt_features_free(p->features);free_puffernet(p->net);free(p->weights);free(p);}}
