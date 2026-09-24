#define _GNU_SOURCE
#include "cdp.h"
#include "dom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static char *read_script(void){FILE *f=fopen("ocean/webnav/web/dom_snapshot.js","rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1,1);if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);s=NULL;}fclose(f);return s;}
int main(int argc,char **argv){if(argc!=2||(strncmp(argv[1],"https://",8)&&strncmp(argv[1],"http://",7))){fprintf(stderr,"usage: capture_dom https://public-page\n");return 2;}
 WebCdp *c=calloc(1,sizeof *c);WebDom *obs=calloc(1,sizeof *obs);if(!c||!obs)return 1;c->input=c->output=-1;c->pid=-1;int status=1;const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
 if(web_cdp_start_ready(c,chrome,argv[1],"Boolean(location.href!=='about:blank'&&document.body&&document.readyState==='complete')"))goto done;
 cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"width",1280);cJSON_AddNumberToObject(p,"height",800);cJSON_AddNumberToObject(p,"deviceScaleFactor",1);cJSON_AddBoolToObject(p,"mobile",0);cJSON *r=web_cdp_call(c,"Emulation.setDeviceMetricsOverride",p);if(!r)goto done;cJSON_Delete(r);
 char *script=read_script();if(!script)goto done;r=web_cdp_eval(c,script);free(script);if(!r)goto done;cJSON_Delete(r);
 r=web_cdp_eval(c,"({url:location.href,title:document.title,observation:webnavDOM('body','#webnav-no-instruction'),ready:document.readyState})");if(!r)goto done;
 if(web_dom_parse(obs,cJSON_GetObjectItem(r,"observation"))){cJSON_Delete(r);goto done;}
 cJSON_AddStringToObject(r,"requested_url",argv[1]);cJSON_AddNumberToObject(r,"captured_unix",(double)time(NULL));cJSON_AddStringToObject(r,"rights_status","not reviewed; local research snapshot, no redistribution grant");cJSON_AddStringToObject(r,"interaction","read-only initial page load; no trace or behavioral coverage claim");cJSON_AddStringToObject(r,"chrome","153.0.8010.52 default; WEBNAV_CHROME can override");char *json=cJSON_PrintUnformatted(r);puts(json);free(json);cJSON_Delete(r);status=0;
done:web_cdp_close(c);free(c);free(obs);return status;}
