#define _GNU_SOURCE
#include "../../src/puffercpu.c"
#include "webnav_catalog.h"
#include "../webnav/cdp.h"
#include <limits.h>
#include <sys/prctl.h>

static const char *const tasks[]={"phone-book","order-food","search-engine"};
static const cJSON *field(const cJSON *j,const char *key){
    return cJSON_GetObjectItemCaseSensitive(j,key);
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *x=field(j,key);return cJSON_IsString(x)?x->valuestring:NULL;
}
static int number(const cJSON *j,const char *key,double *out){
    const cJSON *x=field(j,key);if(!cJSON_IsNumber(x))return -1;
    *out=x->valuedouble;return isfinite(*out)?0:-1;
}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *s=calloc((size_t)n+1,1);
    if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}
    fclose(f);return s;
}
static int browser_view(const cJSON *j,WFView *v,int *done,float *raw){
    const char *q=string(j,"instruction");
    const cJSON *nodes=field(j,"nodes"),*finished=field(j,"done");
    double deadline,reward;
    if(!q||!cJSON_IsArray(nodes)||!cJSON_IsBool(finished)||
       number(j,"deadline",&deadline)||number(j,"raw",&reward)||
       deadline<=0||deadline>UINT32_MAX||field(j,"problem")||field(j,"goal")||
       cJSON_GetArraySize(nodes)>WFC_NODES)return -1;
    wf_view_init(v,0,(uint32_t)deadline);
    if(wf_text_add(v,q,strlen(q),&v->instruction))return -1;
    int count=cJSON_GetArraySize(nodes);if(count<0)return -1;
    for(int i=0;i<count;i++){
        const cJSON *src=cJSON_GetArrayItem(nodes,i);
        const char *name=string(src,"name"),*value=string(src,"value");
        double ref,parent,role,flags,start,end;
        if(!name||!value||field(src,"goal")||field(src,"target")||
           number(src,"ref",&ref)||number(src,"parent",&parent)||
           number(src,"role",&role)||number(src,"flags",&flags)||
           number(src,"start",&start)||number(src,"end",&end)||
           ref<1||ref>=512||parent<0||parent>=512||
           role<0||role>WF_TEXTAREA||flags<0||flags>255||
           start<0||end<start||end>128)return -1;
        WFNode *n=&v->nodes[v->count++];
        n->ref=(uint32_t)ref;n->parent=(uint32_t)parent;
        n->role=(uint32_t)role;n->flags=(uint32_t)flags;
        n->selection_start=(uint32_t)start;n->selection_end=(uint32_t)end;
        n->capacity=n->role==WF_INPUT?128:0;
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    *done=cJSON_IsTrue(finished);*raw=(float)reward;
    return wf_view_valid(v)?0:-1;
}
static int click_ref(WebCdp *c,unsigned ref,unsigned *unhittable){
    char js[80];snprintf(js,sizeof js,"__wfc.point(%u)",ref);
    cJSON *j=web_cdp_eval(c,js);double x,y;
    if(!j)return -1;
    if(!cJSON_IsTrue(field(j,"visible"))||number(j,"x",&x)||number(j,"y",&y)){
        (*unhittable)++;cJSON_Delete(j);return 0;
    }
    cJSON_Delete(j);return web_cdp_click(c,x,y);
}
static int key_event(WebCdp *c,const char *type,const char *key,
                     unsigned code,unsigned modifiers){
    cJSON *p=cJSON_CreateObject();if(!p)return -1;
    cJSON_AddStringToObject(p,"type",type);
    cJSON_AddStringToObject(p,"key",key);
    cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"modifiers",modifiers);
    cJSON *j=web_cdp_call(c,"Input.dispatchKeyEvent",p);
    if(!j)return -1;cJSON_Delete(j);return 0;
}
static int select_all(WebCdp *c){
    return key_event(c,"keyDown","Control",17,0)||
           key_event(c,"keyDown","a",65,2)||
           key_event(c,"keyUp","a",65,2)||
           key_event(c,"keyUp","Control",17,0);
}
static int dispatch(WebCdp *c,const WFAction *a,unsigned *unhittable){
    if(a->kind==WF_WAIT)return 0;
    if(a->kind==WF_CLICK)return click_ref(c,a->target,unhittable);
    if(a->kind==WF_SELECT_ALL)return select_all(c);
    if(a->kind==WF_INSERT){
        if(!a->text||a->text_length>=128)return -1;
        char text[128];memcpy(text,a->text,a->text_length);text[a->text_length]=0;
        cJSON *p=cJSON_CreateObject();if(!p)return -1;
        cJSON_AddStringToObject(p,"text",text);
        cJSON *j=web_cdp_call(c,"Input.insertText",p);
        if(!j)return -1;cJSON_Delete(j);return 0;
    }
    return -1;
}
static int choose(PufferNet *net,const WFView *v,unsigned char visits[512],
                  int random_policy){
    float obs[OBS_SIZE];unsigned char mask[WFC_ACTIONS];
    wfc_project(v,visits,obs,mask);
    if(random_policy){
        unsigned count=0;for(unsigned i=0;i<WFC_ACTIONS;i++)count+=!!mask[i];
        unsigned pick=(unsigned)rand()%count;
        for(unsigned i=0;i<WFC_ACTIONS;i++)if(mask[i]&&!pick--)return (int)i;
        return 0;
    }
    linear(net->encoder,obs);mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    int best=0;float score=-INFINITY;
    for(unsigned i=0;i<WFC_ACTIONS;i++)if(mask[i]&&net->decoder->output[i]>score){
        best=(int)i;score=net->decoder->output[i];
    }
    return best;
}
static size_t align8(size_t n){return (n+7u)&~7u;}
static size_t expected_weights(void){
    size_t n=align8(64u*OBS_SIZE);
    n=align8(n+64u*(WFC_ACTIONS+1u));
    return align8(n+3u*64u*64u);
}
int main(int argc,char **argv){
    if(argc<3||argc>4){
        fprintf(stderr,"usage: %s CHECKPOINT|random EPISODES_PER_TASK [TASK_NAME]\n",argv[0]);return 2;
    }
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    char *end=NULL;unsigned long parsed=strtoul(argv[2],&end,10);
    if(!end||*end||parsed<1||parsed>1000)return 2;
    unsigned episodes=(unsigned)parsed,random_policy=!strcmp(argv[1],"random");
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy){
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()){
            fprintf(stderr,"expected H64/L1 catalog checkpoint with %zu floats\n",expected_weights());
            free(weights);return 2;
        }
        int sizes[]={WFC_ACTIONS};net=make_puffernet(weights,1,OBS_SIZE,64,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights())return 2;
    }
    srand(12345);
    char *script=read_file("ocean/webnav_catalog/browser.js");if(!script)return 2;
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    unsigned evaluated=0;
    for(unsigned task=0;task<3;task++){
        if(argc==4&&strcmp(argv[3],tasks[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",tasks[task]);
        if(!realpath(file,resolved)){fprintf(stderr,"original page missing: %s\n",file);return 2;}
        snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        if(web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"))return 2;
        cJSON *j=web_cdp_eval(&c,script);
        if(!j||!cJSON_IsTrue(j)){fprintf(stderr,"browser setup failed: %s\n",tasks[task]);return 1;}
        cJSON_Delete(j);
        unsigned wins=0,partials=0,timeouts=0,actions=0,unhittable=0;
        double score=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=(getenv("WEBNAV_SEED_OFFSET")?
                (unsigned)strtoul(getenv("WEBNAV_SEED_OFFSET"),NULL,10):300000u)+
                task*10000u+ep;
            char js[128];snprintf(js,sizeof js,"__wfc.reset(%u)",seed);
            j=web_cdp_eval(&c,js);WFView v;int done=0;float raw=0;
            if(!j||browser_view(j,&v,&done,&raw)){
                fprintf(stderr,"invalid public reset: %s seed=%u\n",tasks[task],seed);return 1;
            }
            cJSON_Delete(j);
            unsigned char visits[512]={0};
            if(net)memset(net->mingru->state,0,64*sizeof(float));
            unsigned last_ms=0;
            for(unsigned step=1;step<=100&&!done;step++){
                int choice=choose(net,&v,visits,random_policy);
                WFAction a=wfc_action(&v,(unsigned)choice);
                if(a.target<512&&a.target&&visits[a.target]<255)visits[a.target]++;
                last_ms=step*250u;
                snprintf(js,sizeof js,"__wfc.tick(%u)",last_ms);
                j=web_cdp_eval(&c,js);if(!j)return 1;cJSON_Delete(j);
                if(last_ms<v.deadline_ms&&dispatch(&c,&a,&unhittable))return 1;
                j=web_cdp_eval(&c,"__wfc.snapshot()");
                if(!j||browser_view(j,&v,&done,&raw)){
                    fprintf(stderr,"invalid public step: %s seed=%u step=%u\n",tasks[task],seed,step);return 1;
                }
                cJSON_Delete(j);v.elapsed_ms=last_ms;actions++;
            }
            if(!done){fprintf(stderr,"episode did not end: %s seed=%u\n",tasks[task],seed);return 1;}
            wins+=raw>=0.999f;partials+=raw>0&&raw<0.999f;
            score+=raw;timeouts+=last_ms>=v.deadline_ms&&raw<0.999f;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"partial_positive\":%u,\"full_credit_rate\":%.6f,\"mean_raw_score\":%.6f,\"timeouts\":%u,\"actions\":%u,\"unhittable\":%u,\"policy\":\"%s H64/L1; original Chromium pages\"}\n",
               tasks[task],episodes,wins,partials,(double)wins/episodes,score/episodes,
               timeouts,actions,unhittable,random_policy?"random":"greedy");fflush(stdout);
        evaluated+=episodes;web_cdp_close(&c);
    }
    free(script);if(net)free_puffernet(net);free(weights);
    return evaluated?0:2;
}
