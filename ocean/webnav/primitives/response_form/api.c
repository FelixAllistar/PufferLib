#include "response_form.h"
#include "../finish/format.h"
#include "../transport/utf8.h"
#include <stdlib.h>
#include <string.h>
void primitive_response_form_batch(uint32_t *);

enum { FINAL_DATA=64,FINAL_DETAIL=16448,DRAFT_DATA=18496,DRAFT_DETAIL=34880,
       CANDIDATE_DATA=36928,CANDIDATE_DETAIL=53312,INSERT=55360 };
static int bytes_read(const uint32_t *src,size_t n,char *out) {
    for(size_t i=0;i<n;i++){if(src[i]>255)return -1;out[i]=(char)src[i];}
    out[n]=0;return 0;
}
static void bytes_write(uint32_t *out,const char *src,size_t n) {
    for(size_t i=0;i<n;i++)out[i]=(unsigned char)src[i];
}
static int final_reply(const uint32_t *r,char *data,char *detail,WFNReply *reply) {
    if(r[2]>WFN_RETRIEVE||r[3]>WFN_UNKNOWN_ERROR||r[4]>WFN_JSON_BYTES||r[5]>WFN_DETAIL_BYTES||
       bytes_read(r+FINAL_DATA,r[4],data)||bytes_read(r+FINAL_DETAIL,r[5],detail))return -1;
    *reply=(WFNReply){r[2],r[3],data,r[4],detail,r[5]};return wfn_reply_error(reply)?-1:0;
}
WF_EXPORT int wrf_validate(const WRFState *s) {
    if(!s||!s->ref_base||s->ref_base>UINT32_MAX-WRF_REF_STRIDE)return -1;
    const uint32_t *r=s->words;
    if(r[0]!=1||r[1]>WFN_EXPIRED||r[8]||r[20]>WFN_RETRIEVE||r[21]>WFN_UNKNOWN_ERROR||r[22]>2||r[23]>2||
       r[24]>WFN_JSON_BYTES||r[25]>r[26]||r[26]>r[24]||r[27]>WFN_DETAIL_BYTES||r[28]>r[29]||r[29]>r[27]||
       r[30]||r[31]||wt_encode(r+DRAFT_DATA,r[24],NULL,WFN_JSON_BYTES,NULL)||
       wt_encode(r+DRAFT_DETAIL,r[27],NULL,WFN_DETAIL_BYTES,NULL))return -1;
    if(r[1]!=WFN_SUBMITTED)return r[2]||r[3]||r[4]||r[5]?-1:0;
    char data[WFN_JSON_BYTES+1],detail[WFN_DETAIL_BYTES+1];WFNReply reply;
    if(r[20]!=r[2]||r[21]!=r[3]||r[22]||r[23])return -1;
    return final_reply(r,data,detail,&reply);
}
WF_EXPORT int wrf_phase(const WRFState *s) { return wrf_validate(s)?-1:(int)s->words[1]; }
WF_EXPORT int wrf_reset(WRFState *s,uint32_t first_ref) {
    if(!s||!first_ref||first_ref>UINT32_MAX-WRF_REF_STRIDE)return -1;
    WRFState *next=calloc(1,sizeof *next);if(!next)return -1;
    next->ref_base=first_ref;next->words[8]=1;primitive_response_form_batch(next->words);
    int result=wrf_validate(next);if(!result)*s=*next;free(next);return result;
}
static int add_cap(WUCapabilities *c,WUCapability cap) {
    for(unsigned i=0;i<c->count;i++)if(c->items[i].kind==cap.kind&&c->items[i].ref==cap.ref)return 0;
    if(c->count>=WU_MAX_CAPABILITIES)return -1;c->items[c->count++]=cap;return 0;
}
static WFNode *add_node(WFView *v,uint32_t ref,uint32_t parent,unsigned role,const char *name,const char *value,unsigned flags) {
    if(v->count>=WF_MAX_NODES)return NULL;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return NULL;
    WFNode *n=v->nodes+v->count;
    *n=(WFNode){.ref=ref,.parent=parent,.role=role,.flags=flags,.x=900,.y=20+(float)v->count*20,.width=350,.height=20};
    if(wf_text_add(v,name,strlen(name),&n->name)||wf_text_add(v,value,strlen(value),&n->value))return NULL;
    v->count++;return n;
}
static int choice(WFView *v,WUCapabilities *caps,uint32_t *ref,uint32_t parent,const char *name,
                  const char *const *values,unsigned count,unsigned selected) {
    uint32_t id=(*ref)++;
    if(!add_node(v,id,parent,WF_SELECT,name,values[selected],WF_VISIBLE|WF_ENABLED)||
       add_cap(caps,(WUCapability){.kind=WF_SELECT_OPTION,.ref=id,.wire_target=id,.flags=WU_CAP_RANGE,
                                  .max0=count-1,.step0=1,.unit0=WU_UNIT_INDEX}))return -1;
    for(unsigned i=0;i<count;i++)if(!add_node(v,(*ref)++,id,WF_OPTION,values[i],"",
        WF_VISIBLE|WF_ENABLED|(selected==i?WF_SELECTED:0)))return -1;
    return 0;
}
static int field(WFView *v,WUCapabilities *caps,uint32_t id,uint32_t parent,const char *name,
                 const uint32_t *units,unsigned count,unsigned start,unsigned end,unsigned limit,int focused,unsigned *slot) {
    size_t length,begin,finish;
    if(wt_encode(units,count,NULL,limit,&length)||wt_encode(units,start,NULL,limit,&begin)||
       wt_encode(units,end,NULL,limit,&finish))return -1;
    *slot=v->count;
    WFNode *n=add_node(v,id,parent,WF_TEXTAREA,name,"",WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(focused?WF_FOCUSED:0));
    if(!n||add_cap(caps,(WUCapability){.kind=WF_CLICK,.ref=id,.wire_target=id}))return -1;
    n->capacity=limit+1;n->selection_start=(uint32_t)begin;n->selection_end=(uint32_t)finish;
    if(!focused)return 0;
    unsigned room=limit-(unsigned)length+(unsigned)(finish-begin);
    if(room&&add_cap(caps,(WUCapability){.kind=WF_INSERT,.ref=id,.wire_target=id,.flags=WU_CAP_TEXT,
        .text_capacity=room<WRF_INSERT_BYTES?room:WRF_INSERT_BYTES}))return -1;
    for(unsigned kind=WF_BACKSPACE;kind<=WF_SELECT_ALL;kind++)
        if(add_cap(caps,(WUCapability){.kind=kind,.ref=id,.wire_target=id}))return -1;
    return 0;
}
static int append(const WRFState *s,WFView *v,WUCapabilities *caps) {
    const uint32_t *r=s->words;uint32_t ref=s->ref_base,parent=ref++;
    if(!add_node(v,parent,0,WF_PANEL,"Submission","",WF_VISIBLE|WF_ENABLED))return -1;
    if(r[1]!=WFN_OPEN){
        return add_node(v,ref,parent,WF_TEXT,r[1]==WFN_SUBMITTED?"Response submitted":"Budget expired without a response","",WF_VISIBLE|WF_ENABLED)?0:-1;
    }
    if(choice(v,caps,&ref,parent,"Task type",wfn_kind_names,3,r[20])||
       choice(v,caps,&ref,parent,"Outcome status",wfn_status_names,6,r[21]))return -1;
    unsigned data_slot,detail_slot;
    if(field(v,caps,ref++,parent,"Retrieved data (JSON)",r+DRAFT_DATA,r[24],r[25],r[26],WFN_JSON_BYTES,r[22]==1,&data_slot)||
       field(v,caps,ref++,parent,"Error details (JSON)",r+DRAFT_DETAIL,r[27],r[28],r[29],WFN_DETAIL_BYTES,r[22]==2,&detail_slot))return -1;
    uint32_t finish=ref++;
    if(!add_node(v,finish,parent,WF_BUTTON,"Finish","",WF_VISIBLE|WF_ENABLED|WF_CLICKABLE)||
       add_cap(caps,(WUCapability){.kind=WF_CLICK,.ref=finish,.wire_target=finish}))return -1;
    if(r[23]&&!add_node(v,ref++,parent,WF_TEXT,r[23]==1?
        "Retrieved data must be valid JSON: an array or null for retrieval, and null for other task types.":
        "Error details must be a JSON string or null, and null for SUCCESS.","",WF_VISIBLE|WF_ENABLED))return -1;
    /* Allocate fixed labels first, then detail, then the larger data draft.
     * Clipping only affects this bounded observation, never submission bytes. */
    char data[WFN_JSON_BYTES+1],detail[WFN_DETAIL_BYTES+1];size_t data_bytes,detail_bytes;
    if(wt_encode(r+DRAFT_DATA,r[24],data,WFN_JSON_BYTES,&data_bytes)||
       wt_encode(r+DRAFT_DETAIL,r[27],detail,WFN_DETAIL_BYTES,&detail_bytes))return -1;
    unsigned slots[]={detail_slot,data_slot};const char *texts[]={detail,data};size_t lengths[]={detail_bytes,data_bytes};
    for(unsigned i=0;i<2;i++){
        size_t available=WF_TEXT_BYTES-v->text_bytes;
        if(available<2-i)return -1;
        size_t take=wt_prefix(texts[i],lengths[i],available-(2-i));
        if(wf_text_add(v,texts[i],take,&v->nodes[slots[i]].value))return -1;
        if(take<lengths[i]){v->text_truncated=1;caps->incomplete=1;}
    }
    return add_cap(caps,(WUCapability){.kind=WF_WAIT});
}
WF_EXPORT int wrf_append(const WRFState *s,WFView *v,WUCapabilities *caps) {
    if(wrf_validate(s)||!v||!caps||v->version!=WF_ABI_VERSION||v->count>WF_MAX_NODES||
       v->text_bytes>WF_TEXT_BYTES||caps->version!=WU_CAP_VERSION||caps->count>WU_MAX_CAPABILITIES)return -1;
    WFView *next_view=malloc(sizeof *next_view);WUCapabilities *next_caps=malloc(sizeof *next_caps);
    if(!next_view||!next_caps){free(next_view);free(next_caps);return -1;}
    *next_view=*v;*next_caps=*caps;
    int result=append(s,next_view,next_caps);
    if(!result){
        for(unsigned i=v->count;i<next_view->count;i++)next_view->nodes[i].y=20+(float)(i-v->count)*20;
        *v=*next_view;*caps=*next_caps;
    }
    free(next_view);free(next_caps);return result;
}
WF_EXPORT int wrf_observe(const WRFState *s,WFView *v,WUCapabilities *caps) {
    if(!v||!caps)return -1;
    wf_view_init(v,0,0);wf_text_add(v,"",0,&v->instruction);*caps=(WUCapabilities){.version=WU_CAP_VERSION};
    return wrf_append(s,v,caps);
}
static int apply(WRFState *s,WRFState *next) {
    primitive_response_form_batch(next->words);
    next->ref_base+=WRF_REF_STRIDE;
    int result=wrf_validate(next);if(!result)*s=*next;return result;
}
WF_EXPORT int wrf_step(WRFState *s,const WFAction *a) {
    if(wrf_validate(s)||!a||a->arg1||a->kind>WF_SELECT_OPTION||s->ref_base>UINT32_MAX-2*WRF_REF_STRIDE||
       (a->text_length&&!a->text))return -1;
    WFView v;WUCapabilities caps;if(wrf_observe(s,&v,&caps))return -1;
    const WUCapability *cap=NULL;
    for(unsigned i=0;i<caps.count;i++)if(caps.items[i].kind==a->kind&&caps.items[i].ref==a->target){cap=caps.items+i;break;}
    if(!cap||a->arg0<cap->min0||a->arg0>cap->max0||
       (a->kind==WF_INSERT?(!a->text_length||a->text_length>cap->text_capacity):a->text_length!=0))return -1;
    WRFState *next=malloc(sizeof *next);if(!next)return -1;*next=*s;
    uint32_t *r=next->words;unsigned local=a->target?a->target-s->ref_base:0;
    int result=-1;
    if(a->kind==WF_SELECT_OPTION){r[8]=local==1?2:3;r[31]=a->arg0;}
    else if(a->kind==WF_CLICK){
        if(local==12)r[8]=4;
        else if(local==13)r[8]=5;
        else {
            char data[WFN_JSON_BYTES+1],detail[WFN_DETAIL_BYTES+1];size_t data_bytes,detail_bytes;
            if(wt_encode(r+DRAFT_DATA,r[24],data,WFN_JSON_BYTES,&data_bytes)||
               wt_encode(r+DRAFT_DETAIL,r[27],detail,WFN_DETAIL_BYTES,&detail_bytes))goto done;
            WFNReply reply={r[20],r[21],data,data_bytes,detail,detail_bytes};
            unsigned error=(unsigned)wfn_reply_error(&reply);
            if(error){r[8]=15;r[31]=error;}
            else {
                r[8]=14;r[16]=r[20];r[17]=r[21];r[18]=(uint32_t)data_bytes;r[19]=(uint32_t)detail_bytes;
                bytes_write(r+CANDIDATE_DATA,data,data_bytes);bytes_write(r+CANDIDATE_DETAIL,detail,detail_bytes);
            }
        }
    } else if(a->kind>=WF_INSERT&&a->kind<=WF_SELECT_ALL){
        r[8]=a->kind+4;
        if(a->kind==WF_INSERT){
            size_t count;if(wt_decode(a->text,a->text_length,r+INSERT,WRF_INSERT_BYTES,&count))goto done;
            r[30]=(uint32_t)count;
        }
    }else r[8]=0;
    result=apply(s,next);
done:free(next);return result;
}
static int host_event(WRFState *s,unsigned command) {
    if(wrf_validate(s))return -1;
    if(s->words[1]!=WFN_OPEN)return 0;
    if(s->ref_base>UINT32_MAX-2*WRF_REF_STRIDE)return -1;
    WRFState *next=malloc(sizeof *next);if(!next)return -1;*next=*s;next->words[8]=command;
    int result=apply(s,next);free(next);return result;
}
WF_EXPORT int wrf_blur(WRFState *s) { return host_event(s,17); }
WF_EXPORT int wrf_expire(WRFState *s) { return host_event(s,16); }
WF_EXPORT int wrf_json(const WRFState *s,char *out,size_t capacity,size_t *bytes) {
    if(wrf_validate(s)||s->words[1]!=WFN_SUBMITTED)return -1;
    char data[WFN_JSON_BYTES+1],detail[WFN_DETAIL_BYTES+1];WFNReply reply;
    if(final_reply(s->words,data,detail,&reply))return -1;
    return wfn_format(&reply,out,capacity,bytes);
}
