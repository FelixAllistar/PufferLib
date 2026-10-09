#include "record_browser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void app_record_browser_batch(uint32_t *);
static int valid_row(const uint32_t *r) {
    if (!r || r[11]!=1 || r[1]!=17 || r[0]>=17 || r[4]>2 || r[6]+r[7]>8 ||
        r[6]>8 || r[7]>8 || (r[4]==1?r[5]>r[2]:r[5]!=0) || r[64]>20 ||
        r[67]>16 || r[68]>1 || r[69]>3 || r[70]>4 || r[71]>1 ||
        r[72]>31 || r[73]>r[72] || r[74]>r[72] || r[73]>r[74] || r[75]>31 || r[76]>31) return -1;
    for (unsigned i=0;i<r[6];i++) if (r[16+i]>=17) return -1;
    for (unsigned i=0;i<r[7];i++) if (r[24+i]>=17) return -1;
    for (unsigned slot=0;slot<3;slot++) {
        unsigned length=r[slot==0?72:slot==1?75:76],base=96+slot*32;
        for (unsigned i=0;i<length;i++) if (r[base+i]<32 || r[base+i]>126) return -1;
    }
    if (r[64]==5 && r[72]-(r[74]-r[73])+r[76]>31) return -1;
    if (r[280]>3 || r[283]>3 || r[284]>31 || r[285]>r[286] || r[286]>r[284] || r[287]>1 || r[288]>2) return -1;
    if ((r[280]==1 || r[280]==2) && (r[281]<1 || r[281]>16)) return -1;
    for (unsigned i=0;i<r[284];i++) if (r[320+i]<32 || r[320+i]>126) return -1;
    if (r[64]==16 && r[284]-(r[286]-r[285])+r[76]>31) return -1;
    if ((r[64]==9 && r[65]>3) || (r[64]==10 && r[65]>1) || (r[64]==13 && (r[65]<1 || r[65]>16))) return -1;
    for (unsigned i=0;i<r[67];i++) {
        if (r[192+4*i]<1 || r[192+4*i]>16 || r[194+4*i]>999999 || r[195+4*i]>2) return -1;
        for (unsigned j=0;j<i;j++) if (r[192+4*i]==r[192+4*j]) return -1;
    }
    return 0;
}
/* Kept for the independent raw composition fixture. */
WF_EXPORT int wa_fixture(uint32_t *r) {
    if(valid_row(r))return -1;
    app_record_browser_batch(r);return 0;
}

WF_EXPORT int wa_validate(const WAState *s) {
    if(!s||s->version!=WA_VERSION||!s->ref_base||s->ref_base>UINT32_MAX-WF_MAX_NODES||
       valid_row(s->words)||s->words[12]||s->words[77]>s->words[67]||s->words[260]>4)return -1;
    for(unsigned i=0;i<s->words[260];i++){
        unsigned found=0;
        for(unsigned j=0;j<s->words[67];j++)found|=s->words[256+i]==s->words[192+4*j];
        if(!found)return -1;
        for(unsigned j=0;j<i;j++)if(s->words[256+i]==s->words[256+j])return -1;
    }
    return 0;
}
WF_EXPORT int wa_reset(WAState *s,uint32_t seed,uint32_t first_ref) {
    if(!s||!first_ref||first_ref>UINT32_MAX-WF_MAX_NODES)return -1;
    WAState next={.version=WA_VERSION,.ref_base=first_ref};
    next.words[12]=1;next.words[13]=seed;
    app_record_browser_batch(next.words);
    if(wa_validate(&next))return -1;
    *s=next;return 0;
}

