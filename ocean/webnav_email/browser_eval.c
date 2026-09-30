#define _GNU_SOURCE
#include "../../src/puffercpu.c"
#include "webnav_email.h"
#include "../webnav/cdp.h"
#include <limits.h>
#include <sys/prctl.h>

static const char *const tasks[]={
    "email-inbox-delete","email-inbox-forward-nl-turk",
    "email-inbox-forward-nl","email-inbox-forward","email-inbox-important",
    "email-inbox-nl-turk","email-inbox-noscroll","email-inbox-reply",
    "email-inbox-star-reply","email-inbox"
};
static const cJSON *field(const cJSON *j,const char *key){
    return cJSON_GetObjectItemCaseSensitive(j,key);
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *x=field(j,key);return cJSON_IsString(x)?x->valuestring:NULL;
}
static int number(const cJSON *j,const char *key,double *out){
    const cJSON *x=field(j,key);if(!cJSON_IsNumber(x))return -1;
    *out=x->valuedouble;return 0;
}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *s=calloc((size_t)n+1,1);
    if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}
    fclose(f);return s;
}
/* Reject private fields even if a future fixture accidentally emits them.
 * The reward fields are read only for episode reporting, after projection. */
static int browser_view(const cJSON *j,WFView *v,int *done,float *raw){
    const char *instruction=string(j,"instruction");
    const cJSON *nodes=field(j,"nodes"),*finished=field(j,"done");
    double elapsed,deadline,reward;
    if(!instruction||!cJSON_IsArray(nodes)||!cJSON_IsBool(finished)||
       field(j,"goal")||field(j,"expected")||field(j,"emails")||
       field(j,"target")||field(j,"seed")||field(j,"task")||
       number(j,"elapsed",&elapsed)||number(j,"deadline",&deadline)||
       number(j,"raw",&reward)||elapsed<0||deadline<=0||
       elapsed>deadline||deadline>UINT32_MAX||!isfinite(reward))return -1;
    wf_view_init(v,(uint32_t)elapsed,(uint32_t)deadline);
    if(wf_text_add(v,instruction,strlen(instruction),&v->instruction))return -1;
    int count=cJSON_GetArraySize(nodes);
    if(count<0||count>WFE_NODES)return -1;
    for(int i=0;i<count;i++){
        const cJSON *src=cJSON_GetArrayItem(nodes,i);
        const char *name=string(src,"name"),*value=string(src,"value");
        double ref,parent,role,flags,capacity;
        if(!name||!value||field(src,"goal")||field(src,"target")||
           number(src,"ref",&ref)||number(src,"parent",&parent)||
           number(src,"role",&role)||number(src,"flags",&flags)||
           number(src,"capacity",&capacity)||
           ref<1||ref>255||parent<0||parent>255||role<0||role>WF_TEXTAREA||
           flags<0||flags>255||capacity<0||capacity>160)return -1;
        WFNode *n=v->nodes+v->count++;
        n->ref=(uint32_t)ref;n->parent=(uint32_t)parent;
        n->role=(uint32_t)role;n->flags=(uint32_t)flags;
        n->capacity=(uint32_t)capacity;
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    *done=cJSON_IsTrue(finished);*raw=(float)reward;
    return 0;
}
static cJSON *key_event(WebCdp *c,const char *type,const char *key,
                        unsigned code,unsigned modifiers){
    cJSON *p=cJSON_CreateObject();if(!p)return NULL;
    cJSON_AddStringToObject(p,"type",type);
    cJSON_AddStringToObject(p,"key",key);
    cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"modifiers",modifiers);
    return web_cdp_call(c,"Input.dispatchKeyEvent",p);
}
static int dispatched(cJSON *result){if(!result)return -1;cJSON_Delete(result);return 0;}
static int select_all(WebCdp *c){
    if(dispatched(key_event(c,"keyDown","Control",17,0)))return -1;
    if(dispatched(key_event(c,"keyDown","a",65,2)))return -1;
    if(dispatched(key_event(c,"keyUp","a",65,2)))return -1;
    return dispatched(key_event(c,"keyUp","Control",17,0));
}
static int backspace(WebCdp *c){
    if(dispatched(key_event(c,"keyDown","Backspace",8,0)))return -1;
    return dispatched(key_event(c,"keyUp","Backspace",8,0));
}
static int search_keyup(WebCdp *c){
    cJSON *j=web_cdp_eval(c,"__wfe.searchKeyup()");
    return dispatched(j);
}
static int insert_text(WebCdp *c,const char *value,int search){
    cJSON *p=cJSON_CreateObject();if(!p)return -1;
    cJSON_AddStringToObject(p,"text",value);
    if(dispatched(web_cdp_call(c,"Input.insertText",p)))return -1;
    return search?search_keyup(c):0;
}
static int click_ref(WebCdp *c,unsigned ref){
    char js[80];snprintf(js,sizeof js,"__wfe.point(%u)",ref);
    cJSON *p=web_cdp_eval(c,js);double x,y;
    if(!p)return -1;
    if(cJSON_IsNull(p)||!cJSON_IsTrue(field(p,"visible"))||
       number(p,"x",&x)||number(p,"y",&y)){
        cJSON_Delete(p);return 1;
    }
    cJSON_Delete(p);return web_cdp_click(c,x,y)?-1:0;
}
static size_t align8(size_t n){return (n+7u)&~7u;}
static size_t expected_weights(void){
    size_t n=align8(64u*OBS_SIZE);
    n=align8(n+64u*(WFE_ACTIONS+1u));
    return align8(n+3u*64u*64u);
}
static int choose(PufferNet *net,const WFView *v,WFEHistory *history,
                  WFECatalog *cat,unsigned char mask[WFE_ACTIONS],int random_policy){
    float obs[OBS_SIZE];
    wfe_record(history,v);wfe_catalog(v,history,cat);
    wfe_project(v,history,cat,obs,mask);
    if(random_policy){
        unsigned legal[WFE_ACTIONS],count=0;
        for(unsigned i=0;i<WFE_ACTIONS;i++)if(mask[i])legal[count++]=i;
        return count?(int)legal[(unsigned)rand()%count]:0;
    }
    linear(net->encoder,obs);mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    int best=0;float score=-INFINITY;
    for(unsigned i=0;i<WFE_ACTIONS;i++)if(mask[i]&&net->decoder->output[i]>score){
        best=(int)i;score=net->decoder->output[i];
    }
    return best;
}
static int act(WebCdp *c,const WFView *v,WFEHistory *h,
               const WFECatalog *cat,int choice,unsigned *unhittable){
    if(choice>=WFE_CLICK_BASE&&choice<WFE_SELECT){
        unsigned index=(unsigned)(choice-WFE_CLICK_BASE);
        if(index>=v->count)return -1;
        unsigned ref=v->nodes[index].ref;
        h->clicks++;h->last_ref=ref;h->last_kind=WF_CLICK;h->selected_ref=0;
        if(ref<256)h->visited[ref]=1;
        int result=click_ref(c,ref);
        if(result==1){(*unhittable)++;return 0;}
        return result;
    }
    if(choice==WFE_SELECT){
        const WFNode *f=wfe_focus(v);
        h->edits++;h->last_kind=WF_SELECT_ALL;h->selected_ref=f?f->ref:0;
        return select_all(c);
    }
    if(choice==WFE_BACKSPACE){
        h->edits++;h->last_kind=WF_BACKSPACE;h->selected_ref=0;
        return backspace(c);
    }
    if(choice>=WFE_COPY_BASE&&choice<WFE_CHAR_BASE){
        const WFECandidate *candidate=cat->items+(choice-WFE_COPY_BASE);
        h->edits++;h->last_kind=WF_INSERT;h->selected_ref=0;
        return insert_text(c,candidate->text,wfe_focus(v)&&wfe_focus(v)->ref==2);
    }
    if(choice>=WFE_CHAR_BASE&&choice<WFE_ACTIONS){
        char text[2]={(char)(32+choice-WFE_CHAR_BASE),0};
        h->edits++;h->last_kind=WF_INSERT;h->selected_ref=0;
        return insert_text(c,text,wfe_focus(v)&&wfe_focus(v)->ref==2);
    }
    h->last_kind=WF_WAIT;return 0;
}
int main(int argc,char **argv){
    if(argc<3||argc>4){
        fprintf(stderr,"usage: %s CHECKPOINT|random EPISODES_PER_TASK [TASK_NAME]\n",argv[0]);return 2;
    }
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    char *end=NULL;unsigned long parsed=strtoul(argv[2],&end,10);
    if(!end||*end||parsed<1||parsed>1000)return 2;
    unsigned episodes=(unsigned)parsed;
    int random_policy=!strcmp(argv[1],"random");
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy){
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()){
            fprintf(stderr,"expected H64/L1 webnav_email checkpoint with %zu floats\n",expected_weights());
            free(weights);return 2;
        }
        int sizes[]={WFE_ACTIONS};
        net=make_puffernet(weights,1,OBS_SIZE,64,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights()){
            fputs("checkpoint layout mismatch\n",stderr);return 2;
        }
    }
    char *script=read_file("ocean/webnav_email/browser.js");if(!script)return 2;
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    srand(12345);
    unsigned evaluated=0;
    for(unsigned task=0;task<10;task++){
        if(argc==4&&strcmp(argv[3],tasks[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",tasks[task]);
        if(!realpath(file,resolved)){
            fprintf(stderr,"original page missing: %s\n",file);return 2;
        }
        snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        if(web_cdp_start_ready(&c,chrome,url,
           "Boolean(document.readyState==='complete'&&window.core&&core.cover_div)"))return 2;
        cJSON *j=web_cdp_eval(&c,script);
        if(!j||!cJSON_IsTrue(j)){
            fprintf(stderr,"browser fixture failed: %s\n",tasks[task]);return 1;
        }
        cJSON_Delete(j);
        unsigned wins=0,actions=0,unhittable=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=900000u+task*10000u+ep;
            char js[128];snprintf(js,sizeof js,"__wfe.reset(%u)",seed);
            j=web_cdp_eval(&c,js);WFView v;int done=0;float raw=0;
            if(!j||browser_view(j,&v,&done,&raw)){
                fprintf(stderr,"bad public reset: %s seed=%u\n",tasks[task],seed);return 1;
            }
            cJSON_Delete(j);
            WFEHistory history={0};
            if(net)for(unsigned layer=0;layer<(unsigned)net->mingru->num_layers;layer++)
                memset(net->mingru->state+layer*64,0,64*sizeof(float));
            for(unsigned step=1;step<=120&&!done;step++){
                WFECatalog catalog;unsigned char mask[WFE_ACTIONS];
                int choice=choose(net,&v,&history,&catalog,mask,random_policy);
                if(choice<0||choice>=WFE_ACTIONS||!mask[choice])return 1;
                unsigned now=step*250u;
                snprintf(js,sizeof js,"__wfe.tick(%u)",now);
                j=web_cdp_eval(&c,js);if(!j)return 1;cJSON_Delete(j);
                if(now<v.deadline_ms&&act(&c,&v,&history,&catalog,choice,&unhittable))return 1;
                j=web_cdp_eval(&c,"__wfe.snapshot()");
                if(!j||browser_view(j,&v,&done,&raw)){
                    fprintf(stderr,"bad public step: %s seed=%u step=%u\n",tasks[task],seed,step);return 1;
                }
                cJSON_Delete(j);actions++;
            }
            if(!done){
                fprintf(stderr,"episode never ended: %s seed=%u\n",tasks[task],seed);return 1;
            }
            wins+=raw>=0.999f;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f,\"actions\":%u,\"unhittable\":%u,\"policy\":\"%s H64/L1; original Chromium pages\"}\n",
            tasks[task],episodes,wins,(double)wins/episodes,actions,unhittable,
            random_policy?"random":"greedy");fflush(stdout);
        evaluated+=episodes;web_cdp_close(&c);
    }
    free(script);if(net)free_puffernet(net);free(weights);
    return evaluated?0:2;
}
