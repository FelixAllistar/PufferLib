#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 4096u
#define NODE_BASE 64u
#define NODE_STRIDE 48u
#define QUERY_BASE 1600u

static double number(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;
}
static float f32(uint32_t bits){float value;memcpy(&value,&bits,sizeof value);return value;}
static cJSON *eval(WebCdp *c,const char *expression){cJSON *j=web_cdp_eval(c,expression);assert(j);return j;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void units(uint32_t *out,unsigned cap,const char *s){
    if(strlen(s)>=cap){fprintf(stderr,"Import text exceeds cap=%u len=%zu text=[%s]\n",cap,strlen(s),s);exit(1);}for(unsigned i=0;s[i];i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);out[i]=(unsigned char)s[i];}
}
static uint32_t mix(uint32_t x){
    x^=x>>16;x*=2146121005u;x^=x>>15;x*=2221713035u;x^=x>>16;return x;
}
static void import(uint32_t *r,unsigned task,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_SEED]=seed;
    r[WF_OP]=WF_OBSERVE;r[WF_STATUS]=WF_RUNNING;r[WF_DEADLINE]=(uint32_t)number(j,"deadline");
    r[32]=(uint32_t)number(j,"phase");r[34]=(uint32_t)number(j,"cursor");
    r[35]=(uint32_t)number(j,"selected");r[36]=(uint32_t)number(j,"secret");
    r[37]=(uint32_t)number(j,"limit");r[38]=(uint32_t)number(j,"mode");
    r[39]=(uint32_t)(int32_t)number(j,"expected");r[40]=(uint32_t)number(j,"feedback");
    r[41]=(uint32_t)number(j,"candidate");r[44]=mix(seed+(task==2?9:task));
    units(r+QUERY_BASE,256,string(j,"query"));
    const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");assert(cJSON_IsArray(nodes));
    r[33]=(uint32_t)cJSON_GetArraySize(nodes);assert(r[33]<=32);
    for(unsigned i=0;i<r[33];i++){
        const cJSON *n=cJSON_GetArrayItem(nodes,(int)i);uint32_t *out=r+NODE_BASE+i*NODE_STRIDE;
        out[0]=(uint32_t)number(n,"kind");out[1]=boolean(n,"selected");
        out[2]=boolean(n,"target");out[3]=(uint32_t)(int32_t)number(n,"number");
        const char *name=string(n,"name");
        if(task==2&&i==2)name="Generated";
        units(out+4,16,name);units(out+20,24,task==3&&i==2?"":string(n,"value"));
        out[44]=(uint32_t)number(n,"x");out[45]=(uint32_t)number(n,"y");
        out[46]=(uint32_t)number(n,"width");out[47]=(uint32_t)number(n,"height");
    }
}
static unsigned role(unsigned kind){
    static const unsigned roles[]={WF_BUTTON,WF_CHECKBOX,WF_INPUT,WF_CANVAS,WF_TEXT,WF_BUTTON};
    assert(kind<6);return roles[kind];
}
static void compare(WFLoaded *f,const cJSON *j,unsigned step,unsigned ref){
    const uint32_t *r=f->words;WFView v;assert(!wf_observe(f,0,&v));
    int mismatch=strcmp(wf_text_get(&v,v.instruction),string(j,"query"))!=0 ||
        number(j,"deadline")!=r[WF_DEADLINE];
    const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");
    unsigned visible_count=0;if(cJSON_IsArray(nodes))for(int i=0;i<cJSON_GetArraySize(nodes);i++)
        visible_count+=boolean(cJSON_GetArrayItem(nodes,i),"visible");
    mismatch|=!cJSON_IsArray(nodes)||visible_count!=v.count;
    if(!mismatch){unsigned vi=0;for(unsigned i=0;i<(unsigned)cJSON_GetArraySize(nodes);i++){
        const cJSON *n=cJSON_GetArrayItem(nodes,(int)i);if(!boolean(n,"visible"))continue;
        const WFNode *o=v.nodes+vi++;
        const char *name=wf_text_get(&v,o->name),*value=wf_text_get(&v,o->value);
        mismatch|=o->ref!=i+1||o->role!=role((unsigned)number(n,"kind"));
        mismatch|=!name||strcmp(name,string(n,"name"))!=0;
        mismatch|=!value||strcmp(value,string(n,"value"))!=0;
        mismatch|=!!(o->flags&WF_CHECKED)!=boolean(n,"selected")&&number(n,"kind")==1;
        mismatch|=!!(o->flags&WF_SELECTED)!=boolean(n,"selected")&&
            (number(n,"kind")==5||(r[WF_TASK]==6&&number(n,"kind")==0));
    }}
    if(mismatch){
        fprintf(stderr,"numeric browser mismatch task=%s step=%u ref=%u nodes=%u/%u\n",
            f->api->task_names[r[WF_TASK]],step,ref,
            cJSON_IsArray(nodes)?(unsigned)cJSON_GetArraySize(nodes):0,v.count);
        const char *actual_query=wf_text_get(&v,v.instruction);
        fprintf(stderr,"  query expected=[%s] actual=[%s] deadline expected=%.0f actual=%u\n",
            string(j,"query"),actual_query?actual_query:"<invalid>",number(j,"deadline"),r[WF_DEADLINE]);
        if(cJSON_IsArray(nodes))for(int i=0;i<cJSON_GetArraySize(nodes);i++){
            const cJSON *n=cJSON_GetArrayItem(nodes,i);
            if(!boolean(n,"visible"))continue;
            unsigned vi=0;for(int k=0;k<i;k++)vi+=boolean(cJSON_GetArrayItem(nodes,k),"visible");
            if(vi>=v.count)continue;
            const WFNode *o=v.nodes+vi;
            const char *name=wf_text_get(&v,o->name),*value=wf_text_get(&v,o->value);
            int bad=o->ref!=(unsigned)i+1||o->role!=role((unsigned)number(n,"kind"))||
                !name||strcmp(name,string(n,"name"))!=0||!value||strcmp(value,string(n,"value"))!=0;
            if(bad)fprintf(stderr,"  node[%d] ref=%u/%d role=%u/%u name=[%s]/[%s] value=[%s]/[%s]\n",
                i,o->ref,i+1,o->role,role((unsigned)number(n,"kind")),name?name:"<invalid>",
                string(n,"name"),value?value:"<invalid>",string(n,"value"));
        }
        exit(1);
    }
}
static void compare_snapshot(WFLoaded *f,const cJSON *j,unsigned step,unsigned ref){
    const uint32_t *r=f->words;int done=boolean(j,"done");
    if(done!=(r[WF_STATUS]!=WF_RUNNING)||(done&&
       (fabs(number(j,"raw")-f32(r[WF_RAW_REWARD]))>1e-6||
        fabs(number(j,"reward")-f32(r[WF_TIMED_REWARD]))>1e-5))){
        fprintf(stderr,"numeric reward mismatch task=%s step=%u ref=%u done=%d/%u raw=%g/%g reward=%g/%g\n",
            f->api->task_names[r[WF_TASK]],step,ref,done,r[WF_STATUS],number(j,"raw"),
            f32(r[WF_RAW_REWARD]),number(j,"reward"),f32(r[WF_TIMED_REWARD]));abort();
    }
}
static void js_edit(WebCdp *c,unsigned kind,unsigned ref,const char *text){
    char expression[256];snprintf(expression,sizeof expression,"__num.edit(%u,%u,\"%s\")",kind,ref,text?text:"");
    cJSON *j=eval(c,expression);assert(cJSON_IsTrue(j));cJSON_Delete(j);
}
static void step_action(WFLoaded *f,WebCdp *c,const WFAction *a,unsigned step){
    uint32_t *r=f->words;char js[256];snprintf(js,sizeof js,"__num.tick(%u)",a->elapsed_ms);
    cJSON *j=eval(c,js);cJSON_Delete(j);
    if(a->elapsed_ms<r[WF_DEADLINE]){
        if(a->kind==WF_CLICK){
            if(r[WF_TASK]==2&&a->target==1){
                uint32_t candidate=mix(r[44])%11;
                snprintf(js,sizeof js,"__num.forceCandidate(%u)",candidate);j=eval(c,js);assert(cJSON_IsTrue(j));cJSON_Delete(j);
            }
            if(r[WF_TASK]==4&&a->target==1){
                snprintf(js,sizeof js,"__num.tap(%u,%u)",a->arg0,a->arg1);j=eval(c,js);assert(cJSON_IsTrue(j));cJSON_Delete(j);
            }else{
                snprintf(js,sizeof js,"__num.point(%u)",a->target);j=eval(c,js);
                if(cJSON_IsNull(j)){fprintf(stderr,"missing original control task=%s ref=%u\n",f->api->task_names[r[WF_TASK]],a->target);abort();}
                double x=number(j,"x"),y=number(j,"y");cJSON_Delete(j);assert(!web_cdp_click(c,x,y));
            }
        }else if(a->kind==WF_POINTER_MOVE){
            snprintf(js,sizeof js,"__num.move(%u,%u)",a->arg0,a->arg1);j=eval(c,js);assert(cJSON_IsTrue(j));cJSON_Delete(j);
        }else if(a->kind==WF_INSERT)js_edit(c,2,a->target,a->text);
        else if(a->kind==WF_SELECT_ALL)js_edit(c,9,a->target,"");
        else if(a->kind==WF_BACKSPACE)js_edit(c,3,a->target,"");
        else if(a->kind==WF_DELETE)js_edit(c,4,a->target,"");
        else if(a->kind==WF_LEFT)js_edit(c,5,a->target,"");
        else if(a->kind==WF_RIGHT)js_edit(c,6,a->target,"");
        else if(a->kind==WF_HOME)js_edit(c,7,a->target,"");
        else if(a->kind==WF_END)js_edit(c,8,a->target,"");
    }
    assert(!wf_apply(f,0,a));for(unsigned lane=1;lane<8;lane++)memcpy(f->words+lane*ROW,f->words,ROW*sizeof(uint32_t));
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    j=eval(c,"__num.instance()");compare(f,j,step,a->target);cJSON_Delete(j);
    j=eval(c,"__num.snapshot()");compare_snapshot(f,j,step,a->target);cJSON_Delete(j);
}
static void act(WFLoaded *f,WebCdp *c,unsigned kind,unsigned ref,unsigned x,unsigned y,const char *text,unsigned elapsed,unsigned *step){
    WFAction a={.kind=kind,.target=ref,.arg0=x,.arg1=y,.elapsed_ms=elapsed,.text=text,.text_length=text?strlen(text):0};
    step_action(f,c,&a,(*step)++);
}
static int allowed(const uint32_t *r,unsigned candidate){
    if(r[38]==0)return candidate<r[37];if(r[38]==1)return candidate>r[37];
    if(r[38]==2)return candidate%2==1;return candidate%2==0;
}
static void solve(WFLoaded *f,WebCdp *c,unsigned *now,unsigned *step){
    uint32_t *r=f->words;char answer[32];
    switch(r[WF_TASK]){
    case 0:
        for(unsigned ref=1;ref<=5;ref++)act(f,c,WF_CLICK,ref,0,0,NULL,(*now+=100),step);
        break;
    case 1:{
        unsigned best=0;for(unsigned i=0;i<3;i++)if(r[NODE_BASE+i*NODE_STRIDE+2])best=i+1;
        assert(best);act(f,c,WF_CLICK,best,0,0,NULL,(*now+=100),step);act(f,c,WF_CLICK,4,0,0,NULL,(*now+=100),step);break;
    }
    case 2:
        for(unsigned i=0;i<128&&!r[WF_STATUS];i++){
            act(f,c,WF_CLICK,1,0,0,NULL,(*now+=100),step);if(allowed(r,r[41]))break;
        }
        if(r[WF_STATUS]==WF_RUNNING)act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);
        break;
    case 3:
        snprintf(answer,sizeof answer,"%u",r[36]);act(f,c,WF_INSERT,1,0,0,answer,(*now+=100),step);
        act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);break;
    case 4:
        act(f,c,WF_POINTER_MOVE,1,r[36]<=147?r[36]+7:r[36]-7,r[37],NULL,(*now+=100),step);
        act(f,c,WF_CLICK,1,r[36],r[37],NULL,(*now+=100),step);break;
    case 5:
        for(unsigned i=0;i<28;i++)if(r[NODE_BASE+i*NODE_STRIDE+2])act(f,c,WF_CLICK,i+1,0,0,NULL,(*now+=50),step);
        act(f,c,WF_CLICK,29,0,0,NULL,(*now+=100),step);break;
    case 6:
        for(unsigned row=0;row<3;row++){
            unsigned first=2*row,ref=r[NODE_BASE+first*NODE_STRIDE+2]?first+1:first+2;
            act(f,c,WF_CLICK,ref,0,0,NULL,(*now+=100),step);
        }
        act(f,c,WF_CLICK,7,0,0,NULL,(*now+=100),step);break;
    case 7: case 8:
        snprintf(answer,sizeof answer,"%d",(int32_t)r[39]);act(f,c,WF_INSERT,1,0,0,answer,(*now+=100),step);
        act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);break;
    }
    assert(r[WF_STATUS]!=WF_RUNNING);
}
static void fail_trace(WFLoaded *f,WebCdp *c,unsigned *now,unsigned *step){
    uint32_t *r=f->words;char answer[32];
    switch(r[WF_TASK]){
    case 0: act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);break;
    case 1: act(f,c,WF_CLICK,4,0,0,NULL,(*now+=100),step);break;
    case 2: act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);break;
    case 3:{
        unsigned wrong=(r[36]+1)%10;snprintf(answer,sizeof answer,"%u",wrong);
        act(f,c,WF_INSERT,1,0,0,answer,(*now+=100),step);act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);
        assert(r[WF_STATUS]==WF_RUNNING);snprintf(answer,sizeof answer,"%u",r[36]);
        act(f,c,WF_SELECT_ALL,1,0,0,NULL,(*now+=100),step);act(f,c,WF_INSERT,1,0,0,answer,(*now+=100),step);
        act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);break;
    }
    case 4:{
        unsigned x=0,y=0;uint32_t max=0;
        const unsigned corners[4][2]={{0,0},{154,0},{0,125},{154,125}};
        for(unsigned i=0;i<4;i++){int dx=(int)r[36]-(int)corners[i][0],dy=(int)r[37]-(int)corners[i][1];unsigned d=(unsigned)(dx*dx+dy*dy);if(d>max){max=d;x=corners[i][0];y=corners[i][1];}}
        act(f,c,WF_CLICK,1,x,y,NULL,(*now+=100),step);break;
    }
    case 5:{
        unsigned wrong=0;while(wrong<28&&r[NODE_BASE+wrong*NODE_STRIDE+2])wrong++;
        assert(wrong<28);act(f,c,WF_CLICK,wrong+1,0,0,NULL,(*now+=100),step);act(f,c,WF_CLICK,29,0,0,NULL,(*now+=100),step);break;
    }
    case 6:{
        unsigned ref=r[NODE_BASE+2]?2:1;act(f,c,WF_CLICK,ref,0,0,NULL,(*now+=100),step);act(f,c,WF_CLICK,7,0,0,NULL,(*now+=100),step);break;
    }
    case 7: case 8:
        snprintf(answer,sizeof answer,"%d",(int32_t)r[39]+1);act(f,c,WF_INSERT,1,0,0,answer,(*now+=100),step);
        act(f,c,WF_CLICK,2,0,0,NULL,(*now+=100),step);break;
    }
    assert(r[WF_STATUS]!=WF_RUNNING);
}
int main(int argc,char **argv){
    (void)prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],NULL,10):4;if(!episodes||episodes>200)return 2;
    WFLoaded f;char error[512];if(wf_open(&f,"build/webnav/families/numeric/libnumeric.so",error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    char *script=read_file("ocean/webnav/families/numeric/browser.js");unsigned total=0;
    for(unsigned task=0;task<9;task++){
        if(argc>2&&strcmp(argv[2],f.api->task_names[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=eval(&c,script);cJSON_Delete(j);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=410000+task*10000+ep;char js[192];
            snprintf(js,sizeof js,"__num.reset(%u,%u)",seed,task);j=eval(&c,js);import(f.words,task,seed,j);compare(&f,j,0,0);cJSON_Delete(j);
            for(unsigned lane=1;lane<8;lane++)memcpy(f.words+lane*ROW,f.words,ROW*sizeof(uint32_t));
            assert(!f.api->validate(f.words));unsigned now=0,step=1;
            if(ep%4==0)solve(&f,&c,&now,&step);
            else if(ep%4==1)fail_trace(&f,&c,&now,&step);
            else if(ep%4==2)act(&f,&c,WF_WAIT,0,0,0,NULL,f.words[WF_DEADLINE],&step);
            else if(task==3){
                char answer[32];snprintf(answer,sizeof answer,"%u",f.words[36]?f.words[36]-1:1);
                act(&f,&c,WF_INSERT,1,0,0,answer,100,&step);act(&f,&c,WF_CLICK,2,0,0,NULL,200,&step);
                assert(f.words[WF_STATUS]==WF_RUNNING);
            }else act(&f,&c,WF_WAIT,0,0,0,NULL,500,&step);
            if(ep%4!=3)assert(f.words[WF_STATUS]!=WF_RUNNING);
            total++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"state_reward_projection_conformance\":\"PASS\",\"preset\":\"matched original instances; controlled draws/clock; CDP actions\"}\n",f.api->task_names[task],episodes);fflush(stdout);
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);fprintf(stderr,"PASS: %u original-generated numeric episodes; CDP control clicks, controlled time, source state/reward comparison\n",total);return 0;
}
