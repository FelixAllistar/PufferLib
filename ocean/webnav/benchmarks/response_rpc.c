/* Native Bend response controls for a composed browser episode. Each JSON line
 * is exactly one of reset, append:WFView, action:WFAction, blur, expire, result.
 * No task ID, expected answer or grading input is accepted. Final JSON travels
 * as a string so a JavaScript host cannot round integer or exponent tokens. */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/prctl.h>
#include "../primitives/response_form/response_form.h"
#include "../primitives/transport/public_json.h"
#include "../primitives/transport/line.h"
#define RESPONSE_REF_BASE 0xfff00000u
#define LINE_LIMIT (1024u*1024u)

static void fatal(const char *message) { fprintf(stderr,"response_rpc: %s\n",message);exit(2); }
static void send(cJSON *o) {
    if(!o)fatal("response allocation failed");
    char *line=cJSON_PrintUnformatted(o);if(!line)fatal("response serialization failed");
    puts(line);cJSON_free(line);cJSON_Delete(o);
    if(fflush(stdout)||ferror(stdout))fatal("stdout write failed");
}
static void error_reply(const char *message) {
    cJSON *o=cJSON_CreateObject();
    if(!o||!cJSON_AddStringToObject(o,"error",message))fatal("error allocation failed");send(o);
}
static cJSON *state_reply(const WRFState *s) {
    int phase=wrf_phase(s);if(phase<0)fatal("invalid native form state");
    cJSON *o=cJSON_CreateObject();
    if(!o||!cJSON_AddNumberToObject(o,"phase",phase)||
       !cJSON_AddNumberToObject(o,"ref_base",s->ref_base))fatal("state allocation failed");
    if(phase==WFN_SUBMITTED) {
        size_t bytes=0;
        if(wrf_json(s,NULL,0,&bytes))fatal("response sizing failed");
        char *json=malloc(bytes+1u);if(!json)fatal("response allocation failed");
        if(wrf_json(s,json,bytes+1u,NULL)||!cJSON_AddStringToObject(o,"response_json",json))
            fatal("response encoding failed");
        free(json);
    } else if(!cJSON_AddNullToObject(o,"response_json"))fatal("state allocation failed");
    return o;
}
static void suppress_browser_keyboard(WFView *v,WUCapabilities *caps) {
    for(unsigned i=0;i<v->count;i++)v->nodes[i].flags&=~WF_FOCUSED;
    unsigned n=0;
    for(unsigned i=0;i<caps->count;i++) {
        unsigned kind=caps->items[i].kind;
        if((kind>=WF_INSERT&&kind<=WF_TAB_KEY)||
           (kind>=WF_KEY_DOWN&&kind<=WF_PASTE))continue;
        caps->items[n++]=caps->items[i];
    }
    caps->count=n;
}
int main(int argc,char **argv) {
    (void)argv;
    if(argc!=1)fatal("no command-line arguments expected");
    if(prctl(PR_SET_DUMPABLE,0,0,0,0))fatal("cannot disable process dumps");
    WRFState *form=calloc(1,sizeof *form);
    WFView *view=calloc(1,sizeof *view);WUCapabilities *caps=calloc(1,sizeof *caps);
    char *line=malloc(LINE_LIMIT+1u);
    if(!form||!view||!caps||!line)fatal("allocation failed");
    int initialized=0,form_active=0,framing;size_t bytes;
    while((framing=wl_read(stdin,line,LINE_LIMIT,&bytes))) {
        if(framing==-2)fatal("stdin read failed");
        if(framing<0){error_reply("input line exceeds 1 MiB or contains NUL");continue;}
        if(!wv_json_valid(line,bytes)){error_reply("invalid strict JSON or UTF-8");continue;}
        cJSON *request=cJSON_ParseWithLengthOpts(line,bytes+1u,NULL,1),*reply=NULL;
        static const char *const keys[]={"reset","append","action","blur","expire","result"};
        const char *error=NULL;
        if(!wv_shape(request,keys,6)||!request->child||request->child->next)error="expected one response command";
        else {
            const cJSON *item=request->child;const char *name=item->string;
            int toggle=strcmp(name,"append")&&strcmp(name,"action");
            if(toggle&&!cJSON_IsTrue(item))error="response command must be true";
            else if(!strcmp(name,"reset")) {
                if(initialized&&form->ref_base>UINT32_MAX-2u*WRF_REF_STRIDE)error="response reference space exhausted";
                else if(wrf_reset(form,initialized?form->ref_base+WRF_REF_STRIDE:RESPONSE_REF_BASE))error="native reset failed";
                else {initialized=1;form_active=0;reply=state_reply(form);}
            } else if(!initialized)error="reset required";
            else if(!strcmp(name,"append")) {
                error=wv_view(item,view,caps);
                if(!error)for(unsigned i=0;i<view->count;i++)if(view->nodes[i].ref>=RESPONSE_REF_BASE){
                    error="browser reference overlaps reserved host interval";break;
                }
                if(!error) {
                    if(form_active)suppress_browser_keyboard(view,caps);
                    if(wrf_append(form,view,caps))error="insufficient public capacity for response controls";
                    else {
                        reply=state_reply(form);cJSON *json=wv_encode(view,caps);
                        if(!json||!cJSON_AddItemToObject(reply,"view",json))fatal("view serialization failed");
                    }
                }
            } else if(!strcmp(name,"action")) {
                WFAction action;error=wv_action(item,&action);
                if(!error&&wrf_step(form,&action))error="invalid or stale response action";
                if(!error){if(action.kind!=WF_WAIT)form_active=1;reply=state_reply(form);}
            } else if(!strcmp(name,"blur")) {
                if(wrf_blur(form))error="native focus handoff failed";
                else {form_active=0;reply=state_reply(form);}
            } else if(!strcmp(name,"expire")) {
                if(wrf_expire(form))error="native expiry failed";
                else reply=state_reply(form);
            } else reply=state_reply(form);
        }
        cJSON_Delete(request);
        if(error)error_reply(error);else send(reply);
    }
    free(form);free(view);free(caps);free(line);return 0;
}
