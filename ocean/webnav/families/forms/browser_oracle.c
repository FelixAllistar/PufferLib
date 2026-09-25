#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/prctl.h>

#define ROW 8192u
#define FIELD_BASE 128u
#define FIELD_STRIDE 544u
#define STATIC_BASE 2400u
#define STATIC_STRIDE 128u

static double number(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;}
static int boolean(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *string(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static char *read_file(const char *path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;}
static void put_ascii(uint32_t *dst,unsigned cap,const char *src){size_t n=strlen(src);assert(n<=cap);for(size_t i=0;i<n;i++){assert((unsigned char)src[i]>=32&&(unsigned char)src[i]<=126);dst[i]=(unsigned char)src[i];}}
static const cJSON *item(const cJSON *array,int index){const cJSON *v=cJSON_GetArrayItem(array,index);assert(v);return v;}

static void import_original(uint32_t *r,unsigned task,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);r[0]=WF_ABI_VERSION;r[1]=task;r[2]=WF_OBSERVE;r[3]=seed;
    r[12]=(uint32_t)number(j,"deadline");
    const cJSON *fields=cJSON_GetObjectItemCaseSensitive(j,"fields");
    const cJSON *statics=cJSON_GetObjectItemCaseSensitive(j,"statics");
    assert(cJSON_IsArray(fields)&&cJSON_IsArray(statics));
    r[32]=(uint32_t)cJSON_GetArraySize(fields);r[38]=(uint32_t)cJSON_GetArraySize(statics);
    r[34]=(uint32_t)boolean(j,"popup");r[35]=(uint32_t)number(j,"popup_mode");
    r[36]=0;r[37]=0;put_ascii(r+4608,511,string(j,"query"));
    for(unsigned i=0;i<r[32];i++){
        const cJSON *src=item(fields,(int)i);uint32_t *dst=r+FIELD_BASE+i*FIELD_STRIDE;
        const char *value=string(src,"value"),*goal=string(src,"goal"),*name=string(src,"name");
        dst[0]=(uint32_t)number(src,"role");dst[1]=(uint32_t)strlen(value);
        dst[2]=(uint32_t)number(src,"start");dst[3]=(uint32_t)number(src,"end");
        dst[4]=(uint32_t)strlen(goal);dst[5]=(uint32_t)strlen(name);
        put_ascii(dst+8,255,value);put_ascii(dst+264,255,goal);put_ascii(dst+520,23,name);
    }
    for(unsigned i=0;i<r[38];i++){
        const cJSON *src=item(statics,(int)i);uint32_t *dst=r+STATIC_BASE+i*STATIC_STRIDE;
        const char *name=string(src,"name"),*value=string(src,"value");
        dst[0]=(uint32_t)number(src,"role");dst[1]=(uint32_t)number(src,"action");
        dst[2]=0;dst[3]=(uint32_t)boolean(src,"visible");
        dst[5]=(uint32_t)strlen(name);dst[6]=(uint32_t)strlen(value);
        put_ascii(dst+8,63,name);put_ascii(dst+72,55,value);
    }
}

static cJSON *key_event(WebCdp *c,const char *type,const char *key,unsigned code,unsigned modifiers){
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",type);
    cJSON_AddStringToObject(p,"key",key);cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
    cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);cJSON_AddNumberToObject(p,"modifiers",modifiers);
    return web_cdp_call(c,"Input.dispatchKeyEvent",p);
}
static void key_pair(WebCdp *c,const char *key,unsigned code,unsigned modifiers){
    cJSON *r=key_event(c,"keyDown",key,code,modifiers);assert(r);cJSON_Delete(r);
    r=key_event(c,"keyUp",key,code,modifiers);assert(r);cJSON_Delete(r);
}
static void shortcut(WebCdp *c,const char *key,unsigned code){
    cJSON *r=key_event(c,"keyDown","Control",17,0);assert(r);cJSON_Delete(r);
    key_pair(c,key,code,2);
    r=key_event(c,"keyUp","Control",17,0);assert(r);cJSON_Delete(r);
}
static void select_all(WebCdp *c){shortcut(c,"a",65);}
static void insert_text(WebCdp *c,const char *text){
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",text);
    cJSON *r=web_cdp_call(c,"Input.insertText",p);assert(r);cJSON_Delete(r);
}
static void point_click(WebCdp *c,unsigned ref){
    char expression[96];snprintf(expression,sizeof expression,"__fm.point(%u)",ref);
    cJSON *p=web_cdp_eval(c,expression);assert(p&&!cJSON_IsNull(p));
    assert(boolean(p,"visible"));double x=number(p,"x"),y=number(p,"y");cJSON_Delete(p);
    assert(!web_cdp_click(c,x,y));
}
static int sync_model(WFLoaded *f,unsigned now,const WFAction *action){
    WFAction a=*action;a.elapsed_ms=now;
    if(wf_apply(f,0,&a))return -1;
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    return wf_batch_checked(f);
}
static void compare_view(WFLoaded *f,const cJSON *snapshot,unsigned task,unsigned step){
    WFView v;assert(!wf_observe(f,0,&v));
    const cJSON *fields=cJSON_GetObjectItemCaseSensitive(snapshot,"fields");
    const cJSON *statics=cJSON_GetObjectItemCaseSensitive(snapshot,"statics");
    assert(cJSON_IsArray(fields)&&cJSON_IsArray(statics));
    assert((unsigned)cJSON_GetArraySize(fields)==f->words[32]);
    assert(!strcmp(wf_text_get(&v,v.instruction),string(snapshot,"query")));
    for(unsigned i=0;i<f->words[32];i++){
        const cJSON *src=item(fields,(int)i);const uint32_t *r=f->words+FIELD_BASE+i*FIELD_STRIDE;
        unsigned ref=i+1;const WFNode *node=NULL;
        for(unsigned k=0;k<v.count;k++)if(v.nodes[k].ref==ref){node=&v.nodes[k];break;}
        assert(node&&node->role==r[0]);
        assert(!strcmp(wf_text_get(&v,node->name),string(src,"name")));
        assert(!strcmp(wf_text_get(&v,node->value),string(src,"value")));
        assert(node->selection_start==(uint32_t)number(src,"start"));
        assert(node->selection_end==(uint32_t)number(src,"end"));
        int enabled=!!(node->flags&WF_ENABLED),browser_enabled=boolean(src,"enabled");
        if(enabled!=browser_enabled){fprintf(stderr,"enabled mismatch task=%s step=%u field=%u\n",f->api->task_names[task],step,i);abort();}
    }
    for(unsigned i=0;i<f->words[38];i++){
        const cJSON *src=item(statics,(int)i);const uint32_t *r=f->words+STATIC_BASE+i*STATIC_STRIDE;
        if(r[3]&&!boolean(snapshot,"popup"))continue;
        unsigned ref=f->words[32]+i+1;const WFNode *node=NULL;
        for(unsigned k=0;k<v.count;k++)if(v.nodes[k].ref==ref){node=&v.nodes[k];break;}
        assert(node&&node->role==r[0]);
        assert(!strcmp(wf_text_get(&v,node->name),string(src,"name")));
        assert(!strcmp(wf_text_get(&v,node->value),string(src,"value")));
        int enabled=!!(node->flags&WF_ENABLED),browser_enabled=boolean(src,"enabled");
        if(enabled!=browser_enabled){fprintf(stderr,"enabled mismatch task=%s step=%u node=%u\n",f->api->task_names[task],step,i);abort();}
    }
    unsigned done=boolean(snapshot,"done"),expected=f->words[9]!=0;
    if(done!=expected){fprintf(stderr,"done mismatch task=%s step=%u browser=%u model=%u\n",f->api->task_names[task],step,done,expected);abort();}
    if(done){
        double raw=number(snapshot,"raw"),reward=number(snapshot,"reward");
        float expected_raw=real(f->words[10]),expected_reward=real(f->words[11]);
        if(fabs(raw-expected_raw)>1e-6||fabs(reward-expected_reward)>1e-5){
            fprintf(stderr,"reward mismatch task=%s step=%u browser=%g/%g model=%g/%g\n",f->api->task_names[task],step,raw,reward,expected_raw,expected_reward);abort();
        }
    }
    if(v.elapsed_ms!=(uint32_t)number(snapshot,"elapsed")){
        fprintf(stderr,"elapsed mismatch task=%s step=%u browser=%.0f model=%u\n",f->api->task_names[task],step,number(snapshot,"elapsed"),v.elapsed_ms);abort();
    }
}

