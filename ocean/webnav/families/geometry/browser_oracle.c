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
static double number(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsNumber(v));return v->valuedouble;
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsString(v));return v->valuestring;
}
static const cJSON *array(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsArray(v));return v;
}
static const cJSON *part(const cJSON *j,int i){
    const cJSON *v=cJSON_GetArrayItem(j,i);assert(v);return v;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1u,1u);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void ascii(uint32_t *r,unsigned offset,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){
        unsigned char ch=(unsigned char)s[i];assert(ch>=32u&&ch<=126u);
        r[offset+(unsigned)i]=ch;
    }
}
static unsigned coordinate(const cJSON *j,const char *name){
    double x=number(j,name);assert(x>=0&&x<=150&&x==floor(x));
    return (unsigned)x;
}
static void put_circle(uint32_t *r,unsigned xslot,const cJSON *j){
    r[xslot]=coordinate(j,"x");r[xslot+1u]=coordinate(j,"y");
}
static void import_original(WFLoaded *f,unsigned task,unsigned seed,const cJSON *j){
    uint32_t *r=f->words;memset(r,0,ROW*sizeof *r);
    r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_OBSERVE;
    r[WF_SEED]=seed;r[WF_DEADLINE]=(unsigned)number(j,"deadline");
    ascii(r,512,256,string(j,"query"));
    if(task==3u){
        int x,y;const char *p=strchr(string(j,"query"),'(');
        assert(p&&sscanf(p,"(%d,%d)",&x,&y)==2&&x>=-2&&x<=2&&y>=-2&&y<=2);
        r[32]=(unsigned)((x+2)*5+(2-y));r[33]=25u;
        assert(cJSON_GetArraySize(array(j,"grid"))==25);
    }else{
        const cJSON *black=array(j,"black");
        if(task==1u){assert(cJSON_GetArraySize(black)==1);
            const cJSON *circle=part(black,0);put_circle(r,40,circle);
            r[46]=coordinate(circle,"r");
        }else{
            if(task==0u||task==2u){assert(cJSON_GetArraySize(black)==2);
                put_circle(r,40,part(black,0));put_circle(r,42,part(black,1));
            }else{assert(cJSON_GetArraySize(black)==1);
                put_circle(r,40,part(black,0));
                put_circle(r,42,part(array(j,"vertex"),0));
            }
            if(task==0u){assert(cJSON_GetArraySize(array(j,"vertex"))==1);
                put_circle(r,44,part(array(j,"vertex"),0));}
        }
    }
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,r,ROW*sizeof *r);
    assert(!f->api->validate(r));
}
static const WFNode *node(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,unsigned seed,unsigned step){
    const uint32_t *r=f->words;WFView v;
    if(wf_observe(f,0,&v)){
        fprintf(stderr,"geometry observe invalid task=%u seed=%u step=%u status=%u\n",
            task,seed,step,r[WF_STATUS]);abort();
    }
    assert(v.deadline_ms==(unsigned)number(j,"deadline"));
    assert(v.elapsed_ms==(unsigned)number(j,"elapsed"));
    assert(!strcmp(wf_text_get(&v,v.instruction),string(j,"query")));
    assert((r[WF_STATUS]!=WF_RUNNING)==boolean(j,"done"));
    if(task==3u){assert(v.count==25u);}
    else{
        const cJSON *user=cJSON_GetObjectItemCaseSensitive(j,"user");
        assert(!!r[49]==!cJSON_IsNull(user));
        if(r[49]){
            const WFNode *n=node(&v,20u);assert(n);
            if(fabs(r[47]/256.0-number(user,"x"))>0.01||
               fabs(r[48]/256.0-number(user,"y"))>0.01){
                fprintf(stderr,"geometry user mismatch task=%u seed=%u step=%u model=%g,%g browser=%g,%g\n",
                    task,seed,step,r[47]/256.0,r[48]/256.0,number(user,"x"),number(user,"y"));
                abort();
            }
        }
    }
    if(r[WF_STATUS]!=WF_RUNNING){
        double raw=number(j,"raw"),reward=number(j,"reward");
        if(fabs(real(r[WF_RAW_REWARD])-raw)>5e-4||
           fabs(real(r[WF_TIMED_REWARD])-reward)>5e-4){
            fprintf(stderr,"geometry reward mismatch task=%u seed=%u step=%u browser=%g/%g model=%g/%g\n",
                task,seed,step,raw,reward,real(r[WF_RAW_REWARD]),real(r[WF_TIMED_REWARD]));
            abort();
        }
    }
}
static cJSON *eval(WebCdp *c,const char *source){
    cJSON *j=web_cdp_eval(c,source);assert(j);return j;
}
static void apply(WFLoaded *f,const WFAction *a){
    assert(!wf_apply(f,0,a));
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
}
static void action(WFLoaded *f,WebCdp *c,unsigned task,unsigned seed,
                   unsigned *step,unsigned target,double x,double y,unsigned ms){
    char js[96];snprintf(js,sizeof js,"__geom.advance(%u)",ms);
    cJSON *j=eval(c,js);cJSON_Delete(j);
    double click_x=x,click_y=y+50.0;
    if(task==3u||target==2u){
        snprintf(js,sizeof js,"__geom.point(%u)",task==3u?target-1u:0u);
        j=eval(c,js);click_x=number(j,"x");click_y=number(j,"y");cJSON_Delete(j);
    }
    assert(!web_cdp_click(c,click_x,click_y));
    j=eval(c,"__geom.snapshot(false)");
    WFAction a={.kind=WF_CLICK,.target=target,
        .arg0=task==3u||target==2u?0u:(unsigned)lround(x*256.0),
        .arg1=task==3u||target==2u?0u:(unsigned)lround(y*256.0),
        .elapsed_ms=ms};
    apply(f,&a);compare(f,j,task,seed,++*step);cJSON_Delete(j);
}
static void wait_until(WFLoaded *f,WebCdp *c,unsigned task,unsigned seed,
                       unsigned *step,unsigned ms){
    char js[96];snprintf(js,sizeof js,"__geom.advance(%u)",ms);
    cJSON *j=eval(c,js);
    WFAction a={.kind=WF_WAIT,.elapsed_ms=ms};apply(f,&a);
    compare(f,j,task,seed,++*step);cJSON_Delete(j);
}
static void good_point(unsigned task,const uint32_t *r,double *x,double *y){
    double ax=r[40],ay=r[41],bx=r[42],by=r[43],cx=r[44],cy=r[45];
    if(task==1u){*x=ax;*y=ay;return;}
    if(task==2u){*x=round((ax+bx)/2.0);*y=round((ay+by)/2.0);return;}
    if(task==4u){
        double dx=ax-bx,dy=ay-by,length=hypot(dx,dy);
        for(double distance=20.0;distance>=2.0;distance/=2.0){
            for(int sign=-1;sign<=1;sign+=2){
                double tx=round(bx+sign*dy*distance/length-0.5);
                double ty=round(by-sign*dx*distance/length-0.5);
                if(tx>=2&&tx<=150&&ty>=2&&ty<=130&&
                   hypot(tx-bx,ty-by)>1){*x=tx;*y=ty;return;}
            }
        }
        assert(0);
    }
    double dx=ax-cx,dy=ay-cy,ex=bx-cx,ey=by-cy;
    double al=hypot(dx,dy),bl=hypot(ex,ey);
    double sx=dx/al+ex/bl,sy=dy/al+ey/bl,sl=hypot(sx,sy);
    assert(sl>0.01);
    for(double distance=20.0;distance>=2.0;distance/=2.0){
        double tx=round(cx+sx*distance/sl),ty=round(cy+sy*distance/sl);
        if(tx>=2&&tx<=150&&ty>=2&&ty<=130&&
           hypot(tx-cx,ty-cy)>1){*x=tx;*y=ty;return;}
    }
    assert(0);
}
int main(int argc,char **argv){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):10u;
    if(!episodes||episodes>100u)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/geometry/libgeometry.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/geometry/browser.js");
    const char *names[]={"bisect-angle","circle-center","find-midpoint",
                         "grid-coordinate","right-angle"};
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    unsigned comparisons=0,actions=0;
    for(unsigned task=0;task<5u;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=eval(&c,script);assert(cJSON_IsTrue(j));cJSON_Delete(j);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=930000u+task*1000u+ep,step=0;
            char js[96];snprintf(js,sizeof js,"__geom.reset(%u,%u)",task,seed);
            j=eval(&c,js);import_original(&f,task,seed,j);
            compare(&f,j,task,seed,step);cJSON_Delete(j);comparisons++;
            if(task==3u){
                unsigned goal=f.words[32];
                action(&f,&c,task,seed,&step,
                       ep%2u?((goal+1u)%25u)+1u:goal+1u,0,0,100u);
                actions++;
            }else if(ep%4u==0u){
                action(&f,&c,task,seed,&step,2u,0,0,200u);actions++;
            }else if(ep%4u==3u){
                action(&f,&c,task,seed,&step,1u,20,20,100u);
                wait_until(&f,&c,task,seed,&step,10000u);actions+=2;
            }else{
                double x,y;good_point(task,f.words,&x,&y);
                /* pageX/pageY may differ from D3 client-coordinate rounding. */
                if(ep%4u==1u){x+=0.5;y+=0.25;}
                if(ep%4u==2u){
                    action(&f,&c,task,seed,&step,1u,20,20,100u);actions++;
                }
                action(&f,&c,task,seed,&step,1u,x,y,150u);
                action(&f,&c,task,seed,&step,2u,0,0,250u);actions+=2;
            }
            assert(f.words[WF_STATUS]!=WF_RUNNING);
            comparisons+=step;
        }
        web_cdp_close(&c);
    }
    printf("{\"family\":\"geometry\",\"episodes\":%u,\"comparisons\":%u,\"actions\":%u,\"differential\":\"PASS\",\"preset\":\"pinned original DOM points imported only at reset; real pointer clicks; controlled source clock\"}\n",
        episodes*5u,comparisons,actions);
    fflush(stdout);free(script);wf_close(&f);return 0;
}
