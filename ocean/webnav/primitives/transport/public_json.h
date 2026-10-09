#ifndef WEBNAV_PUBLIC_JSON_H
#define WEBNAV_PUBLIC_JSON_H
/* Public WF ABI JSON transport shared by native policy and composition RPCs.
 * Call wv_json_valid before cJSON parsing to reject lossy or non-JSON inputs. */
#include <float.h>
#include <math.h>
#include <string.h>
#include "../../../../vendor/cJSON.h"
#include "../../unified/capabilities.h"
#include "json.h"

/* cJSON intentionally accepts some non-JSON numbers and stores strings as C
 * strings. Check RFC JSON grammar, UTF-8, escaped surrogate pairs and NULs
 * before parsing, so neither permissive numbers nor truncation enter the ABI.
 * The public schema only needs four levels; cap arbitrary input at sixteen. */
static int wv_json_valid(const char *line,size_t bytes) {
    return wj_validate(line,bytes,16,WJ_NO_NUL_STRING,NULL);
}

/* Exact, case-sensitive object schemas reject duplicates and private fields. */
static int wv_shape(const cJSON *object,const char *const *keys,size_t count) {
    if(!cJSON_IsObject(object))return 0;
    for(const cJSON *item=object->child;item;item=item->next) {
        size_t k=0;
        if(!item->string)return 0;
        while(k<count&&strcmp(item->string,keys[k]))k++;
        if(k==count)return 0;
        for(const cJSON *previous=object->child;previous!=item;previous=previous->next)
            if(!strcmp(previous->string,item->string))return 0;
    }
    return 1;
}
static const cJSON *wv_get(const cJSON *o,const char *key) {
    return cJSON_GetObjectItemCaseSensitive(o,key);
}
static int wv_u32(const cJSON *o,const char *key,uint32_t *out) {
    const cJSON *item=wv_get(o,key);
    if(!cJSON_IsNumber(item)||!isfinite(item->valuedouble)||item->valuedouble<0||
       item->valuedouble>UINT32_MAX||floor(item->valuedouble)!=item->valuedouble)return 0;
    *out=(uint32_t)item->valuedouble;return 1;
}
static int wv_flag(const cJSON *o,const char *key,uint32_t *out) {
    const cJSON *item=wv_get(o,key);
    if(cJSON_IsBool(item)){*out=cJSON_IsTrue(item)?1u:0u;return 1;}
    return wv_u32(o,key,out)&&*out<=1u;
}
static int wv_float(const cJSON *o,const char *key,float *out) {
    const cJSON *item=wv_get(o,key);
    if(!cJSON_IsNumber(item)||!isfinite(item->valuedouble)||
       item->valuedouble<-FLT_MAX||item->valuedouble>FLT_MAX)return 0;
    *out=(float)item->valuedouble;return isfinite(*out);
}
static int wv_text(const cJSON *o,const char *key,WFView *v,WFText *out) {
    const cJSON *item=wv_get(o,key);
    return cJSON_IsString(item)&&item->valuestring&&
        !wf_text_add(v,item->valuestring,strlen(item->valuestring),out);
}
static int wv_ref(const WFView *v,uint32_t ref) {
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return (int)i;
    return -1;
}
static const char *wv_view(const cJSON *object,WFView *v,WUCapabilities *caps) {
    static const char *const root_keys[]={"version","instruction","elapsed_ms",
        "deadline_ms","omitted","text_truncated","nodes","capabilities","incomplete"};
    static const char *const node_keys[]={"ref","parent","role","flags","name","value",
        "x","y","width","height","selection_start","selection_end","capacity",
        "scroll_x","scroll_y","scroll_max_x","scroll_max_y"};
    static const char *const cap_keys[]={"kind","ref","wire_target","flags","text_capacity",
        "min0","max0","step0","unit0","min1","max1","step1","unit1"};
    memset(v,0,sizeof *v);memset(caps,0,sizeof *caps);caps->version=WU_CAP_VERSION;
    if(!wv_shape(object,root_keys,sizeof root_keys/sizeof *root_keys))return "invalid public view fields";
    if(!wv_u32(object,"version",&v->version)||v->version!=WF_ABI_VERSION||
       !wv_u32(object,"elapsed_ms",&v->elapsed_ms)||
       !wv_u32(object,"deadline_ms",&v->deadline_ms)||!v->deadline_ms||
       v->elapsed_ms>v->deadline_ms||!wv_u32(object,"omitted",&v->omitted)||
       !wv_flag(object,"text_truncated",&v->text_truncated)||
       !wv_flag(object,"incomplete",&caps->incomplete)||
       !wv_text(object,"instruction",v,&v->instruction))return "invalid public view header or text";
    const cJSON *nodes=wv_get(object,"nodes"),*capabilities=wv_get(object,"capabilities");
    if(!cJSON_IsArray(nodes)||!cJSON_IsArray(capabilities)||
       cJSON_GetArraySize(nodes)>(int)WF_MAX_NODES||
       cJSON_GetArraySize(capabilities)>(int)WU_MAX_CAPABILITIES)return "invalid public array size";
    for(const cJSON *item=nodes->child;item;item=item->next) {
        WFNode *n=v->nodes+v->count;
        if(!wv_shape(item,node_keys,sizeof node_keys/sizeof *node_keys)||
           !wv_u32(item,"ref",&n->ref)||!n->ref||wv_ref(v,n->ref)>=0||
           !wv_u32(item,"parent",&n->parent)||!wv_u32(item,"role",&n->role)||n->role>WF_TEXTAREA||
           !wv_u32(item,"flags",&n->flags)||n->flags>255u||
           !wv_text(item,"name",v,&n->name)||!wv_text(item,"value",v,&n->value)||
           !wv_float(item,"x",&n->x)||!wv_float(item,"y",&n->y)||
           !wv_float(item,"width",&n->width)||n->width<0||
           !wv_float(item,"height",&n->height)||n->height<0||
           !wv_u32(item,"selection_start",&n->selection_start)||
           !wv_u32(item,"selection_end",&n->selection_end)||n->selection_start>n->selection_end||
           !wv_u32(item,"capacity",&n->capacity)||
           !wv_float(item,"scroll_x",&n->scroll_x)||!wv_float(item,"scroll_y",&n->scroll_y)||
           !wv_float(item,"scroll_max_x",&n->scroll_max_x)||n->scroll_max_x<0||
           !wv_float(item,"scroll_max_y",&n->scroll_max_y)||n->scroll_max_y<0)
            return "invalid public node or text pool overflow";
        v->count++;
    }
    /* Parent order may be arbitrary, but every parent must resolve and the
     * graph must be a forest rooted at page ref zero. */
    for(unsigned i=0;i<v->count;i++) {
        uint32_t parent=v->nodes[i].parent;
        unsigned visited=0;
        while(parent) {
            int index=wv_ref(v,parent);
            if(index<0||++visited>v->count)return "invalid public parent topology";
            parent=v->nodes[index].parent;
        }
    }
    for(const cJSON *item=capabilities->child;item;item=item->next) {
        WUCapability c={0};
        if(!wv_shape(item,cap_keys,sizeof cap_keys/sizeof *cap_keys)||
           !wv_u32(item,"kind",&c.kind)||c.kind>WF_SELECT_OPTION||
           !wv_u32(item,"ref",&c.ref)||(c.ref&&wv_ref(v,c.ref)<0)||
           !wv_u32(item,"wire_target",&c.wire_target)||
           (c.wire_target&&wv_ref(v,c.wire_target)<0)||
           !wv_u32(item,"flags",&c.flags)||c.flags>7u||
           !wv_u32(item,"text_capacity",&c.text_capacity)||
           !wv_u32(item,"min0",&c.min0)||!wv_u32(item,"max0",&c.max0)||c.min0>c.max0||
           !wv_u32(item,"step0",&c.step0)||!wv_u32(item,"unit0",&c.unit0)||c.unit0>WU_UNIT_TEXT_OFFSET||
           !wv_u32(item,"min1",&c.min1)||!wv_u32(item,"max1",&c.max1)||c.min1>c.max1||
           !wv_u32(item,"step1",&c.step1)||!wv_u32(item,"unit1",&c.unit1)||c.unit1>WU_UNIT_TEXT_OFFSET)
            return "invalid public capability";
        for(unsigned k=0;k<caps->count;k++)
            if(caps->items[k].kind==c.kind&&caps->items[k].ref==c.ref)
                return "duplicate public capability kind/ref";
        /* Same structural constraints as wu_cap_add, without linking family
         * providers. Duplicate pairs are stricter: even equal duplicates fail. */
        caps->items[caps->count++]=c;
    }
    return NULL;
}

