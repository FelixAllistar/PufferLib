#define _GNU_SOURCE
#include "../../src/puffercpu.c"
#include "webnav_numeric.h"
#include "../webnav/cdp.h"
#include <limits.h>
#include <sys/prctl.h>

static const char *tasks[]={"ascending-numbers","find-greatest","generate-number",
    "guess-number","hot-cold","number-checkboxes","odd-or-even",
    "simple-algebra","simple-arithmetic"};
static const cJSON *field(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static const char *string(const cJSON *j,const char *key){
    const cJSON *x=field(j,key);return cJSON_IsString(x)?x->valuestring:NULL;
}
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
    if(count<0||(unsigned)count>WNN_NODES)return -1;
    for(int i=0;i<count;i++){
        const cJSON *src=cJSON_GetArrayItem(nodes,i);
        const char *name=string(src,"name"),*value=string(src,"value");
        double ref,role,flags,x,y,width,height,start,end,capacity;
        if(!name||!value||field(src,"goal")||field(src,"target")||
           number(src,"ref",&ref)||number(src,"role",&role)||
           number(src,"flags",&flags)||number(src,"x",&x)||number(src,"y",&y)||
           number(src,"width",&width)||number(src,"height",&height)||
           number(src,"selectionStart",&start)||number(src,"selectionEnd",&end)||
           number(src,"capacity",&capacity)||ref<1||ref>WNN_NODES||
           role<0||role>WF_TEXTAREA||flags<0||flags>255||
           width<0||height<0||start<0||end<start||capacity<0)return -1;
        WFNode *n=v->nodes+v->count++;
        n->ref=(uint32_t)ref;n->role=(uint32_t)role;n->flags=(uint32_t)flags;
        n->x=(float)x;n->y=(float)y;n->width=(float)width;n->height=(float)height;
        n->selection_start=(uint32_t)start;n->selection_end=(uint32_t)end;
        n->capacity=(uint32_t)capacity;
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    *done=cJSON_IsTrue(finished);*raw=(float)reward;
    return wf_view_valid(v)?0:-1;
}
static int choose(PufferNet *net,const WFView *v,Env *env,
                  float out[OBS_SIZE],unsigned char mask[WNN_ACTIONS],uint32_t *rng){
    wnn_project(v,env,0,out,mask);
    if(!net){
        unsigned legal[WNN_ACTIONS],count=0;
        for(unsigned i=0;i<WNN_ACTIONS;i++)if(mask[i])legal[count++]=i;
        *rng=*rng*1664525u+1013904223u;
        return (int)legal[(*rng>>8)%count];
    }
    linear(net->encoder,out);mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    int best=0;float score=-INFINITY;
    for(unsigned i=0;i<WNN_ACTIONS;i++)if(mask[i]&&net->decoder->output[i]>score){
        best=(int)i;score=net->decoder->output[i];
    }
    return best;
}
static int eval_true(WebCdp *c,const char *expression){
    cJSON *j=web_cdp_eval(c,expression);
    int okay=j&&cJSON_IsTrue(j);cJSON_Delete(j);return okay?0:-1;
}
static int point(WebCdp *c,unsigned ref,double *x,double *y){
    char js[80];snprintf(js,sizeof js,"__numppo.point(%u)",ref);
    cJSON *j=web_cdp_eval(c,js);
    if(!j||cJSON_IsNull(j)||number(j,"x",x)||number(j,"y",y)){
        cJSON_Delete(j);return -1;
    }
    cJSON_Delete(j);return 0;
}
static int mouse(WebCdp *c,double x,double y,int click){
    cJSON *p=cJSON_CreateObject();if(!p)return -1;
    cJSON_AddStringToObject(p,"type","mouseMoved");
    cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);
    cJSON *r=web_cdp_call(c,"Input.dispatchMouseEvent",p);/* consumes p */
    if(!r)return -1;cJSON_Delete(r);
    return click?web_cdp_click(c,x,y):0;
}
static int key_event(WebCdp *c,const char *type,const char *key,unsigned code,unsigned modifiers){
    cJSON *p=cJSON_CreateObject();if(!p)return -1;
    cJSON_AddStringToObject(p,"type",type);cJSON_AddStringToObject(p,"key",key);
    cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"modifiers",modifiers);
    cJSON *r=web_cdp_call(c,"Input.dispatchKeyEvent",p);
    if(!r)return -1;cJSON_Delete(r);return 0;
}
static int key(WebCdp *c,const char *name,unsigned code){
    return key_event(c,"keyDown",name,code,0)||key_event(c,"keyUp",name,code,0);
}
static int select_all(WebCdp *c){
    return key_event(c,"keyDown","Control",17,0)||
           key_event(c,"keyDown","a",65,2)||
           key_event(c,"keyUp","a",65,2)||
           key_event(c,"keyUp","Control",17,0);
}
static int insert_text(WebCdp *c,const char *s){
    cJSON *p=cJSON_CreateObject();if(!p)return -1;
    cJSON_AddStringToObject(p,"text",s);
    cJSON *r=web_cdp_call(c,"Input.insertText",p);
    if(!r)return -1;cJSON_Delete(r);return 0;
}
static int perform(WebCdp *c,const WFView *v,WFAction a){
    if(a.kind==WF_WAIT)return 0;
    if(a.kind==WF_CLICK||a.kind==WF_POINTER_MOVE){
        double x,y;
        if(a.target==wnn_canvas(v)){
            x=a.arg0;y=a.arg1+50.0;
        }else if(point(c,a.target,&x,&y))return -1;
        return mouse(c,x,y,a.kind==WF_CLICK);
    }
    char js[80];snprintf(js,sizeof js,"__numppo.focus(%u)",a.target);
    if(eval_true(c,js))return -1;
    switch(a.kind){
    case WF_INSERT: return insert_text(c,a.text);
    case WF_SELECT_ALL: return select_all(c);
    case WF_BACKSPACE: return key(c,"Backspace",8);
    case WF_DELETE: return key(c,"Delete",46);
    case WF_LEFT: return key(c,"ArrowLeft",37);
    case WF_RIGHT: return key(c,"ArrowRight",39);
    case WF_HOME: return key(c,"Home",36);
    case WF_END: return key(c,"End",35);
    case WF_ENTER: return key(c,"Enter",13);
    default: return -1;
    }
}
static size_t align8(size_t n){return (n+7u)&~7u;}
static size_t expected_weights(void){
    size_t n=align8(64u*OBS_SIZE);
    n=align8(n+64u*(WNN_ACTIONS+1u));
    return align8(n+3u*64u*64u);
}
int main(int argc,char **argv){
    if(argc<3||argc>4){
        fprintf(stderr,"usage: %s CHECKPOINT|random EPISODES [TASK_NAME]\n",argv[0]);return 2;
    }
    (void)prctl(PR_SET_DUMPABLE,0,0,0,0);
    char *end=NULL;unsigned long parsed=strtoul(argv[2],&end,10);
    if(!end||*end||parsed<1||parsed>1000)return 2;
    int random_policy=!strcmp(argv[1],"random");
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy){
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()){
            fprintf(stderr,"expected H64/L1 numeric checkpoint with %zu floats\n",expected_weights());
            free(weights);return 2;
        }
        int sizes[]={WNN_ACTIONS};net=make_puffernet(weights,1,OBS_SIZE,64,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights())return 2;
    }
    char *script=read_file("ocean/webnav_numeric/browser.js");if(!script)return 2;
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    unsigned evaluated=0;uint32_t rng=91037;
    for(unsigned task=0;task<9;task++){
        if(argc==4&&strcmp(argv[3],tasks[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",tasks[task]);
        if(!realpath(file,resolved)){fprintf(stderr,"original page missing: %s\n",file);return 2;}
        snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        if(web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"))return 2;
        cJSON *j=web_cdp_eval(&c,script);
        if(!j||!cJSON_IsTrue(j)){fprintf(stderr,"browser adapter failed: %s\n",tasks[task]);return 1;}
        cJSON_Delete(j);
        unsigned wins=0,actions=0;
        for(unsigned ep=0;ep<(unsigned)parsed;ep++){
            unsigned seed=(getenv("WEBNAV_SEED_OFFSET")?
                (unsigned)strtoul(getenv("WEBNAV_SEED_OFFSET"),NULL,10):300000u)+
                task*10000u+ep;
            char js[128];snprintf(js,sizeof js,"__numppo.reset(%u,%u)",seed,task);
            j=web_cdp_eval(&c,js);WFView v;int done=0;float raw=0;
            if(!j||browser_view(j,&v,&done,&raw)){
                fprintf(stderr,"bad public reset: %s seed=%u\n",tasks[task],seed);return 1;
            }
            cJSON_Delete(j);
            Env history={0};history.x[0]=history.last_x[0]=77;
            history.y[0]=history.last_y[0]=62;
            if(net)memset(net->mingru->state,0,64*sizeof(float));
            for(unsigned step=1;step<=120&&!done;step++){
                float obs[OBS_SIZE];unsigned char mask[WNN_ACTIONS];
                int choice=choose(net,&v,&history,obs,mask,&rng);
                if(choice<0||choice>=(int)WNN_ACTIONS||!mask[choice])return 1;
                char payload[16]={0};WFAction a=wnn_action(&history,0,&v,(unsigned)choice,payload);
                snprintf(js,sizeof js,"__numppo.tick(%u)",a.elapsed_ms);
                if(eval_true(&c,js))return 1;
                if(a.elapsed_ms<v.deadline_ms&&perform(&c,&v,a)){
                    fprintf(stderr,"browser action failed: %s seed=%u step=%u choice=%d\n",
                        tasks[task],seed,step,choice);return 1;
                }
                j=web_cdp_eval(&c,"__numppo.snapshot()");
                if(!j||browser_view(j,&v,&done,&raw)){
                    fprintf(stderr,"bad public step: %s seed=%u step=%u\n",tasks[task],seed,step);return 1;
                }
                cJSON_Delete(j);v.elapsed_ms=a.elapsed_ms;actions++;
            }
            if(!done){fprintf(stderr,"episode never ended: %s seed=%u\n",tasks[task],seed);return 1;}
            wins+=raw>=0.999f;
        }
        printf("{\"task\":\"%s\",\"episodes\":%lu,\"successes\":%u,\"full_credit_rate\":%.6f,\"actions\":%u,\"policy\":\"%s H64/L1; original Chromium pages\"}\n",
            tasks[task],parsed,wins,(double)wins/parsed,actions,random_policy?"random":"greedy");
        fflush(stdout);evaluated+=(unsigned)parsed;web_cdp_close(&c);
    }
    free(script);if(net)free_puffernet(net);free(weights);
    return evaluated?0:2;
}
