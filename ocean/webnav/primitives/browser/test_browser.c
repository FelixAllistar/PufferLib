#include "browser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

/* Independent reference uses a visit log/cursor and absolute event deadline;
 * the Bend model uses back/forward stacks and a remaining-time counter. */
typedef struct {
    unsigned visits[9], length, cursor, count, latency, failures, phase;
    uint64_t now, due;
} Oracle;
static void compare(const WBState *state,const Oracle *o) {
    const uint32_t *r=state->words;
    assert(!wb_validate(state));
    assert(r[0]==o->visits[o->cursor] && r[1]==o->count && r[2]==o->latency);
    assert(r[3]==o->failures && r[4]==o->phase);
    assert(r[5]==(o->phase==WB_LOADING?o->due-o->now:0));
    assert(r[6]==o->cursor && r[7]==o->length-o->cursor-1);
    for (unsigned i=0;i<r[6];i++) assert(r[16+i]==o->visits[o->cursor-i-1]);
    for (unsigned i=0;i<r[7];i++) assert(r[24+i]==o->visits[o->cursor+i+1]);
    for (unsigned i=r[6];i<8;i++) assert(r[16+i]==0);
    for (unsigned i=r[7];i<8;i++) assert(r[24+i]==0);
    WBView v;assert(!wb_observe(state,&v));
    assert(v.route==o->visits[o->cursor] && v.phase==o->phase);
    assert(v.can_back==(o->cursor!=0) && v.can_forward==(o->cursor+1<o->length));
}
static void reference(Oracle *o,unsigned command,unsigned route,uint32_t delta) {
    o->now+=delta;
    if (o->phase==WB_LOADING && o->now>=o->due) {
        o->phase=o->failures?WB_FAILED:WB_READY;
        if (o->failures) o->failures--;
    }
    int load=0;
    if (command==WB_NAVIGATE && route!=o->visits[o->cursor]) {
        o->length=o->cursor+1;
        if (o->length==9) {
            memmove(o->visits,o->visits+1,8*sizeof o->visits[0]);o->length--;
        }
        o->visits[o->length++]=route;o->cursor=o->length-1;load=1;
    } else if (command==WB_NAVIGATE) load=1;
    else if (command==WB_BACK && o->cursor) {o->cursor--;load=1;}
    else if (command==WB_FORWARD && o->cursor+1<o->length) {o->cursor++;load=1;}
    else if (command==WB_RETRY && o->phase==WB_FAILED) load=1;
    else if (command==WB_RELOAD) load=1;
    if (load) {o->phase=WB_LOADING;o->due=o->now+o->latency;}
}
static uint32_t random_word(uint32_t *seed) {
    *seed=*seed*1664525u+1013904223u;return *seed;
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned checks=0;
    for (unsigned seed=0;seed<32;seed++) {
        WBState s;memset(&s,0xa5,sizeof s);assert(!wb_reset(&s,seed));
        Oracle o={.length=1,.count=2+seed%7,.latency=25+(seed^2654435769u)%7*25,
            .failures=(seed^2246822507u)%3};
        compare(&s,&o);
        for (unsigned i=32;i<64;i++) assert(s.words[i]==0);
        uint32_t rng=seed+1;
        for (unsigned step=0;step<300;step++) {
            unsigned cmd=random_word(&rng)%6;
            unsigned route=cmd==WB_NAVIGATE?random_word(&rng)%o.count:0;
            uint32_t delta=step%37==0?UINT32_MAX:random_word(&rng)%201;
            reference(&o,cmd,route,delta);assert(!wb_step(&s,cmd,route,delta));
            compare(&s,&o);checks++;
        }
        WBState before=s;
        assert(wb_step(&s,WB_NAVIGATE,o.count,100)<0 && !memcmp(&before,&s,sizeof s));
        assert(wb_step(&s,99,0,100)<0 && !memcmp(&before,&s,sizeof s));
        assert(wb_step(&s,WB_WAIT,1,100)<0 && !memcmp(&before,&s,sizeof s));
        WBView a,b;assert(!wb_observe(&s,&a));
        s.words[3]^=15;s.words[2]+=1000;
        assert(!wb_observe(&s,&b) && !memcmp(&a,&b,sizeof a));
    }
    WBState s;assert(!wb_reset(&s,0));
    for (unsigned i=0;i<12;i++) assert(!wb_step(&s,WB_NAVIGATE,i%2,0));
    assert(s.words[6]==8);
    for (unsigned i=0;i<8;i++) assert(!wb_step(&s,WB_BACK,0,0));
    assert(s.words[6]==0 && s.words[7]==8);
    uint32_t forward[8];memcpy(forward,s.words+24,sizeof forward);
    unsigned route=s.words[0];
    assert(!wb_step(&s,WB_RELOAD,0,0));
    assert(s.words[0]==route && s.words[6]==0 && s.words[7]==8);
    assert(!memcmp(forward,s.words+24,sizeof forward));
    assert(s.words[4]==WB_LOADING && s.words[5]==s.words[2]);
    assert(!wb_step(&s,WB_NAVIGATE,route,0));
    assert(s.words[6]==0 && s.words[7]==8 && !memcmp(forward,s.words+24,sizeof forward));
    assert(!wb_step(&s,WB_NAVIGATE,route^1u,0) && s.words[7]==0);
    assert(!wb_step(&s,WB_WAIT,0,UINT32_MAX));
    for (unsigned i=0;i<4 && s.words[4]!=WB_READY;i++) {
        assert(!wb_step(&s,WB_RETRY,0,0));
        assert(!wb_step(&s,WB_WAIT,0,UINT32_MAX));
    }
    assert(s.words[4]==WB_READY);
    printf("PASS: browser primitive, %u differential steps, history cap, retry, reload, reset, atomic rejection and public projection\n",checks);
    return 0;
}
