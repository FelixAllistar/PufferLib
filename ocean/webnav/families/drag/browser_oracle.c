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
static const char *const pages[]={"drag-box","drag-circle","drag-cube",
    "drag-items","drag-items-grid","drag-shapes","drag-shapes-2",
    "drag-single-shape","drag-sort-numbers","resize-textarea"};
static const char *const shape_names[]={"circle","rectangle","triangle"};
static const char *const colors[]={"red","green","blue","aqua","black",
    "magenta","yellow"};
static const char *const circle_colors[]={"blue","red","black","green",
    "yellow","orange","white","brown"};
static const char *const rectangle_colors[]={"blue","red","black","green",
    "yellow","orange","white","cyan"};
static const char *const triangle_colors[]={"blue","red","black","green",
    "yellow","orange","white","purple"};
static const cJSON *field(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(v);return v;
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsString(v));return v->valuestring;
}
static double num(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int yes(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static const cJSON *item(const cJSON *a,unsigned i){
    const cJSON *v=cJSON_GetArrayItem(a,(int)i);assert(v);return v;
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
static unsigned word(const char *value,const char *const *choices,unsigned n){
    for(unsigned i=0;i<n;i++)if(!strcmp(value,choices[i]))return i;
    fprintf(stderr,"unknown source token: %s\n",value);abort();
}
static void units(uint32_t *r,unsigned at,unsigned cap,const char *value){
    size_t n=strlen(value);assert(n<cap);
    for(unsigned i=0;i<cap;i++)r[at+i]=0;
    for(size_t i=0;i<n;i++){
        assert((unsigned char)value[i]>=32&&(unsigned char)value[i]<=126);
        r[at+i]=(unsigned char)value[i];
    }
}
static unsigned goal_cube(const char *q){
    const char *p=strchr(q,'"');assert(p&&p[1]>='1'&&p[1]<='6');
    return (unsigned)(p[1]-'0');
}
static void import_sort(uint32_t *r,unsigned task,const cJSON *j,const char *q){
    const cJSON *items=field(j,"items");unsigned n=task==3?5:task==4?9:4;
    assert(cJSON_IsArray(items)&&cJSON_GetArraySize(items)==(int)n);
    r[32]=n;r[33]=r[34]=0;
    for(unsigned i=0;i<n;i++){
        const cJSON *x=item(items,i);assert(cJSON_IsString(x));r[37+i]=i+1;
        if(task==8){
            char *end;long value=strtol(x->valuestring,&end,10);
            assert(!*end&&value>=-100&&value<=99);
            r[64+i]=(uint32_t)(value+100);
        }else units(r,384+i*64,64,x->valuestring);
    }
    if(task==8){r[35]=r[36]=0;return;}
    char target[80];assert(!drag_target_name(q,target,sizeof target));
    unsigned ix=0;
    for(unsigned i=0;i<n;i++)if(!strcmp(item(items,i)->valuestring,target)){
        ix=i+1;break;
    }
    assert(ix);unsigned goal=drag_items_goal(task,q,ix);
    assert(goal>=1&&goal<=n&&goal!=ix);r[35]=ix;r[36]=goal;
}
static void import_shapes(uint32_t *r,unsigned task,const cJSON *j,const char *q){
    unsigned n=task==5?4:5;
    const cJSON *list=field(j,"shapes");
    assert(cJSON_IsArray(list)&&cJSON_GetArraySize(list)==(int)n);
    r[32]=(unsigned)num(j,"boxX");r[33]=(unsigned)num(j,"boxY");
    char goal[48];assert(!drag_shape_target(q,goal,sizeof goal));
    unsigned mode=1,target=0;
    for(unsigned i=0;i<3;i++){
        char plural[24];snprintf(plural,sizeof plural,"%ss",shape_names[i]);
        if(!strcmp(goal,plural)){mode=0;target=i;break;}
    }
    if(mode)target=word(goal,colors,7);
    r[34]=mode;r[35]=target;r[36]=0;r[37]=n;
    for(unsigned i=0;i<n;i++){
        const cJSON *s=item(list,i);uint32_t *p=r+64+i*4;
        p[0]=(unsigned)num(s,"kind");p[1]=word(str(s,"color"),colors,7);
        p[2]=(uint32_t)((int)num(s,"x")+2560);
        p[3]=(uint32_t)((int)num(s,"y")+2560);
    }
}
static void import_direction(uint32_t *r,unsigned task,const cJSON *j,const char *q){
    const cJSON *s=field(j,"shape");
    unsigned kind=(unsigned)num(s,"kind");assert(kind<3);
    unsigned direction=drag_direction(q);assert(direction<4);
    unsigned size=(unsigned)num(s,"size");
    const char *const *palette=kind==0?circle_colors:
        kind==1?rectangle_colors:triangle_colors;
    unsigned color=word(str(s,"color"),palette,task==1?7:8);
    r[32]=(uint32_t)((int)num(s,"initialX")+2560);
    r[33]=(uint32_t)((int)num(s,"initialY")+2560);
    r[34]=(uint32_t)((int)num(s,"x")+2560);
    r[35]=(uint32_t)((int)num(s,"y")+2560);
    r[36]=direction;r[37]=0;r[38]=kind;
    r[39]=kind==0?size/2:size;r[40]=color;
}
static void import_initial(uint32_t *r,unsigned task,const cJSON *j){
    const char *q=str(j,"query");units(r,256,128,q);
    switch(task){
      case 0:{
        const cJSON *small=field(j,"small"),*large=field(j,"large");
        r[32]=(uint32_t)((int)num(small,"x")+256);
        r[33]=(uint32_t)((int)num(small,"y")+256);
        r[34]=(uint32_t)((int)num(large,"x")+256);
        r[35]=(uint32_t)((int)num(large,"y")+256);r[36]=0;break;
      }
      case 1:case 7: import_direction(r,task,j,q);break;
      case 2: r[32]=goal_cube(q);r[33]=(unsigned)num(j,"face");r[34]=0;break;
      case 3:case 4:case 8: import_sort(r,task,j,q);break;
      case 5:case 6: import_shapes(r,task,j,q);break;
      case 9:
        r[32]=r[34]=(unsigned)num(j,"width");
        r[33]=r[35]=(unsigned)num(j,"height");
        r[36]=(unsigned)num(j,"left");r[37]=(unsigned)num(j,"top");
        r[38]=strstr(q,"width")?0:1;r[39]=strstr(q,"smaller")?0:1;
        r[40]=0;break;
    }
}
static void equal_coordinate(int model,int browser,unsigned task,unsigned seed,
                             unsigned phase,unsigned action,const char *field){
    if(model==browser)return;
    fprintf(stderr,"drag geometry mismatch task=%u seed=%u phase=%u action=%u %s model=%d browser=%d\n",
            task,seed,phase,action,field,model,browser);
    abort();
}
static void near_svg_coordinate(int model,int browser,unsigned task,unsigned seed,
                                unsigned phase,unsigned action,const char *field){
    /* Both sides round source D3 geometry to tenths of a CSS pixel. */
    if(abs(model-browser)<=1)return;
    equal_coordinate(model,browser,task,seed,phase,action,field);
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,
                    unsigned seed,unsigned phase,unsigned action){
    const uint32_t *r=f->words;WFView v;assert(!wf_observe(f,0,&v));
    const char *q=wf_text_get(&v,v.instruction);
    if(!q||strcmp(q,str(j,"query"))){
        fprintf(stderr,"drag query mismatch task=%u seed=%u phase=%u\n",task,seed,phase);abort();
    }
    assert(v.deadline_ms==(unsigned)num(j,"deadline"));
    if(task==0){
        const cJSON *a=field(j,"small"),*b=field(j,"large");
        equal_coordinate((int)r[32]-256,(int)num(a,"x"),task,seed,phase,action,"small.x");
        equal_coordinate((int)r[33]-256,(int)num(a,"y"),task,seed,phase,action,"small.y");
        equal_coordinate((int)r[34]-256,(int)num(b,"x"),task,seed,phase,action,"large.x");
        equal_coordinate((int)r[35]-256,(int)num(b,"y"),task,seed,phase,action,"large.y");
    }else if(task==1||task==7){
        const cJSON *s=field(j,"shape");
        near_svg_coordinate((int)r[34]-2560,(int)num(s,"x"),task,seed,phase,action,"shape.x10");
        near_svg_coordinate((int)r[35]-2560,(int)num(s,"y"),task,seed,phase,action,"shape.y10");
    }else if(task==2){equal_coordinate((int)r[33],(int)num(j,"face"),task,seed,phase,action,"face");}
    else if(task==3||task==4||task==8){
        const cJSON *items=field(j,"items");
        if(action!=WF_POINTER_MOVE){
            unsigned n=r[32];assert(cJSON_GetArraySize(items)==(int)n);
            for(unsigned i=0;i<n;i++){
                const char *label=drag_name(&v,v.nodes+i);
                const char *original=item(items,i)->valuestring;
                if(!label||strcmp(label,original)){
                    fprintf(stderr,"drag order mismatch task=%u seed=%u phase=%u action=%u index=%u model=%s browser=%s\n",
                        task,seed,phase,action,i,label?label:"(null)",original);
                    for(unsigned k=0;k<n;k++){
                        const char *m=drag_name(&v,v.nodes+k);
                        fprintf(stderr,"  %u: model=%s browser=%s\n",k,
                            m?m:"(null)",item(items,k)->valuestring);
                    }
                    abort();
                }
            }
        }
    }else if(task==5||task==6){
        const cJSON *shapes=field(j,"shapes");
        for(unsigned i=0;i<r[37];i++){
            const cJSON *s=item(shapes,i);const uint32_t *p=r+64+i*4;
            char label[32];snprintf(label,sizeof label,"shape%u.x10",i+1);
            near_svg_coordinate((int)p[2]-2560,(int)num(s,"x"),task,seed,phase,action,label);
            snprintf(label,sizeof label,"shape%u.y10",i+1);
            near_svg_coordinate((int)p[3]-2560,(int)num(s,"y"),task,seed,phase,action,label);
        }
    }else if(task==9){
        equal_coordinate((int)r[32],(int)num(j,"width"),task,seed,phase,action,"width");
        equal_coordinate((int)r[33],(int)num(j,"height"),task,seed,phase,action,"height");
    }
    if(!!r[WF_STATUS]!=yes(j,"done")||
       (r[WF_STATUS]&&
        (fabsf(real(r[WF_RAW_REWARD])-(float)num(j,"raw"))>1e-5f||
         fabsf(real(r[WF_TIMED_REWARD])-(float)num(j,"reward"))>2e-5f))){
        fprintf(stderr,"drag reward mismatch task=%u seed=%u phase=%u model=%u %.6f/%.6f browser=%d %.6f/%.6f\n",
            task,seed,phase,r[WF_STATUS],real(r[WF_RAW_REWARD]),
            real(r[WF_TIMED_REWARD]),yes(j,"done"),num(j,"raw"),num(j,"reward"));abort();
    }
}
static void action(WebCdp *c,WFLoaded *f,unsigned task,unsigned seed,
                   unsigned phase,const WFAction *a){
    char js[240];snprintf(js,sizeof js,
        "__drag.advance(%u);__drag.act(%u,%u,%u,%u)",
        a->elapsed_ms,a->kind,a->target,a->arg0,a->arg1);
    cJSON *j=eval(c,js);
    assert(!wf_apply(f,0,a));assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    compare(f,j,task,seed,phase,a->kind);cJSON_Delete(j);
}
static int open_page(WebCdp *c,unsigned task){
    char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
    snprintf(file,sizeof file,
      "build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",
      pages[task]);
    assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    return web_cdp_start_ready(c,chrome,url,"Boolean(window.core&&core.cover_div)");
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):2;
    if(!episodes||episodes>128)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/drag/libdrag.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    assert(f.api->task_count==10);
    char *script=read_file("ocean/webnav/families/drag/browser.js");
    unsigned compared=0,actions=0;
    for(unsigned task=0;task<10;task++){
        WebCdp c={.input=-1,.output=-1,.pid=-1};assert(!open_page(&c,task));
        cJSON *installed=eval(&c,script);assert(cJSON_IsTrue(installed));
        cJSON_Delete(installed);
        /* Six solved cube episodes cover every visible face. The extra two
           exercise an immediate wrong submit and timeout from the source. */
        unsigned limit=task==2&&episodes>=8?8:episodes;
        for(unsigned ep=0;ep<limit;ep++){
            unsigned seed=990000+task*1009+ep*97;
            unsigned desired=task==2&&episodes>=8?(ep<6?ep+1:ep==6?1:2):0;
            cJSON *j=NULL;
            for(unsigned attempt=0;attempt<(desired?1000u:1u);attempt++){
                seed=990000+task*1009+ep*97+attempt*7919;
                char js[80];snprintf(js,sizeof js,"__drag.reset(%u)",seed);
                j=eval(&c,js);
                if(!desired||goal_cube(str(j,"query"))==desired)break;
                cJSON_Delete(j);j=NULL;
            }
            if(!j){fprintf(stderr,"cube goal %u not sampled\n",desired);abort();}
            for(unsigned lane=0;lane<f.api->batch_lanes;lane++){
                uint32_t *r=f.words+(size_t)lane*ROW;
                r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
                r[WF_SEED]=seed+lane;
            }
            assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
            import_initial(f.words,task,j);assert(!f.api->validate(f.words));
            compare(&f,j,task,seed,0,0);cJSON_Delete(j);
            DragPublic controller={.task=task};unsigned phase=0;
            unsigned mode=task==2&&episodes>=8?(ep<6?0:ep==6?1:2):ep%3;
            if(mode==2){
                WFAction a={.kind=WF_WAIT,.elapsed_ms=f.words[WF_DEADLINE]};
                action(&c,&f,task,seed,++phase,&a);actions++;
                assert(f.words[WF_STATUS]==WF_TIMEOUT);
            }else if(mode==1){
                if(task==3||task==4){
                    unsigned wrong=1;
                    while(wrong==f.words[35]||wrong==f.words[36])wrong++;
                    WFAction a={.kind=WF_POINTER_DOWN,.target=wrong,.elapsed_ms=100};
                    action(&c,&f,task,seed,++phase,&a);actions++;
                    a=(WFAction){.kind=WF_POINTER_UP,.elapsed_ms=150};
                    action(&c,&f,task,seed,++phase,&a);actions++;
                    assert(f.words[WF_STATUS]==WF_RUNNING);
                    a=(WFAction){.kind=WF_POINTER_DOWN,.target=wrong,.elapsed_ms=175};
                    action(&c,&f,task,seed,++phase,&a);actions++;
                    a=(WFAction){.kind=WF_POINTER_MOVE,.arg0=f.words[36],.elapsed_ms=200};
                    action(&c,&f,task,seed,++phase,&a);actions++;
                    a=(WFAction){.kind=WF_POINTER_UP,.elapsed_ms=300};
                    action(&c,&f,task,seed,++phase,&a);actions++;
                }else{
                    unsigned submit=task==0?3:task==8?5:
                        task==2||task==5||task==6?8:2;
                    WFAction a={.kind=WF_CLICK,.target=submit,.elapsed_ms=100};
                    action(&c,&f,task,seed,++phase,&a);actions++;
                }
            }else for(unsigned step=0;step<40&&f.words[WF_STATUS]==WF_RUNNING;step++){
                    WFView v;assert(!wf_observe(&f,0,&v));
                    WFAction a={.elapsed_ms=100+step*100};
                    assert(!drag_public_next(&controller,&v,a.elapsed_ms,&a));
                    action(&c,&f,task,seed,++phase,&a);actions++;
            }
            assert(f.words[WF_STATUS]!=WF_RUNNING);compared++;
        }
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    printf("drag original-page matched episodes %u, actions %u\n",compared,actions);
    return 0;
}
