#ifndef WEBNAV_DOM_H
#define WEBNAV_DOM_H
#include <stdint.h>
#include "cJSON.h"
#define WEBNAV_DOM_VERSION 2
#define WEBNAV_DOM_NODES 128
#define WEBNAV_DOM_TEXT 32768
/* Public observations only. Goal/reward/task-private state has no field here. */
typedef struct { uint32_t offset,length; } WebText;
typedef struct {
    uint32_t ref,parent,role,flags,name_source;
    float x,y,width,height;
    WebText name,value,text;
} WebDomNode;
typedef struct {
    uint32_t version,count,omitted,text_used,truncated,quality;
    WebText instruction;
    WebDomNode nodes[WEBNAV_DOM_NODES];
    char text[WEBNAV_DOM_TEXT];
} WebDom;
enum { WEB_ROLE_OTHER,WEB_ROLE_BUTTON,WEB_ROLE_CHECKBOX,WEB_ROLE_TEXTBOX,
    WEB_ROLE_LINK,WEB_ROLE_SELECT,WEB_ROLE_RADIO,WEB_ROLE_OPTION };
enum { WEB_VISIBLE=1,WEB_ENABLED=2,WEB_CHECKED=4,WEB_FOCUSED=8,WEB_ACTIONABLE=16 };
enum { WEB_NAME_NONE,WEB_NAME_ARIA,WEB_NAME_LABEL,WEB_NAME_TEXT,WEB_NAME_FALLBACK };
typedef enum { WEB_ACT_WAIT,WEB_ACT_CLICK,WEB_ACT_FOCUS,WEB_ACT_TYPE,
    WEB_ACT_SELECT,WEB_ACT_KEY,WEB_ACT_SCROLL,WEB_ACT_BACK } WebActionKind;
typedef struct { uint32_t version,kind,target_ref; WebText payload; float dx,dy; } WebDomAction;
/* Returns -1 on malformed/overflow input. Never silently truncates C storage. */
int web_dom_parse(WebDom *out,const cJSON *json);
const char *web_dom_text(const WebDom *obs,WebText span);
#endif
