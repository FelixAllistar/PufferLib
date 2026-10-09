/* Public-only, persistent batch-one policy inference for browser adapters.
 * Run from the repository root so the pinned semantic assets resolve.
 * CLI: policy_rpc CHECKPOINT|random [--sample]
 * Each input line is either {"reset":true,"seed":optional_uint32} or the
 * complete public JSON view. Local choices require resubmitting the same view.
 * Malformed requests do not change policy state. No family/environment is used.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#if defined(__AVX2__) && defined(__FMA__)
#include <immintrin.h>
#endif
#include "../../../vendor/cJSON.h"

static void rpc_fatal(const char *message) {
    fprintf(stderr,"policy_rpc: %s\n",message);
    exit(2);
}
static void *rpc_calloc(size_t count,size_t size) {
    void *p=calloc(count,size);
    if(!p)rpc_fatal("allocation failed");
    return p;
}
/* The existing constructors assume allocation succeeds. Preserve their layout
 * while making an allocation failure a stderr-only, exit-2 failure. */
#define calloc rpc_calloc
#include "../../../src/puffercpu.c"
#undef calloc
#include "../../webnav_unified/cpu_linear.h"
#include "../unified/policy.h"
#include "../unified/semantic.h"
#include "../primitives/transport/public_json.h"
#include "../primitives/transport/line.h"

#define RPC_HIDDEN 64u
#define RPC_LINE_LIMIT (1024u*1024u)
#define RPC_DEFAULT_SEED 91037u
_Static_assert(WU_OBS_SIZE==24864u,"unexpected shared observation layout");
_Static_assert(WU_ACTIONS==3832u,"unexpected shared action layout");

static size_t rpc_align8(size_t n) { return (n+7u)&~(size_t)7u; }
static size_t rpc_weights_count(void) {
    size_t n=rpc_align8(RPC_HIDDEN*WU_OBS_SIZE);
    n=rpc_align8(n+RPC_HIDDEN*(WU_ACTIONS+1u));
    return rpc_align8(n+3u*RPC_HIDDEN*RPC_HIDDEN);
}
static Weights *rpc_load(const char *path) {
    FILE *file=fopen(path,"rb");
    if(!file)rpc_fatal("cannot open checkpoint");
    struct stat st;
    size_t count=rpc_weights_count();
    if(fstat(fileno(file),&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)(count*sizeof(float)))
        rpc_fatal("expected H64/L1 FP32 shared checkpoint (24864 observations, 3832 actions)");
    Weights *w=rpc_calloc(1,sizeof *w+(count+7u)*sizeof(float));
    w->data=(float *)(w+1);w->size=(int)(count+7u);
    if(fread(w->data,sizeof(float),count,file)!=count||fgetc(file)!=EOF||ferror(file))
        rpc_fatal("checkpoint read failed or file changed during loading");
    if(fclose(file))rpc_fatal("checkpoint close failed");
    for(size_t i=0;i<count;i++)if(!isfinite(w->data[i]))rpc_fatal("checkpoint contains nonfinite weights");
    return w;
}
static uint32_t rpc_random(uint32_t *rng) {
    *rng=*rng*1664525u+1013904223u;return *rng;
}
static int rpc_choose(const unsigned char *mask,const float *logits,int sample,
                      uint32_t *rng,uint32_t *choice) {
    unsigned legal=0,best=0;
    float maximum=-INFINITY;
    for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a]) {
        if(logits&&!isfinite(logits[a]))return -1;
        if(!legal|| (logits&&logits[a]>maximum)) {
            best=a;if(logits)maximum=logits[a];
        }
        legal++;
    }
    if(!legal)return -1;
    if(!logits) {
        unsigned selected=(rpc_random(rng)>>8)%legal;
        for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a]) {
            if(!selected){*choice=a;return 0;}
            selected--;
        }
    } else if(sample) {
        double total=0;
        for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a])total+=exp((double)logits[a]-maximum);
        double threshold=((double)rpc_random(rng)+0.5)/4294967296.0*total;
        for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a]) {
            threshold-=exp((double)logits[a]-maximum);
            if(threshold<0){*choice=a;return 0;}
            best=a;
        }
    }
    *choice=best;return 0;
}
static int rpc_finite(const float *values,size_t n) {
    for(size_t i=0;i<n;i++)if(!isfinite(values[i]))return 0;
    return 1;
}
static void rpc_flush(void) {
    if(fflush(stdout)||ferror(stdout))rpc_fatal("stdout write failed");
}
static void rpc_error(const char *message) {
    /* Every error message is a fixed literal without JSON metacharacters. */
    printf("{\"error\":\"%s\"}\n",message);rpc_flush();
}
static void rpc_reply(uint32_t choice,int local,const WFAction *action,uint32_t steps) {
    cJSON *o=cJSON_CreateObject();
    if(!o||!cJSON_AddNumberToObject(o,"choice",choice)||
       !cJSON_AddBoolToObject(o,"local",local)||
       !cJSON_AddNumberToObject(o,"kind",action->kind)||
       !cJSON_AddNumberToObject(o,"target",action->target)||
       !cJSON_AddNumberToObject(o,"arg0",action->arg0)||
       !cJSON_AddNumberToObject(o,"arg1",action->arg1)||
       !cJSON_AddStringToObject(o,"text",action->text?action->text:"")||
       !cJSON_AddNumberToObject(o,"steps",steps))rpc_fatal("response allocation failed");
    char *output=cJSON_PrintUnformatted(o);
    if(!output)rpc_fatal("response serialization failed");
    puts(output);rpc_flush();cJSON_free(output);cJSON_Delete(o);
}

