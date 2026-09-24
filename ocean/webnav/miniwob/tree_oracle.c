#define _GNU_SOURCE
#include "bridge.h"
#include "cdp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int eval(WebCdp *c,const char *js) { cJSON *r=web_cdp_eval(c,js); if(!r)return -1; cJSON_Delete(r); return 0; }
static unsigned num(const cJSON *j,const char *key) { return (unsigned)cJSON_GetNumberValue(cJSON_GetObjectItem(j,key)); }
static int flag(const cJSON *j,const char *key) { return cJSON_IsTrue(cJSON_GetObjectItem(j,key)); }
static void batch(uint32_t *w) { for(unsigned i=1;i<32;i++)memcpy(w+i*256,w,256*sizeof *w); webnav_batch(w); }

static int compare(const cJSON *s,const uint32_t *w) {
    const cJSON *nodes=cJSON_GetObjectItem(s,"nodes");
    if(cJSON_GetArraySize(nodes)!=(int)w[1]||flag(s,"done")!=(w[2]!=0))return -1;
    for(unsigned i=0;i<w[1];i++) {
        const cJSON *n=cJSON_GetArrayItem(nodes,(int)i); const uint32_t *r=w+16+6*i;
        if((unsigned)flag(n,"folder")!=r[0]||(unsigned)flag(n,"target")!=r[1]||
           (unsigned)flag(n,"expanded")!=r[2]||num(n,"parent")!=r[3]||num(n,"end")!=r[4]||
           (unsigned)flag(n,"visible")!=r[5]) {
            fprintf(stderr,"tree node mismatch i=%u: model expanded=%u visible=%u browser expanded=%d visible=%d\n",i,r[2],r[5],flag(n,"expanded"),flag(n,"visible")); return -1;
        }
    }
    if(w[2]) {
        float raw,timed; memcpy(&raw,w+11,4); memcpy(&timed,w+12,4);
        if(fabs(raw-cJSON_GetNumberValue(cJSON_GetObjectItem(s,"raw")))>1e-6||
           fabs(timed-cJSON_GetNumberValue(cJSON_GetObjectItem(s,"reward")))>1e-6||num(s,"elapsed")!=w[10])return -1;
    }
    return 0;
}
static int import_tree(WebCdp *c,uint32_t *w) {
    cJSON *s=web_cdp_eval(c,"__tree.snapshot()"); if(!s)return -1;
    const cJSON *nodes=cJSON_GetObjectItem(s,"nodes"); int count=cJSON_GetArraySize(nodes);
    if(count<1||count>8){fprintf(stderr,"Unsupported tree size %d; no silent seed omission\n",count);cJSON_Delete(s);return -1;}
    memset(w,0,8192*sizeof *w);w[0]=9;w[1]=(unsigned)count;w[5]=1;
    for(int i=0;i<count;i++) {
        const cJSON *n=cJSON_GetArrayItem(nodes,i);uint32_t *r=w+16+6*i;
        r[0]=flag(n,"folder");r[1]=flag(n,"target");r[2]=flag(n,"expanded");
        r[3]=num(n,"parent");r[4]=num(n,"end");r[5]=flag(n,"visible");
    }
    batch(w);int bad=compare(s,w);cJSON_Delete(s);return bad;
}
static int step(WebCdp *c,uint32_t *w,unsigned command,unsigned index,unsigned *actions) {
    unsigned delta=command==2?10000:137;char js[512];
    snprintf(js,sizeof js,"__tree.advance(%u);true",delta);if(eval(c,js))return -1;
    if(command==1&&index<w[1]&&w[21+6*index]&&w[2]==0&&w[9]+delta<10000) {
        snprintf(js,sizeof js,"(()=>{const e=document.querySelectorAll('#tree li')[%u].querySelector(':scope > span');const b=e.getBoundingClientRect();const x=b.x+b.width/2,y=b.y+b.height/2;return {x,y,index:[...document.querySelectorAll('#tree li')].indexOf(document.elementFromPoint(x,y)?.closest('li'))}})()",index);
        cJSON *point=web_cdp_eval(c,js);if(!point)return -1;
        if(num(point,"index")!=index){fprintf(stderr,"Tree label click obstructed index=%u\n",index);cJSON_Delete(point);return -1;}
        double x=cJSON_GetNumberValue(cJSON_GetObjectItem(point,"x")),y=cJSON_GetNumberValue(cJSON_GetObjectItem(point,"y"));cJSON_Delete(point);
        if(web_cdp_click(c,x,y))return -1;
    }
    w[7]=command;w[8]=index;w[9]+=delta;batch(w);(*actions)++;
    cJSON *s=web_cdp_eval(c,"__tree.snapshot()");if(!s)return -1;
    int bad=compare(s,w);cJSON_Delete(s);return bad;
}

