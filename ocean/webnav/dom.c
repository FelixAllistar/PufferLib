#include "dom.h"
#include <math.h>
#include <string.h>
static int number(const cJSON *o,const char *k,uint32_t *out){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>UINT32_MAX||floor(v->valuedouble)!=v->valuedouble)return -1;*out=(uint32_t)v->valuedouble;return 0;}
static int string(WebDom *o,const cJSON *j,const char *key,WebText *out){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);if(!cJSON_IsString(v))return -1;size_t n=strlen(v->valuestring);if(n+1>WEBNAV_DOM_TEXT-o->text_used)return -1;out->offset=o->text_used;out->length=(uint32_t)n;memcpy(o->text+o->text_used,v->valuestring,n+1);o->text_used+=(uint32_t)n+1;return 0;}
const char *web_dom_text(const WebDom *o,WebText s){if(s.offset>=o->text_used||s.length>=o->text_used-s.offset||o->text[s.offset+s.length])return NULL;return o->text+s.offset;}
int web_dom_parse(WebDom *o,const cJSON *j){
    memset(o,0,sizeof *o);
    if(number(j,"version",&o->version)||o->version!=WEBNAV_DOM_VERSION||number(j,"omitted",&o->omitted)||number(j,"truncated",&o->truncated)||string(o,j,"instruction",&o->instruction))return -1;
    if(cJSON_HasObjectItem(j,"quality")&&(number(j,"quality",&o->quality)||o->quality>3))return -1;
    const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");if(!cJSON_IsArray(nodes))return -1;
    int count=cJSON_GetArraySize(nodes);if(count>WEBNAV_DOM_NODES)return -1;o->count=(uint32_t)count;
    for(int i=0;i<count;i++){
        const cJSON *v=cJSON_GetArrayItem(nodes,i);WebDomNode *n=o->nodes+i;
        if(number(v,"ref",&n->ref)||!n->ref||number(v,"parent",&n->parent)||number(v,"role",&n->role)||n->role>WEB_ROLE_OPTION||number(v,"flags",&n->flags)||n->flags>31||number(v,"name_source",&n->name_source)||n->name_source>WEB_NAME_FALLBACK||string(o,v,"name",&n->name)||string(o,v,"value",&n->value)||string(o,v,"text",&n->text))return -1;
        for(int k=0;k<i;k++)if(o->nodes[k].ref==n->ref)return -1;
        const cJSON *b=cJSON_GetObjectItemCaseSensitive(v,"bounds");if(!cJSON_IsArray(b)||cJSON_GetArraySize(b)!=4)return -1;
        float values[4];for(int k=0;k<4;k++){const cJSON *x=cJSON_GetArrayItem(b,k);if(!cJSON_IsNumber(x)||!isfinite(x->valuedouble)||fabs(x->valuedouble)>1e7)return -1;values[k]=(float)x->valuedouble;}
        n->x=values[0];n->y=values[1];n->width=values[2];n->height=values[3];if(n->width<0||n->height<0)return -1;
    }
    for(int i=0;i<count;i++)if(o->nodes[i].parent){int found=0;for(int k=0;k<i;k++)found|=o->nodes[k].ref==o->nodes[i].parent;if(!found)return -1;}
    return 0;
}
