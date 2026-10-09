#ifndef WEBNAV_PUBLIC_PAGE_H
#define WEBNAV_PUBLIC_PAGE_H
#include "../../families/common/family_api.h"
#include <string.h>

/* Additive public metadata. The WF ABI v2 layout and policy dimensions stay
 * unchanged. All refs resolve in the accompanying WFView; no entity IDs,
 * hidden route keys, task IDs, rewards or private goals belong here. */
#define WP_VERSION 1u
#define WP_MAX_RELATIONS 512u
#define WP_TEXT_BYTES 16384u
enum { WP_NONE, WP_TABLE, WP_ROW, WP_COLUMN_HEADER, WP_ROW_HEADER, WP_HEADING, WP_CELL };
enum { WP_LABELLED_BY=1, WP_DESCRIBED_BY, WP_HEADER };
typedef struct {
    uint32_t ref,kind,heading_level;
    /* One-based positions in the enclosing table; zero means unknown. */
    uint32_t row,column,row_span,column_span;
    WFText href;
} WPNode;
typedef struct { uint32_t source,target,kind; } WPRelation;
typedef struct {
    uint32_t version,count,relation_count,text_bytes,omitted,text_truncated;
    WFText url,title;
    WPNode nodes[WF_MAX_NODES];
    WPRelation relations[WP_MAX_RELATIONS];
    char text[WP_TEXT_BYTES];
} WPPage;

static inline int wp_text_add(WPPage *p,const char *s,WFText *out) {
    if(!p||!s||!out)return -1;
    size_t n=strlen(s);
    if(n>=WP_TEXT_BYTES||p->text_bytes>WP_TEXT_BYTES-n-1){p->text_truncated=1;return -1;}
    *out=(WFText){p->text_bytes,(uint32_t)n};
    memcpy(p->text+p->text_bytes,s,n+1);p->text_bytes+=(uint32_t)n+1;
    return 0;
}
static inline const char *wp_text_get(const WPPage *p,WFText t) {
    if(!p||p->text_bytes>WP_TEXT_BYTES||t.offset>=p->text_bytes||
       t.length>=p->text_bytes-t.offset||p->text[t.offset+t.length]||
       memchr(p->text+t.offset,0,t.length))return NULL;
    return p->text+t.offset;
}
static inline int wp_init(WPPage *p,const char *url,const char *title) {
    if(!p||!url||!title)return -1;
    *p=(WPPage){.version=WP_VERSION};
    /* Offset zero is the canonical empty optional string. */
    p->text_bytes=1;
    return wp_text_add(p,url,&p->url)||wp_text_add(p,title,&p->title)?-1:0;
}
static inline int wp_node_add(WPPage *p,WPNode node,const char *href) {
    if(!p||p->count>=WF_MAX_NODES){if(p)p->omitted++;return -1;}
    if(!node.ref||node.kind>WP_CELL)return -1;
    for(unsigned i=0;i<p->count;i++)if(p->nodes[i].ref==node.ref)return -1;
    if(wp_text_add(p,href?href:"",&node.href))return -1;
    p->nodes[p->count++]=node;return 0;
}
static inline int wp_relation_add(WPPage *p,uint32_t source,uint32_t target,uint32_t kind) {
    if(!p||p->relation_count>=WP_MAX_RELATIONS){if(p)p->omitted++;return -1;}
    if(!source||!target||source==target||kind<WP_LABELLED_BY||kind>WP_HEADER)return -1;
    for(unsigned i=0;i<p->relation_count;i++){
        WPRelation r=p->relations[i];
        if(r.source==source&&r.target==target&&r.kind==kind)return 0;
    }
    p->relations[p->relation_count++]=(WPRelation){source,target,kind};return 0;
}
static inline const WFNode *wp_view_node(const WFView *v,uint32_t ref) {
    if(!v||v->count>WF_MAX_NODES||!ref)return NULL;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static inline int wp_validate(const WPPage *p,const WFView *v) {
    if(!p||!v||p->version!=WP_VERSION||v->version!=WF_ABI_VERSION||
       p->count>WF_MAX_NODES||v->count>WF_MAX_NODES||v->text_bytes>WF_TEXT_BYTES||
       p->relation_count>WP_MAX_RELATIONS||!wp_text_get(p,p->url)||!wp_text_get(p,p->title))return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(!n->ref||!wf_text_get(v,n->name)||!wf_text_get(v,n->value))return -1;
        for(unsigned j=0;j<i;j++)if(v->nodes[j].ref==n->ref)return -1;
        unsigned depth=0;
        while(n->parent){
            n=wp_view_node(v,n->parent);
            if(!n||++depth>v->count)return -1;
        }
    }
    for(unsigned i=0;i<p->count;i++){
        const WPNode *n=p->nodes+i;
        if(!wp_view_node(v,n->ref)||n->kind>WP_CELL||!wp_text_get(p,n->href)||
           (n->kind==WP_HEADING?(n->heading_level<1||n->heading_level>6):n->heading_level!=0)||
           (n->row_span&&!n->row)||(n->column_span&&!n->column))return -1;
        for(unsigned j=0;j<i;j++)if(p->nodes[j].ref==n->ref)return -1;
    }
    for(unsigned i=0;i<p->relation_count;i++){
        WPRelation r=p->relations[i];
        if(!wp_view_node(v,r.source)||!wp_view_node(v,r.target)||r.source==r.target||
           r.kind<WP_LABELLED_BY||r.kind>WP_HEADER)return -1;
    }
    return 0;
}
#endif