typedef struct { unsigned click,argument,editor,select; } Binding;
typedef struct {
    WFView *v;WPPage *p;uint32_t base;
    Binding *bindings;
} Projection;
static uint32_t node(Projection *b,unsigned role,uint32_t parent,const char *name,
                     const char *value,unsigned flags,Binding binding,WPNode meta,const char *href) {
    WFView *v=b->v;
    if(v->count>=WF_MAX_NODES){v->omitted++;return 0;}
    unsigned slot=v->count;uint32_t ref=b->base+slot;
    WFNode *n=v->nodes+slot;
    *n=(WFNode){.ref=ref,.parent=parent,.role=role,.flags=flags,
        .x=16,.y=16+(float)slot*20,.width=200,.height=20};
    if(wf_text_add(v,name,strlen(name),&n->name)||wf_text_add(v,value,strlen(value),&n->value))return 0;
    if(b->p){meta.ref=ref;if(wp_node_add(b->p,meta,href))return 0;}
    if(b->bindings)b->bindings[slot]=binding;
    v->count++;return ref;
}
static uint32_t plain(Projection *b,unsigned role,uint32_t parent,const char *name,const char *value,unsigned flags) {
    return node(b,role,parent,name,value,flags,(Binding){0},(WPNode){0},NULL);
}
static uint32_t button(Projection *b,uint32_t parent,const char *name,int enabled,unsigned command,unsigned arg) {
    return node(b,WF_BUTTON,parent,name,"",WF_VISIBLE|WF_CLICKABLE|(enabled?WF_ENABLED:0),
        (Binding){.click=command,.argument=arg},(WPNode){0},NULL);
}
static void units(const uint32_t *src,unsigned length,char *out) {
    for(unsigned i=0;i<length;i++)out[i]=(char)src[i];out[length]=0;
}
static uint32_t editor(Projection *b,uint32_t parent,const char *label,const uint32_t *r,int draft,int enabled) {
    unsigned length=r[draft?284:72],start=r[draft?285:73],end=r[draft?286:74];
    char text[32];units(r+(draft?320:96),length,text);
    uint32_t ref=node(b,WF_INPUT,parent,label,text,WF_VISIBLE|WF_CLICKABLE|
        (enabled?WF_ENABLED:0)|(enabled&&r[draft?287:71]?WF_FOCUSED:0),
        (Binding){.click=draft?15:4,.editor=draft?2:1},(WPNode){0},NULL);
    if(ref){WFNode *n=b->v->nodes+b->v->count-1;n->capacity=32;n->selection_start=start;n->selection_end=end;}
    return ref;
}
static const uint32_t *record(const uint32_t *r,uint32_t key) {
    for(unsigned i=0;i<r[67];i++)if(r[192+4*i]==key)return r+192+4*i;
    return NULL;
}
static int capability(WUCapabilities *c,WUCapability item) {
    if(c->count>=WU_MAX_CAPABILITIES){c->incomplete=1;return -1;}
    c->items[c->count++]=item;return 0;
}
/* Capability construction reads public fields only. It cannot see the world,
 * query results, records, latency, future failures or transaction state. */