static unsigned submit_ref(unsigned task,unsigned nf){return task==3?nf+2u:task==6?15u:nf+1u;}
static void browser_click(WebCdp *c,WFLoaded *f,unsigned task,unsigned ref,unsigned now,unsigned step){
    point_click(c,ref);WFAction a={.kind=WF_CLICK,.target=ref};
    assert(!sync_model(f,now,&a));(void)task;(void)step;
}
static void browser_type(WebCdp *c,WFLoaded *f,unsigned ref,const char *value,unsigned now){
    browser_click(c,f,(unsigned)f->words[1],ref,now,0);select_all(c);WFAction all={.kind=WF_SELECT_ALL};assert(!sync_model(f,now,&all));
    insert_text(c,value);WFAction text={.kind=WF_INSERT,.text=value,.text_length=strlen(value)};assert(!sync_model(f,now,&text));
}
static void browser_copy(WebCdp *c,WFLoaded *f,unsigned source,unsigned target,unsigned now){
    browser_click(c,f,(unsigned)f->words[1],source,now,0);select_all(c);WFAction all={.kind=WF_SELECT_ALL};assert(!sync_model(f,now,&all));
    shortcut(c,"c",67);WFAction copy={.kind=WF_COPY};assert(!sync_model(f,now,&copy));
    browser_click(c,f,(unsigned)f->words[1],target,now,0);select_all(c);assert(!sync_model(f,now,&all));
    shortcut(c,"v",86);WFAction paste={.kind=WF_PASTE};assert(!sync_model(f,now,&paste));
}

