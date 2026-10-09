#include "public.h"
#include "../transport/utf8.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

static const char *const route_titles[]={"Blank","Records","Orders","Reports"};
static const char *const route_urls[]={"about:blank","https://example.test/records","https://example.test/orders","https://example.test/reports"};
static void observe(const WBCSession *s,WFView *view,WUCapabilities *caps) {
    WBCView public;assert(!wbc_observe(&s->browser,&public));
    const char *titles[16],*address=NULL;
    for (unsigned i=0;i<public.count;i++) {
        assert(public.tabs[i].browser.route<4);
        titles[i]=route_titles[public.tabs[i].browser.route];
        if (public.tabs[i].identity==public.active) address=route_urls[public.tabs[i].browser.route];
    }
    assert(!wbc_session_observe(s,titles,address,"Browse the public documents",view,caps));
}
static const WFNode *node(const WFView *v,const char *name,unsigned role) {
    for (unsigned i=0;i<v->count;i++) if (v->nodes[i].role==role && !strcmp(wf_text_get(v,v->nodes[i].name),name)) return v->nodes+i;
    assert(0);return NULL;
}
static const WUCapability *capability(const WUCapabilities *caps,uint32_t ref,unsigned kind) {
    for (unsigned i=0;i<caps->count;i++) if (caps->items[i].ref==ref && caps->items[i].kind==kind) return caps->items+i;
    return NULL;
}
static WFAction click(WBCSession *s,const char *name,unsigned role) {
    WFView v;WUCapabilities caps;observe(s,&v,&caps);
    const WFNode *n=node(&v,name,role);
    assert((n->flags&WF_ENABLED) && capability(&caps,n->ref,WF_CLICK));
    WFAction action={.kind=WF_CLICK,.target=n->ref,.elapsed_ms=25};
    assert(!wbc_session_action(s,&action));return action;
}
static void rejected(WBCSession *s,WFAction action) {
    WBCSession before=*s;assert(wbc_session_action(s,&action)<0 && !memcmp(s,&before,sizeof before));
}
static void settle(WBCSession *s) {
    for (unsigned i=0;i<7;i++) {
        WFView v;WUCapabilities caps;observe(s,&v,&caps);
        const char *phase=wf_text_get(&v,node(&v,"Browser",WF_PANEL)->value);
        if (!strcmp(phase,"Ready")) return;
        if (!strcmp(phase,"Loading")) assert(!wbc_session_action(s,&(WFAction){.kind=WF_WAIT,.elapsed_ms=1000}));
        else {assert(!strcmp(phase,"Could not load page"));click(s,"Retry",WF_BUTTON);}
    }
    assert(0);
}
static void address_is(WBCSession *s,const char *expected) {
    WFView v;WUCapabilities caps;observe(s,&v,&caps);
    assert(!strcmp(wf_text_get(&v,node(&v,"Address",WF_TEXT)->value),expected));
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    for (unsigned seed=0;seed<32;seed++) {
        WBCSession s;memset(&s,0xa5,sizeof s);assert(!wbc_session_reset(&s,seed,4,16,1000+seed*10000));
        WFView v;WUCapabilities caps;observe(&s,&v,&caps);
        const WFNode *back=node(&v,"Back",WF_BUTTON);
        assert(!(back->flags&WF_ENABLED) && !capability(&caps,back->ref,WF_CLICK));
        rejected(&s,(WFAction){.kind=WF_CLICK,.target=back->ref,.elapsed_ms=UINT32_MAX});
        WFAction old=click(&s,"New tab",WF_BUTTON);rejected(&s,old);
        assert(s.browser.words[1]==2 && s.browser.words[2]==2);
        /* These two trusted entries stand in for page-link router callbacks;
         * numeric route commands are never included in policy capabilities. */
        assert(!wbc_session_command(&s,&(WBCCommand){.kind=WB_NAVIGATE,.argument=1}));settle(&s);
        assert(!wbc_session_command(&s,&(WBCCommand){.kind=WB_NAVIGATE,.argument=2}));settle(&s);
        click(&s,"Back",WF_BUTTON);settle(&s);address_is(&s,route_urls[1]);
        click(&s,"Forward",WF_BUTTON);settle(&s);address_is(&s,route_urls[2]);
        click(&s,"Reload",WF_BUTTON);settle(&s);address_is(&s,route_urls[2]);
        click(&s,"Blank",WF_TAB);assert(s.browser.words[2]==1);address_is(&s,"about:blank");
        click(&s,"Close tab",WF_BUTTON);assert(s.browser.words[2]==2);address_is(&s,route_urls[2]);
        click(&s,"Close tab",WF_BUTTON);assert(s.browser.words[2]==3);address_is(&s,"about:blank");
        observe(&s,&v,&caps);WFView before=v;WUCapabilities before_caps=caps;
        s.browser.words[6]^=0xa511e9b3u;s.browser.words[64+2]+=1000;s.browser.words[64+3]^=31;
        observe(&s,&v,&caps);assert(!memcmp(&before,&v,sizeof v) && !memcmp(&before_caps,&caps,sizeof caps));
        old=click(&s,"New tab",WF_BUTTON);
        old.text="x";old.text_length=1;rejected(&s,old);
        rejected(&s,(WFAction){.kind=WF_WAIT,.target=s.ref_base});
        rejected(&s,(WFAction){.kind=WF_CLICK,.target=s.ref_base,.arg0=1});
        rejected(&s,(WFAction){.kind=WF_INSERT,.target=s.ref_base});
    }
    WBCSession s;assert(!wbc_session_reset(&s,0,4,1,1000));
    WFView v;WUCapabilities caps;observe(&s,&v,&caps);
    const WFNode *new_tab=node(&v,"New tab",WF_BUTTON);
    assert(!(new_tab->flags&WF_ENABLED));rejected(&s,(WFAction){.kind=WF_CLICK,.target=new_tab->ref});
    s.browser.words[3]=UINT32_MAX;observe(&s,&v,&caps);
    assert(!(node(&v,"Close tab",WF_BUTTON)->flags&WF_ENABLED));
    s.ref_base=UINT32_MAX-WBC_REF_STRIDE;
    rejected(&s,(WFAction){.kind=WF_WAIT,.elapsed_ms=10});
    assert(!wbc_session_reset(&s,42,4,16,1000));
    for (unsigned i=1;i<16;i++) click(&s,"New tab",WF_BUTTON);
    s.browser.words[64+64*15+4]=WB_FAILED;
    WBCView public;assert(!wbc_observe(&s.browser,&public));
    char title[401],url[801];
    for (unsigned i=0;i<200;i++) {title[2*i]=(char)0xc3;title[2*i+1]=(char)0xa9;memcpy(url+4*i,"\xf0\x9f\x9a\x80",4);}
    title[400]=url[800]=0;const char *titles[16];for (unsigned i=0;i<16;i++) titles[i]=title;
    wf_view_init(&v,0,10000);assert(!wf_text_add(&v,"",0,&v.instruction));caps=(WUCapabilities){.version=WU_CAP_VERSION};
    caps.items[caps.count++]=(WUCapability){.kind=WF_WAIT,.step0=1,.step1=1};
    assert(!wbc_controls_append(&public,titles,url,1000,&v,&caps));
    assert(caps.items[0].kind==WF_WAIT && caps.items[0].step0==1 && caps.items[0].step1==1);
    assert(v.count==WBC_MAX_CONTROLS && v.text_bytes<WBC_CONTROL_TEXT_BUDGET && v.text_truncated && caps.incomplete);
    for (unsigned i=0;i<v.count;i++) {
        assert(!wt_decode(wf_text_get(&v,v.nodes[i].name),v.nodes[i].name.length,NULL,SIZE_MAX,NULL));
        assert(!wt_decode(wf_text_get(&v,v.nodes[i].value),v.nodes[i].value.length,NULL,SIZE_MAX,NULL));
    }
    WFView before=v;WUCapabilities before_caps=caps;
    assert(wbc_controls_append(&public,titles,url,1000,&v,&caps)<0 && !memcmp(&v,&before,sizeof v) && !memcmp(&caps,&before_caps,sizeof caps));
    for (unsigned mode=0;mode<4;mode++) {
        wf_view_init(&v,0,10000);wf_text_add(&v,"",0,&v.instruction);caps=(WUCapabilities){.version=WU_CAP_VERSION};
        if (mode==0) v.count=WF_MAX_NODES-WBC_MAX_CONTROLS+1;
        if (mode==1) v.text_bytes=WF_TEXT_BYTES;
        if (mode==2) caps.count=WU_MAX_CAPABILITIES;
        if (mode==3) titles[0]="\xc0\x80";
        before=v;before_caps=caps;
        assert(wbc_controls_append(&public,titles,url,1000,&v,&caps)<0);
        assert(!memcmp(&v,&before,sizeof v) && !memcmp(&caps,&before_caps,sizeof caps));
    }
    puts("PASS: public browser controls across 32 worlds, WF actions, stale/disabled rejection, retry/history/tab composition, timing independence, Unicode clipping and atomic append boundaries");
    return 0;
}