static int capabilities(const WFView *v,WUCapabilities *c) {
    *c=(WUCapabilities){.version=WU_CAP_VERSION,.incomplete=!!(v->omitted||v->text_truncated)};
    if(capability(c,(WUCapability){.kind=WF_WAIT}))return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if((n->flags&(WF_VISIBLE|WF_ENABLED))!=(WF_VISIBLE|WF_ENABLED))continue;
        if((n->flags&WF_CLICKABLE)&&capability(c,(WUCapability){.kind=WF_CLICK,.ref=n->ref,.wire_target=n->ref}))return -1;
        if(n->role==WF_SELECT){
            unsigned options=0;
            for(unsigned j=0;j<v->count;j++)options+=v->nodes[j].role==WF_OPTION&&v->nodes[j].parent==n->ref;
            if(options&&capability(c,(WUCapability){.kind=WF_SELECT_OPTION,.ref=n->ref,.wire_target=n->ref,
                .flags=WU_CAP_RANGE,.max0=options-1,.step0=1,.unit0=WU_UNIT_INDEX}))return -1;
        }
        if(n->role!=WF_INPUT||!(n->flags&WF_FOCUSED)||(n->flags&WF_READONLY))continue;
        if(n->selection_start>n->selection_end||n->selection_end>n->value.length||
           !n->capacity||n->value.length>=n->capacity)return -1;
        unsigned room=n->capacity-1-n->value.length+n->selection_end-n->selection_start;
        if(room&&capability(c,(WUCapability){.kind=WF_INSERT,.ref=n->ref,.wire_target=n->ref,
            .flags=WU_CAP_TEXT|WU_CAP_ASCII,.text_capacity=room}))return -1;
        if(capability(c,(WUCapability){.kind=WF_SELECT_ALL,.ref=n->ref,.wire_target=n->ref})||
           capability(c,(WUCapability){.kind=WF_BACKSPACE,.ref=n->ref,.wire_target=n->ref}))return -1;
    }
    return 0;
}
static int project(const WAState *s,const char *instruction,WFView *v,WPPage *p,WUCapabilities *caps,Binding *bindings) {
    if(wa_validate(s)||!instruction||!v||!caps)return -1;
    const uint32_t *r=s->words;
    const char *const statuses[]={"New","Active","Closed"};
    int modal=r[280]==1,ready=r[4]==0;
    char url[96],title[64];
    if(r[0]){snprintf(url,sizeof url,"https://records.webnav.local/records/%u",r[0]);snprintf(title,sizeof title,"Row %u",r[0]);}
    else {strcpy(url,"https://records.webnav.local/records");strcpy(title,"Records");}
    if(p&&wp_init(p,url,title))return -1;
    wf_view_init(v,s->elapsed_ms,0);
    if(wf_text_add(v,instruction,strlen(instruction),&v->instruction))return -1;
    Projection b={v,p,s->ref_base,bindings};
    uint32_t root=plain(&b,WF_PANEL,0,"Record browser","",WF_VISIBLE|WF_ENABLED);
    node(&b,WF_TEXT,root,title,"",WF_VISIBLE|WF_ENABLED,(Binding){0},(WPNode){.kind=WP_HEADING,.heading_level=1},NULL);
    button(&b,root,"Back",r[6]&&!modal,1,0);button(&b,root,"Forward",r[7]&&!modal,2,0);
    if(!ready){
        plain(&b,WF_TEXT,root,r[4]==1?"Loading":"Could not load this page","",WF_VISIBLE|WF_ENABLED);
        if(r[4]==2)button(&b,root,"Retry",!modal,3,0);
    } else if(!r[0]){
        editor(&b,root,"Search records",r,0,1);button(&b,root,"Search",1,8,0);
        uint32_t filter=node(&b,WF_SELECT,root,"Status",r[69]?statuses[r[69]-1]:"All statuses",WF_VISIBLE|WF_ENABLED,
            (Binding){.select=9},(WPNode){0},NULL);
        for(unsigned i=0;i<4;i++)plain(&b,WF_OPTION,filter,i?statuses[i-1]:"All statuses","",
            WF_VISIBLE|WF_ENABLED|(r[69]==i?WF_SELECTED:0));
        button(&b,root,r[68]?"Sort amount ascending":"Sort amount descending",1,10,!r[68]);
        button(&b,root,"Previous page",r[70]>0,11,0);button(&b,root,"Next page",(r[70]+1)*4<r[77],12,0);
        char count[64],query[32];snprintf(count,sizeof count,"Page %u; %u matching records",r[70]+1,r[77]);
        plain(&b,WF_TEXT,root,count,"",WF_VISIBLE|WF_ENABLED);
        units(r+128,r[75],query);plain(&b,WF_TEXT,root,"Applied search",query,WF_VISIBLE|WF_ENABLED);
        uint32_t table=node(&b,WF_PANEL,root,"Records","",WF_VISIBLE|WF_ENABLED,(Binding){0},(WPNode){.kind=WP_TABLE},NULL);
        uint32_t header=node(&b,WF_PANEL,table,"","",WF_VISIBLE|WF_ENABLED,(Binding){0},(WPNode){.kind=WP_ROW,.row=1},NULL);
        uint32_t headers[3];const char *const labels[]={"Name","Amount","Status"};
        for(unsigned col=0;col<3;col++)headers[col]=node(&b,WF_CELL,header,labels[col],"",WF_VISIBLE|WF_ENABLED,
            (Binding){0},(WPNode){.kind=WP_COLUMN_HEADER,.row=1,.column=col+1,.row_span=1,.column_span=1},NULL);
        for(unsigned i=0;i<r[260];i++){
            const uint32_t *row=record(r,r[256+i]);if(!row)return -1;
            unsigned row_index=2+r[70]*4+i;
            uint32_t parent=node(&b,WF_PANEL,table,"","",WF_VISIBLE|WF_ENABLED,(Binding){0},(WPNode){.kind=WP_ROW,.row=row_index},NULL);
            char name[32],amount[32],href[96];snprintf(name,sizeof name,"Row %u",row[0]);snprintf(amount,sizeof amount,"%u",row[2]);
            snprintf(href,sizeof href,"https://records.webnav.local/records/%u",row[0]);
            const char *const values[]={name,amount,statuses[row[3]]};
            for(unsigned col=0;col<3;col++){
                uint32_t cell=node(&b,WF_CELL,parent,labels[col],values[col],WF_VISIBLE|WF_ENABLED,(Binding){0},
                    (WPNode){.kind=WP_CELL,.row=row_index,.column=col+1,.row_span=1,.column_span=1},NULL);
                if(p&&wp_relation_add(p,cell,headers[col],WP_HEADER))return -1;
                if(!col)node(&b,WF_LINK,cell,name,"",WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    (Binding){.click=13,.argument=row[0]},(WPNode){0},href);
            }
        }
        if(!r[260])plain(&b,WF_TEXT,table,"No matching records","",WF_VISIBLE|WF_ENABLED);
    } else {
        const uint32_t *row=record(r,r[0]);
        if(row){
            char amount[32];snprintf(amount,sizeof amount,"%u",row[2]);
            plain(&b,WF_TEXT,root,"Amount",amount,WF_VISIBLE|WF_ENABLED);
            plain(&b,WF_TEXT,root,"Status",statuses[row[3]],WF_VISIBLE|WF_ENABLED);
            button(&b,root,"Edit amount",!modal,14,0);
        }else plain(&b,WF_TEXT,root,"Record not found","",WF_VISIBLE|WF_ENABLED);
    }
    if(modal){
        uint32_t dialog=plain(&b,WF_PANEL,root,"Edit amount","",WF_VISIBLE|WF_ENABLED);
        editor(&b,dialog,"Amount",r,1,1);
        if(r[283])plain(&b,WF_TEXT,dialog,r[283]==3?"Amount must be an integer from 0 to 999999.":
            r[283]==2?"The record changed; cancel and open it again.":"Record not found.","",WF_VISIBLE|WF_ENABLED);
        button(&b,dialog,"Save",1,19,0);button(&b,dialog,"Cancel",1,20,0);
    }
    if(v->omitted||v->text_truncated||(p&&wp_validate(p,v)))return -1;
    return capabilities(v,caps);
}
WF_EXPORT int wa_observe(const WAState *s,const char *instruction,WFView *v,WPPage *p,WUCapabilities *caps) {
    return project(s,instruction,v,p,caps,NULL);
}
WF_EXPORT int wa_step(WAState *s,const WFAction *action) {
    if(!s||!action||action->kind>WF_SELECT_OPTION||action->arg1||
       (action->text_length&&!action->text)||s->ref_base>UINT32_MAX-2*WF_MAX_NODES)return -1;
    WFView view;WUCapabilities caps;Binding bindings[WF_MAX_NODES]={0};
    if(project(s,"",&view,NULL,&caps,bindings))return -1;
    const WUCapability *cap=NULL;
    for(unsigned i=0;i<caps.count;i++)if(caps.items[i].kind==action->kind&&caps.items[i].ref==action->target){cap=caps.items+i;break;}
    if(!cap||action->arg0<cap->min0||action->arg0>cap->max0||
       (action->kind==WF_INSERT?(!action->text_length||action->text_length>cap->text_capacity):action->text_length!=0))return -1;
    for(size_t i=0;i<action->text_length;i++)if((unsigned char)action->text[i]<32||(unsigned char)action->text[i]>126)return -1;
    unsigned command=0,arg=0;
    if(action->target){
        unsigned index=action->target-s->ref_base;if(index>=view.count)return -1;
        Binding bind=bindings[index];
        if(action->kind==WF_CLICK){command=bind.click;arg=bind.argument;}
        else if(action->kind==WF_SELECT_OPTION){command=bind.select;arg=action->arg0;}
        else if(bind.editor){
            if(action->kind==WF_INSERT)command=bind.editor==1?5:16;
            else if(action->kind==WF_SELECT_ALL)command=bind.editor==1?6:17;
            else if(action->kind==WF_BACKSPACE)command=bind.editor==1?7:18;
        }
        if(!command)return -1;
    }
    WAState next=*s;
    next.words[64]=command;next.words[65]=arg;next.words[66]=action->elapsed_ms;
    next.words[76]=(uint32_t)action->text_length;memset(next.words+160,0,32*sizeof(uint32_t));
    for(size_t i=0;i<action->text_length;i++)next.words[160+i]=(unsigned char)action->text[i];
    if(wa_fixture(next.words))return -1;
    next.words[64]=next.words[65]=next.words[66]=next.words[76]=0;
    memset(next.words+160,0,32*sizeof(uint32_t));
    next.ref_base+=WF_MAX_NODES;
    uint64_t elapsed=(uint64_t)next.elapsed_ms+action->elapsed_ms;
    next.elapsed_ms=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    if(wa_validate(&next))return -1;
    *s=next;return 0;
}
