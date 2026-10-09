#include "finish.h"
#include "format.h"
#include "../../families/common/family_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void primitive_finish_batch(uint32_t *);
enum { DATA=64, DETAIL=16448, CANDIDATE_DATA=32768, CANDIDATE_DETAIL=49152 };

static int reply_valid(const WFNReply *reply) {
    return !wfn_reply_error(reply);
}
static int read_bytes(const uint32_t *words,size_t length,char *out) {
    for (size_t i=0;i<length;i++) {
        if (words[i]>255) return -1;
        out[i]=(char)words[i];
    }
    out[length]=0;
    return 0;
}
static void write_bytes(uint32_t *words,const char *text,size_t length) {
    for (size_t i=0;i<length;i++) words[i]=(unsigned char)text[i];
}
WF_EXPORT int wfn_validate(const WFNState *s) {
    if (!s) return -1;
    const uint32_t *r=s->words;
    if (r[0]!=WFN_VERSION || r[1]>WFN_EXPIRED || r[8] || r[2]>WFN_RETRIEVE ||
        r[3]>WFN_UNKNOWN_ERROR || r[4]>WFN_JSON_BYTES || r[5]>WFN_DETAIL_BYTES) return -1;
    if (r[1]!=WFN_SUBMITTED) return (r[2]||r[3]||r[4]||r[5])?-1:0;
    char data[WFN_JSON_BYTES+1],detail[WFN_DETAIL_BYTES+1];
    if (read_bytes(r+DATA,r[4],data) || read_bytes(r+DETAIL,r[5],detail)) return -1;
    WFNReply reply={r[2],r[3],data,r[4],detail,r[5]};
    return reply_valid(&reply)?0:-1;
}
WF_EXPORT int wfn_reset(WFNState *s) {
    if (!s) return -1;
    memset(s,0,sizeof *s);
    s->words[8]=3;
    primitive_finish_batch(s->words);
    return wfn_validate(s);
}
static int apply(WFNState *s,unsigned command,const WFNReply *reply) {
    if (wfn_validate(s) || (reply && !reply_valid(reply))) return -1;
    WFNState *next=malloc(sizeof *next);
    if (!next) return -1;
    memcpy(next,s,sizeof *next);
    uint32_t *r=next->words;
    r[8]=command;
    if (reply) {
        r[16]=reply->kind;r[17]=reply->status;
        r[18]=(uint32_t)reply->data_bytes;r[19]=(uint32_t)reply->detail_bytes;
        write_bytes(r+CANDIDATE_DATA,reply->data_json,reply->data_bytes);
        write_bytes(r+CANDIDATE_DETAIL,reply->detail_json,reply->detail_bytes);
    }
    primitive_finish_batch(r);
    int result=wfn_validate(next);
    if (!result) memcpy(s,next,sizeof *s);
    free(next);
    return result;
}
WF_EXPORT int wfn_submit(WFNState *s,const WFNReply *reply) {
    if (!reply) return -1;
    return apply(s,1,reply);
}
WF_EXPORT int wfn_expire(WFNState *s) { return apply(s,2,NULL); }

WF_EXPORT int wfn_json(const WFNState *s,char *out,size_t capacity,size_t *bytes) {
    if (wfn_validate(s) || s->words[1]!=WFN_SUBMITTED || (!out && capacity)) return -1;
    const uint32_t *r=s->words;
    char data[WFN_JSON_BYTES+1],detail[WFN_DETAIL_BYTES+1];
    if(read_bytes(r+DATA,r[4],data)||read_bytes(r+DETAIL,r[5],detail))return -1;
    WFNReply reply={r[2],r[3],data,r[4],detail,r[5]};
    return wfn_format(&reply,out,capacity,bytes);
}
