#define _GNU_SOURCE
#include "bridge.h"
#include "cdp.h"
#include "policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static unsigned random_u32(unsigned *x){*x^=*x<<13;*x^=*x>>17;*x^=*x<<5;return *x;}
/* Only public bytes/roles/checked state/value are used by this baseline. */
static int expert(const uint32_t *obs) {
    if(obs[0]=='C'&&obs[1]=='l') {
        for(int i=0;i<4;i++)if(obs[32+i*16+10]==obs[6])return i+1;
    } else if(obs[0]=='C') {
        for(int i=0;i<4;i++) {
            unsigned name=obs[32+i*16+10];int wanted=0;
            for(int j=6;j<10;j++)wanted|=obs[j]==name;
            if(wanted!=(int)obs[32+i*16+7])return i+1;
        }
        return 5;
    } else {
        unsigned first=obs[46],second=obs[47];
        if(first==obs[5]&&second==obs[6])return 5;
        if(!obs[40])return 1;
        if((first&&first!=obs[5])||(second&&second!=obs[6]))return 10;
        return 6+(int)(first?obs[6]:obs[5])-97;
    }
    return 0;
}

static int compare(cJSON *actual,const uint32_t *r,int episode,int step,int action,uint32_t *browser_obs) {
    if(!actual)return -1;
    cJSON *a=cJSON_GetObjectItemCaseSensitive(actual,"obs");
    if(cJSON_GetArraySize(a)!=WEBNAV_OBS)return -1;
    for(int i=0;i<WEBNAV_OBS;i++) {
        cJSON *v=cJSON_GetArrayItem(a,i);if(!cJSON_IsNumber(v))return -1;
        browser_obs[i]=(uint32_t)v->valuedouble;
        if(v->valuedouble!=(double)r[32+i]) {
            fprintf(stderr,"episode=%d seed=%u task=%u step=%d action=%d obs[%d]: browser=%g bend=%u\n",episode,r[0],r[1],step,action,i,v->valuedouble,r[32+i]);return -1;
        }
    }
    const char *names[]={"done","result","steps"};int indices[]={9,10,8};
    for(int i=0;i<3;i++) {
        cJSON *v=cJSON_GetObjectItemCaseSensitive(actual,names[i]);
        if(!cJSON_IsNumber(v)||v->valueint!=(int)r[indices[i]]) {
            fprintf(stderr,"episode=%d seed=%u task=%u step=%d action=%d %s browser=%d bend=%u\n",episode,r[0],r[1],step,action,names[i],v?v->valueint:-1,r[indices[i]]);return -1;
        }
    }
    return 0;
}
int main(int argc,char **argv) {
    if(argc<2){fprintf(stderr,"usage: %s native|browser|watch [episodes=300] [checkpoint|-] [hidden=128] [layers=2] [seed=100000] [expert|random|mixed]\n",argv[0]);return 2;}
    int watch=!strcmp(argv[1],"watch");
    int browser=watch||!strcmp(argv[1],"browser"),episodes=argc>2?atoi(argv[2]):300;
    if((!browser&&strcmp(argv[1],"native"))||episodes<1)return 2;
    const char *path=argc>3?argv[3]:"-";
    int hidden=argc>4?atoi(argv[4]):128,layers=argc>5?atoi(argv[5]):2;
    unsigned seed=argc>6?(unsigned)strtoul(argv[6],NULL,10):100000;
    const char *mode=argc>7?argv[7]:"mixed";
    int scripted[16]={0},scripted_count=0;
    if(!strncmp(mode,"script:",7)) {
        char *copy=strdup(mode+7),*save=NULL;
        for(char *s=strtok_r(copy,",",&save);s&&scripted_count<16;s=strtok_r(NULL,",",&save)) {
            char *end;long value=strtol(s,&end,10);
            if(*end||value<0||value>255){free(copy);return 2;}
            scripted[scripted_count++]=(int)value;
        }
        free(copy);
    } else if(strcmp(mode,"mixed")&&strcmp(mode,"expert")&&strcmp(mode,"random"))return 2;
    void *policy=strcmp(path,"-")?webnav_policy_load(path,hidden,layers):NULL;
    if(strcmp(path,"-")&&!policy)return 2;
    WebCdp *c=calloc(1,sizeof *c);int status=1;
    if(!c){webnav_policy_free(policy);return 1;}
    c->input=c->output=-1;c->pid=-1;
    if(browser) {
        char file[4096],url[4200];if(!realpath("ocean/webnav/web/index.html",file))goto finish;
        snprintf(url,sizeof url,"file://%s",file);
        const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome=watch?"build/webnav/chrome-linux64/chrome":"build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        if(web_cdp_start(c,chrome,url)){fprintf(stderr,"Chromium startup failed; see build/webnav/chromium.log\n");goto finish;}
    }
    if(watch) {
        cJSON *p=cJSON_CreateObject();
        cJSON_AddNumberToObject(p,"width",800);cJSON_AddNumberToObject(p,"height",500);
        cJSON_AddNumberToObject(p,"deviceScaleFactor",1);cJSON_AddBoolToObject(p,"mobile",0);
        cJSON *r=web_cdp_call(c,"Emulation.setDeviceMetricsOverride",p);if(!r)goto finish;cJSON_Delete(r);
        r=web_cdp_call(c,"Page.bringToFront",NULL);if(!r)goto finish;cJSON_Delete(r);
        r=web_cdp_eval(c,"(()=>{const p=document.createElement('pre');p.id='viewer';p.setAttribute('aria-hidden','true');p.style='position:absolute;left:280px;top:16px;font:16px monospace;white-space:pre-wrap;width:480px';document.body.append(p);return true})()");
        if(!r)goto finish;cJSON_Delete(r);
    }
    uint32_t words[8192]={0},browser_obs[128]={0},browser_mask[13];
    int successes[3]={0},counts[3]={0};long transitions=0;double start=now(),reset_seconds=0,action_seconds=0,observe_seconds=0,bend_seconds=0;
    for(int ep=0;ep<episodes;ep++) {
        memset(words,0,sizeof words);
        for(int i=0;i<32;i++){words[i*256]=seed+(unsigned)ep;words[i*256+14]=1;}
        webnav_batch(words);
        int task=words[1];counts[task]++;
        if(policy)webnav_policy_reset(policy);
        if(browser) {
            char js[256];snprintf(js,sizeof js,"webnav.reset({task:%u,target:%u,order:%u});webnav.observe()",words[1],words[2],words[3]);
            double t=now();cJSON *r=web_cdp_eval(c,js);reset_seconds+=now()-t;
            int mismatch=compare(r,words,ep,0,-1,browser_obs);cJSON_Delete(r);if(mismatch)goto finish;
        }
        if(watch) {
            char js[512];snprintf(js,sizeof js,"document.getElementById('viewer').textContent='Episode %d/%d | %s\\nInstruction and controls at left.\\nPolicy acts every 350 ms.'",ep+1,episodes,task==0?"Buttons":task==1?"Checkboxes":"Text entry");
            cJSON *r=web_cdp_eval(c,js);if(!r)goto finish;cJSON_Delete(r);usleep(800000);
        }
        unsigned rng=seed+(unsigned)ep+7;
        for(int step=0;step<16&&!words[9];step++) {
            const uint32_t *obs=browser?browser_obs:words+32;
            for(int a=0;a<13;a++)browser_mask[a]=a>=1&&a<=5?obs[32+(a-1)*16]:1;
            for(int a=0;a<13;a++)if(browser_mask[a]!=words[160+a]){fprintf(stderr,"action mask mismatch\n");goto finish;}
            int action=policy?webnav_policy_action(policy,obs,browser_mask):!strcmp(mode,"expert")?expert(obs):!strcmp(mode,"random")?random_u32(&rng)%16:(ep%2?expert(obs):random_u32(&rng)%16);
            if(scripted_count)action=step<scripted_count?scripted[step]:0;
            if(getenv("WEBNAV_TRACE"))fprintf(stderr,"seed=%u step=%d action=%d focus=%u text=%u length=%u\n",words[0],step,action,words[5],words[6],words[7]);
            if(browser) {double t=now();if(web_cdp_action(c,action,obs))goto finish;action_seconds+=now()-t;}
            for(int i=0;i<32;i++){words[i*256+13]=action;words[i*256+14]=0;}
            double t=now();webnav_batch(words);bend_seconds+=now()-t;
            if(browser) {
                t=now();cJSON *r=web_cdp_eval(c,"webnav.tick()");observe_seconds+=now()-t;
                int mismatch=compare(r,words,ep,step+1,action,browser_obs);cJSON_Delete(r);if(mismatch)goto finish;
            }
            if(watch) {
                const char *names[]={"Wait","Click slot 1","Click slot 2","Click slot 3","Click slot 4","Click slot 5 / Submit","Type a","Type b","Type c","Type d","Backspace","Tab","Enter"};
                char js[512];snprintf(js,sizeof js,"document.getElementById('viewer').textContent='Episode %d/%d | %s\\nStep %d/16: %s\\n%s'",ep+1,episodes,task==0?"Buttons":task==1?"Checkboxes":"Text entry",step+1,action<13?names[action]:"Invalid action",words[9]?(words[10]==1?"SUCCESS":"FAILED"):"Running");
                cJSON *r=web_cdp_eval(c,js);if(!r)goto finish;cJSON_Delete(r);usleep(words[9]?1000000:350000);
            }
            transitions++;
        }
        successes[task]+=words[10]==1;
    }
    printf("{\"backend\":\"%s\",\"policy\":\"%s\",\"episodes\":%d,\"transitions\":%ld,\"seconds\":%.6f,\"reset_seconds\":%.6f,\"action_seconds\":%.6f,\"observation_seconds\":%.6f,\"bend_batch_seconds\":%.6f,\"conformance\":\"%s\",\"button\":[%d,%d],\"checkbox\":[%d,%d],\"text\":[%d,%d]}\n",browser?"chromium":"bend-cpu",policy?"checkpoint":mode,episodes,transitions,now()-start,reset_seconds,action_seconds,observe_seconds,bend_seconds,browser?"PASS":"not-applicable",successes[0],counts[0],successes[1],counts[1],successes[2],counts[2]);
    fprintf(stderr,"\nEvaluation: %d/%d successes (%.1f%%)\n",successes[0]+successes[1]+successes[2],episodes,100.0*(successes[0]+successes[1]+successes[2])/episodes);
    const char *tasks[]={"Buttons","Checkboxes","Text entry"};
    for(int i=0;i<3;i++)fprintf(stderr,"  %-12s %d/%d\n",tasks[i],successes[i],counts[i]);
    if(browser)fprintf(stderr,"Browser/Bend observations and outcomes: PASS (%ld actions)\n",transitions);
    status=0;
finish:
    if(browser)web_cdp_close(c);free(c);webnav_policy_free(policy);return status;
}