int main(int argc,char **argv) {
    int episodes=argc>1?atoi(argv[1]):200;if(episodes<1)return 2;
    WebCdp *c=calloc(1,sizeof *c);if(!c)return 1;c->input=c->output=-1;c->pid=-1;
    int status=1;char path[4096],url[4300];
    if(!realpath("build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/navigate-tree.html",path))goto done;
    snprintf(url,sizeof url,"file://%s",path);const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    if(web_cdp_start_ready(c,chrome,url,"Boolean(window.core&&core.cover_div)"))goto done;
    if(eval(c,
        "(()=>{window.__tree={clock:0,elapsed:0};const D=Date;window.Date=class extends D{constructor(...a){super(...(a.length?a:[__tree.clock]))}static now(){return __tree.clock}};"
        "const end=core.endEpisode;core.endEpisode=function(r,t,why){if(core.EP_TIMER!==null)__tree.elapsed=Date.now()-core.ept0;return end(r,t,why)};"
        "__tree.reset=seed=>{__tree.clock=0;__tree.elapsed=0;Math.seedrandom(String(seed));core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer()};"
        "__tree.advance=ms=>{__tree.clock+=ms;if(!WOB_DONE_GLOBAL&&__tree.clock-core.ept0>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out')};"
        "__tree.snapshot=()=>{const nodes=[...document.querySelectorAll('#tree li')];const target=document.querySelector('#query').textContent.match(/\"([^\"]*)\"/)[1];return {nodes:nodes.map((e,i)=>{const span=e.querySelector(':scope > span'),ul=e.querySelector(':scope > ul'),b=span.getBoundingClientRect();let end=i+1;while(end<nodes.length&&e.contains(nodes[end]))end++;return {name:span.textContent,folder:span.classList.contains('folder'),target:span.textContent===target,expanded:!!ul&&ul.style.display!=='none',parent:nodes.indexOf(e.parentElement.closest('li'))+1,end,visible:b.width>0&&b.height>0&&getComputedStyle(span).visibility!=='hidden'}}),done:WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,elapsed:__tree.elapsed}};return true})()"))goto done;
    unsigned actions=0,success=0,wrong=0,timeouts=0;uint32_t w[8192];
    for(int ep=0;ep<episodes;ep++) {
        char js[128];snprintf(js,sizeof js,"__tree.reset(%d);true",300000+ep);if(eval(c,js)||import_tree(c,w))goto done;
        unsigned schedule=(unsigned)ep%5;
#define STEP(cmd,idx) do{if(step(c,w,cmd,idx,&actions)){fprintf(stderr,"tree mismatch seed=%d command=%u index=%u\n",300000+ep,(unsigned)(cmd),(unsigned)(idx));goto done;}}while(0)
        if(schedule==1){STEP(2,0);}
        else if(schedule==2){unsigned chosen=0;for(unsigned i=0;i<w[1];i++)if(w[21+6*i]&&!w[16+6*i]&&!w[17+6*i]){chosen=i;break;}STEP(1,chosen);}
        else {
            STEP(1,99);
            for(unsigned i=0;i<w[1];i++)if(!w[21+6*i]){STEP(1,i);break;}
            if(schedule==3)for(unsigned i=0;i<w[1];i++)if(w[21+6*i]&&w[16+6*i]&&!w[17+6*i]){STEP(1,i);STEP(1,i);break;}
        }
        /* Conformance schedule may use the whole imported tree, including
         * currently hidden labels. This is not a policy or an agent score. */
        for(unsigned attempt=0;w[2]==0&&attempt<10;attempt++) {
            unsigned chosen=w[1];for(unsigned i=0;i<w[1];i++)if(w[17+6*i]){chosen=i;break;}
            if(chosen==w[1]){STEP(2,0);break;}
            unsigned cursor=chosen;
            while(w[19+6*cursor]){unsigned p=w[19+6*cursor]-1;if(!w[18+6*p])chosen=p;cursor=p;}
            STEP(1,chosen);
        }
        if(w[2]==0){fprintf(stderr,"tree expert failed to terminate seed=%d\n",300000+ep);goto done;}
        if(w[2]==2)timeouts++;else if(w[4])success++;else wrong++;
        STEP(0,0); /* Original terminal reward and simulator outcome stay frozen. */
#undef STEP
    }
    unsigned fixture_actions=0;
    for(unsigned fixture=0;fixture<3;fixture++) {
        if(eval(c,"__tree.reset(399999);document.querySelector('#query').textContent='Find \"Bingo\"';true"))goto done;
        const char *html=fixture==0?"<li><span class=folder>Bingo</span><ul><li><span class=folder>Wrong</span><ul></ul></li></ul></li>":fixture==1?"<li><span class=folder>Bingo</span><ul><li><span class=file>Wrong</span></li></ul></li>":"<li><span class=file>Bingo</span></li><li><span class=file>Bingo</span></li>";
        char js[1024];snprintf(js,sizeof js,"document.querySelector('#tree').innerHTML='%s';$('#tree').treeview({collapsed:true});$('#tree > li > ul').show();bindClickEvents('Bingo');true",html);
        if(eval(c,js)||import_tree(c,w)||step(c,w,1,1,&fixture_actions)||w[2]!=1||w[4]!=(fixture!=1)){fprintf(stderr,"tree directed fixture failed %u\n",fixture);goto done;}
    }
    printf("{\"task\":\"navigate-tree\",\"episodes\":%d,\"actions\":%u,\"success\":%u,\"wrong\":%u,\"timeouts\":%u,\"directed_fixtures\":3,\"fixture_actions\":%u,\"conformance\":\"PASS\",\"scope\":\"original seeded instances, real label clicks, tree topology/expansion/visibility and rewards; three custom initial fixtures using original handlers; controlled clock; no full DOM, hitarea/blank-LI or generator parity\"}\n",episodes,actions,success,wrong,timeouts,fixture_actions);status=0;
done:web_cdp_close(c);free(c);return status;
}