int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):2u;if(!episodes||episodes>200u)return 2;
    WFLoaded f;char error[512];if(wf_open(&f,"build/webnav/families/forms/libforms.so",error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    char *script=read_file("ocean/webnav/families/forms/browser.js");unsigned total=0;
    for(unsigned task=0;task<8;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *loaded=web_cdp_eval(&c,script);assert(loaded&&cJSON_IsTrue(loaded));cJSON_Delete(loaded);
        unsigned successes=0,actions=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=810000u+task*1000u+ep;char js[160];
            snprintf(js,sizeof js,task==7?"__fm.probePopupMode(%u);__fm.export()":"__fm.reset(%u)",seed);
            cJSON *state=web_cdp_eval(&c,js);assert(state);import_original(f.words,task,seed,state);
            assert(!f.api->validate(f.words));for(unsigned lane=1;lane<f.api->batch_lanes;lane++)memcpy(f.words+(size_t)lane*ROW,f.words,ROW*sizeof *f.words);
            compare_view(&f,state,task,0);cJSON_Delete(state);
            unsigned now=0,step=0;
            if(ep%5u==4u){
                now=f.words[12];snprintf(js,sizeof js,"__fm.advance(%u);__fm.export()",now);state=web_cdp_eval(&c,js);assert(state);
                WFAction wait={.kind=WF_WAIT};assert(!sync_model(&f,now,&wait));compare_view(&f,state,task,++step);cJSON_Delete(state);actions++;
            }else if(task==7&&f.words[35]&&ep%3u==2u){
                unsigned popup_field=f.words[35],popup_target=f.words[32]+4u;now=250;
                snprintf(js,sizeof js,"__fm.advance(%u)",now);state=web_cdp_eval(&c,js);assert(state);cJSON_Delete(state);
                browser_click(&c,&f,task,popup_field,now,++step);state=web_cdp_eval(&c,"__fm.export()");assert(state);compare_view(&f,state,task,step);cJSON_Delete(state);actions++;
                browser_click(&c,&f,task,popup_target,now,++step);state=web_cdp_eval(&c,"__fm.export()");assert(state);compare_view(&f,state,task,step);cJSON_Delete(state);actions++;
            }else{
                now=250;
                if(task==7&&f.words[35]){
                    snprintf(js,sizeof js,"__fm.advance(%u)",now);state=web_cdp_eval(&c,js);assert(state);cJSON_Delete(state);
                    browser_click(&c,&f,task,f.words[35],now,++step);state=web_cdp_eval(&c,"__fm.export()");assert(state);compare_view(&f,state,task,step);cJSON_Delete(state);actions++;
                    browser_click(&c,&f,task,f.words[32]+5u,now,++step);state=web_cdp_eval(&c,"__fm.export()");assert(state);compare_view(&f,state,task,step);cJSON_Delete(state);actions++;
                }
                if(task==4||task==5){
                    unsigned answer=task==4?1u:3u,source=0;const uint32_t *goal=f.words+FIELD_BASE+answer*FIELD_STRIDE+264;
                    unsigned len=f.words[FIELD_BASE+answer*FIELD_STRIDE+4];
                    for(unsigned i=0;i<answer;i++){const uint32_t *field=f.words+FIELD_BASE+i*FIELD_STRIDE;if(field[1]==len&&!memcmp(field+8,goal,len*sizeof *goal)){source=i;break;}}
                    assert(source<answer);browser_copy(&c,&f,source+1u,answer+1u,now);step+=5;actions+=5;
                    snprintf(js,sizeof js,"__fm.advance(%u);__fm.export()",++now);state=web_cdp_eval(&c,js);assert(state);
                    WFAction wait={.kind=WF_WAIT};assert(!sync_model(&f,now,&wait));compare_view(&f,state,task,++step);cJSON_Delete(state);actions++;
                }else{
                    for(unsigned i=0;i<f.words[32];i++){
                        const uint32_t *field=f.words+FIELD_BASE+i*FIELD_STRIDE;char value[256];unsigned len=field[4];assert(len<sizeof value);
                        for(unsigned j=0;j<len;j++)value[j]=(char)field[264+j];value[len]=0;
                        browser_type(&c,&f,i+1u,value,now);step+=3;actions+=3;
                    }
                }
                snprintf(js,sizeof js,"__fm.advance(%u);__fm.export()",++now);state=web_cdp_eval(&c,js);assert(state);
                WFAction wait={.kind=WF_WAIT};assert(!sync_model(&f,now,&wait));compare_view(&f,state,task,++step);cJSON_Delete(state);actions++;
                unsigned submit=submit_ref(task,f.words[32]);browser_click(&c,&f,task,submit,now,++step);
                state=web_cdp_eval(&c,"__fm.export()");assert(state);compare_view(&f,state,task,step);successes+=real(f.words[10])>0.99f;cJSON_Delete(state);actions++;
            }
            assert(f.words[9]);total++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"differential\":\"PASS\",\"preset\":\"pinned original HTML, seeded instances, controlled clock and CDP input\"}\n",f.api->task_names[task],episodes,actions,successes);fflush(stdout);web_cdp_close(&c);
    }
    free(script);wf_close(&f);fprintf(stderr,"PASS: %u original-generated forms episodes\n",total);return 0;
}
