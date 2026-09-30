#define _GNU_SOURCE
#include "../common/loader.h"
#include "public_controller.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static double num(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);assert(cJSON_IsNumber(v));return v->valuedouble;}
static unsigned boolean(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *str(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static char *read_file(const char *name){FILE *f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc(n+1,1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);return s;}
static void packed(uint32_t *out,unsigned cap,const char *s){assert(strlen(s)<cap);for(unsigned i=0;s[i];i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);out[i/4]|=(uint32_t)(unsigned char)s[i]<<(8*(i%4));}}
static void import(uint32_t *r,const cJSON *j){
    memset(r,0,512*sizeof *r);r[0]=2;r[12]=10000;
    uint32_t *b=r+32;b[0]=9;b[5]=1;
    const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");assert(cJSON_IsArray(nodes));b[1]=cJSON_GetArraySize(nodes);assert(b[1]>0&&b[1]<=8);
    packed(b+224,128,str(j,"query"));
    for(unsigned i=0;i<b[1];i++){
        const cJSON *n=cJSON_GetArrayItem(nodes,i);uint32_t *p=b+16+6*i;
        p[0]=boolean(n,"folder");p[1]=boolean(n,"goal");p[2]=boolean(n,"expanded");p[3]=(unsigned)num(n,"parent");p[4]=(unsigned)num(n,"end");p[5]=boolean(n,"visible");
        packed(b+64+8*i,32,str(n,"name"));
    }
}
static void compare(WFLoaded *f,const cJSON *j,unsigned seed,unsigned step){
    WFView v;assert(!wf_observe(f,0,&v));const uint32_t *b=f->words+32;
    const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");
    int mismatch=boolean(j,"done")!=(f->words[9]!=0);
    if(f->words[9])mismatch|=fabs(num(j,"raw")-real(f->words[10]))>1e-6||fabs(num(j,"reward")-real(f->words[11]))>1e-5;
    unsigned visible=0;
    for(unsigned i=0;i<b[1];i++){
        const cJSON *n=cJSON_GetArrayItem(nodes,i);const uint32_t *p=b+16+6*i;
        mismatch|=p[2]!=boolean(n,"expanded")||p[5]!=boolean(n,"visible");
        if(boolean(n,"visible")){assert(visible<v.count);const WFNode *node=v.nodes+visible++;mismatch|=node->ref!=i+1||node->parent!=(unsigned)num(n,"parent")||strcmp(wf_text_get(&v,node->name),str(n,"name"));}
    }
    mismatch|=visible!=v.count;
    if(mismatch){fprintf(stderr,"tree mismatch seed=%u step=%u status=%u\n",seed,step,f->words[9]);abort();}
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);unsigned episodes=argc>1?strtoul(argv[1],0,10):100;if(!episodes||episodes>10000)return 2;
    WFLoaded f;char error[512];assert(!wf_open(&f,"build/webnav/families/tree/libtree.so",error,sizeof error));
    char file[PATH_MAX],url[PATH_MAX+8];assert(realpath("build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/navigate-tree.html",file));snprintf(url,sizeof url,"file://%s",file);
    WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));char *script=read_file("ocean/webnav/families/tree/browser.js");cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);free(script);
    unsigned actions=0,wins=0,reversals=0;
    for(unsigned ep=0;ep<episodes;ep++){
        unsigned seed=910000+ep,opened=0;char js[128];snprintf(js,sizeof js,"__tr.reset(%u)",seed);j=web_cdp_eval(&c,js);assert(j);import(f.words,j);assert(!f.api->validate(f.words));compare(&f,j,seed,0);cJSON_Delete(j);
        for(unsigned l=1;l<8;l++)memcpy(f.words+l*512,f.words,512*sizeof *f.words);
        for(unsigned step=1;step<=40&&!f.words[9];step++){
            WFView v;assert(!wf_observe(&f,0,&v));WFAction a={.kind=WF_WAIT};unsigned ms=ep%5==4?10000:step*200;
            if(ep%5!=4){
                assert(!tree_public_next(&v,&a));
                if(ep%5==1)for(unsigned i=0;i<v.count;i++)if(v.nodes[i].role==WF_FILE&&v.nodes[i].ref!=a.target){a.target=v.nodes[i].ref;break;}
                if(ep%5==2&&step==1)for(unsigned i=0;i<v.count;i++)if(v.nodes[i].role==WF_FOLDER&&v.nodes[i].ref!=a.target){opened=v.nodes[i].ref;a.target=opened;break;}
                if(ep%5==2&&step==2&&opened){a.target=opened;reversals++;}
            }
            snprintf(js,sizeof js,"__tr.tick(%u)",ms);j=web_cdp_eval(&c,js);assert(j);cJSON_Delete(j);
            if(a.kind==WF_CLICK&&ms<10000){snprintf(js,sizeof js,"__tr.point(%u)",a.target);j=web_cdp_eval(&c,js);assert(j);double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);assert(!web_cdp_click(&c,x,y));}
            a.elapsed_ms=ms;assert(!wf_apply(&f,0,&a));assert(!wf_batch_checked(&f));j=web_cdp_eval(&c,"__tr.export()");assert(j);compare(&f,j,seed,step);cJSON_Delete(j);actions++;
        }
        assert(f.words[9]);wins+=real(f.words[10])>0.99;
    }
    printf("{\"task\":\"navigate-tree\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"collapse_schedules\":%u,\"differential\":\"PASS\",\"preset\":\"original-generated trees, CDP span clicks, settled plugin and controlled clock\"}\n",episodes,actions,wins,reversals);
    web_cdp_close(&c);wf_close(&f);return 0;
}
