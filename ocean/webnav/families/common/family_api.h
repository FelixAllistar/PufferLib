#ifndef WEBNAV_FAMILY_API_H
#define WEBNAV_FAMILY_API_H
#include <stdint.h>
#include <stddef.h>
#define WF_ABI_VERSION 2u
#define WF_MAX_NODES 128u
#define WF_TEXT_BYTES 16384u
#define WF_HEADER_WORDS 32u
/* All header offsets and enum values are stable for ABI v2. */
enum {WF_VERSION,WF_TASK,WF_OP,WF_SEED,WF_ACTION,WF_TARGET,WF_ARG0,WF_ARG1,WF_ELAPSED,
      WF_STATUS,WF_RAW_REWARD,WF_TIMED_REWARD,WF_DEADLINE,WF_ERROR};
enum {WF_OBSERVE=0,WF_RESET=1,WF_STEP=2};
enum {WF_RUNNING=0,WF_TERMINAL=1,WF_TIMEOUT=2};
enum {WF_WAIT=0,WF_CLICK=1,WF_INSERT=2,WF_BACKSPACE=3,WF_DELETE=4,
      WF_LEFT=5,WF_RIGHT=6,WF_HOME=7,WF_END=8,WF_SELECT_ALL=9,WF_ENTER=10,
      WF_TAB_KEY=11,WF_SCROLL=12,WF_POINTER_DOWN=13,WF_POINTER_MOVE=14,
      WF_POINTER_UP=15,WF_KEY_DOWN=16,WF_KEY_UP=17,WF_SELECT_RANGE=18,
      WF_COPY=19,WF_PASTE=20,WF_SELECT_OPTION=21};
enum {WF_OTHER=0,WF_BUTTON=1,WF_CHECKBOX=2,WF_INPUT=3,WF_LINK=4,WF_RADIO=5,
      WF_SELECT=6,WF_OPTION=7,WF_FOLDER=8,WF_FILE=9,WF_CELL=10,WF_TAB=11,
      WF_PANEL=12,WF_TEXT=13,WF_SLIDER=14,WF_CANVAS=15,WF_TEXTAREA=16};
enum {WF_VISIBLE=1,WF_ENABLED=2,WF_CLICKABLE=4,WF_CHECKED=8,WF_FOCUSED=16,
      WF_EXPANDED=32,WF_READONLY=64,WF_SELECTED=128};
typedef struct {uint32_t offset,length;} WFText;
typedef struct {
    uint32_t ref,parent,role,flags;
    WFText name,value;
    float x,y,width,height;
    uint32_t selection_start,selection_end,capacity;
    float scroll_x,scroll_y,scroll_max_x,scroll_max_y;
} WFNode;
typedef struct {
    uint32_t version,count,text_bytes,omitted,text_truncated,elapsed_ms,deadline_ms;
    WFText instruction;
    WFNode nodes[WF_MAX_NODES];
    char text[WF_TEXT_BYTES];
} WFView;
typedef struct {
    uint32_t kind,target,arg0,arg1,elapsed_ms;
    const char *text;
    size_t text_length;
} WFAction;
typedef struct {
    uint32_t abi_version,row_words,batch_lanes,task_count;
    const char *family;
    const char *const *task_names;
    /* validate: 0 valid, negative invalid; no abort on caller input. */
    int (*validate)(const uint32_t *row);
    void (*batch)(uint32_t *words);
    /* Only public data; no private targets, seed, rewards or goal flags. */
    int (*observe)(const uint32_t *row,WFView *view);
    /* Transport only: pack command, do not implement task transitions here. */
    int (*action)(uint32_t *row,const WFAction *action);
} WFFamily;
typedef const WFFamily *(*WFGetFamily)(void);
/* Each CPU-only, locally loaded family DSO exports this one descriptor. */
#if defined(__GNUC__)
#define WF_EXPORT __attribute__((visibility("default")))
#else
#define WF_EXPORT
#endif
WF_EXPORT const WFFamily *webnav_family_v2(void);
/* Helpers are header-only so a family does not depend on global mutable state. */
static inline void wf_view_init(WFView *v,uint32_t elapsed,uint32_t deadline) {
    *v=(WFView){0};v->version=WF_ABI_VERSION;v->elapsed_ms=elapsed;v->deadline_ms=deadline;
}
static inline int wf_text_add(WFView *v,const char *s,size_t n,WFText *out) {
    if(!s||n>=WF_TEXT_BYTES||v->text_bytes>WF_TEXT_BYTES-n-1) {v->text_truncated=1;return -1;}
    out->offset=v->text_bytes;out->length=(uint32_t)n;
    for(size_t i=0;i<n;i++)v->text[v->text_bytes++]=s[i];
    v->text[v->text_bytes++]=0;return 0;
}
static inline const char *wf_text_get(const WFView *v,WFText t) {
    if(t.offset>=v->text_bytes||t.length>=v->text_bytes-t.offset||v->text[t.offset+t.length])return NULL;
    return v->text+t.offset;
}
#endif
