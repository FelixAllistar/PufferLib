#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include "public_controller.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
static const cJSON *field(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(v);return v;
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsString(v));return v->valuestring;
}
static double num(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);
    fclose(f);return s;
}
static cJSON *eval(WebCdp *c,const char *expression){
    cJSON *j=web_cdp_eval(c,expression);assert(j);return j;
}
static void compare(WFLoaded *f,const cJSON *j,unsigned seed,unsigned phase){
    WFView view;assert(!wf_observe(f,0,&view));
    assert(!strcmp(wf_text_get(&view,view.instruction),str(j,"query")));
    assert(view.deadline_ms==(unsigned)num(j,"deadline"));
    const cJSON *board=field(j,"board");
    assert(cJSON_IsArray(board)&&cJSON_GetArraySize(board)==9&&view.count==9);
    for(unsigned i=0;i<9;i++){
        const cJSON *cell=cJSON_GetArrayItem(board,(int)i);
        assert(cell&&cJSON_IsNumber(cell));
        const WFNode *node=&view.nodes[i];
        assert(node->ref==i+1&&node->role==WF_CELL);
        const char *value=wf_text_get(&view,node->value);
        const char *expected=cell->valueint==1?"X":cell->valueint==-1?"O":"";
        if(!value||strcmp(value,expected)){
            fprintf(stderr,"board cell mismatch seed=%u phase=%u cell=%u browser=%s model=%s\n",
                    seed,phase,i,expected,value?value:"(invalid)");abort();
        }
    }
    if(f->words[32]!=(unsigned)num(j,"rng")){
        fprintf(stderr,"RNG mismatch seed=%u phase=%u browser=%u model=%u\n",
                seed,phase,(unsigned)num(j,"rng"),f->words[32]);abort();
    }
    assert(!!f->words[WF_STATUS]==boolean(j,"done"));
    if(boolean(j,"done")&&
       (fabs(real(f->words[WF_RAW_REWARD])-num(j,"raw"))>1e-6||
        fabs(real(f->words[WF_TIMED_REWARD])-num(j,"reward"))>1e-5)){
        fprintf(stderr,"reward mismatch seed=%u phase=%u browser=%g/%g model=%g/%g\n",
                seed,phase,num(j,"raw"),num(j,"reward"),
                real(f->words[WF_RAW_REWARD]),real(f->words[WF_TIMED_REWARD]));abort();
    }
}
static void action(WebCdp *c,WFLoaded *f,unsigned seed,unsigned kind,
                   unsigned ref,unsigned now,unsigned phase){
    char js[128];
    snprintf(js,sizeof js,"__board.advance(%u);__board.act(%u,%u)",now,kind,ref);
    cJSON *j=eval(c,js);
    WFAction a={.kind=kind,.target=ref,.elapsed_ms=now};
    assert(!wf_apply(f,0,&a));
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    compare(f,j,seed,phase);cJSON_Delete(j);
}
static void public_step(WebCdp *c,WFLoaded *f,unsigned seed,
                        unsigned now,unsigned phase){
    WFView view;assert(!wf_observe(f,0,&view));
    WFAction a={.elapsed_ms=now};
    assert(!board_public_action(&view,&a));
    action(c,f,seed,a.kind,a.target,now,phase);
}
static unsigned first_empty(const WFView *v){
    for(unsigned i=0;i<9;i++){
        const char *value=wf_text_get(v,v->nodes[i].value);
        if(value&&!value[0])return i+1;
    }
    assert(0);return 0;
}
static unsigned first_marked(const WFView *v){
    for(unsigned i=0;i<9;i++){
        const char *value=wf_text_get(v,v->nodes[i].value);
        if(value&&value[0])return i+1;
    }
    return 0;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):25;
    if(!episodes||episodes>200)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/board/libboard.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/board/browser.js");
    char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
    snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/tic-tac-toe.html");
    assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
    WebCdp c={.input=-1,.output=-1,.pid=-1};
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
    cJSON *installed=eval(&c,script);assert(cJSON_IsTrue(installed));cJSON_Delete(installed);
    unsigned compared=0,marked_clicks=0,wins=0,draws=0,losses=0,timeouts=0;
    for(unsigned ep=0;ep<episodes;ep++){
        unsigned seed=960000+ep*137,phase=0;char js[64];
        snprintf(js,sizeof js,"__board.reset(%u)",seed);
        cJSON *j=eval(&c,js);
        for(unsigned lane=0;lane<f.api->batch_lanes;lane++){
            uint32_t *r=f.words+(size_t)lane*ROW;
            r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
        }
        assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
        compare(&f,j,seed,phase);cJSON_Delete(j);
        if(ep%5==2){
            action(&c,&f,seed,WF_WAIT,0,10000,++phase);
        }else{
            if(ep%5==1){
                WFView view;assert(!wf_observe(&f,0,&view));
                unsigned marked=first_marked(&view);
                if(!marked){
                    public_step(&c,&f,seed,100,++phase);
                    assert(!wf_observe(&f,0,&view));
                    marked=first_marked(&view);
                }
                if(f.words[WF_STATUS]==WF_RUNNING){
                    action(&c,&f,seed,WF_CLICK,marked,150,++phase);marked_clicks++;
                }
            }
            for(unsigned move=0;move<5&&f.words[WF_STATUS]==WF_RUNNING;move++){
                unsigned now=200+move*400;
                if(ep%5==3){
                    WFView view;assert(!wf_observe(&f,0,&view));
                    action(&c,&f,seed,WF_CLICK,first_empty(&view),now,++phase);
                }else public_step(&c,&f,seed,now,++phase);
            }
        }
        assert(f.words[WF_STATUS]!=WF_RUNNING);
        float reward=real(f.words[WF_RAW_REWARD]);
        if(reward==1.0f)wins++;
        else if(reward==-0.5f)draws++;
        else if(reward==-0.75f)losses++;
        else if(reward==-1.0f)timeouts++;
        else assert(0);
        compared++;
    }
    web_cdp_close(&c);free(script);wf_close(&f);
    printf("board original-page episodes compared: %u (%u wins, %u draws, %u losses, %u timeouts, %u marked clicks)\n",
           compared,wins,draws,losses,timeouts,marked_clicks);
    return 0;
}