int main(int argc,char **argv) {
    if(prctl(PR_SET_DUMPABLE,0,0,0,0))rpc_fatal("cannot disable process dumps");
    if(argc<2||argc>3||(argc==3&&strcmp(argv[2],"--sample"))) {
        fprintf(stderr,"usage: %s CHECKPOINT|random [--sample]\n",argv[0]);return 2;
    }
    int random_policy=!strcmp(argv[1],"random"),sample=argc==3;
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy) {
        weights=rpc_load(argv[1]);int sizes[]={WU_ACTIONS};
        net=make_puffernet(weights,1,WU_OBS_SIZE,RPC_HIDDEN,1,sizes,1);
        if((size_t)weights->idx!=rpc_weights_count())rpc_fatal("checkpoint layout mismatch");
    }
    if(wu_semantic_init(1))rpc_fatal("pinned frozen semantic encoder failed to load");
    char *line=rpc_calloc(RPC_LINE_LIMIT+1u,1);
    WFView *view=rpc_calloc(1,sizeof *view);
    WUCapabilities *caps=rpc_calloc(1,sizeof *caps);
    float *obs=rpc_calloc(WU_OBS_SIZE,sizeof *obs);
    unsigned char *mask=rpc_calloc(WU_ACTIONS,1);
    WUState state={0};uint32_t rng=RPC_DEFAULT_SEED;
    size_t bytes;int status;
    while((status=wl_read(stdin,line,RPC_LINE_LIMIT,&bytes))) {
        if(status==-2)rpc_fatal("stdin read failed");
        if(status<0){rpc_error("input line exceeds 1 MiB or contains NUL");continue;}
        if(!wv_json_valid(line,bytes)){rpc_error("invalid strict JSON or UTF-8");continue;}
        cJSON *object=cJSON_ParseWithLengthOpts(line,bytes+1u,NULL,1);
        if(!object){rpc_error("JSON parse failed");continue;}
        const char *error=NULL;
        if(wv_get(object,"reset")) {
            static const char *const keys[]={"reset","seed"};
            uint32_t seed=RPC_DEFAULT_SEED;
            if(!wv_shape(object,keys,2)||!cJSON_IsTrue(wv_get(object,"reset"))||
               (wv_get(object,"seed")&&!wv_u32(object,"seed",&seed)))error="invalid reset request";
            else {
                state=(WUState){0};rng=seed;
                if(net)memset(net->mingru->state,0,RPC_HIDDEN*sizeof(float));
                puts("{\"reset\":true}");rpc_flush();
            }
        } else if((error=wv_view(object,view,caps))) {
            /* Parsing and validation leave both state stores untouched. */
        } else if(state.steps==UINT32_MAX)error="step counter exhausted; reset required";
        else if(wu_project(view,caps,&state,obs,mask)||!rpc_finite(obs,WU_OBS_SIZE))
            error="public observation projection failed";
        else {
            float carry[RPC_HIDDEN];uint32_t next_rng=rng,choice=0;
            int finite=1;
            if(net) {
                memcpy(carry,net->mingru->state,sizeof carry);
                wu_linear(net->encoder,obs);
                finite=rpc_finite(net->encoder->output,RPC_HIDDEN);
                if(finite) {
                    mingru(net->mingru,net->encoder->output);
                    finite=rpc_finite(net->mingru->state,RPC_HIDDEN)&&
                           rpc_finite(net->mingru->output,RPC_HIDDEN);
                }
                if(finite) {
                    wu_linear(net->decoder,net->mingru->output);
                    finite=rpc_finite(net->decoder->output,WU_ACTIONS+1u);
                }
            }
            if(!finite||rpc_choose(mask,net?net->decoder->output:NULL,sample,&next_rng,&choice))
                error="policy produced nonfinite values or an empty action mask";
            else {
                WUState next=state;WFAction action={0};
                uint32_t elapsed=view->deadline_ms-view->elapsed_ms<50u?
                    view->deadline_ms:view->elapsed_ms+50u;
                int execute=wu_decode(view,caps,&next,choice,elapsed,&action);
                if(execute<0)error="masked action decode failed";
                else {
                    next.steps++;state=next;rng=next_rng;
                    /* wu_decode borrows the temporary register's text; it is
                     * still alive here until the response is serialized. */
                    rpc_reply(choice,execute==0,&action,state.steps);
                }
            }
            if(error&&net)memcpy(net->mingru->state,carry,sizeof carry);
        }
        cJSON_Delete(object);
        if(error)rpc_error(error);
    }
    free(line);free(view);free(caps);free(obs);free(mask);
    wu_semantic_close();if(net)free_puffernet(net);free(weights);
    return 0;
}
