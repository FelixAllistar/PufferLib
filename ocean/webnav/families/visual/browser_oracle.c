#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 2048u
static const char *const palette[]={"red","green","blue","aqua","black",
    "magenta","yellow"};
static const char *const boxes[]={"red","blue","olive","lime","black",
    "white","grey","purple","orange","yellow","cyan","pink","magenta"};
static const cJSON *field(const cJSON *j,const char *key){
    const cJSON *x=cJSON_GetObjectItemCaseSensitive(j,key);assert(x);return x;
}
static const cJSON *at(const cJSON *a,int index){
    const cJSON *x=cJSON_GetArrayItem(a,index);assert(x);return x;
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *x=field(j,key);assert(cJSON_IsString(x));return x->valuestring;
}
static unsigned num(const cJSON *j,const char *key){
    const cJSON *x=field(j,key);assert(cJSON_IsNumber(x));return (unsigned)x->valuedouble;
}
static int yes(const cJSON *j,const char *key){
    const cJSON *x=field(j,key);assert(cJSON_IsBool(x));return cJSON_IsTrue(x);
}
static int code(const char *s,const char *const *names,unsigned count){
    for(unsigned i=0;i<count;i++)if(!strcmp(s,names[i]))return (int)i;
    return -1;
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1u,1u);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void words(uint32_t *dst,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){
        assert((unsigned char)s[i]>=32u&&(unsigned char)s[i]<=126u);
        dst[i]=(unsigned char)s[i];
    }
}
static unsigned shape_kind(const char *kind){
    return !strcmp(kind,"letter")?1u:!strcmp(kind,"digit")?2u:3u;
}
static unsigned shape_glyph(const char *s){
    if(!strcmp(s,"circle"))return 201u;
    if(!strcmp(s,"rectangle"))return 202u;
    if(!strcmp(s,"triangle"))return 203u;
    assert(strlen(s)==1u);return (unsigned char)s[0];
}
static unsigned desc_token(const char *s){
    if(!strcmp(s,"item"))return 0u;
    if(!strcmp(s,"letter"))return 1u;
    if(!strcmp(s,"digit"))return 2u;
    if(!strcmp(s,"shape"))return 3u;
    return shape_glyph(s);
}
static unsigned match_shape(const uint32_t *r,unsigned i){
    const uint32_t *s=r+128u+i*6u;
    return (!r[33]||r[33]==s[3]+1u)&&
        (!r[34]||r[34]==s[2]+1u)&&
        (!r[35]||r[35]==s[4]||r[35]==s[5]);
}
static void import_original(uint32_t *r,unsigned task,unsigned seed,
                            const cJSON *j){
    memset(r,0,ROW*sizeof *r);
    r[WF_VERSION]=2u;r[WF_TASK]=task;r[WF_OP]=WF_OBSERVE;r[WF_SEED]=seed;
    r[WF_DEADLINE]=num(j,"deadline");
    words(r+512u,256u,str(j,"query"));
    if(task==0u){
        const cJSON *colors=field(j,"colors");assert(cJSON_GetArraySize(colors)==4);
        for(unsigned i=0;i<4u;i++){
            int c=code(at(colors,(int)i)->valuestring,boxes,13u);
            assert(c>=0);r[32u+i]=(unsigned)c+1u;
        }
        const char *swatch=str(j,"swatch");
        r[37]=*swatch!=0;
        int goal=code(str(j,"goal"),boxes,13u);assert(goal>=0);
        r[36]=(unsigned)goal+1u;
    }else if(task==1u||task==2u){
        const cJSON *labels=field(j,"labels");
        r[32]=(unsigned)cJSON_GetArraySize(labels);
        const char *goal=str(j,"goal");assert(strlen(goal)==1u);
        r[33]=(unsigned char)goal[0];
        for(unsigned i=0;i<r[32];i++){
            const cJSON *x=at(labels,(int)i);assert(cJSON_IsString(x)&&strlen(x->valuestring)==1u);
            r[64u+i]=(unsigned char)x->valuestring[0];
        }
    }else if(task==3u){
        static const char *const color_names[]={"red","green","blue"};
        int goal=code(str(j,"goal"),color_names,3u);assert(goal>=0);
        r[32]=(unsigned)goal;r[34]=12u;
        const cJSON *shades=field(j,"shades");assert(cJSON_GetArraySize(shades)==12);
        for(unsigned i=0;i<12u;i++){
            const cJSON *s=at(shades,(int)i);
            int hue=code(str(s,"color"),color_names,3u);assert(hue>=0);
            unsigned h,sat,light;
            assert(sscanf(str(s,"hsl"),"hsl(%u, %u%%, %u%%)",&h,&sat,&light)==3);
            assert(h==(unsigned)hue*120u);
            r[64u+i]=(unsigned)hue;r[80u+i]=sat;r[96u+i]=light;
        }
    }else if(task==4u||task==5u){
        const cJSON *shapes=field(j,"shapes"),*desc=field(j,"desc");
        r[32]=(unsigned)cJSON_GetArraySize(shapes);
        r[33]=!strcmp(at(desc,0)->valuestring,"small")?1u:
              !strcmp(at(desc,0)->valuestring,"large")?2u:0u;
        int color=code(at(desc,1)->valuestring,palette,7u);
        r[34]=color<0?0u:(unsigned)color+1u;
        r[35]=desc_token(at(desc,2)->valuestring);
        r[37]=task==5u;
        const cJSON *svg=field(j,"svg");assert(cJSON_GetArraySize(svg)==(int)r[32]);
        for(unsigned i=0;i<r[32];i++){
            const cJSON *s=at(shapes,(int)i),*visible=at(svg,(int)i);
            uint32_t *dst=r+128u+i*6u;
            dst[0]=num(s,"x");dst[1]=num(s,"y");
            int c=code(str(s,"color"),palette,7u);assert(c>=0);
            dst[2]=(unsigned)c;dst[3]=num(s,"size");
            dst[4]=shape_kind(str(s,"kind"));
            dst[5]=shape_glyph(str(s,"glyph"));
            assert(!strcmp(str(visible,"fill"),str(s,"color")));
        }
        for(unsigned i=0;i<r[32];i++)r[36]+=match_shape(r,i);
        if(task==5u){
            const cJSON *buttons=field(j,"buttons");
            assert(cJSON_GetArraySize(buttons)==5);
            for(unsigned i=0;i<5u;i++)r[64u+i]=(unsigned)at(buttons,(int)i)->valuedouble;
        }
    }else if(task==6u){
        r[32]=num(j,"sides");r[33]=r[32];
        const cJSON *vertices=field(j,"vertices");
        assert(cJSON_GetArraySize(vertices)==(int)r[32]);
        for(unsigned i=0;i<r[32];i++){
            const cJSON *p=at(vertices,(int)i);
            r[64u+i*2u]=num(p,"x");r[65u+i*2u]=num(p,"y");
        }
    }else if(task==7u){
        static const char *const categories[]={"rectangle","circle",
            "triangle","letter","digit"};
        int c=code(str(j,"category"),categories,5u);assert(c>=0);
        r[32]=(unsigned)c;
        const cJSON *figure=field(j,"figure");
        const char *tag=str(figure,"tag");
        r[33]=!strcmp(tag,"circle")?1u:!strcmp(tag,"rect")?2u:
              !strcmp(tag,"polygon")?3u:0u;
        r[34]=r[33]?0u:(unsigned char)str(figure,"glyph")[0];
        int fill=code(str(figure,"fill"),palette,7u);assert(fill>=0);
        r[35]=(unsigned)fill;
    }else{
        r[32]=num(j,"left");r[33]=num(j,"right");
    }
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,
                    unsigned seed,unsigned step){
    WFView v;assert(!wf_observe(f,0,&v));
    assert(!strcmp(wf_text_get(&v,v.instruction),str(j,"query")));
    if(v.elapsed_ms!=num(j,"elapsed")){
        fprintf(stderr,"elapsed mismatch %s seed=%u step=%u browser=%u model=%u\n",
            f->api->task_names[task],seed,step,num(j,"elapsed"),v.elapsed_ms);abort();
    }
    unsigned done=yes(j,"done");
    if(done!=(f->words[WF_STATUS]!=WF_RUNNING)){
        fprintf(stderr,"done mismatch %s seed=%u step=%u\n",
            f->api->task_names[task],seed,step);abort();
    }
    if(done){
        double raw=field(j,"raw")->valuedouble;
        double timed=field(j,"reward")->valuedouble;
        if(fabs(raw-real(f->words[WF_RAW_REWARD]))>1e-6||
           fabs(timed-real(f->words[WF_TIMED_REWARD]))>1e-5){
            fprintf(stderr,"reward mismatch %s seed=%u step=%u browser=%g/%g model=%g/%g\n",
                f->api->task_names[task],seed,step,raw,timed,
                real(f->words[WF_RAW_REWARD]),real(f->words[WF_TIMED_REWARD]));abort();
        }
    }
    if(task==3u){
        const cJSON *shades=field(j,"shades");
        for(unsigned i=0;i<12u;i++)
            assert(yes(at(shades,(int)i),"selected")==((f->words[33u]>>i)&1u));
    }
    if(task==8u){
        const char *value=str(j,"input");size_t n=strlen(value);
        assert(n==f->words[34u]);
        for(size_t i=0;i<n;i++)assert((unsigned char)value[i]==f->words[64u+i]);
        if(yes(j,"focused")!=((int)f->words[37u])){
            fprintf(stderr,"focus mismatch %s seed=%u step=%u done=%u browser=%d model=%u value='%s' model_len=%u\n",
                f->api->task_names[task],seed,step,done,yes(j,"focused"),
                f->words[37u],value,f->words[34u]);
            abort();
        }
    }
}
static cJSON *eval(WebCdp *c,const char *expression){
    cJSON *j=web_cdp_eval(c,expression);assert(j);return j;
}
static cJSON *advance(WebCdp *c,unsigned now){
    char js[80];snprintf(js,sizeof js,"__vi.advance(%u)",now);return eval(c,js);
}
static cJSON *click(WebCdp *c,unsigned ref){
    char js[80];snprintf(js,sizeof js,"__vi.click(%u)",ref);return eval(c,js);
}
static cJSON *cdp_input(WebCdp *c,unsigned kind,unsigned ref,const char *text){
    if(kind==WF_CLICK){
        char js[80];snprintf(js,sizeof js,"__vi.point(%u)",ref);
        cJSON *point=eval(c,js);assert(cJSON_IsObject(point));
        double x=field(point,"x")->valuedouble;
        double y=field(point,"y")->valuedouble;cJSON_Delete(point);
        assert(!web_cdp_click(c,x,y));
    }else{
        assert(kind==WF_INSERT&&text);
        cJSON *params=cJSON_CreateObject();
        cJSON_AddStringToObject(params,"text",text);
        cJSON *result=web_cdp_call(c,"Input.insertText",params);
        assert(result);cJSON_Delete(result);
    }
    return eval(c,"__vi.export()");
}
static void model_action(WFLoaded *f,unsigned kind,unsigned target,
                         unsigned now,const char *text){
    WFAction a={.kind=kind,.target=target,.elapsed_ms=now,
        .text=text,.text_length=text?strlen(text):0u};
    assert(!wf_apply(f,0,&a));
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
}
static void step(WFLoaded *f,WebCdp *c,unsigned task,unsigned seed,
                 unsigned serial,unsigned now,unsigned kind,
                 unsigned target,const char *text){
    cJSON *j=advance(c,now);cJSON_Delete(j);
    if(task==8u&&(kind==WF_CLICK||kind==WF_INSERT))
        j=cdp_input(c,kind,target,text);
    else if(kind==WF_CLICK)j=click(c,target);
    else j=eval(c,"__vi.export()");
    model_action(f,kind,target,now,text);
    compare(f,j,task,seed,serial);cJSON_Delete(j);
}
int main(int argc,char **argv){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):4u;
    if(episodes<4u||episodes>200u)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/visual/libvisual.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/visual/browser.js");
    unsigned total=0;
    for(unsigned task=0;task<f.api->task_count;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,
            "build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",
            f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=eval(&c,script);assert(cJSON_IsTrue(j));cJSON_Delete(j);
        unsigned wins=0,actions=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=940000u+task*1000u+ep,mode=ep%4u,serial=0;
            char js[80];snprintf(js,sizeof js,"__vi.reset(%u)",seed);
            j=eval(&c,js);import_original(f.words,task,seed,j);
            assert(!f.api->validate(f.words));
            for(unsigned lane=1;lane<f.api->batch_lanes;lane++)
                memcpy(f.words+(size_t)lane*ROW,f.words,ROW*sizeof *f.words);
            compare(&f,j,task,seed,serial);cJSON_Delete(j);
            if(mode==2u){
                step(&f,&c,task,seed,++serial,f.words[WF_DEADLINE],WF_WAIT,0,NULL);
                actions++;
            }else if(task==0u){
                unsigned ref=0;
                for(unsigned i=0;i<4u;i++)if(f.words[32u+i]==f.words[36u])ref=10u+i;
                assert(ref);
                if(mode==1u)ref=ref==10u?11u:10u;
                if(mode==3u&&f.words[37u])ref=1u;
                step(&f,&c,task,seed,++serial,100u,WF_CLICK,ref,NULL);actions++;
            }else if(task==1u||task==2u){
                unsigned goal=f.words[33u],count=f.words[32u],index=count;
                for(unsigned i=0;i<count;i++)if(f.words[64u+i]==goal)index=i;
                assert(index<count);
                step(&f,&c,task,seed,++serial,100u,WF_CLICK,1u,NULL);actions++;
                unsigned ready=task==1u?1800u:100u;
                if(task==1u){
                    step(&f,&c,task,seed,++serial,ready,WF_WAIT,0,NULL);actions++;
                }
                if(mode==1u)index=(index+1u)%count;
                step(&f,&c,task,seed,++serial,ready,WF_CLICK,10u+index,NULL);actions++;
            }else if(task==3u){
                unsigned goal=f.words[32u],now=100u;
                if(mode==1u){
                    step(&f,&c,task,seed,++serial,now,WF_CLICK,2u,NULL);actions++;
                }else{
                    if(mode==3u){
                        unsigned wrong=0;while(f.words[64u+wrong]==goal)wrong++;
                        assert(wrong<12u);
                        step(&f,&c,task,seed,++serial,now,WF_CLICK,10u+wrong,NULL);actions++;
                        step(&f,&c,task,seed,++serial,now+50u,WF_CLICK,10u+wrong,NULL);actions++;
                        now+=100u;
                    }
                    for(unsigned i=0;i<12u;i++)if(f.words[64u+i]==goal){
                        now+=100u;
                        step(&f,&c,task,seed,++serial,now,WF_CLICK,10u+i,NULL);actions++;
                    }
                    step(&f,&c,task,seed,++serial,now+100u,WF_CLICK,2u,NULL);actions++;
                }
            }else if(task==4u){
                unsigned match=0,wrong=0,count=f.words[32u];
                while(match<count&&!match_shape(f.words,match))match++;
                while(wrong<count&&match_shape(f.words,wrong))wrong++;
                assert(match<count);
                unsigned ref=mode==1u?(wrong<count?10u+wrong:1u):10u+match;
                step(&f,&c,task,seed,++serial,100u,WF_CLICK,ref,NULL);actions++;
            }else if(task==5u){
                unsigned answer=f.words[36u],index=5u;
                for(unsigned i=0;i<5u;i++)if(f.words[64u+i]==answer)index=i;
                assert(index<5u);
                if(mode==1u)index=(index+1u)%5u;
                step(&f,&c,task,seed,++serial,100u,WF_CLICK,40u+index,NULL);actions++;
            }else if(task==6u||task==7u){
                unsigned index=task==6u?f.words[32u]-3u:f.words[32u];
                if(mode==1u)index=(index+1u)%5u;
                step(&f,&c,task,seed,++serial,100u,WF_CLICK,10u+index,NULL);actions++;
            }else{
                char answer[8];snprintf(answer,sizeof answer,
                    mode==1u?"0%u":"%u",f.words[32u]+f.words[33u]);
                step(&f,&c,task,seed,++serial,100u,WF_CLICK,1u,NULL);actions++;
                step(&f,&c,task,seed,++serial,200u,WF_INSERT,1u,answer);actions++;
                step(&f,&c,task,seed,++serial,300u,WF_CLICK,2u,NULL);actions++;
            }
            assert(f.words[WF_STATUS]!=WF_RUNNING);
            wins+=real(f.words[WF_RAW_REWARD])>0.99f;total++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"differential\":\"PASS\",\"preset\":\"pinned original HTML, seeded instances, controlled clock and original handlers\"}\n",
            f.api->task_names[task],episodes,actions,wins);fflush(stdout);
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    fprintf(stderr,"PASS: %u original-generated visual episodes\n",total);
}
