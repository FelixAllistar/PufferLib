#include "response_form.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
void primitive_response_form_batch(uint32_t *);
static WFView view;
static WUCapabilities caps;
static void observe(const WRFState *s) { assert(!wrf_observe(s,&view,&caps));assert(view.count<=WRF_MAX_NODES); }
static const WFNode *find(unsigned role,const char *name) {
    for(unsigned i=0;i<view.count;i++)if(view.nodes[i].role==role&&!strcmp(wf_text_get(&view,view.nodes[i].name),name))return view.nodes+i;
    return NULL;
}
static int have(unsigned kind,uint32_t ref) {
    for(unsigned i=0;i<caps.count;i++)if(caps.items[i].kind==kind&&caps.items[i].ref==ref)return 1;
    return 0;
}
static void action(WRFState *s,unsigned kind,unsigned role,const char *name,unsigned arg,const char *text) {
    observe(s);const WFNode *n=find(role,name);assert(n&&have(kind,n->ref));
    WFAction a={.kind=kind,.target=n->ref,.arg0=arg,.text=text,.text_length=text?strlen(text):0};
    assert(!wrf_step(s,&a));assert(!wrf_validate(s));observe(s);
}
static void replace(WRFState *s,const char *field,const char *text) {
    action(s,WF_CLICK,WF_TEXTAREA,field,0,NULL);
    action(s,WF_SELECT_ALL,WF_TEXTAREA,field,0,NULL);
    action(s,WF_INSERT,WF_TEXTAREA,field,0,text);
}
static void reject(WRFState *s,WFAction a) {
    WRFState *before=malloc(sizeof *before);assert(before);*before=*s;
    assert(wrf_step(s,&a)<0&&!memcmp(s,before,sizeof *s));free(before);
}
static void result(const WRFState *s,const char *expected) {
    size_t bytes=0;assert(!wrf_json(s,NULL,0,&bytes)&&bytes==strlen(expected));
    char *out=malloc(bytes+1);assert(out);memset(out,0xa5,bytes+1);
    assert(wrf_json(s,out,bytes,NULL)==-2);
    for(size_t i=0;i<bytes+1;i++)assert((unsigned char)out[i]==0xa5);
    assert(!wrf_json(s,out,bytes+1,NULL)&&!strcmp(out,expected));free(out);
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    WRFState *s=malloc(sizeof *s),*before=malloc(sizeof *before);assert(s&&before);
    /* Independently check Bend dirty reset, beyond the zeroed C staging row. */
    memset(s,0xa5,sizeof *s);s->ref_base=1048576;s->words[8]=1;primitive_response_form_batch(s->words);
    assert(!wrf_validate(s));
    const unsigned offsets[]={18496,34880};const unsigned null_units[]={'n','u','l','l'};
    for(unsigned i=0;i<WRF_WORDS;i++){
        unsigned expected=i==0?1:(i==24||i==27)?4:0;
        for(unsigned field=0;field<2;field++)if(i>=offsets[field]&&i<offsets[field]+4)expected=null_units[i-offsets[field]];
        assert(s->words[i]==expected);
    }
    observe(s);assert(wrf_phase(s)==WFN_OPEN&&wrf_json(s,NULL,0,NULL)<0);
    assert(!have(WF_INSERT,find(WF_TEXTAREA,"Retrieved data (JSON)")->ref));
    action(s,WF_SELECT_OPTION,WF_SELECT,"Task type",WFN_RETRIEVE,NULL);
    replace(s,"Retrieved data (JSON)","[\"caf\xc3\xa9\",9007199254740993]");
    uint32_t old_finish=find(WF_BUTTON,"Finish")->ref;
    action(s,WF_CLICK,WF_BUTTON,"Finish",0,NULL);assert(wrf_phase(s)==WFN_SUBMITTED);
    result(s,"{\"task_type\":\"RETRIEVE\",\"status\":\"SUCCESS\",\"retrieved_data\":[\"caf\xc3\xa9\",9007199254740993],\"error_details\":null}");
    *before=*s;assert(!wrf_expire(s)&&!memcmp(s,before,sizeof *s));
    reject(s,(WFAction){.kind=WF_CLICK,.target=old_finish});

    assert(!wrf_reset(s,1048576));replace(s,"Retrieved data (JSON)","[]");
    action(s,WF_CLICK,WF_BUTTON,"Finish",0,NULL);assert(wrf_phase(s)==WFN_OPEN&&s->words[23]==1);
    action(s,WF_SELECT_OPTION,WF_SELECT,"Task type",WFN_RETRIEVE,NULL);assert(!s->words[23]);
    replace(s,"Error details (JSON)","42");action(s,WF_CLICK,WF_BUTTON,"Finish",0,NULL);assert(s->words[23]==2);
    action(s,WF_SELECT_OPTION,WF_SELECT,"Outcome status",WFN_NOT_FOUND,NULL);
    replace(s,"Error details (JSON)","\"The policy reports a missing record.\"");
    action(s,WF_CLICK,WF_BUTTON,"Finish",0,NULL);
    result(s,"{\"task_type\":\"RETRIEVE\",\"status\":\"NOT_FOUND_ERROR\",\"retrieved_data\":[],\"error_details\":\"The policy reports a missing record.\"}");

    static const char *const kinds[]={"NAVIGATE","MUTATE","RETRIEVE"};
    static const char *const statuses[]={"SUCCESS","ACTION_NOT_ALLOWED_ERROR","PERMISSION_DENIED_ERROR","NOT_FOUND_ERROR","DATA_VALIDATION_ERROR","UNKNOWN_ERROR"};
    for(unsigned kind=0;kind<3;kind++)for(unsigned status=0;status<6;status++){
        assert(!wrf_reset(s,1048576));action(s,WF_SELECT_OPTION,WF_SELECT,"Task type",kind,NULL);
        action(s,WF_SELECT_OPTION,WF_SELECT,"Outcome status",status,NULL);
        if(kind==2)replace(s,"Retrieved data (JSON)","[]");
        if(status)replace(s,"Error details (JSON)","\"reported\"");
        action(s,WF_CLICK,WF_BUTTON,"Finish",0,NULL);
        char expected[256];snprintf(expected,sizeof expected,"{\"task_type\":\"%s\",\"status\":\"%s\",\"retrieved_data\":%s,\"error_details\":%s}",
            kinds[kind],statuses[status],kind==2?"[]":"null",status?"\"reported\"":"null");result(s,expected);
    }
    assert(!wrf_reset(s,1048576));replace(s,"Retrieved data (JSON)","\xc3\xa9\xf0\x9f\x98\x80Z");
    action(s,WF_LEFT,WF_TEXTAREA,"Retrieved data (JSON)",0,NULL);
    action(s,WF_BACKSPACE,WF_TEXTAREA,"Retrieved data (JSON)",0,NULL);
    assert(!strcmp(wf_text_get(&view,find(WF_TEXTAREA,"Retrieved data (JSON)")->value),"\xc3\xa9Z"));
    action(s,WF_HOME,WF_TEXTAREA,"Retrieved data (JSON)",0,NULL);
    action(s,WF_DELETE,WF_TEXTAREA,"Retrieved data (JSON)",0,NULL);
    assert(!strcmp(wf_text_get(&view,find(WF_TEXTAREA,"Retrieved data (JSON)")->value),"Z"));
    uint32_t old_data=find(WF_TEXTAREA,"Retrieved data (JSON)")->ref;
    assert(!wrf_blur(s));observe(s);
    const WFNode *blurred=find(WF_TEXTAREA,"Retrieved data (JSON)");
    assert(blurred->ref!=old_data&&!(blurred->flags&WF_FOCUSED)&&!have(WF_INSERT,blurred->ref));
    assert(!strcmp(wf_text_get(&view,blurred->value),"Z")&&blurred->selection_start==0&&blurred->selection_end==0);
    reject(s,(WFAction){.kind=WF_INSERT,.target=old_data,.text="x",.text_length=1});
    action(s,WF_CLICK,WF_TEXTAREA,"Retrieved data (JSON)",0,NULL);
    reject(s,(WFAction){.kind=WF_INSERT,.target=find(WF_TEXTAREA,"Retrieved data (JSON)")->ref,.text="\xed\xa0\x80",.text_length=3});
    reject(s,(WFAction){.kind=WF_SELECT_OPTION,.target=find(WF_SELECT,"Task type")->ref,.arg0=3});
    assert(!wrf_expire(s)&&wrf_phase(s)==WFN_EXPIRED&&wrf_json(s,NULL,0,NULL)<0);
    *before=*s;assert(!wrf_expire(s)&&!memcmp(s,before,sizeof *s));
    assert(!wrf_blur(s)&&!memcmp(s,before,sizeof *s));

    /* Fill the declared UTF-8 byte capacity using normal chunked WF_INSERT. */
    assert(!wrf_reset(s,1048576));action(s,WF_SELECT_OPTION,WF_SELECT,"Task type",WFN_RETRIEVE,NULL);
    replace(s,"Retrieved data (JSON)","[\"");
    char chunk[WRF_INSERT_BYTES+1];memset(chunk,'x',WRF_INSERT_BYTES);chunk[WRF_INSERT_BYTES]=0;
    unsigned left=WFN_JSON_BYTES-4;
    while(left){unsigned n=left<WRF_INSERT_BYTES?left:WRF_INSERT_BYTES;chunk[n]=0;
        action(s,WF_INSERT,WF_TEXTAREA,"Retrieved data (JSON)",0,chunk);left-=n;}
    action(s,WF_INSERT,WF_TEXTAREA,"Retrieved data (JSON)",0,"\"]");
    assert(view.text_truncated&&caps.incomplete&&!have(WF_INSERT,find(WF_TEXTAREA,"Retrieved data (JSON)")->ref));
    reject(s,(WFAction){.kind=WF_INSERT,.target=find(WF_TEXTAREA,"Retrieved data (JSON)")->ref,.text="x",.text_length=1});
    action(s,WF_CLICK,WF_BUTTON,"Finish",0,NULL);
    size_t bytes=0;assert(!wrf_json(s,NULL,0,&bytes)&&bytes>WFN_JSON_BYTES);
    char *large=malloc(bytes+1);assert(large);assert(!wrf_json(s,large,bytes+1,NULL));
    char *payload=strstr(large,"[\"");assert(payload);
    for(unsigned i=0;i<WFN_JSON_BYTES-4;i++)assert(payload[i+2]=='x');assert(payload[WFN_JSON_BYTES-2]=='"'&&payload[WFN_JSON_BYTES-1]==']');free(large);

    /* Appending preserves existing public nodes/caps and rejects collisions. */
    assert(!wrf_reset(s,1048576));wf_view_init(&view,123,1000);wf_text_add(&view,"Host instruction",16,&view.instruction);
    caps=(WUCapabilities){.version=WU_CAP_VERSION,.count=1,.items={{.kind=WF_WAIT}}};
    assert(!wrf_append(s,&view,&caps));assert(view.elapsed_ms==123&&view.deadline_ms==1000);
    WFView saved=view;WUCapabilities saved_caps=caps;
    assert(wrf_append(s,&view,&caps)<0&&!memcmp(&view,&saved,sizeof view)&&!memcmp(&caps,&saved_caps,sizeof caps));
    free(s);free(before);
    puts("PASS: response form public actions, 18 outcome combinations, Unicode editing, exact JSON, correction, expiry, atomic rejection, composition and 16 KiB capacity");
    return 0;
}
