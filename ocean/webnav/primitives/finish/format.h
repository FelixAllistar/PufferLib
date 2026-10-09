#ifndef WEBNAV_FINISH_FORMAT_H
#define WEBNAV_FINISH_FORMAT_H
#include "finish.h"
#include "../transport/json.h"
#include <stdio.h>
#include <string.h>

static const char *const wfn_kind_names[]={"NAVIGATE","MUTATE","RETRIEVE"};
static const char *const wfn_status_names[]={"SUCCESS","ACTION_NOT_ALLOWED_ERROR",
    "PERMISSION_DENIED_ERROR","NOT_FOUND_ERROR","DATA_VALIDATION_ERROR","UNKNOWN_ERROR"};
/* Zero is valid; 1 identifies the data/type field, 2 the detail/status field.
 * This checks public transport format, never whether the claimed outcome is true. */
static inline int wfn_reply_error(const WFNReply *reply) {
    WJInfo data,detail;
    if(!reply||reply->kind>WFN_RETRIEVE||reply->data_bytes>WFN_JSON_BYTES||
       !wj_validate(reply->data_json,reply->data_bytes,64,0,&data))return 1;
    if(data.type!=WJ_NULL&&(reply->kind!=WFN_RETRIEVE||data.type!=WJ_ARRAY||
       (data.array_item_types&(1u<<WJ_ARRAY))))return 1;
    if(reply->status>WFN_UNKNOWN_ERROR||reply->detail_bytes>WFN_DETAIL_BYTES||
       !wj_validate(reply->detail_json,reply->detail_bytes,64,0,&detail))return 2;
    if(detail.type!=WJ_NULL&&(reply->status==WFN_SUCCESS||detail.type!=WJ_STRING))return 2;
    return 0;
}
static inline int wfn_format(const WFNReply *reply,char *out,size_t capacity,size_t *bytes) {
    static const char separator[]=",\"error_details\":";
    if(wfn_reply_error(reply)||(!out&&capacity))return -1;
    char prefix[160];
    int n=snprintf(prefix,sizeof prefix,"{\"task_type\":\"%s\",\"status\":\"%s\",\"retrieved_data\":",
        wfn_kind_names[reply->kind],wfn_status_names[reply->status]);
    if(n<0||(size_t)n>=sizeof prefix)return -1;
    size_t length=(size_t)n+reply->data_bytes+sizeof separator-1+reply->detail_bytes+1;
    if(bytes)*bytes=length;
    if(!out)return 0;
    if(capacity<=length)return -2;
    size_t at=(size_t)n;memcpy(out,prefix,at);
    memcpy(out+at,reply->data_json,reply->data_bytes);at+=reply->data_bytes;
    memcpy(out+at,separator,sizeof separator-1);at+=sizeof separator-1;
    memcpy(out+at,reply->detail_json,reply->detail_bytes);at+=reply->detail_bytes;
    out[at++]='}';out[at]=0;return 0;
}
#endif
