#define _GNU_SOURCE
#include <stdint.h>
#include "cdp.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int send_all(int fd,const char *s,size_t n) {
    while(n){ssize_t k=write(fd,s,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;s+=k;n-=k;}return 0;
}
cJSON *web_cdp_call(WebCdp *c,const char *method,cJSON *params) {
    int id=++c->id;
    cJSON *req=cJSON_CreateObject();
    cJSON_AddNumberToObject(req,"id",id);cJSON_AddStringToObject(req,"method",method);
    if(c->session[0])cJSON_AddStringToObject(req,"sessionId",c->session);
    if(params)cJSON_AddItemToObject(req,"params",params);
    char *s=cJSON_PrintUnformatted(req);
    int error=send_all(c->input,s,strlen(s)+1);free(s);cJSON_Delete(req);
    if(error)return NULL;
    double deadline=now()+15;
    for(;;) {
        char *end=memchr(c->pending,0,c->used);
        if(end) {
            size_t len=end-c->pending;
            cJSON *r=cJSON_ParseWithLength(c->pending,len+1);
            memmove(c->pending,end+1,c->used-len-1);c->used-=len+1;
            cJSON *got=cJSON_GetObjectItemCaseSensitive(r,"id");
            if(cJSON_IsNumber(got)&&got->valueint==id) {
                if(cJSON_HasObjectItem(r,"error")) {char *e=cJSON_PrintUnformatted(r);fprintf(stderr,"CDP %s: %s\n",method,e);free(e);cJSON_Delete(r);return NULL;}
                cJSON *result=cJSON_DetachItemFromObject(r,"result");cJSON_Delete(r);return result;
            }
            cJSON_Delete(r);continue;
        }
        int ms=(int)((deadline-now())*1000);
        if(ms<=0||c->used==sizeof c->pending){fprintf(stderr,"CDP timeout/overflow: %s\n",method);return NULL;}
        struct pollfd p={c->output,POLLIN,0};
        int ready=poll(&p,1,ms);if(ready<0&&errno==EINTR)continue;if(ready<=0)return NULL;
        ssize_t n=read(c->output,c->pending+c->used,sizeof c->pending-c->used);
        if(n<=0)return NULL;c->used+=n;
    }
}
cJSON *web_cdp_eval(WebCdp *c,const char *expression) {
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"expression",expression);
    cJSON_AddBoolToObject(p,"returnByValue",1);cJSON_AddBoolToObject(p,"awaitPromise",1);
    cJSON *r=web_cdp_call(c,"Runtime.evaluate",p);if(!r)return NULL;
    if(cJSON_HasObjectItem(r,"exceptionDetails")){char *s=cJSON_PrintUnformatted(r);fprintf(stderr,"JS: %s\n",s);free(s);cJSON_Delete(r);return NULL;}
    cJSON *obj=cJSON_GetObjectItemCaseSensitive(r,"result");
    cJSON *v=cJSON_DetachItemFromObject(obj,"value");cJSON_Delete(r);return v;
}
static int discard(WebCdp *c,const char *method,cJSON *p){cJSON *r=web_cdp_call(c,method,p);if(!r)return -1;cJSON_Delete(r);return 0;}
int web_cdp_start_ready(WebCdp *c,const char *chrome,const char *page,const char *ready_expression) {
    memset(c,0,sizeof *c);c->input=c->output=-1;c->pid=-1;
    int a[2],b[2];if(pipe(a)||pipe(b))return -1;
    int in=fcntl(a[0],F_DUPFD_CLOEXEC,10),out=fcntl(b[1],F_DUPFD_CLOEXEC,10);
    char profile[]="/tmp/webnav-chrome-XXXXXX";if(!mkdtemp(profile))return -1;
    char flag[256];snprintf(flag,sizeof flag,"--user-data-dir=%s",profile);
    c->pid=fork();
    if(c->pid==0) {
        close(a[0]);close(a[1]);close(b[0]);close(b[1]);
        dup2(in,3);dup2(out,4);close(in);close(out);
        int log=open("build/webnav/chromium.log",O_WRONLY|O_CREAT|O_APPEND,0600);
        if(log>=0){dup2(log,2);if(log!=2)close(log);}
        execl(chrome,chrome,"--remote-debugging-pipe","--no-sandbox","--disable-gpu","--disable-dev-shm-usage","--no-first-run",flag,"about:blank",(char*)NULL);
        _exit(127);
    }
    close(in);close(out);close(a[0]);close(b[1]);c->input=a[1];c->output=b[0];
    signal(SIGPIPE,SIG_IGN);
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"url",page);
    cJSON *r=web_cdp_call(c,"Target.createTarget",p);if(!r)return -1;
    cJSON *target=cJSON_GetObjectItemCaseSensitive(r,"targetId");if(!cJSON_IsString(target)){cJSON_Delete(r);return -1;}
    p=cJSON_CreateObject();cJSON_AddStringToObject(p,"targetId",target->valuestring);cJSON_AddBoolToObject(p,"flatten",1);cJSON_Delete(r);
    r=web_cdp_call(c,"Target.attachToTarget",p);if(!r)return -1;
    cJSON *session=cJSON_GetObjectItemCaseSensitive(r,"sessionId");if(!cJSON_IsString(session)){cJSON_Delete(r);return -1;}
    snprintf(c->session,sizeof c->session,"%s",session->valuestring);cJSON_Delete(r);
    p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"width",256);cJSON_AddNumberToObject(p,"height",256);cJSON_AddNumberToObject(p,"deviceScaleFactor",1);cJSON_AddBoolToObject(p,"mobile",0);
    if(discard(c,"Emulation.setDeviceMetricsOverride",p))return -1;
    double deadline=now()+15;
    do {r=web_cdp_eval(c,ready_expression);int ready=cJSON_IsTrue(r);cJSON_Delete(r);if(ready)return 0;usleep(1000);}while(now()<deadline);
    return -1;
}
int web_cdp_start(WebCdp *c,const char *chrome,const char *page) {
    return web_cdp_start_ready(c,chrome,page,"Boolean(window.webnav)");
}
int web_cdp_click(WebCdp *c,double x,double y) {
    for(int up=0;up<2;up++) {
        cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",up?"mouseReleased":"mousePressed");
        cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);
        cJSON_AddStringToObject(p,"button","left");cJSON_AddNumberToObject(p,"clickCount",1);
        if(discard(c,"Input.dispatchMouseEvent",p))return -1;
    }
    return 0;
}
int web_cdp_action(WebCdp *c,unsigned action,const uint32_t *obs) {
    if(action>=1&&action<=5) {
        const uint32_t *n=obs+32+(action-1)*16;if(!n[0])return 0;
        for(int up=0;up<2;up++) {
            cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",up?"mouseReleased":"mousePressed");
            cJSON_AddNumberToObject(p,"x",n[3]+n[5]/2.0);cJSON_AddNumberToObject(p,"y",n[4]+n[6]/2.0);
            cJSON_AddStringToObject(p,"button","left");cJSON_AddNumberToObject(p,"clickCount",1);
            if(discard(c,"Input.dispatchMouseEvent",p))return -1;
        }
    } else if(action>=6&&action<=12) {
        char letter[2]={(char)('a'+action-6),0};
        const char *key=action<10?letter:action==10?"Backspace":action==11?"Tab":"Enter";
        int code=action<10?'A'+action-6:action==10?8:action==11?9:13;
        for(int up=0;up<2;up++) {
            cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",up?"keyUp":"keyDown");
            cJSON_AddStringToObject(p,"key",key);cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
            if(!up&&action<10)cJSON_AddStringToObject(p,"text",letter);
            if(!up&&action==12)cJSON_AddStringToObject(p,"text","\r");
            if(discard(c,"Input.dispatchKeyEvent",p))return -1;
        }
    }
    return 0;
}
void web_cdp_close(WebCdp *c) {
    if(c->input>=0)close(c->input);if(c->output>=0)close(c->output);
    if(c->pid>0){kill(c->pid,SIGTERM);for(int i=0;i<100;i++){if(waitpid(c->pid,NULL,WNOHANG)==c->pid){c->pid=-1;return;}usleep(10000);}kill(c->pid,SIGKILL);waitpid(c->pid,NULL,0);c->pid=-1;}
}
