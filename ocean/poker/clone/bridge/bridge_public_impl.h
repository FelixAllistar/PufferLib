#pragma once
/* Public-only reducer, separated from the prior retained-context source.
 * The new proof must independently compare it with a4b replay. No solver,
 * own cards, private receipt or terminal payout is included here.
 */
#include "clone_bridge.h"
static int64_t bb_minimum(int64_t a,int64_t b){return a<b?a:b;}
static int64_t bb_maximum(int64_t a,int64_t b){return a>b?a:b;}
static int64_t bb_stack(const BbPublic* s,int p){return p?s->stack1:s->stack0;}
static int64_t bb_initial(const BbPublic* s,int p){return p?s->initial1:s->initial0;}
static int64_t bb_total(const BbPublic* s,int p){return p?s->total1:s->total0;}
static int64_t bb_bet(const BbPublic* s,int p){return p?s->bet1:s->bet0;}
static int64_t bb_reopen(const BbPublic* s,int p){return p?s->reopen1:s->reopen0;}
static void bb_set_stack(BbPublic* s,int p,int64_t x){if(p)s->stack1=x;else s->stack0=x;}
static void bb_set_total(BbPublic* s,int p,int64_t x){if(p)s->total1=x;else s->total0=x;}
static void bb_set_bet(BbPublic* s,int p,int64_t x){if(p)s->bet1=x;else s->bet0=x;}
static void bb_set_reopen(BbPublic* s,int p,int64_t x){if(p)s->reopen1=x;else s->reopen0=x;}
int bb_public_valid(const BbPublic* s) {
    if(!s || (s->actor!=255 && (s->actor<0 || s->actor>1)) || s->street<0 || s->street>3
        || s->button<0 || s->button>1 || s->decisions<0 || s->decisions>65535
        || s->pot<=0 || s->pot>131070 || s->high<0 || s->high>65535
        || s->increment<=0 || s->increment>65535 || s->live<1 || s->live>3
        || s->active<0 || s->active>3 || s->pending<0 || s->pending>3
        || (s->active & ~s->live) || (s->pending & ~s->active)
        || s->raises<0 || s->raises>4 || (s->last_raiser!=255 && (s->last_raiser<0 || s->last_raiser>1))
        || s->board_count!=(s->street?s->street+2:0) || s->chips_per_bb!=POKER_BB) return 0;
    uint64_t board=0;
    for(int i=0;i<5;i++) {
        int64_t x=s->board[i];
        if(i>=s->board_count) {if(x!=-1)return 0;continue;}
        if(x<0 || x>=52 || (board&(UINT64_C(1)<<x)))return 0;
        board|=UINT64_C(1)<<x;
    }
    int active=0;
    for(int p=0;p<2;p++) {
        if(bb_initial(s,p)<1 || bb_initial(s,p)>65535 || bb_stack(s,p)<0 || bb_stack(s,p)>65535
            || bb_total(s,p)<0 || bb_total(s,p)>65535 || bb_bet(s,p)<0 || bb_bet(s,p)>bb_total(s,p)
            || (s->actor!=255 && bb_bet(s,p)>s->high) || bb_stack(s,p)+bb_total(s,p)!=bb_initial(s,p)
            || bb_reopen(s,p)<0 || bb_reopen(s,p)>131070 || s->line[p]<0 || s->line[p]>255) return 0;
        if((s->live&(1<<p)) && bb_stack(s,p)>0)active|=1<<p;
    }
    if(active!=s->active || s->total0+s->total1!=s->pot)return 0;
    if(s->actor==255)return s->pending==0;
    if(s->live!=3 || !(s->pending&(1<<s->actor)) || bb_stack(s,(int)s->actor)<=0)return 0;
    if(s->high!=bb_maximum(s->bet0,s->bet1))return 0;
    return 1;
}
static int bb_can_raise_public(const BbPublic* s) {
    int p=(int)s->actor,q=1-p;
    return s->high>=bb_reopen(s,p) && bb_bet(s,p)+bb_stack(s,p)>s->high
        && (s->active&(1<<q)) && bb_bet(s,q)+bb_stack(s,q)>s->high;
}
static int bb_public_money_next(const BbPublic* b,int32_t action,BbPublic* a) {
    if(!a || !bb_public_valid(b) || b->actor==255 || b->decisions==65535 || action< -1)return 0;
    int p=(int)b->actor,q=1-p;int64_t call=b->high-bb_bet(b,p),target=action?action:b->high;
    if(action<0 && call<=0)return 0;
    if(action>0 && (!bb_can_raise_public(b) || target<=b->high
        || target>bb_bet(b,p)+bb_stack(b,p)
        || (target<b->high+b->increment && target!=bb_bet(b,p)+bb_stack(b,p))))return 0;
    *a=*b;a->decisions++;a->pending&=~(INT64_C(1)<<p);
    if(action<0) {a->live&=~(INT64_C(1)<<p);a->active&=~(INT64_C(1)<<p);}
    else {
        int kind=action?3:call?2:1,shift=2*(int)b->street;
        if(kind>((a->line[p]>>shift)&3))a->line[p]=(a->line[p]&~(INT64_C(3)<<shift))|((int64_t)kind<<shift);
        int64_t pay=bb_minimum(target-bb_bet(b,p),bb_stack(b,p));
        bb_set_stack(a,p,bb_stack(b,p)-pay);bb_set_total(a,p,bb_total(b,p)+pay);bb_set_bet(a,p,bb_bet(b,p)+pay);a->pot+=pay;
        if(!bb_stack(a,p))a->active&=~(INT64_C(1)<<p);
        if(target>b->high) {
            int64_t inc=target-b->high;
            if(inc>=a->increment){a->increment=inc;if(a->raises<4)a->raises++;a->last_raiser=p;}
            a->high=target;a->pending=a->active&~(INT64_C(1)<<p);
        }
        bb_set_reopen(a,p,a->high+a->increment);
    }
    if(a->live!=3) {a->actor=255;a->pending=0;}
    else {
        a->pending&=a->active;
        if(a->active==1 || a->active==2) {
            int live_actor=a->active==1?0:1;
            a->high=bb_bet(a,1-live_actor);
            if(bb_bet(a,live_actor)>=a->high)a->pending=0;
        }
        if(a->pending)a->actor=(a->pending&(1<<q))?q:p;
        else if(a->street==3 || a->active!=3) {a->actor=255;a->pending=0;a->street=3;}
        else {
            a->street++;a->raises=0;a->last_raiser=255;a->high=0;a->increment=POKER_BB;
            a->bet0=a->bet1=a->reopen0=a->reopen1=0;a->pending=a->active;a->actor=1-a->button;
        }
    }
    return 1;
}
int bb_public_next(const BbPublic* b,int32_t action,const int64_t board[5],int count,BbPublic* a) {
    if(!board || !bb_public_money_next(b,action,a))return 0;
    a->board_count=a->street?a->street+2:0;
    if(count!=a->board_count || count<b->board_count)return 0;
    for(int i=0;i<5;i++) {
        if(i<b->board_count && board[i]!=b->board[i])return 0;
        a->board[i]=i<count?board[i]:-1;
    }
    return bb_public_valid(a);
}
int bb_public_transition(const BbPublic* b,int32_t action,const BbPublic* a) {
    BbPublic expected;
    return a && bb_public_next(b,action,a->board,(int)a->board_count,&expected)
        && !memcmp(&expected,a,sizeof(expected));
}
int bb_public_from_game(const void* ptr,int lane,BbPublic* o) {
    const Poker* g=ptr;if(!g || !o || lane<0 || lane>=POKER_BATCH || g->actor[lane]==255)return 0;
    memset(o,0,sizeof(*o));
    o->actor=g->actor[lane];o->street=g->street[lane];o->button=g->button[lane];o->decisions=g->decisions[lane];
    o->pot=g->pot[lane];o->high=g->high[lane];o->increment=g->increment[lane];o->live=g->live[lane];
    o->active=g->active[lane];o->pending=g->pending[lane];o->raises=g->raises[lane];o->last_raiser=g->last_raiser[lane];
    o->board_count=o->street?o->street+2:0;
    for(int i=0;i<5;i++)o->board[i]=i<o->board_count?g->board[i][lane]:-1;
    o->line[0]=g->line[0][lane];o->line[1]=g->line[1][lane];
    o->stack0=g->stack[0][lane];o->initial0=g->initial[0][lane];o->total0=g->total[0][lane];o->bet0=g->bet[0][lane];o->reopen0=g->reopen[0][lane];
    o->stack1=g->stack[1][lane];o->initial1=g->initial[1][lane];o->total1=g->total[1][lane];o->bet1=g->bet[1][lane];o->reopen1=g->reopen[1][lane];
    o->chips_per_bb=POKER_BB;return bb_public_valid(o);
}
