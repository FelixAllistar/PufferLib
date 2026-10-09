#include "browser_contexts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

/* Independent oracle: each context has a chronological visit log/cursor and
 * an absolute completion deadline. The implementation has stacks/counters. */
typedef struct {
    uint32_t id, visits[9], length, cursor, latency, failures, phase;
    uint64_t due;
} Tab;
typedef struct {
    Tab tabs[16];
    uint32_t count, active, next, capacity, routes, seed;
    uint64_t now;
} Oracle;
static void create(Oracle *o,uint32_t route,int foreground) {
    uint32_t id=o->next++,salt=o->seed^(id*2654435769u);
    Tab *t=o->tabs+o->count++;
    *t=(Tab){.id=id,.visits={route},.length=1,
        .latency=25+(salt^2246822507u)%7*25,
        .failures=(salt^3266489909u)%3,.phase=route?WB_LOADING:WB_READY};
    if (route) {t->visits[0]=0;t->visits[1]=route;t->length=2;t->cursor=1;}
    t->due=o->now+t->latency;
    if (foreground) o->active=id;
}
static unsigned index_of(const Oracle *o,uint32_t id) {
    for (unsigned i=0;i<o->count;i++) if (o->tabs[i].id==id) return i;
    assert(0);return 0;
}
static void reference(Oracle *o,unsigned command,uint32_t argument,unsigned foreground,uint32_t delta) {
    o->now+=delta;
    for (unsigned i=0;i<o->count;i++) {
        Tab *t=o->tabs+i;
        if (t->phase==WB_LOADING && o->now>=t->due) {
            t->phase=t->failures?WB_FAILED:WB_READY;
            if (t->failures) t->failures--;
        }
    }
    if (command==WBC_OPEN) {create(o,argument,foreground);return;}
    if (command==WBC_SWITCH) {o->active=argument;return;}
    if (command==WBC_CLOSE) {
        unsigned i=index_of(o,argument);
        memmove(o->tabs+i,o->tabs+i+1,(o->count-i-1)*sizeof(Tab));o->count--;
        if (!o->count) create(o,0,1);
        else if (o->active==argument) o->active=o->tabs[o->count-1].id;
        return;
    }
    Tab *t=o->tabs+index_of(o,o->active);int load=0;
    if (command==WB_NAVIGATE && argument!=t->visits[t->cursor]) {
        t->length=t->cursor+1;
        if (t->length==9) {
            memmove(t->visits,t->visits+1,8*sizeof(uint32_t));t->length--;
        }
        t->visits[t->length++]=argument;t->cursor=t->length-1;load=1;
    } else if (command==WB_NAVIGATE) load=1;
    else if (command==WB_BACK && t->cursor) {t->cursor--;load=1;}
    else if (command==WB_FORWARD && t->cursor+1<t->length) {t->cursor++;load=1;}
    else if (command==WB_RETRY && t->phase==WB_FAILED) load=1;
    else if (command==WB_RELOAD) load=1;
    if (load) {t->phase=WB_LOADING;t->due=o->now+t->latency;}
}
static void compare(const WBCState *s,const Oracle *o) {
    assert(!wbc_validate(s));const uint32_t *w=s->words;
    assert(w[1]==o->count && w[2]==o->active && w[3]==o->next);
    assert(w[4]==o->capacity && w[5]==o->routes && w[6]==o->seed);
    WBCView view;assert(!wbc_observe(s,&view));
    assert(view.count==o->count && view.active==o->active);
    assert(view.can_open==(o->count<o->capacity && o->next<UINT32_MAX));
    assert(view.can_close==(o->count>1 || o->next<UINT32_MAX));
    for (unsigned i=0;i<o->count;i++) {
        const Tab *t=o->tabs+i;const uint32_t *r=w+64+64*i;
        assert(r[32]==t->id && r[0]==t->visits[t->cursor]);
        assert(r[1]==o->routes && r[2]==t->latency && r[3]==t->failures && r[4]==t->phase);
        assert(r[5]==(t->phase==WB_LOADING?t->due-o->now:0));
        assert(r[6]==t->cursor && r[7]==t->length-t->cursor-1);
        for (unsigned j=0;j<r[6];j++) assert(r[16+j]==t->visits[t->cursor-j-1]);
        for (unsigned j=0;j<r[7];j++) assert(r[24+j]==t->visits[t->cursor+j+1]);
        WBCTabView expected={t->id,{t->visits[t->cursor],t->phase,t->cursor!=0,t->cursor+1<t->length}};
        assert(!memcmp(view.tabs+i,&expected,sizeof expected));
    }
    for (unsigned i=o->count;i<WBC_MAX_TABS;i++) {
        WBCTabView zero={0};assert(!memcmp(view.tabs+i,&zero,sizeof zero));
    }
}
static uint32_t random_word(uint32_t *seed) {
    *seed^=*seed<<13;*seed^=*seed>>17;*seed^=*seed<<5;return *seed;
}
static void step(WBCState *s,Oracle *o,unsigned cmd,uint32_t arg,unsigned fg,uint32_t delta) {
    reference(o,cmd,arg,fg,delta);assert(!wbc_step(s,cmd,arg,fg,delta));compare(s,o);
}
static void rejected(WBCState *s,unsigned cmd,uint32_t arg,unsigned fg) {
    WBCState before=*s;
    assert(wbc_step(s,cmd,arg,fg,UINT32_MAX)<0 && !memcmp(s,&before,sizeof before));
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned checks=0,actions[9]={0},timings[7]={0},failure_counts[3]={0};
    for (uint32_t seed=0;seed<64;seed++) {
        WBCState s;memset(&s,0xa5,sizeof s);
        Oracle o={.next=1,.capacity=1+seed%16,.routes=1+seed%32,.seed=seed};
        create(&o,0,1);assert(!wbc_reset(&s,seed,o.routes,o.capacity));compare(&s,&o);
        timings[(o.tabs[0].latency-25)/25]++;failure_counts[o.tabs[0].failures]++;
        uint32_t rng=seed+1;
        for (unsigned i=0;i<500;i++) {
            unsigned cmd=random_word(&rng)%9,fg=0;uint32_t arg=0;
            if (cmd==WBC_OPEN && o.count==o.capacity) cmd=WBC_CLOSE;
            if (cmd==WB_NAVIGATE || cmd==WBC_OPEN) arg=random_word(&rng)%o.routes;
            if (cmd==WBC_OPEN) fg=random_word(&rng)%2;
            if (cmd==WBC_SWITCH || cmd==WBC_CLOSE) arg=o.tabs[random_word(&rng)%o.count].id;
            uint32_t delta=i%31==0?UINT32_MAX:random_word(&rng)%201;
            step(&s,&o,cmd,arg,fg,delta);checks++;actions[cmd]++;
        }
        rejected(&s,99,0,0);rejected(&s,WBC_SWITCH,o.next,0);rejected(&s,WBC_CLOSE,0,0);
        rejected(&s,WB_NAVIGATE,o.routes,0);rejected(&s,WBC_OPEN,o.routes,1);
        rejected(&s,WB_WAIT,1,0);rejected(&s,WB_RELOAD,0,1);rejected(&s,WBC_OPEN,0,2);
        WBCState before=s;
        assert(wbc_reset(&s,0,0,1)<0 && !memcmp(&s,&before,sizeof s));
        assert(wbc_reset(&s,0,33,1)<0 && !memcmp(&s,&before,sizeof s));
        assert(wbc_reset(&s,0,4,0)<0 && !memcmp(&s,&before,sizeof s));
        assert(wbc_reset(&s,0,4,17)<0 && !memcmp(&s,&before,sizeof s));
        WBCView a,b;assert(!wbc_observe(&s,&a));
        s.words[6]^=0x9e3779b9u;
        for (unsigned i=0;i<s.words[1];i++) {
            uint32_t *r=s.words+64+64*i;r[2]+=1000;r[3]^=31;
        }
        assert(!wbc_observe(&s,&b) && !memcmp(&a,&b,sizeof a));
    }
    for (unsigned i=0;i<9;i++) assert(actions[i]);
    for (unsigned i=0;i<7;i++) assert(timings[i]);
    for (unsigned i=0;i<3;i++) assert(failure_counts[i]);
    WBCState s;Oracle o={.next=1,.capacity=16,.routes=4,.seed=42};
    create(&o,0,1);assert(!wbc_reset(&s,42,4,16));
    step(&s,&o,WB_NAVIGATE,1,0,0);
    step(&s,&o,WBC_OPEN,2,0,0);step(&s,&o,WBC_OPEN,3,1,0);
    step(&s,&o,WB_WAIT,0,0,UINT32_MAX);
    assert(s.words[64+4]!=WB_LOADING && s.words[128+4]!=WB_LOADING);
    step(&s,&o,WBC_SWITCH,1,0,0);step(&s,&o,WBC_CLOSE,1,0,0);
    assert(o.active==3);rejected(&s,WBC_SWITCH,1,0);
    step(&s,&o,WBC_CLOSE,2,0,0);assert(o.active==3);
    step(&s,&o,WBC_CLOSE,3,0,0);assert(o.count==1 && o.active==4);
    assert(s.words[64]==0 && s.words[64+6]==0 && s.words[64+7]==0 && s.words[64+4]==WB_READY);
    while (o.count<o.capacity) step(&s,&o,WBC_OPEN,o.count%4,0,0);
    rejected(&s,WBC_OPEN,0,1);
    WBCState bad=s;bad.words[128+32]=bad.words[64+32];assert(wbc_validate(&bad)<0);
    bad=s;bad.words[2]=0;assert(wbc_validate(&bad)<0);
    bad=s;bad.words[64+1]=5;assert(wbc_validate(&bad)<0);
    bad=s;bad.words[64+6]=9;assert(wbc_validate(&bad)<0);
    bad=s;bad.words[2047]=1;assert(wbc_validate(&bad)<0);
    assert(!wbc_reset(&s,UINT32_MAX,32,1));s.words[3]=UINT32_MAX;
    rejected(&s,WBC_OPEN,1,1);rejected(&s,WBC_CLOSE,1,0);
    WBCView v;assert(!wbc_observe(&s,&v) && !v.can_open && !v.can_close);
    assert(!wbc_step(&s,WB_NAVIGATE,31,0,0));
    assert(!wbc_reset(&s,42,4,16));s.words[3]=UINT32_MAX-1;
    assert(!wbc_step(&s,WBC_OPEN,3,1,0));
    assert(s.words[2]==UINT32_MAX-1 && s.words[3]==UINT32_MAX);
    rejected(&s,WBC_OPEN,1,1);
    assert(!wbc_step(&s,WBC_CLOSE,UINT32_MAX-1,0,0) && s.words[2]==1);
    printf("PASS: browser contexts, %u differential steps, 64 dirty resets, independent histories/background loads, close/replacement, capacity/identity limits, atomic rejection and private timing independence\n",checks);
    return 0;
}