/* Serializers take a validated native public snapshot. No private state or
 * expected answer is accepted by this interface. */
static cJSON *wv_encode(const WFView *v,const WUCapabilities *caps) {
    cJSON *out=cJSON_CreateObject(),*nodes=NULL,*items=NULL;
    if(!out)return NULL;
#define WV_NUMBER(o,key,value) do { if(!cJSON_AddNumberToObject(o,key,value))goto failed; } while(0)
#define WV_TEXT(o,key,value) do { const char *str=(value); if(!str||!cJSON_AddStringToObject(o,key,str))goto failed; } while(0)
    WV_NUMBER(out,"version",v->version);WV_NUMBER(out,"elapsed_ms",v->elapsed_ms);
    WV_NUMBER(out,"deadline_ms",v->deadline_ms);WV_NUMBER(out,"omitted",v->omitted);
    WV_NUMBER(out,"text_truncated",v->text_truncated);WV_NUMBER(out,"incomplete",caps->incomplete);
    WV_TEXT(out,"instruction",wf_text_get(v,v->instruction));
    nodes=cJSON_AddArrayToObject(out,"nodes");items=cJSON_AddArrayToObject(out,"capabilities");
    if(!nodes||!items)goto failed;
    for(unsigned i=0;i<v->count;i++) {
        const WFNode *n=v->nodes+i;cJSON *o=cJSON_CreateObject();
        if(!o)goto failed;
        if(!cJSON_AddItemToArray(nodes,o)){cJSON_Delete(o);goto failed;}
#define WV_NODE(key) WV_NUMBER(o,#key,n->key)
        WV_NODE(ref);WV_NODE(parent);WV_NODE(role);WV_NODE(flags);
        WV_TEXT(o,"name",wf_text_get(v,n->name));WV_TEXT(o,"value",wf_text_get(v,n->value));
        WV_NODE(x);WV_NODE(y);WV_NODE(width);WV_NODE(height);
        WV_NODE(selection_start);WV_NODE(selection_end);WV_NODE(capacity);
        WV_NODE(scroll_x);WV_NODE(scroll_y);WV_NODE(scroll_max_x);WV_NODE(scroll_max_y);
#undef WV_NODE
    }
    for(unsigned i=0;i<caps->count;i++) {
        const WUCapability *c=caps->items+i;cJSON *o=cJSON_CreateObject();
        if(!o)goto failed;
        if(!cJSON_AddItemToArray(items,o)){cJSON_Delete(o);goto failed;}
#define WV_CAP(key) WV_NUMBER(o,#key,c->key)
        WV_CAP(kind);WV_CAP(ref);WV_CAP(wire_target);WV_CAP(flags);WV_CAP(text_capacity);
        WV_CAP(min0);WV_CAP(max0);WV_CAP(step0);WV_CAP(unit0);
        WV_CAP(min1);WV_CAP(max1);WV_CAP(step1);WV_CAP(unit1);
#undef WV_CAP
    }
#undef WV_NUMBER
#undef WV_TEXT
    return out;
failed:cJSON_Delete(out);return NULL;
}
static const char *wv_action(const cJSON *object,WFAction *a) {
    static const char *const keys[]={"kind","target","arg0","arg1","text"};
    *a=(WFAction){0};
    const cJSON *text=wv_get(object,"text");
    if(!wv_shape(object,keys,5)||!wv_u32(object,"kind",&a->kind)||a->kind>WF_SELECT_OPTION||
       !wv_u32(object,"target",&a->target)||!wv_u32(object,"arg0",&a->arg0)||
       !wv_u32(object,"arg1",&a->arg1)||!cJSON_IsString(text)||!text->valuestring)
        return "invalid public action";
    a->text=text->valuestring;a->text_length=strlen(a->text);return NULL;
}
#endif
