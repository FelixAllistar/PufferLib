#ifndef WEBNAV_CDP_H
#define WEBNAV_CDP_H
#include "cJSON.h"
#include <stdint.h>
#include <sys/types.h>
typedef struct {
    int input,output,id;
    pid_t pid;
    char session[128];
    char pending[1048576];
    size_t used;
} WebCdp;
int web_cdp_start(WebCdp *c,const char *chrome,const char *page);
int web_cdp_start_ready(WebCdp *c,const char *chrome,const char *page,const char *ready_expression);
int web_cdp_click(WebCdp *c,double x,double y);
cJSON *web_cdp_call(WebCdp *c,const char *method,cJSON *params);
cJSON *web_cdp_eval(WebCdp *c,const char *expression);
int web_cdp_action(WebCdp *c,unsigned action,const uint32_t *obs);
void web_cdp_close(WebCdp *c);
#endif
