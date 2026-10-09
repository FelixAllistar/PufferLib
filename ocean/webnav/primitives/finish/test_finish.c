#include "finish.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

void primitive_finish_batch(uint32_t *);
static WFNReply reply(unsigned kind,unsigned status,const char *data,const char *detail) {
    return (WFNReply){kind,status,data,strlen(data),detail,strlen(detail)};
}
static void result(const WFNState *s,const char *expected) {
    char output[WFN_JSON_BYTES+WFN_DETAIL_BYTES+256];
    size_t bytes=0;
    assert(!wfn_json(s,NULL,0,&bytes));assert(bytes==strlen(expected));
    memset(output,0xa5,sizeof output);
    assert(wfn_json(s,output,bytes,&bytes)==-2);
    for (size_t i=0;i<sizeof output;i++) assert((unsigned char)output[i]==0xa5);
    assert(!wfn_json(s,output,sizeof output,&bytes));assert(!strcmp(output,expected));
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    WFNState *s=malloc(sizeof *s),*before=malloc(sizeof *before);
    assert(s && before);
    /* Independently check dirty-row Bend reset, including staging/padding. */
    memset(s,0xa5,sizeof *s);s->words[8]=3;primitive_finish_batch(s->words);
    assert(s->words[0]==WFN_VERSION);
    for (unsigned i=1;i<WFN_WORDS;i++) assert(!s->words[i]);
    assert(!wfn_validate(s));
    size_t length=999;char sentinel[]="untouched";
    assert(wfn_json(s,sentinel,sizeof sentinel,&length)<0);
    assert(!strcmp(sentinel,"untouched") && length==999);

    WFNReply navigate=reply(WFN_NAVIGATE,WFN_SUCCESS,"null","null");
    assert(!wfn_submit(s,&navigate));
    result(s,"{\"task_type\":\"NAVIGATE\",\"status\":\"SUCCESS\",\"retrieved_data\":null,\"error_details\":null}");
    memcpy(before,s,sizeof *s);
    WFNReply replacement=reply(WFN_RETRIEVE,WFN_SUCCESS,"[42]","null");
    assert(!wfn_submit(s,&replacement));assert(!memcmp(before,s,sizeof *s));
    assert(!wfn_expire(s));assert(!memcmp(before,s,sizeof *s));

    assert(!wfn_reset(s));assert(!wfn_expire(s));memcpy(before,s,sizeof *s);
    assert(s->words[1]==WFN_EXPIRED && wfn_json(s,NULL,0,NULL)<0);
    assert(!wfn_submit(s,&navigate));assert(!memcmp(before,s,sizeof *s));

    const char *const statuses[]={"SUCCESS","ACTION_NOT_ALLOWED_ERROR","PERMISSION_DENIED_ERROR",
        "NOT_FOUND_ERROR","DATA_VALIDATION_ERROR","UNKNOWN_ERROR"};
    const char *const kinds[]={"NAVIGATE","MUTATE","RETRIEVE"};
    for (unsigned kind=0;kind<3;kind++) for (unsigned status=0;status<6;status++) {
        const char *data=kind==WFN_RETRIEVE?"[]":"null";
        const char *detail=status?"\"reported by the policy\"":"null";
        WFNReply input=reply(kind,status,data,detail);
        assert(!wfn_reset(s));assert(!wfn_submit(s,&input));
        char expected[256];
        snprintf(expected,sizeof expected,
            "{\"task_type\":\"%s\",\"status\":\"%s\",\"retrieved_data\":%s,\"error_details\":%s}",
            kinds[kind],statuses[status],data,detail);
        result(s,expected);
    }
    const char *const values[]={"null","[]","[true,false]","[9007199254740993,-12.50,1e-3]",
        "[\"caf\xc3\xa9\",\"\xf0\x9f\x98\x80\",\"\\u0000\"]",
        "[{\"name\":\"first\",\"amount\":12.75,\"tags\":[\"a\",\"b\"]}]"};
    for (unsigned i=0;i<sizeof values/sizeof *values;i++) {
        WFNReply input=reply(WFN_RETRIEVE,WFN_SUCCESS,values[i],"null");
        assert(!wfn_reset(s));assert(!wfn_submit(s,&input));
        char expected[512];snprintf(expected,sizeof expected,
            "{\"task_type\":\"RETRIEVE\",\"status\":\"SUCCESS\",\"retrieved_data\":%s,\"error_details\":null}",values[i]);
        result(s,expected);
    }
    const char *const invalid[]={"", "[01]", "[NaN]", "[1,]", "[] trailing", "[[\"alternative\"]]",
        "[\"\\ud800\"]", "[\"\xc0\x80\"]", "{\"wrong\":\"root\"}"};
    assert(!wfn_reset(s));memcpy(before,s,sizeof *s);
    for (unsigned i=0;i<sizeof invalid/sizeof *invalid;i++) {
        WFNReply input=reply(WFN_RETRIEVE,WFN_SUCCESS,invalid[i],"null");
        assert(wfn_submit(s,&input)<0);assert(!memcmp(before,s,sizeof *s));
    }
    WFNReply wrong=reply(WFN_NAVIGATE,WFN_SUCCESS,"[]","null");
    assert(wfn_submit(s,&wrong)<0);assert(!memcmp(before,s,sizeof *s));
    wrong=reply(WFN_RETRIEVE,WFN_SUCCESS,"[]","\"error\"");
    assert(wfn_submit(s,&wrong)<0);assert(!memcmp(before,s,sizeof *s));
    wrong=reply(3,0,"null","null");assert(wfn_submit(s,&wrong)<0);
    wrong=reply(0,6,"null","null");assert(wfn_submit(s,&wrong)<0);
    wrong=reply(0,1,"null","42");assert(wfn_submit(s,&wrong)<0);
    assert(!memcmp(before,s,sizeof *s));

    char *large=malloc(WFN_JSON_BYTES+2),*expected=malloc(WFN_JSON_BYTES+256);
    assert(large && expected);
    memset(large,'x',WFN_JSON_BYTES);large[0]='[';large[1]='"';
    large[WFN_JSON_BYTES-2]='"';large[WFN_JSON_BYTES-1]=']';large[WFN_JSON_BYTES]=0;
    WFNReply full=reply(WFN_RETRIEVE,WFN_SUCCESS,large,"null");
    assert(!wfn_submit(s,&full));
    snprintf(expected,WFN_JSON_BYTES+256,
        "{\"task_type\":\"RETRIEVE\",\"status\":\"SUCCESS\",\"retrieved_data\":%s,\"error_details\":null}",large);
    result(s,expected);memcpy(before,s,sizeof *s);
    full.data_bytes++;assert(wfn_submit(s,&full)<0);assert(!memcmp(before,s,sizeof *s));
    free(large);free(expected);free(s);free(before);
    puts("PASS: explicit finish, all type/status combinations, strict JSON/Unicode, exact numeric payload, 16 KiB boundary, expiry and atomic rejection");
    return 0;
}
