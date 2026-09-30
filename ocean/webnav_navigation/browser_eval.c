#define _GNU_SOURCE
#include "../../src/puffercpu.c"
#include "webnav_navigation.h"
#include "../webnav/cdp.h"
#include <limits.h>
#include <sys/prctl.h>

static const char *panel_tasks[]={"click-tab","click-tab-2","click-tab-2-easy",
    "click-tab-2-medium","click-tab-2-hard","click-collapsible",
    "click-collapsible-nodelay","click-collapsible-2","click-collapsible-2-nodelay"};
static const char *menu_tasks[]={"click-menu","click-menu-2"};

static const cJSON *field(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static const char *string(const cJSON *j,const char *key){const cJSON *x=field(j,key);return cJSON_IsString(x)?x->valuestring:NULL;}
static int number(const cJSON *j,const char *key,double *out){
    const cJSON *x=field(j,key);if(!cJSON_IsNumber(x))return -1;*out=x->valuedouble;return 0;
}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *s=calloc((size_t)n+1,1);
    if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}
    fclose(f);return s;
}
/* Parse only the public projection. References remain stable across visibility
 * changes, while action indexes address the current observation ordinal. */
static int browser_view(const cJSON *j,WFView *v,int *done,float *raw){
    const char *instruction=string(j,"instruction");
    const cJSON *nodes=field(j,"nodes"),*finished=field(j,"done");
    double deadline,reward;
    if(!instruction||!cJSON_IsArray(nodes)||!cJSON_IsBool(finished)||
       number(j,"deadline",&deadline)||number(j,"raw",&reward)||
       deadline<=0||deadline>UINT32_MAX||!isfinite(reward))return -1;
    wf_view_init(v,0,(uint32_t)deadline);
    if(wf_text_add(v,instruction,strlen(instruction),&v->instruction))return -1;
    int count=cJSON_GetArraySize(nodes);
    if(count<0)return -1;
    for(int i=0;i<count;i++){
        if(v->count==WFN_NODES){v->omitted++;continue;}
        const cJSON *src=cJSON_GetArrayItem(nodes,i);
        const char *name=string(src,"name"),*value=string(src,"value");
        double ref,parent,role,flags,x,y,width,height;
        if(!name||!value||field(src,"goal")||field(src,"target")||
           number(src,"ref",&ref)||number(src,"parent",&parent)||
           number(src,"role",&role)||number(src,"flags",&flags)||
           number(src,"x",&x)||number(src,"y",&y)||
           number(src,"width",&width)||number(src,"height",&height)||
           ref<1||ref>255||parent<0||parent>255||role<0||role>WF_TEXTAREA||
           flags<0||flags>255||width<0||height<0)return -1;
        WFNode *n=&v->nodes[v->count++];
        n->ref=(uint32_t)ref;n->parent=(uint32_t)parent;
        n->role=(uint32_t)role;n->flags=(uint32_t)flags;
        n->x=(float)x;n->y=(float)y;n->width=(float)width;n->height=(float)height;
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    *done=cJSON_IsTrue(finished);*raw=(float)reward;return 0;
}
static int move_mouse(WebCdp *c,double x,double y){
    cJSON *p=cJSON_CreateObject();if(!p)return -1;
    cJSON_AddStringToObject(p,"type","mouseMoved");
    cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);
    /* web_cdp_call consumes p. */
    cJSON *r=web_cdp_call(c,"Input.dispatchMouseEvent",p);
    if(!r)return -1;cJSON_Delete(r);return 0;
}
static int point_action(WebCdp *c,unsigned ref,int move,int menu_click,unsigned *unhittable){
    char js[80];snprintf(js,sizeof js,"__nav.point(%u)",ref);
    cJSON *j=web_cdp_eval(c,js);double x,y;
    if(!j)return -1;
    if(cJSON_IsNull(j)||!cJSON_IsTrue(field(j,"visible"))||
       number(j,"x",&x)||number(j,"y",&y)){
        (*unhittable)++;cJSON_Delete(j);return 0;
    }
    cJSON_Delete(j);
    if(move)return move_mouse(c,x,y);
    /* A physical menu click reaches its target by moving the pointer first.
     * This clears stale hover when the Menu opener is clicked again. */
    if(menu_click&&move_mouse(c,x,y))return -1;
    return web_cdp_click(c,x,y);
}
static int choose(PufferNet *net,const WFView *v,const unsigned char visited[256],int random_policy){
    float obs[OBS_SIZE];unsigned char mask[WFN_ACTIONS];
    wfn_project(v,visited,obs,mask);
    if(random_policy){
        unsigned count=0;for(unsigned i=0;i<WFN_ACTIONS;i++)count+=!!mask[i];
        if(!count)return 0;
        unsigned pick=(unsigned)rand()%count;
        for(unsigned i=0;i<WFN_ACTIONS;i++)if(mask[i]&&!pick--)return (int)i;
        return 0;
    }
    linear(net->encoder,obs);mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    int best=0;float score=-INFINITY;
    for(unsigned i=0;i<WFN_ACTIONS;i++)
        if(mask[i]&&net->decoder->output[i]>score){best=(int)i;score=net->decoder->output[i];}
    return best;
}
static size_t align8(size_t n){return (n+7u)&~7u;}
static size_t expected_weights(void){
    size_t n=align8(64u*OBS_SIZE);
    n=align8(n+64u*(WFN_ACTIONS+1u));
    return align8(n+3u*64u*64u);
}
int main(int argc,char **argv){
    if(argc<4||argc>5){fprintf(stderr,"usage: %s CHECKPOINT|random FAMILY EPISODES [TASK_NAME]\n",argv[0]);return 2;}
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    int family=!strcmp(argv[2],"panels")?0:!strcmp(argv[2],"menus")?1:-1;
    if(family<0){fprintf(stderr,"FAMILY must be panels or menus\n");return 2;}
    const char *const *tasks=family?menu_tasks:panel_tasks;
    unsigned task_count=family?2u:9u;
    char *end=NULL;unsigned long parsed=strtoul(argv[3],&end,10);
    if(!end||*end||parsed<1||parsed>1000)return 2;
    unsigned episodes=(unsigned)parsed;
    int random_policy=!strcmp(argv[1],"random");
    int trace=getenv("WEBNAV_TRACE")!=NULL;
    unsigned trace_ep=getenv("WEBNAV_TRACE_EP")?
        (unsigned)strtoul(getenv("WEBNAV_TRACE_EP"),NULL,10):0;
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy){
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()){
            fprintf(stderr,"expected H64/L1 navigation checkpoint with %zu floats\n",expected_weights());
            free(weights);return 2;
        }
        int sizes[]={WFN_ACTIONS};net=make_puffernet(weights,1,OBS_SIZE,64,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights()){
            fprintf(stderr,"checkpoint layout mismatch\n");return 2;
        }
    }
    srand(12345);
    char *script=read_file("ocean/webnav_navigation/browser.js");if(!script)return 2;
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    unsigned evaluated=0;
    for(unsigned task=0;task<task_count;task++){
        if(argc==5&&strcmp(argv[4],tasks[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",tasks[task]);
        if(!realpath(file,resolved)){fprintf(stderr,"original page missing: %s\n",file);return 2;}
        snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        if(web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"))return 2;
        cJSON *j=web_cdp_eval(&c,script);
        if(!j||!cJSON_IsTrue(j)){fprintf(stderr,"browser harness failed: %s\n",tasks[task]);return 1;}
        cJSON_Delete(j);
        unsigned wins=0,timeouts=0,actions=0,unhittable=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=(getenv("WEBNAV_SEED_OFFSET")?(unsigned)strtoul(getenv("WEBNAV_SEED_OFFSET"),NULL,10):300000u)+family*100000u+task*10000u+ep;
            char js[128];snprintf(js,sizeof js,"__nav.reset(%d,%u,%u)",family,seed,task);
            j=web_cdp_eval(&c,js);WFView v;int done=0;float raw=0;
            if(!j||browser_view(j,&v,&done,&raw)){
                fprintf(stderr,"bad public reset: %s seed=%u\n",tasks[task],seed);return 1;
            }
            cJSON_Delete(j);
            unsigned char visited[256]={0};
            if(trace&&ep==trace_ep)
                fprintf(stderr,"task=%s seed=%u instruction=%s nodes=%u\n",
                    tasks[task],seed,wf_text_get(&v,v.instruction),v.count);
            if(net)for(unsigned layer=0;layer<(unsigned)net->mingru->num_layers;layer++)
                memset(net->mingru->state+layer*64,0,64*sizeof(float));
            unsigned last_now=0;
            for(unsigned step=1;step<=100&&!done;step++){
                int action=choose(net,&v,visited,random_policy);
                if(action<0||action>WFN_NODES||
                   (action&&((unsigned)action>v.count))){
                    fprintf(stderr,"invalid action %d: %s seed=%u\n",action,tasks[task],seed);return 1;
                }
                unsigned kind=wfn_action_kind((unsigned)family,&v,(unsigned)action);
                WFNode selected={0};if(action)selected=v.nodes[action-1];
                if(trace&&ep==trace_ep&&step<=20)
                    fprintf(stderr,"  step=%u ordinal=%d ref=%u kind=%u role=%u name=%s\n",
                        step,action,selected.ref,kind,selected.role,
                        action?wf_text_get(&v,selected.name):"");
                if(action&&selected.ref<256)visited[selected.ref]=1;
                unsigned now=step*250u;last_now=now;
                snprintf(js,sizeof js,"__nav.tick(%u)",now);
                j=web_cdp_eval(&c,js);if(!j)return 1;cJSON_Delete(j);
                if(action&&now<v.deadline_ms){
                    if(point_action(&c,selected.ref,kind==WF_POINTER_MOVE,family==1,&unhittable))return 1;
                }
                j=web_cdp_eval(&c,family==1?
                    "new Promise(resolve=>setTimeout(()=>resolve(__nav.snapshot()),350))":
                    "__nav.snapshot()");
                if(!j||browser_view(j,&v,&done,&raw)){
                    fprintf(stderr,"bad public step: %s seed=%u step=%u\n",tasks[task],seed,step);return 1;
                }
                cJSON_Delete(j);v.elapsed_ms=now;actions++;
            }
            if(!done){fprintf(stderr,"episode never ended: %s seed=%u\n",tasks[task],seed);return 1;}
            wins+=raw>=0.999f;timeouts+=last_now>=v.deadline_ms&&raw<0.999f;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f,\"timeouts\":%u,\"actions\":%u,\"unhittable\":%u,\"policy\":\"%s H64/L1; original Chromium pages\"}\n",
            tasks[task],episodes,wins,(double)wins/episodes,timeouts,actions,unhittable,
            random_policy?"random":"greedy");fflush(stdout);
        evaluated+=episodes;web_cdp_close(&c);
    }
    free(script);if(net)free_puffernet(net);free(weights);
    return evaluated?0:2;
}
