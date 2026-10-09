#include "record_browser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

static WFView view;
static WPPage page;
static WUCapabilities caps;
static void observe(const WAState *s) {
    assert(!wa_observe(s,"Inspect a record, edit its amount, and return to the list.",&view,&page,&caps));
    assert(!wp_validate(&page,&view));
    for(unsigned i=0;i<caps.count;i++)if(caps.items[i].ref)assert(wp_view_node(&view,caps.items[i].ref));
}
static const WFNode *find(unsigned role,const char *name) {
    for(unsigned i=0;i<view.count;i++)if(view.nodes[i].role==role&&
        (!name||!strcmp(wf_text_get(&view,view.nodes[i].name),name)))return view.nodes+i;
    return NULL;
}
static int has_cap(unsigned kind,uint32_t ref) {
    for(unsigned i=0;i<caps.count;i++)if(caps.items[i].kind==kind&&caps.items[i].ref==ref)return 1;
    return 0;
}
static void action(WAState *s,unsigned kind,unsigned role,const char *name,const char *text) {
    observe(s);const WFNode *n=find(role,name);assert(n&&has_cap(kind,n->ref));
    WFAction a={.kind=kind,.target=n->ref,.text=text,.text_length=text?strlen(text):0};
    assert(!wa_step(s,&a));observe(s);
}
static void reject(WAState *s,WFAction a) {
    WAState before=*s;assert(wa_step(s,&a)<0);assert(!memcmp(s,&before,sizeof *s));
}
static void settle(WAState *s) {
    for(unsigned i=0;i<8;i++){
        observe(s);
        if(find(WF_TEXT,"Loading")){WFAction a={.kind=WF_WAIT,.elapsed_ms=250};assert(!wa_step(s,&a));}
        else if(find(WF_TEXT,"Could not load this page"))action(s,WF_CLICK,WF_BUTTON,"Retry",NULL);
        else return;
    }
    assert(!"load failed to settle within the preset's finite failure schedule");
}
static const WPNode *meta(uint32_t ref) {
    for(unsigned i=0;i<page.count;i++)if(page.nodes[i].ref==ref)return page.nodes+i;
    return NULL;
}
static void table_relationships(void) {
    unsigned cells=0,links=0;
    for(unsigned i=0;i<page.count;i++){
        const WPNode *n=page.nodes+i;
        if(n->kind==WP_CELL){
            cells++;assert(n->row>=2&&n->column>=1&&n->column<=3);
            unsigned found=0;
            for(unsigned j=0;j<page.relation_count;j++){
                WPRelation r=page.relations[j];if(r.source!=n->ref||r.kind!=WP_HEADER)continue;
                const WPNode *header=meta(r.target);assert(header&&header->kind==WP_COLUMN_HEADER&&header->column==n->column);
                found++;
            }
            assert(found==1);
        }
        const WFNode *node=wp_view_node(&view,n->ref);
        if(node->role==WF_LINK){
            links++;const char *href=wp_text_get(&page,n->href);
            assert(href&&!strncmp(href,"https://records.webnav.local/records/",36));
            const WPNode *parent=meta(node->parent);assert(parent&&parent->kind==WP_CELL&&parent->column==1);
        }
    }
    assert(cells==links*3&&links>0&&links<=4);
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    WAState s,same;unsigned counts=0,statuses=0,amount_changes=0,first_amount=0;
    for(unsigned seed=0;seed<64;seed++){
        memset(&s,0xa5,sizeof s);assert(!wa_reset(&s,seed,4096));assert(!wa_validate(&s));
        memset(&same,0x5a,sizeof same);assert(!wa_reset(&same,seed,4096));assert(!memcmp(&s,&same,sizeof s));
        assert(s.words[67]>=8&&s.words[67]<=16);counts|=1u<<(s.words[67]-8);
        for(unsigned i=0;i<s.words[67];i++){
            const uint32_t *r=s.words+192+4*i;assert(r[0]>=1&&r[0]<=16&&r[1]==0&&r[2]<=999999&&r[3]<3);
            statuses|=1u<<r[3];if(!seed&&!i)first_amount=r[2];else amount_changes+=r[2]!=first_amount;
        }
        for(unsigned i=352;i<WA_WORDS;i++)assert(!s.words[i]);
        observe(&s);table_relationships();
    }
    assert(counts==511&&statuses==7&&amount_changes>500);
    /* Changing future latency/failures cannot change a ready public view. */
    assert(!wa_reset(&s,9,4096));observe(&s);
    WFView before_view=view;WPPage before_page=page;WUCapabilities before_caps=caps;
    same=s;same.words[2]+=17;same.words[3]+=7;observe(&same);
    assert(!memcmp(&view,&before_view,sizeof view)&&!memcmp(&page,&before_page,sizeof page)&&!memcmp(&caps,&before_caps,sizeof caps));
    observe(&s);
    const WFNode *link=find(WF_LINK,NULL);assert(link);
    char chosen[32],original_amount[32];strcpy(chosen,wf_text_get(&view,link->name));
    uint32_t record_row=wp_view_node(&view,link->parent)->parent;
    int found_amount=0;
    for(unsigned i=0;i<view.count;i++)if(view.nodes[i].parent==record_row&&
        !strcmp(wf_text_get(&view,view.nodes[i].name),"Amount")){
        strcpy(original_amount,wf_text_get(&view,view.nodes[i].value));found_amount=1;
    }
    assert(found_amount);
    WFAction obsolete={.kind=WF_CLICK,.target=link->ref};assert(!wa_step(&s,&obsolete));reject(&s,obsolete);
    settle(&s);observe(&s);
    assert(!strcmp(wf_text_get(&view,find(WF_TEXT,"Amount")->value),original_amount));
    action(&s,WF_CLICK,WF_BUTTON,"Edit amount",NULL);
    const WFNode *back=find(WF_BUTTON,"Back");assert(back&&!has_cap(WF_CLICK,back->ref));
    reject(&s,(WFAction){.kind=WF_CLICK,.target=back->ref});
    action(&s,WF_SELECT_ALL,WF_INPUT,"Amount",NULL);
    action(&s,WF_INSERT,WF_INPUT,"Amount","x");action(&s,WF_CLICK,WF_BUTTON,"Save",NULL);
    assert(find(WF_TEXT,"Amount must be an integer from 0 to 999999."));
    assert(!strcmp(wf_text_get(&view,find(WF_TEXT,"Amount")->value),original_amount));
    action(&s,WF_CLICK,WF_BUTTON,"Cancel",NULL);assert(!find(WF_INPUT,"Amount"));
    action(&s,WF_CLICK,WF_BUTTON,"Edit amount",NULL);action(&s,WF_SELECT_ALL,WF_INPUT,"Amount",NULL);
    action(&s,WF_INSERT,WF_INPUT,"Amount","99");
    obsolete=(WFAction){.kind=WF_CLICK,.target=find(WF_BUTTON,"Save")->ref};assert(!wa_step(&s,&obsolete));reject(&s,obsolete);
    observe(&s);assert(!strcmp(wf_text_get(&view,find(WF_TEXT,"Amount")->value),"99"));
    action(&s,WF_CLICK,WF_BUTTON,"Back",NULL);settle(&s);
    action(&s,WF_CLICK,WF_INPUT,"Search records",NULL);
    reject(&s,(WFAction){.kind=WF_INSERT,.target=find(WF_INPUT,"Search records")->ref,
        .text="12345678901234567890123456789012",.text_length=32});
    /* Twenty bytes fit once; validating the result must not count them twice. */
    action(&s,WF_INSERT,WF_INPUT,"Search records","12345678901234567890");
    action(&s,WF_SELECT_ALL,WF_INPUT,"Search records",NULL);action(&s,WF_INSERT,WF_INPUT,"Search records",chosen);
    assert(!strcmp(wf_text_get(&view,find(WF_TEXT,"Applied search")->value),""));
    action(&s,WF_CLICK,WF_BUTTON,"Search",NULL);assert(!strcmp(wf_text_get(&view,find(WF_TEXT,"Applied search")->value),chosen));
    assert(find(WF_LINK,chosen));table_relationships();
    action(&s,WF_CLICK,WF_INPUT,"Search records",NULL);action(&s,WF_SELECT_ALL,WF_INPUT,"Search records",NULL);
    action(&s,WF_INSERT,WF_INPUT,"Search records","no such record");action(&s,WF_CLICK,WF_BUTTON,"Search",NULL);
    assert(find(WF_TEXT,"No matching records"));assert(!has_cap(WF_CLICK,find(WF_BUTTON,"Next page")->ref));
    reject(&s,(WFAction){.kind=WF_SELECT_OPTION,.target=find(WF_SELECT,"Status")->ref,.arg0=4});
    /* Public metadata rejects dangling refs, parent cycles and bad spans. */
    observe(&s);WPPage bad=page;bad.nodes[0].ref=1;assert(wp_validate(&bad,&view)<0);
    WFView bad_view=view;bad_view.nodes[0].parent=bad_view.nodes[1].ref;assert(wp_validate(&page,&bad_view)<0);
    bad=page;bad.nodes[0].row_span=1;assert(wp_validate(&bad,&view)<0);
    WAState before=s;assert(wa_reset(&s,0,0)<0&&!memcmp(&s,&before,sizeof s));
    puts("PASS: 64 generated worlds, public-only list/detail/edit workflow, stale/disabled action rejection, private timing independence and table/link metadata");
    return 0;
}
