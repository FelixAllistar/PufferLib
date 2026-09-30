#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include "public_controller.h"
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static double num(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsNumber(v));return v->valuedouble;
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsString(v));return v->valuestring;
}
static int flag(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);
    long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1u,1u);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static int units(uint32_t *out,unsigned cap,const char *s){
    size_t n=strlen(s);if(n>=cap)return -1;
    for(size_t i=0;i<n;i++){
        unsigned c=(unsigned char)s[i];
        if((c<32u&&c!=10u)||c>126u)return -1;
        out[i]=c;
    }
    return 0;
}
static int equal_units(const uint32_t *p,const char *s,unsigned cap){
    size_t n=strlen(s);if(n>=cap)return 0;
    for(size_t i=0;i<n;i++)if(p[i]!=(unsigned char)s[i])return 0;
    return p[n]==0u;
}
static unsigned word_count(const char *s){
    unsigned count=0u;int in=0;
    for(;*s;s++){
        if(isspace((unsigned char)*s))in=0;
        else if(!in){count++;in=1;}
    }
    return count;
}
static int extension_code(const char *s){
    static const char *const names[]={"","png","txt","jpg","json","gpg",
        "gif","sh","py","rb","html","zip","tar.gz"};
    const char *dot=strchr(s,'.');if(!dot)return 0;
    for(unsigned i=1;i<13u;i++)if(!strcmp(dot+1,names[i]))return (int)i;
    return -1;
}
static int import_original(uint32_t *r,unsigned task,const cJSON *j){
    memset(r,0,8192u*sizeof *r);
    r[WF_VERSION]=2u;r[WF_TASK]=task;
    r[WF_DEADLINE]=(unsigned)num(j,"deadline");
    const char *query=str(j,"query");if(units(r+256u,256u,query))return -1;
    if(task==0u){
        const cJSON *p=cJSON_GetObjectItemCaseSensitive(j,"problem");
        r[32]=(unsigned)num(p,"expectedIndex");
        r[33]=word_count(str(j,"paragraph"));
        r[38]=(unsigned)strlen(str(p,"expectedWord"));
        if(units(r+512u,512u,str(j,"paragraph"))||
           units(r+1024u,64u,str(p,"expectedWord")))return -1;
    }else if(task<=2u){
        const cJSON *ps=cJSON_GetObjectItemCaseSensitive(j,"paragraphs");
        unsigned count=task==1u?1u:3u;
        if(!cJSON_IsArray(ps)||cJSON_GetArraySize(ps)!=(int)count)return -1;
        r[33]=count;r[38]=flag(j,"buttonFirst")?0u:1u;
        r[32]=strstr(query,"2nd paragraph")?1u:
              strstr(query,"3rd paragraph")?2u:0u;
        unsigned offset=0u;
        for(unsigned i=0;i<count;i++){
            const cJSON *v=cJSON_GetArrayItem(ps,(int)i);
            if(!cJSON_IsString(v))return -1;
            const char *text=v->valuestring;size_t n=strlen(text);
            if(offset+n+1u>=512u)return -1;
            r[40u+2u*i]=offset;r[41u+2u*i]=(uint32_t)n;
            for(size_t k=0;k<n;k++)r[512u+offset+k]=(unsigned char)text[k];
            if(i==r[32]){
                r[37]=(uint32_t)n;
                if(units(r+2048u,1024u,text))return -1;
            }
            offset+=(unsigned)n;
            if(i+1u<count)r[512u+offset++]=10u;
        }
        r[36]=offset;
    }else if(task==3u){
        const char *body=str(j,"body");size_t n=strlen(body);
        if(n<3u||n>=128u||units(r+512u,128u,body))return -1;
        r[37]=(uint32_t)n;r[40]=word_count(body);
        r[42]=flag(j,"colorOpen")?1u:0u;
        if(r[40]<3u||r[40]>4u)return -1;
        r[34]=strstr(query,"bold")?0u:strstr(query,"italics")?1u:
              strstr(query,"underlined")?2u:3u;
        static const char *const colors[]={"red","orange","yellow",
            "green","blue","purple"};
        if(r[34]==3u){
            unsigned i=0u;for(;i<6u;i++)if(strstr(query,colors[i]))break;
            if(i==6u)return -1;r[35]=i;
        }
        r[36]=strstr(query,"everything")?0u:1u;
        r[32]=0u;r[33]=(uint32_t)n;
        if(r[36]){
            const char *p=strstr(query,"give the text ");if(!p)return -1;
            p+=14;
            const char *end=strstr(p,r[34]==3u?" the color ":" the style ");
            if(!end||end<=p)return -1;
            char wanted[128];if(editing_copy(wanted,sizeof wanted,p,end))return -1;
            const char *found=strstr(body,wanted);if(!found)return -1;
            r[32]=(uint32_t)(found-body);
            r[33]=r[32]+(uint32_t)strlen(wanted);
        }
    }else{
        const cJSON *files=cJSON_GetObjectItemCaseSensitive(j,"files");
        if(!cJSON_IsArray(files))return -1;
        int n=cJSON_GetArraySize(files);if(n<3||n>5)return -1;
        r[33]=(uint32_t)n;r[40]=(1u<<n)-1u;
        const char *wanted=strstr(query,"extension .");
        r[32]=wanted?(uint32_t)extension_code(wanted+10):0u;
        if(r[32]>=13u)return -1;
        for(int i=0;i<n;i++){
            const cJSON *v=cJSON_GetArrayItem(files,i);
            if(!cJSON_IsString(v))return -1;
            const char *name=v->valuestring;
            int code=extension_code(name);
            if(code<0||units(r+512u+(unsigned)i*128u,128u,name))return -1;
            r[48u+(unsigned)i]=(uint32_t)code;
            r[56u+(unsigned)i]=(uint32_t)strlen(name);
        }
        r[34]=flag(j,"focus")?1u:0u;
    }
    return 0;
}
static void compact(char *out,size_t cap,const char *src){
    size_t n=0u;
    for(;*src;src++)if(!isspace((unsigned char)*src)){
        assert(n+1u<cap);out[n++]=*src;
    }
    out[n]=0;
}
static int compare(const WFFamily *api,const uint32_t *r,unsigned task,const cJSON *j,
                   unsigned seed,unsigned step,unsigned ref){
    int bad=flag(j,"done")!=(r[WF_STATUS]!=WF_RUNNING);
    bad|=!equal_units(r+256u,str(j,"query"),256u);
    if(task==0u){
        bad|=!equal_units(r+512u,str(j,"paragraph"),512u);
        bad|=!equal_units(r+1152u,str(j,"input"),128u);
    }else if(task<=2u){
        const cJSON *ps=cJSON_GetObjectItemCaseSensitive(j,"paragraphs");
        bad|=!cJSON_IsArray(ps)||cJSON_GetArraySize(ps)!=(int)r[33];
        for(unsigned i=0;i<r[33]&&!bad;i++){
            const cJSON *p=cJSON_GetArrayItem(ps,(int)i);
            if(!cJSON_IsString(p))bad=1;
            else{
                char text[512];unsigned n=r[41u+2u*i],at=r[40u+2u*i];
                for(unsigned k=0;k<n;k++)text[k]=(char)r[512u+at+k];
                text[n]=0;bad|=strcmp(text,p->valuestring)!=0;
            }
        }
        char actual[1024],expected[1024];
        compact(actual,sizeof actual,str(j,"selected"));
        char span[512];unsigned n=r[35]-r[34];
        for(unsigned i=0;i<n;i++)span[i]=(char)r[512u+r[34]+i];
        span[n]=0;compact(expected,sizeof expected,span);
        bad|=strcmp(actual,expected)!=0;
    }else if(task==3u){
        bad|=!equal_units(r+512u,str(j,"body"),128u);
        bad|=r[42]!=(unsigned)flag(j,"colorOpen");
        const cJSON *styles=cJSON_GetObjectItemCaseSensitive(j,"styles");
        bad|=!cJSON_IsArray(styles)||cJSON_GetArraySize(styles)!=(int)r[37];
        for(unsigned i=0;i<r[37]&&!bad;i++){
            const cJSON *v=cJSON_GetArrayItem(styles,(int)i);
            bad|=!cJSON_IsNumber(v)||v->valueint!=(int)r[2048u+i];
        }
    }else{
        bad|=!equal_units(r+2048u,str(j,"command"),128u);
        const cJSON *files=cJSON_GetObjectItemCaseSensitive(j,"files");
        unsigned n=0u;
        for(unsigned i=0;i<r[33];i++)if(r[40u]&(1u<<i)){
            const cJSON *v=cJSON_GetArrayItem(files,(int)n++);
            char name[128];for(unsigned k=0;k<r[56u+i];k++)
                name[k]=(char)r[512u+i*128u+k];
            name[r[56u+i]]=0;
            bad|=!cJSON_IsString(v)||strcmp(v->valuestring,name)!=0;
        }
        bad|=!cJSON_IsArray(files)||cJSON_GetArraySize(files)!=(int)n;
        const cJSON *outs=cJSON_GetObjectItemCaseSensitive(j,"outputs");
        if(r[36]!=0u&&cJSON_IsArray(outs)&&cJSON_GetArraySize(outs)>0){
            const cJSON *last=cJSON_GetArrayItem(outs,cJSON_GetArraySize(outs)-1);
            if(cJSON_IsString(last)){
                const char *line=last->valuestring;
                if(r[36]==2u){
                    WFView v;const char *projected="";
                    if(api->observe(r,&v))bad=1;
                    else{
                        const WFNode *node=editing_ref(&v,3u);
                        if(node)projected=wf_text_get(&v,node->value);
                        bad|=!projected||strcmp(projected,line)!=0;
                    }
                }else if(r[36]==1u)bad|=strcmp(line,"Usage: rm file")!=0;
                else if(r[36]==5u)bad|=!strstr(line,"'*' not supported");
                else if(r[36]==6u)bad|=!strstr(line,"not found.");
                else if(r[36]==7u||r[36]==9u)bad|=*line!=0;
            }
        }
    }
    if(r[WF_STATUS]!=WF_RUNNING){
        bad|=fabs(num(j,"raw")-real(r[WF_RAW_REWARD]))>1e-6;
        bad|=fabs(num(j,"reward")-real(r[WF_TIMED_REWARD]))>1e-5;
    }
    if(bad){
        char *json=cJSON_PrintUnformatted(j);
        fprintf(stderr,"editing browser mismatch task=%u seed=%u step=%u ref=%u status=%u raw=%g/%g state=%s\n",
            task,seed,step,ref,r[WF_STATUS],real(r[WF_RAW_REWARD]),
            num(j,"raw"),json?json:"<null>");
        free(json);return -1;
    }
    return 0;
}
static void key(WebCdp *c,const char *name,unsigned code){
    for(unsigned i=0;i<2u;i++){
        cJSON *p=cJSON_CreateObject();
        cJSON_AddStringToObject(p,"type",i?"keyUp":"keyDown");
        cJSON_AddStringToObject(p,"key",name);
        cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
        cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);
        cJSON *j=web_cdp_call(c,"Input.dispatchKeyEvent",p);
        assert(j);cJSON_Delete(j);
    }
}
static int point_click(WebCdp *c,const char *expr,unsigned task,
                       unsigned seed,unsigned step,unsigned ref){
    cJSON *j=web_cdp_eval(c,expr);assert(j);
    if(!flag(j,"visible")){
        char *info=cJSON_PrintUnformatted(j);
        fprintf(stderr,"Invisible editing control task=%u seed=%u step=%u ref=%u point=%s\n",
            task,seed,step,ref,info?info:"<null>");
        free(info);cJSON_Delete(j);return -1;
    }
    double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);
    return web_cdp_click(c,x,y);
}
static int dispatch(WebCdp *c,unsigned task,const WFAction *a,
                    unsigned seed,unsigned step){
    char js[128];cJSON *j;
    if(a->kind==WF_CLICK){
        snprintf(js,sizeof js,"__editing.point(%u)",a->target);
        return point_click(c,js,task,seed,step,a->target);
    }
    if(a->kind==WF_SELECT_RANGE){
        snprintf(js,sizeof js,"__editing.select(%u,%u)",a->arg0,a->arg1);
        j=web_cdp_eval(c,js);assert(j);cJSON_Delete(j);return 0;
    }
    if(a->kind==WF_INSERT){
        if(task==4u){
            for(size_t i=0;i<a->text_length;i++){
                char ch[2]={a->text[i],0};
                key(c,ch,(unsigned char)a->text[i]);
            }
        }else{
            cJSON *p=cJSON_CreateObject();
            cJSON_AddStringToObject(p,"text",a->text);
            j=web_cdp_call(c,"Input.insertText",p);
            assert(j);cJSON_Delete(j);
        }
        return 0;
    }
    if(a->kind==WF_BACKSPACE){key(c,"Backspace",8u);return 0;}
    if(a->kind==WF_ENTER){key(c,"Enter",13u);return 0;}
    return a->kind==WF_WAIT?0:-1;
}
static int choose(const WFFamily *api,const uint32_t *r,unsigned task,
                  unsigned scenario,unsigned step,int *injected,
                  WFAction *a,char *scratch){
    WFView v;if(api->observe(r,&v))return -1;
    *a=(WFAction){.kind=WF_WAIT};
    if(scenario==4u)return 0;
    if(task==0u){
        if(scenario==0u)return editing_public_next(&v,scratch,256u,a);
        if(step==1u){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(step==2u){
            if(scenario==1u){a->kind=WF_INSERT;a->text="wrong";a->text_length=5u;return 0;}
            const char *p=wf_text_get(&v,editing_ref(&v,3u)->value);
            if(!p)return -1;
            WFAction wanted;if(editing_public_next(&v,scratch,256u,&wanted))return -1;
            if(wanted.kind!=WF_INSERT)return -1;
            size_t n=wanted.text_length;
            if(n+3u>=256u)return -1;
            memmove(scratch+1u,scratch,n);scratch[0]='!';scratch[n+1u]='!';
            scratch[n+2u]=0;
            a->kind=WF_INSERT;a->text=scratch;a->text_length=n+2u;return 0;
        }
        a->kind=WF_CLICK;a->target=2u;return 0;
    }
    if(task<=2u){
        if(scenario==0u)return editing_public_next(&v,scratch,256u,a);
        if(scenario==2u){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(step==1u){
            if(editing_public_next(&v,scratch,256u,a))return -1;
            if(a->kind!=WF_SELECT_RANGE)return -1;
            if(scenario==1u)a->arg1--;
            if(scenario==3u&&task==2u){a->arg0=0u;a->arg1=r[36];}
            return 0;
        }
        a->kind=WF_CLICK;a->target=1u;return 0;
    }
    if(task==3u){
        if(scenario==0u)return editing_public_next(&v,scratch,256u,a);
        if(scenario==3u){
            if(step==1u){a->kind=WF_CLICK;a->target=1u;}
            return 0;
        }
        if(*injected>=2){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(*injected==1){
            a->kind=WF_CLICK;
            a->target=scenario==1u?
                (r[34]==3u?2u:20u+5u*((r[35]+1u)%6u)):
                (r[34]==3u?20u+5u*r[35]:2u+r[34]);
            if(a->target>=20u&&!r[42]){a->target=5u;return 0;}
            *injected=2;return 0;
        }
        if(editing_public_next(&v,scratch,256u,a))return -1;
        if(a->kind!=WF_SELECT_RANGE)return -1;
        if(scenario==2u){
            if(a->arg1<=a->arg0+1u)return -1;
            a->arg1--;
        }
        *injected=1;return 0;
    }
    if(scenario==0u)return editing_public_next(&v,scratch,256u,a);
    if(step==1u){a->kind=WF_CLICK;a->target=1u;return 0;}
    if(scenario==1u){
        if(step==2u){a->kind=WF_INSERT;a->text="exit";a->text_length=4u;return 0;}
        a->kind=WF_ENTER;return 0;
    }
    if(scenario==2u||scenario==3u){
        if(step==2u){a->kind=WF_INSERT;
            a->text=scenario==2u?"rm missing":"rm *";
            a->text_length=strlen(a->text);return 0;}
        if(step==3u){a->kind=WF_ENTER;return 0;}
        if(step==4u){a->kind=WF_INSERT;a->text="ls";a->text_length=2u;return 0;}
        if(step==5u){a->kind=WF_ENTER;return 0;}
        return editing_public_next(&v,scratch,256u,a);
    }
    return editing_public_next(&v,scratch,256u,a);
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],NULL,10):10u;
    if(!episodes||episodes>10000u)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/editing/libediting.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/editing/browser.js");
    unsigned total=0u,actions=0u;
    for(unsigned task=0u;task<5u;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);
        unsigned wins=0u;
        for(unsigned ep=0u;ep<episodes;ep++){
            unsigned seed=100000u+ep,scenario=ep%5u;int injected=0;
            char js[128];snprintf(js,sizeof js,"__editing.reset(%u)",seed);
            j=web_cdp_eval(&c,js);assert(j);
            if(import_original(f.words,task,j)){
                fprintf(stderr,"Original editing instance exceeds wire capacity task=%u seed=%u\n",task,seed);
                return 1;
            }
            cJSON_Delete(j);assert(!f.api->validate(f.words));
            for(unsigned step=1u;step<=40u&&f.words[WF_STATUS]==WF_RUNNING;step++){
                WFAction a;char scratch[256];
                if(choose(f.api,f.words,task,scenario,step,&injected,&a,scratch))return 1;
                char command_before[128]={0};
                if(task==4u&&a.kind==WF_ENTER){
                    unsigned n=f.words[35];assert(n<sizeof command_before);
                    for(unsigned k=0;k<n;k++)
                        command_before[k]=(char)f.words[2048u+k];
                }
                unsigned now=scenario==4u||
                    (task==3u&&scenario==3u&&step>=2u)?
                    f.words[WF_DEADLINE]:step*100u;
                snprintf(js,sizeof js,"__editing.advance(%u)",now);
                j=web_cdp_eval(&c,js);assert(j);cJSON_Delete(j);
                if(now<f.words[WF_DEADLINE]&&dispatch(&c,task,&a,seed,step))return 1;
                a.elapsed_ms=now;assert(!wf_apply(&f,0,&a));
                for(unsigned lane=1u;lane<f.api->batch_lanes;lane++)
                    memcpy(f.words+lane*f.api->row_words,f.words,
                        f.api->row_words*sizeof *f.words);
                assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
                j=web_cdp_eval(&c,"__editing.snapshot()");assert(j);
                if(compare(f.api,f.words,task,j,seed,step,a.target)){
                    if(task==4u){
                        fprintf(stderr,"terminal detail goal_ext=%u active=%u output=%u action_kind=%u text='%.*s' command_before='%s'\n",
                            f.words[32],f.words[40],f.words[36],a.kind,
                            (int)a.text_length,a.text?a.text:"",command_before);
                        for(unsigned i=0;i<f.words[33];i++){
                            char name[128];unsigned n=f.words[56u+i];
                            for(unsigned k=0;k<n;k++)name[k]=(char)f.words[512u+i*128u+k];
                            name[n]=0;
                            fprintf(stderr,"  file[%u]=%s ext=%u active=%u\n",
                                i,name,f.words[48u+i],
                                !!(f.words[40]&(1u<<i)));
                        }
                    }
                    if(a.kind==WF_CLICK){
                        snprintf(js,sizeof js,"__editing.point(%u)",a.target);
                        cJSON *point=web_cdp_eval(&c,js);
                        char *info=point?cJSON_PrintUnformatted(point):NULL;
                        fprintf(stderr,"editing click detail task=%u seed=%u step=%u ref=%u point=%s\n",
                            task,seed,step,a.target,info?info:"<null>");
                        free(info);cJSON_Delete(point);
                    }
                    return 1;
                }
                cJSON_Delete(j);actions++;
            }
            assert(f.words[WF_STATUS]!=WF_RUNNING);
            wins+=real(f.words[WF_RAW_REWARD])>0.99f;total++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"scripted_successes\":%u,\"matched_original\":\"PASS\"}\n",
            f.api->task_names[task],episodes,wins);fflush(stdout);web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    fprintf(stderr,"PASS: %u original editing episodes, %u independent actions\n",total,actions);
    return 0;
}
