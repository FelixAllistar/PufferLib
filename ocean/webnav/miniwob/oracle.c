#define _GNU_SOURCE
#include "bridge.h"
#include "cdp.h"
#include "dom.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char *read_file(const char *path){FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1,1);if(s&&fread(s,1,(size_t)n,f)!=(size_t)n){free(s);s=NULL;}fclose(f);return s;}
static void wire_string(const uint32_t *w,unsigned at,unsigned cap,char *out){
    unsigned i=0;for(;i+1<cap&&w[at+i];i++)out[i]=(char)w[at+i];out[i]=0;
}
static int js(WebCdp *c,const char *s){cJSON *r=web_cdp_eval(c,s);if(!r)return -1;cJSON_Delete(r);return 0;}
static cJSON *snapshot(WebCdp *c,WebDom *obs){cJSON *r=web_cdp_eval(c,"__mw.snapshot()");if(!r)return NULL;if(web_dom_parse(obs,cJSON_GetObjectItem(r,"obs"))){fprintf(stderr,"Invalid DOM schema\n");cJSON_Delete(r);return NULL;}return r;}
static const WebDomNode *by_ref(const WebDom *o,unsigned ref){for(unsigned i=0;i<o->count;i++)if(o->nodes[i].ref==ref)return o->nodes+i;return NULL;}
static int target_label(const char *instruction,const char *name,int task){
    if(!task){const char *start=strchr(instruction,'"'),*end=strrchr(instruction,'"');return start&&end>start&&(size_t)(end-start-1)==strlen(name)&&!strncmp(start+1,name,(size_t)(end-start-1));}
    if(strncmp(instruction,"Select ",7))return 0;const char *start=instruction+7,*end=strstr(start," and click Submit.");if(!end)return 0;
    while(start<end){const char *sep=strstr(start,", ");if(!sep||sep>end)sep=end;if((size_t)(sep-start)==strlen(name)&&!strncmp(start,name,(size_t)(sep-start)))return 1;start=sep+2;}return 0;
}
static int compare(const WebDom *o,WebDom *expected,const cJSON *r,const uint32_t *w,const unsigned *refs,unsigned elapsed,int ep,int step){
    unsigned focus=0;for(unsigned i=0;i<w[1];i++){const WebDomNode *n=by_ref(o,refs[i]);if(!n)return -1;if(n->flags&WEB_FOCUSED)focus=i+1;if((!!(n->flags&WEB_CHECKED))!=w[16+i*3+1]){fprintf(stderr,"Checked mismatch ep=%d step=%d slot=%u\n",ep,step,i);return -1;}}
    if(focus!=w[3]){fprintf(stderr,"Focus mismatch ep=%d step=%d browser=%u bend=%u\n",ep,step,focus,w[3]);return -1;}
    for(unsigned i=0;i<expected->count;i++) {
        WebDomNode *n=expected->nodes+i;
        for(unsigned k=0;k<w[1];k++)if(n->ref==refs[k]) {
            n->flags&=~(WEB_CHECKED|WEB_FOCUSED);
            if(w[17+k*3])n->flags|=WEB_CHECKED;
            if(w[3]==k+1)n->flags|=WEB_FOCUSED;
        }
    }
    if(memcmp(o,expected,sizeof *o)){fprintf(stderr,"Public DOM projection mismatch ep=%d step=%d\n",ep,step);return -1;}
    int done=cJSON_IsTrue(cJSON_GetObjectItem(r,"done"));if(done!=(w[2]!=0)){fprintf(stderr,"Done mismatch ep=%d step=%d\n",ep,step);return -1;}
    if(done){float raw,reward;memcpy(&raw,w+11,sizeof raw);memcpy(&reward,w+12,sizeof reward);
        if(cJSON_GetObjectItem(r,"elapsed")->valuedouble!=elapsed||w[10]!=elapsed){fprintf(stderr,"Elapsed time mismatch\n");return -1;}
        double actual_raw=cJSON_GetObjectItem(r,"raw")->valuedouble,actual_reward=cJSON_GetObjectItem(r,"reward")->valuedouble;
        if(fabs(raw-actual_raw)>1e-6||fabs(reward-actual_reward)>1e-6){fprintf(stderr,"Reward mismatch ep=%d step=%d browser=%g/%g bend=%g/%g\n",ep,step,actual_raw,actual_reward,raw,reward);return -1;}}
    return 0;
}
static void step(uint32_t *w,unsigned command,unsigned index){for(int i=0;i<32;i++){if(i)memcpy(w+i*256,w,256*sizeof *w);w[i*256+7]=command;w[i*256+8]=index;}webnav_batch(w);}
int main(int argc,char **argv){
    const char *task=argc>1?argv[1]:"click-button";int options=!strcmp(task,"click-option"),checks=!strcmp(task,"click-checkboxes"),focus_task=!strcmp(task,"focus-text"),links=!strcmp(task,"click-link"),episodes=argc>2?atoi(argv[2]):200;
    if((!checks&&!options&&!focus_task&&!links&&strcmp(task,"click-button"))||episodes<1){fprintf(stderr,"usage: oracle click-button|click-checkboxes|click-link|focus-text|click-option [episodes=200]\n");return 2;}
    int generated=getenv("WEBNAV_GENERATED_FORMS")!=NULL;
    if(generated&&!checks&&!options){fprintf(stderr,"Generated forms support click-checkboxes/click-option only\n");return 2;}
    WebCdp *c=calloc(1,sizeof *c);WebDom *obs=calloc(1,sizeof *obs),*expected=calloc(1,sizeof *expected);if(!c||!obs||!expected)return 1;c->input=c->output=-1;c->pid=-1;int status=1;
    FILE *record=NULL;cJSON *episode_record=NULL,*trace=NULL;
    const char *record_path=getenv("WEBNAV_RECORD");
    if(record_path){record=fopen(record_path,"w");if(!record){perror(record_path);goto done;}}
    char file[4096],url[4300],path[4096];snprintf(path,sizeof path,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",task);
    if(!realpath(path,file)){perror(path);goto done;}snprintf(url,sizeof url,"file://%s",file);
    const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    if(web_cdp_start_ready(c,chrome,url,"Boolean(window.core&&core.cover_div)"))goto done;
    char *script=read_file("ocean/webnav/web/dom_snapshot.js");if(!script)goto done;int error=js(c,script);free(script);if(error)goto done;
    if(js(c,"(()=>{window.__mw={clock:0,elapsed:0};const D=Date;window.Date=class extends D{constructor(...a){super(...(a.length?a:[__mw.clock]))}static now(){return __mw.clock}};const end=core.endEpisode;core.endEpisode=function(r,t,why){if(core.EP_TIMER!==null)__mw.elapsed=Date.now()-core.ept0;return end(r,t,why)};__mw.reset=seed=>{__mw.clock=0;__mw.elapsed=0;Math.seedrandom(String(seed));core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer()};__mw.advance=ms=>{__mw.clock+=ms;if(!WOB_DONE_GLOBAL&&__mw.clock-core.ept0>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out')};__mw.snapshot=()=>({obs:webnavDOM(),done:WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,elapsed:__mw.elapsed});return true})()"))goto done;
    if(generated&&js(c,"(()=>{const build=(div,radio)=>{const f=__mw.generated;for(let i=0;i<f.labels.length;i++){const label=div.append('label');label.append('input').attr('type',radio?'radio':'checkbox').attr('id','ch'+i).attr('name',radio?'radio':null);label[0][0].appendChild(document.createTextNode(f.labels[i]));div.append('br')}if(radio)return {query:f.labels[f.targets.indexOf(true)],index:f.targets.indexOf(true)};const toclick={};f.targets.forEach((v,i)=>toclick[i]=v);return {elems:f.labels.length,toclick,clickNames:f.labels.filter((_,i)=>f.targets[i])}};if(typeof createCheckboxes==='function')createCheckboxes=div=>build(div,false);if(typeof createElements==='function')createElements=div=>build(div,true);return true})()"))goto done;
    /* Capture the original generator's private radio index / checkbox bits for the simulator spec
     * only. The scripted controller below uses public names, including ambiguity. */
    if(options&&js(c,"(()=>{const original=createElements;createElements=div=>{const r=original(div);__mw.privateRadioIndex=r.index;return r};return true})()"))goto done;
    if(checks&&js(c,"(()=>{const original=createCheckboxes;createCheckboxes=div=>{const r=original(div);__mw.privateChecks=r.toclick;return r};return true})()"))goto done;
    int full=0,partial=0,fail=0,timeouts=0;long actions=0;uint32_t words[8192]={0};
    for(int ep=0;ep<episodes;ep++){
        uint32_t generated_words[8192]={0};
        if(generated){
            generated_words[0]=options?3:1;generated_words[13]=100000+(unsigned)ep;step(generated_words,3,0);
            cJSON *f=cJSON_CreateObject(),*labels=cJSON_AddArrayToObject(f,"labels"),*targets=cJSON_AddArrayToObject(f,"targets");
            for(unsigned i=0;i+1<generated_words[1];i++){char label[8];wire_string(generated_words,64+8*i,sizeof label,label);cJSON_AddItemToArray(labels,cJSON_CreateString(label));cJSON_AddItemToArray(targets,cJSON_CreateBool(generated_words[18+3*i]));}
            char *json=cJSON_PrintUnformatted(f),*set=NULL;cJSON_Delete(f);
            if(!json||asprintf(&set,"__mw.generated=%s;true",json)<0){free(json);goto done;}
            int bad=js(c,set);free(set);free(json);if(bad)goto done;
        }
        char expr[128];snprintf(expr,sizeof expr,"__mw.reset(%d);true",100000+ep);if(js(c,expr))goto done;
        cJSON *r=snapshot(c,obs);if(!r)goto done;
        if(generated){char query[128];wire_string(generated_words,128,sizeof query,query);if(strcmp(query,web_dom_text(obs,obs->instruction))){fprintf(stderr,"Generated instruction differs from original task formatter\n");cJSON_Delete(r);goto done;}}
        if(record){episode_record=cJSON_CreateObject();cJSON_AddStringToObject(episode_record,"schema","webnav-trace-v1");cJSON_AddStringToObject(episode_record,"action_preset","control-click-wait-v1");cJSON_AddStringToObject(episode_record,"generator",generated?"stock-cpu-bend/forms-v1":"upstream-seedrandom");cJSON_AddStringToObject(episode_record,"source",generated?"Bend-generated form / original MiniWoB HTML handlers / controlled-clock CDP":"original MiniWoB HTML / controlled-clock CDP");cJSON_AddStringToObject(episode_record,"revision","33c3b4ddef8c6eb67c57a29663d844b1eda7e614");cJSON_AddStringToObject(episode_record,"task",task);cJSON_AddNumberToObject(episode_record,"seed",100000+ep);cJSON_AddNumberToObject(episode_record,"schedule",ep%5);cJSON_AddStringToObject(episode_record,"controller","scripted public-label/adversarial schedules; not a learned policy or human demonstration");cJSON_AddItemToObject(episode_record,"initial",cJSON_Duplicate(cJSON_GetObjectItem(r,"obs"),1));trace=cJSON_AddArrayToObject(episode_record,"transitions");}
        cJSON_Delete(r);memcpy(expected,obs,sizeof *expected);
        unsigned private_radio=0,private_checks[16]={0};
        if(options){cJSON *v=web_cdp_eval(c,"__mw.privateRadioIndex");if(!cJSON_IsNumber(v)){cJSON_Delete(v);goto done;}private_radio=(unsigned)v->valuedouble;cJSON_Delete(v);}
        if(checks){cJSON *v=web_cdp_eval(c,"Object.values(__mw.privateChecks)");if(!cJSON_IsArray(v)||cJSON_GetArraySize(v)>16){cJSON_Delete(v);goto done;}for(int k=0;k<cJSON_GetArraySize(v);k++){cJSON *b=cJSON_GetArrayItem(v,k);if(!cJSON_IsBool(b)){cJSON_Delete(v);goto done;}private_checks[k]=cJSON_IsTrue(b);}cJSON_Delete(v);}
        memset(words,0,sizeof words);words[0]=options?3u:focus_task?2u:(unsigned)checks;unsigned refs[16],public_match[16]={0},count=0;
        for(unsigned i=0;i<obs->count;i++){WebDomNode *n=obs->nodes+i;unsigned kind=n->role;if(options&&kind==WEB_ROLE_RADIO)kind=5;if(links&&kind==0&&(n->flags&WEB_ACTIONABLE))kind=4;if(kind<1||(kind>3+(unsigned)links&&!(options&&kind==5)))continue;if(count==16){fprintf(stderr,"Too many controls\n");goto done;}refs[count]=n->ref;words[16+count*3]=kind;words[17+count*3]=!!(n->flags&WEB_CHECKED);public_match[count]=target_label(web_dom_text(obs,obs->instruction),web_dom_text(obs,n->name),checks||options);words[18+count*3]=options?(kind==5&&count==private_radio):checks?(kind==2&&private_checks[count]):public_match[count];if(n->flags&WEB_FOCUSED)words[3]=count+1;count++;}
        words[1]=count;if(!count)goto done;
        if(generated&&(count!=generated_words[1]||memcmp(words+16,generated_words+16,count*3*sizeof(uint32_t)))){fprintf(stderr,"Generated private specification differs from browser import\n");goto done;}
        /* Five schedules: public-label expert, arbitrary click, submit/wrong,
         * reversible checkbox edits, and logical-clock timeout. */
        int schedule=ep%5;unsigned elapsed=0;
        for(int turn=0;turn<32&&!words[2];turn++){
            unsigned target=0,command=1,delta=137;
            if(schedule==4||turn==31){command=2;delta=10000;}
            else if(focus_task){target=0;}
            else if(!checks&&!options){int found=0;for(unsigned i=0;i<count;i++)if((words[16+i*3]==1||words[16+i*3]==4)&&(schedule==0||schedule==3?words[18+i*3]:!words[18+i*3])){target=i;found=1;break;}if(!found){for(unsigned i=0;i<count;i++)if(words[16+i*3]==1||words[16+i*3]==4){target=i;break;}}if(schedule==1&&turn==0)target=(unsigned)ep%count;}
            else if(options&&schedule!=1&&schedule!=2){
                int chosen=0;for(unsigned i=0;i<count;i++)if(words[16+i*3]==5&&public_match[i]){target=i;chosen=1;break;}
                if(!chosen){fprintf(stderr,"Radio instruction has no matching public name\n");goto done;}
                if(schedule==3&&turn<2){target=turn==0?0:count-2;}
                else if(words[17+target*3])for(unsigned i=0;i<count;i++)if(words[16+i*3]==1)target=i;
            }
            else if(schedule==1){target=(unsigned)(ep*17+turn*13)%count;}
            else if(schedule==2){for(unsigned i=0;i<count;i++)if(words[16+i*3]==1)target=i;}
            else if(schedule==3&&turn<2){for(unsigned i=0;i<count;i++)if(words[16+i*3]==2){target=i;break;}}
            else {int found=0;for(unsigned i=0;i<count;i++)if(words[16+i*3]==2&&words[17+i*3]!=public_match[i]){target=i;found=1;break;}if(!found)for(unsigned i=0;i<count;i++)if(words[16+i*3]==1)target=i;}
            snprintf(expr,sizeof expr,"__mw.advance(%u);true",delta);if(js(c,expr))goto done;
            if(command==1){const WebDomNode *n=by_ref(obs,refs[target]);if(!n||web_cdp_click(c,n->x+n->width/2,n->y+n->height/2))goto done;}
            elapsed+=delta;words[9]=elapsed;
            step(words,command,target);r=snapshot(c,obs);if(!r)goto done;
            int mismatch=compare(obs,expected,r,words,refs,elapsed,ep,turn);
            if(record&&!mismatch){cJSON *transition=cJSON_CreateObject();cJSON_AddStringToObject(transition,"operation",command==2?"wait_timeout":"click");cJSON_AddNumberToObject(transition,"target_ref",command==2?0:refs[target]);cJSON_AddNumberToObject(transition,"elapsed_delta_ms",delta);cJSON_AddItemToObject(transition,"after",cJSON_Duplicate(r,1));cJSON_AddItemToArray(trace,transition);}
            cJSON_Delete(r);if(mismatch)goto done;actions++;
        }
        if(words[2]==2)timeouts++;else if(words[4]==words[5])full++;else if(2*words[4]>words[5])partial++;else fail++;
        if(record){char *line=cJSON_PrintUnformatted(episode_record);if(!line||fprintf(record,"%s\n",line)<0){free(line);goto done;}free(line);cJSON_Delete(episode_record);episode_record=NULL;trace=NULL;}
    }
    printf("{\"task\":\"%s\",\"episodes\":%d,\"actions\":%ld,\"full_credit\":%d,\"positive_partial\":%d,\"nonpositive\":%d,\"timeouts\":%d,\"state_reward_conformance\":\"PASS\",\"clock\":\"controlled logical milliseconds\",\"reset\":\"%s\",\"scope\":\"click/wait controls; checked/focus/outcome/raw and timed reward; not full DOM or generator parity\"}\n",task,episodes,actions,full,partial,fail,timeouts,generated?"Bend-generated form in original task HTML":"upstream-generated matched instance");status=0;
done:
    cJSON_Delete(episode_record);if(record&&fclose(record))status=1;
    web_cdp_close(c);free(c);free(obs);free(expected);return status;
}
