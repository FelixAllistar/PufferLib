#define _GNU_SOURCE
#include "../../src/puffercpu.c"
#include "webnav_forms.h"
#include "../webnav/cdp.h"
#include <limits.h>
#include <sys/prctl.h>

static const cJSON *item(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static const char *str(const cJSON *j,const char *key){const cJSON *x=item(j,key);return cJSON_IsString(x)?x->valuestring:NULL;}
static int boolean(const cJSON *j,const char *key){return cJSON_IsTrue(item(j,key));}
static int number(const cJSON *j,const char *key,double *out){
    const cJSON *x=item(j,key);if(!cJSON_IsNumber(x))return -1;*out=x->valuedouble;return 0;
}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *s=calloc((size_t)n+1,1);
    if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}
    fclose(f);return s;
}
/* The JS expression used below deletes private goal and popup-mode fields
 * before CDP serializes them. Only public controls enter this projection. */
static int browser_view(const cJSON *j,WFView *v,int *done,float *raw){
    const char *query=str(j,"query");double deadline,elapsed,reward;
    const cJSON *fields=item(j,"fields"),*statics=item(j,"statics");
    if(!query||number(j,"deadline",&deadline)||number(j,"elapsed",&elapsed)||
       number(j,"raw",&reward)||!cJSON_IsBool(item(j,"done"))||
       !cJSON_IsArray(fields)||!cJSON_IsArray(statics)||
       cJSON_GetArraySize(fields)+cJSON_GetArraySize(statics)>WFF_NODES||
       item(j,"popup_mode"))return -1;
    wf_view_init(v,(uint32_t)elapsed,(uint32_t)deadline);
    if(wf_text_add(v,query,strlen(query),&v->instruction))return -1;
    unsigned nf=(unsigned)cJSON_GetArraySize(fields),ns=(unsigned)cJSON_GetArraySize(statics);
    for(unsigned i=0;i<nf;i++){
        const cJSON *s=cJSON_GetArrayItem(fields,i);double role,start,end;
        const char *name=str(s,"name"),*value=str(s,"value");
        if(!name||!value||item(s,"goal")||number(s,"role",&role)||
           number(s,"start",&start)||number(s,"end",&end)||
           !cJSON_IsBool(item(s,"enabled"))||!cJSON_IsBool(item(s,"focused")))return -1;
        WFNode *n=v->nodes+v->count++;n->ref=i+1;n->role=(uint32_t)role;
        n->flags=WF_VISIBLE|WF_CLICKABLE|(boolean(s,"enabled")?WF_ENABLED:0)|
                 (boolean(s,"focused")?WF_FOCUSED:0);
        n->selection_start=(uint32_t)start;n->selection_end=(uint32_t)end;n->capacity=256;
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    for(unsigned i=0;i<ns;i++){
        const cJSON *s=cJSON_GetArrayItem(statics,i);double role;
        const char *name=str(s,"name"),*value=str(s,"value");
        if(!name||!value||number(s,"role",&role)||!cJSON_IsBool(item(s,"visible"))||
           !cJSON_IsBool(item(s,"enabled")))return -1;
        if(boolean(s,"visible")&&!boolean(j,"popup"))continue;
        WFNode *n=v->nodes+v->count++;n->ref=nf+i+1;n->role=(uint32_t)role;n->flags=WF_VISIBLE;
        if(n->role==WF_BUTTON)n->flags|=WF_CLICKABLE|(boolean(s,"enabled")?WF_ENABLED:0);
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    *done=boolean(j,"done");*raw=(float)reward;
    return wf_view_valid(v)?0:-1;
}
static cJSON *public_export(WebCdp *c,const char *expression){
    return web_cdp_eval(c,expression);
}
static cJSON *key_event(WebCdp *c,const char *type,const char *key,unsigned code,unsigned modifiers){
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",type);
    cJSON_AddStringToObject(p,"key",key);cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);cJSON_AddNumberToObject(p,"modifiers",modifiers);
    return web_cdp_call(c,"Input.dispatchKeyEvent",p);
}
static int select_all(WebCdp *c){
    cJSON *r=key_event(c,"keyDown","Control",17,0);if(!r)return -1;cJSON_Delete(r);
    r=key_event(c,"keyDown","a",65,2);if(!r)return -1;cJSON_Delete(r);
    r=key_event(c,"keyUp","a",65,2);if(!r)return -1;cJSON_Delete(r);
    r=key_event(c,"keyUp","Control",17,0);if(!r)return -1;cJSON_Delete(r);
    return 0;
}
static int insert_text(WebCdp *c,const char *text){
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",text);
    cJSON *r=web_cdp_call(c,"Input.insertText",p);if(!r)return -1;cJSON_Delete(r);return 0;
}
static int click_ref(WebCdp *c,unsigned ref){
    char js[64];snprintf(js,sizeof js,"__fm.point(%u)",ref);
    cJSON *p=web_cdp_eval(c,js);double x,y;
    if(!p||cJSON_IsNull(p)||!boolean(p,"visible")||number(p,"x",&x)||number(p,"y",&y)){
        cJSON_Delete(p);return -1;
    }
    cJSON_Delete(p);return web_cdp_click(c,x,y);
}
static int choose(PufferNet *net,const WFView *v,WFFCatalog *cat,unsigned char mask[WFF_ACTIONS]){
    float obs[OBS_SIZE];wff_catalog(v,cat);wff_project(v,cat,obs,mask);
    linear(net->encoder,obs);mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    int best=0;float score=-INFINITY;
    for(unsigned i=0;i<WFF_ACTIONS;i++)if(mask[i]&&net->decoder->output[i]>score){best=(int)i;score=net->decoder->output[i];}
    return best;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    if(argc<3||argc>4){fprintf(stderr,"usage: %s CHECKPOINT EPISODES_PER_TASK [TASK_NAME]\n",argv[0]);return 2;}
    unsigned episodes=(unsigned)strtoul(argv[2],NULL,10);if(!episodes||episodes>1000)return 2;
    unsigned trace_ep=getenv("WEBNAV_TRACE_EP")?(unsigned)strtoul(getenv("WEBNAV_TRACE_EP"),NULL,10):0;
    Weights *weights=load_weights(argv[1]);if(!weights){fprintf(stderr,"checkpoint not found\n");return 2;}
    srand(12345);int sizes[]={WFF_ACTIONS};
    PufferNet *net=make_puffernet(weights,1,OBS_SIZE,64,1,sizes,1);
    char *script=read_file("ocean/webnav/families/forms/browser.js");if(!script)return 2;
    const char *names[]={"enter-text-dynamic","enter-text-2","enter-password","text-transform",
        "copy-paste","copy-paste-2","read-table-2","login-user-popup"};
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    unsigned total=0;
    for(unsigned task=0;task<8;task++){
        if(argc==4&&strcmp(argv[3],names[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",names[task]);
        if(!realpath(file,resolved))return 2;snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        if(web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"))return 2;
        cJSON *j=web_cdp_eval(&c,script);if(!j||!cJSON_IsTrue(j))return 2;cJSON_Delete(j);
        unsigned wins=0,actions=0,unhittable=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=900000+task*1000+ep;char js[320];
            snprintf(js,sizeof js,"(()=>{let s=__fm.reset(%u);s.fields.forEach(f=>delete f.goal);delete s.popup_mode;return s})()",seed);
            j=public_export(&c,js);WFView v;int done=0;float raw=0;
            if(!j||browser_view(j,&v,&done,&raw)){fprintf(stderr,"bad public reset %s seed=%u\n",names[task],seed);return 1;}cJSON_Delete(j);
            for(unsigned layer=0;layer<(unsigned)net->mingru->num_layers;layer++)
                memset(net->mingru->state+layer*64,0,64*sizeof(float));
            for(unsigned step=1;step<=100&&!done;step++){
                WFFCatalog cat;unsigned char mask[WFF_ACTIONS];int choice=choose(net,&v,&cat,mask);
                if(getenv("WEBNAV_TRACE")&&ep==trace_ep&&step<=12)
                    fprintf(stderr,"task=%s seed=%u step=%u choice=%d query=%s\n",names[task],seed,step,choice,wf_text_get(&v,v.instruction));
                unsigned now=step*250;
                snprintf(js,sizeof js,"__fm.advance(%u)",now);j=web_cdp_eval(&c,js);if(!j)return 1;cJSON_Delete(j);
                if(now<v.deadline_ms){
                    int error=0;
                    if(choice>=1&&choice<=WFF_NODES)error=click_ref(&c,v.nodes[choice-1].ref);
                    else if(choice==17)error=select_all(&c);
                    else if(choice>=18)error=insert_text(&c,cat.c[choice-18].text);
                    if(error)unhittable++;
                }
                j=public_export(&c,"(()=>{let s=__fm.export();s.fields.forEach(f=>delete f.goal);delete s.popup_mode;return s})()");
                if(!j||browser_view(j,&v,&done,&raw)){fprintf(stderr,"bad public step %s seed=%u step=%u\n",names[task],seed,step);return 1;}
                cJSON_Delete(j);actions++;
            }
            if(!done){fprintf(stderr,"episode never ended %s seed=%u\n",names[task],seed);return 1;}
            if(getenv("WEBNAV_FAILURES")&&raw<0.999f)
                fprintf(stderr,"failure task=%s seed=%u query=%s\n",names[task],seed,wf_text_get(&v,v.instruction));
            wins+=raw>=0.999f;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f,\"actions\":%u,\"unhittable\":%u,\"policy\":\"greedy H64/L1 checkpoint; original Chromium pages\"}\n",
            names[task],episodes,wins,(double)wins/episodes,actions,unhittable);fflush(stdout);
        total+=episodes;web_cdp_close(&c);
    }
    free(script);free_puffernet(net);free(weights);return total?0:2;
}
