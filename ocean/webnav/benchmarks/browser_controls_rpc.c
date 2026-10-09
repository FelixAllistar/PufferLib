/* Public browser chrome projection and action binding. The host supplies actual
 * public tab/history facts; this service never simulates or grades the website.
 * Native and browser hosts call the same wbc_controls_* implementation. */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/prctl.h>
#include "../primitives/browser_contexts/public.h"
#include "../primitives/transport/public_json.h"
#include "../primitives/transport/line.h"
#define CONTROL_REF_BASE 0xffe10000u
#define CONTROL_REF_END 0xfff00000u
#define LINE_LIMIT (1024u*1024u)

static void fatal(const char *message) {fprintf(stderr,"browser_controls_rpc: %s\n",message);exit(2);}
static void send(cJSON *value) {
    if (!value) fatal("response allocation failed");
    char *line=cJSON_PrintUnformatted(value);if (!line) fatal("response serialization failed");
    puts(line);cJSON_free(line);cJSON_Delete(value);
    if (fflush(stdout) || ferror(stdout)) fatal("stdout write failed");
}
static void error_reply(const char *message) {
    cJSON *value=cJSON_CreateObject();
    if (!value || !cJSON_AddStringToObject(value,"error",message)) fatal("error allocation failed");
    send(value);
}
static cJSON *state_reply(uint32_t base,int ready) {
    cJSON *value=cJSON_CreateObject();
    if (!value || !cJSON_AddNumberToObject(value,"ref_base",base) ||
        !cJSON_AddBoolToObject(value,"ready",ready)) fatal("state allocation failed");
    return value;
}
static const char *navigation(const cJSON *object,WBCView *out,const char **titles,const char **address) {
    static const char *const keys[]={"active","can_open","can_close","address","tabs"};
    static const char *const tab_keys[]={"identity","phase","can_back","can_forward","title"};
    const cJSON *tabs=wv_get(object,"tabs"),*url=wv_get(object,"address");
    if (!wv_shape(object,keys,5) || !wv_u32(object,"active",&out->active) ||
        !wv_flag(object,"can_open",&out->can_open) || !wv_flag(object,"can_close",&out->can_close) ||
        !cJSON_IsString(url) || !url->valuestring || !cJSON_IsArray(tabs) ||
        cJSON_GetArraySize(tabs)<1 || cJSON_GetArraySize(tabs)>WBC_MAX_TABS) return "invalid public navigation";
    *address=url->valuestring;
    for (const cJSON *item=tabs->child;item;item=item->next) {
        WBCTabView *tab=out->tabs+out->count;const cJSON *title=wv_get(item,"title");
        if (!wv_shape(item,tab_keys,5) || !wv_u32(item,"identity",&tab->identity) ||
            !wv_u32(item,"phase",&tab->browser.phase) ||
            !wv_flag(item,"can_back",&tab->browser.can_back) || !wv_flag(item,"can_forward",&tab->browser.can_forward) ||
            !cJSON_IsString(title) || !title->valuestring) return "invalid public tab";
        titles[out->count++]=title->valuestring;
    }
    return NULL;
}
int main(int argc,char **argv) {
    (void)argv;
    if (argc!=1) fatal("no command-line arguments expected");
    if (prctl(PR_SET_DUMPABLE,0,0,0,0)) fatal("cannot disable process dumps");
    WFView *view=calloc(1,sizeof *view);WUCapabilities *caps=calloc(1,sizeof *caps);
    char *line=malloc(LINE_LIMIT+1u);
    if (!view || !caps || !line) fatal("allocation failed");
    WBCView current={0};uint32_t next_base=CONTROL_REF_BASE,base=0;
    int initialized=0,ready=0,framing;size_t bytes;
    while ((framing=wl_read(stdin,line,LINE_LIMIT,&bytes))) {
        if (framing==-2) fatal("stdin read failed");
        if (framing<0) {error_reply("input line exceeds 1 MiB or contains NUL");continue;}
        if (!wv_json_valid(line,bytes)) {error_reply("invalid strict JSON or UTF-8");continue;}
        cJSON *request=cJSON_ParseWithLengthOpts(line,bytes+1u,NULL,1),*reply=NULL;
        static const char *const keys[]={"reset","append","action","result"};
        const char *error=NULL;
        if (!wv_shape(request,keys,4) || !request->child || request->child->next) error="expected one browser-control command";
        else {
            const cJSON *item=request->child;const char *name=item->string;
            if ((!strcmp(name,"reset") || !strcmp(name,"result")) && !cJSON_IsTrue(item)) error="browser-control command must be true";
            else if (!strcmp(name,"reset")) {
                initialized=1;ready=0;base=0;current=(WBCView){0};reply=state_reply(base,ready);
            } else if (!initialized) error="reset required";
            else if (!strcmp(name,"append")) {
                static const char *const append_keys[]={"navigation","view"};
                WBCView candidate={0};const char *titles[WBC_MAX_TABS],*address=NULL;
                if (!wv_shape(item,append_keys,2)) error="invalid composition fields";
                else error=navigation(wv_get(item,"navigation"),&candidate,titles,&address);
                if (!error) error=wv_view(wv_get(item,"view"),view,caps);
                if (!error) for (unsigned i=0;i<view->count;i++) if (
                    view->nodes[i].ref>=CONTROL_REF_BASE && view->nodes[i].ref<CONTROL_REF_END) {
                    error="public view overlaps reserved browser-control interval";break;
                }
                if (!error && next_base>CONTROL_REF_END-WBC_REF_STRIDE) error="browser-control reference space exhausted";
                if (!error && wbc_controls_append(&candidate,titles,address,next_base,view,caps))
                    error="invalid navigation or insufficient public capacity for browser controls";
                if (!error) {
                    cJSON *encoded=wv_encode(view,caps);
                    if (!encoded) fatal("public view allocation failed");
                    reply=state_reply(next_base,1);
                    if (!cJSON_AddItemToObject(reply,"view",encoded)) fatal("public view allocation failed");
                    current=candidate;base=next_base;next_base+=WBC_REF_STRIDE;ready=1;
                }
            } else if (!strcmp(name,"action")) {
                WFAction action;WBCCommand command;
                if (!ready) error="observe browser controls before acting";
                else error=wv_action(item,&action);
                if (!error && wbc_controls_action(&current,base,&action,&command)) error="invalid, disabled or stale browser-control action";
                if (!error) {
                    reply=cJSON_CreateObject();
                    if (!reply || !cJSON_AddNumberToObject(reply,"command",command.kind) ||
                        !cJSON_AddNumberToObject(reply,"argument",command.argument) ||
                        !cJSON_AddNumberToObject(reply,"foreground",command.foreground)) fatal("command allocation failed");
                    ready=0;
                }
            } else reply=state_reply(base,ready);
        }
        cJSON_Delete(request);
        if (error) error_reply(error);else send(reply);
    }
    free(view);free(caps);free(line);return 0;
}
