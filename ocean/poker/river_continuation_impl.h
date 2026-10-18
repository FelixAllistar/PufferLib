#pragma once
#if POKER_PLAYERS != 2 || !POKER_RIVER_CFR
#error "Retained river context requires explicit HU CFR build"
#endif
#include "river_continuation.h"
#include "pc_sha256.h"
#ifndef PC_CREATE
#define PC_CREATE rcfr_create
#endif
#ifndef PC_ITERATE
#define PC_ITERATE rcfr_iterate
#endif
#ifndef PC_CERTIFICATE
#define PC_CERTIFICATE rcfr_certificate
#endif
_Static_assert(sizeof(PcPublic)==31*sizeof(int64_t),"PcPublic transport layout");
_Static_assert(sizeof(double)==sizeof(uint64_t),"Float audit format");

struct PcContext {
    unsigned char owner[32],prefix_hash[32],anchor_hash[32],strategy_hash[32];
    uint64_t generation,events,requests,creates,iterate_calls,iterations,anchors,hits,fallbacks;
    uint64_t pending_request,pending_rng_before,pending_rng_after;
    int seat,policy,samples,phase,started,own_bound,own[2],node,checkpoint,solve_status;
    int pending,end_reason,profile_mode;int32_t pending_action;
    PcPublic state,pending_state;
    RcfrGame* game;RcfrCertificate certificate;
    char error[160];
};

static void pc_sha_public(PcSha256* s,const PcPublic* p) {
    int64_t words[31];memcpy(words,p,sizeof(words));
    for(int i=0;i<31;i++)pc_sha_u64(s,(uint64_t)words[i]);
}
static void pc_public_hash(const PcPublic* p,unsigned char out[32]) {
    PcSha256 s;pc_sha_domain(&s,"poker-continuation-public-v1");pc_sha_public(&s,p);pc_sha_finish(&s,out);
}
static int pc_error(PcContext* c,const char* text) {
    if(c) { c->phase=PC_ERROR;snprintf(c->error,sizeof(c->error),"%s",text); }
    return 0;
}
static int64_t pc_stack(const PcPublic* s,int p){return p?s->stack1:s->stack0;}
static int64_t pc_initial(const PcPublic* s,int p){return p?s->initial1:s->initial0;}
static int64_t pc_total(const PcPublic* s,int p){return p?s->total1:s->total0;}
static int64_t pc_bet(const PcPublic* s,int p){return p?s->bet1:s->bet0;}
static int64_t pc_reopen(const PcPublic* s,int p){return p?s->reopen1:s->reopen0;}
static void pc_set_stack(PcPublic* s,int p,int64_t x){if(p)s->stack1=x;else s->stack0=x;}
static void pc_set_total(PcPublic* s,int p,int64_t x){if(p)s->total1=x;else s->total0=x;}
static void pc_set_bet(PcPublic* s,int p,int64_t x){if(p)s->bet1=x;else s->bet0=x;}
static void pc_set_reopen(PcPublic* s,int p,int64_t x){if(p)s->reopen1=x;else s->reopen0=x;}
static int pc_public_valid(const PcPublic* s) {
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
        if(pc_initial(s,p)<1 || pc_initial(s,p)>65535 || pc_stack(s,p)<0 || pc_stack(s,p)>65535
            || pc_total(s,p)<0 || pc_total(s,p)>65535 || pc_bet(s,p)<0 || pc_bet(s,p)>pc_total(s,p)
            || (s->actor!=255 && pc_bet(s,p)>s->high) || pc_stack(s,p)+pc_total(s,p)!=pc_initial(s,p)
            || pc_reopen(s,p)<0 || pc_reopen(s,p)>131070 || s->line[p]<0 || s->line[p]>255) return 0;
        if((s->live&(1<<p)) && pc_stack(s,p)>0)active|=1<<p;
    }
    if(active!=s->active || s->total0+s->total1!=s->pot)return 0;
    if(s->actor==255)return s->pending==0;
    if(s->live!=3 || !(s->pending&(1<<s->actor)) || pc_stack(s,(int)s->actor)<=0)return 0;
    if(s->high!=maximum(s->bet0,s->bet1))return 0;
    return 1;
}
static int pc_can_raise_public(const PcPublic* s) {
    int p=(int)s->actor,q=1-p;
    return s->high>=pc_reopen(s,p) && pc_bet(s,p)+pc_stack(s,p)>s->high
        && (s->active&(1<<q)) && pc_bet(s,q)+pc_stack(s,q)>s->high;
}
int pc_public_next(const PcPublic* b,int32_t action,const int64_t board[5],int count,PcPublic* a) {
    if(!a || !board || !pc_public_valid(b) || b->actor==255 || b->decisions==65535 || action< -1)return 0;
    int p=(int)b->actor,q=1-p;int64_t call=b->high-pc_bet(b,p),target=action?action:b->high;
    if(action<0 && call<=0)return 0;
    if(action>0 && (!pc_can_raise_public(b) || target<=b->high
        || target>pc_bet(b,p)+pc_stack(b,p)
        || (target<b->high+b->increment && target!=pc_bet(b,p)+pc_stack(b,p))))return 0;
    *a=*b;a->decisions++;a->pending&=~(INT64_C(1)<<p);
    if(action<0) {a->live&=~(INT64_C(1)<<p);a->active&=~(INT64_C(1)<<p);}
    else {
        int kind=action?3:call?2:1,shift=2*(int)b->street;
        if(kind>((a->line[p]>>shift)&3))a->line[p]=(a->line[p]&~(INT64_C(3)<<shift))|((int64_t)kind<<shift);
        int64_t pay=minimum(target-pc_bet(b,p),pc_stack(b,p));
        pc_set_stack(a,p,pc_stack(b,p)-pay);pc_set_total(a,p,pc_total(b,p)+pay);pc_set_bet(a,p,pc_bet(b,p)+pay);a->pot+=pay;
        if(!pc_stack(a,p))a->active&=~(INT64_C(1)<<p);
        if(target>b->high) {
            int64_t inc=target-b->high;
            if(inc>=a->increment){a->increment=inc;if(a->raises<4)a->raises++;a->last_raiser=p;}
            a->high=target;a->pending=a->active&~(INT64_C(1)<<p);
        }
        pc_set_reopen(a,p,a->high+a->increment);
    }
    if(a->live!=3) {a->actor=255;a->pending=0;}
    else {
        a->pending&=a->active;
        if(a->active==1 || a->active==2) {
            int live_actor=a->active==1?0:1;
            a->high=pc_bet(a,1-live_actor);
            if(pc_bet(a,live_actor)>=a->high)a->pending=0;
        }
        if(a->pending)a->actor=(a->pending&(1<<q))?q:p;
        else if(a->street==3 || a->active!=3) {a->actor=255;a->pending=0;a->street=3;}
        else {
            a->street++;a->raises=0;a->last_raiser=255;a->high=0;a->increment=POKER_BB;
            a->bet0=a->bet1=a->reopen0=a->reopen1=0;a->pending=a->active;a->actor=1-a->button;
        }
    }
    a->board_count=a->street?a->street+2:0;
    if(count!=a->board_count || count<b->board_count)return 0;
    for(int i=0;i<5;i++) {
        if(i<b->board_count && board[i]!=b->board[i])return 0;
        a->board[i]=i<count?board[i]:-1;
    }
    return pc_public_valid(a);
}
int pc_public_transition(const PcPublic* b,int32_t action,const PcPublic* a) {
    PcPublic expected;
    return a && pc_public_next(b,action,a->board,(int)a->board_count,&expected)
        && !memcmp(&expected,a,sizeof(expected));
}
int pc_public_from_game(const void* ptr,int lane,PcPublic* o) {
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
    o->chips_per_bb=POKER_BB;return pc_public_valid(o);
}
PcContext* pc_create(const unsigned char owner[32],int seat) {
    if(!owner || seat<0 || seat>1)return NULL;
    PcContext* c=calloc(1,sizeof(*c));if(c){memcpy(c->owner,owner,32);c->seat=seat;c->node=-1;}return c;
}
void pc_free(PcContext* c){if(c){rcfr_free(c->game);memset(c,0,sizeof(*c));free(c);}}
int pc_envelope(PcContext* c,const unsigned char owner[32],uint64_t gen) {
    if(!c)return 0;
    if(!owner || memcmp(c->owner,owner,32) || !gen || gen!=c->generation)return pc_error(c,"owner or generation envelope mismatch");
    if(c->phase==PC_UNSTARTED || c->phase==PC_ENDED || c->phase==PC_ERROR)return pc_error(c,"closed or unstarted context");
    return 1;
}
int pc_closed_envelope(PcContext* c,const unsigned char owner[32],uint64_t gen) {
    if(!c)return 0;
    if(!owner || memcmp(c->owner,owner,32) || !gen || gen!=c->generation)
        return pc_error(c,"closed receipt owner or generation mismatch");
    if(c->phase!=PC_ENDED || c->end_reason!=PC_TERMINAL || !c->started || c->state.actor!=255)
        return pc_error(c,"no closed terminal receipt");
    return 1;
}
int pc_closed_receipt(PcContext* c,const unsigned char owner[32],uint64_t gen,const PcPublic* state,int a,int b) {
    if(!pc_closed_envelope(c,owner,gen))return 0;
    if(a>b){int t=a;a=b;b=t;}
    if(!state || memcmp(&c->state,state,sizeof(*state)) || !c->own_bound || a!=c->own[0] || b!=c->own[1])
        return pc_error(c,"closed terminal state or own holding changed");
    return 1;
}
int pc_begin(PcContext* c,const unsigned char owner[32],uint64_t gen,int policy,int samples) {
    if(!c)return 0;
    if(!owner || memcmp(c->owner,owner,32) || !gen || gen<=c->generation)
        return pc_error(c,"new hand requires same owner and strictly increasing generation");
    if(c->phase!=PC_UNSTARTED && c->phase!=PC_ENDED && c->phase!=PC_ERROR)return pc_error(c,"begin cannot erase an open hand");
    if(!poker_bot_supported(policy) || (samples!=0 && samples!=32 && samples!=64 && samples!=128))return pc_error(c,"unsupported policy configuration");
    rcfr_free(c->game);c->game=NULL;int seat=c->seat;unsigned char identity[32];memcpy(identity,c->owner,32);
    memset(c,0,sizeof(*c));memcpy(c->owner,identity,32);c->seat=seat;c->generation=gen;c->policy=policy;c->samples=samples;
    c->phase=PC_UNANCHORED;c->node=-1;c->profile_mode=-1;return 1;
}
int pc_start(PcContext* c,const unsigned char owner[32],uint64_t gen,const PcPublic* initial) {
    if(!pc_envelope(c,owner,gen))return 0;
    if(c->started || !pc_public_valid(initial) || initial->actor==255)return pc_error(c,"invalid or repeated public start");
    if(c->own_bound)for(int i=0;i<initial->board_count;i++)
        if(initial->board[i]==c->own[0] || initial->board[i]==c->own[1])return pc_error(c,"initial board collides with own holding");
    c->state=*initial;c->started=1;
    PcSha256 hash;pc_sha_domain(&hash,"poker-continuation-prefix-start-v1");pc_sha_public(&hash,initial);pc_sha_finish(&hash,c->prefix_hash);
    return 1;
}
int pc_own(PcContext* c,const unsigned char owner[32],uint64_t gen,int a,int b) {
    if(!pc_envelope(c,owner,gen))return 0;
    if(a<0 || b<0 || a>=52 || b>=52 || a==b)return pc_error(c,"invalid own holding");
    if(a>b){int t=a;a=b;b=t;}
    if(c->own_bound && (a!=c->own[0] || b!=c->own[1]))return pc_error(c,"own holding changed within hand");
    if(c->started)for(int i=0;i<c->state.board_count;i++)
        if(c->state.board[i]==a || c->state.board[i]==b)return pc_error(c,"own holding collides with public board");
    c->own[0]=a;c->own[1]=b;c->own_bound=1;return 1;
}
static int pc_rcfr_state_matches(const RcfrPublic* r,const PcPublic* s) {
    return r->actor==(s->actor==255?-1:s->actor) && r->pending==s->pending && r->pot==s->pot
        && r->high==s->high && r->increment==s->increment && r->chips_per_bb==s->chips_per_bb
        && r->total[0]==s->total0 && r->total[1]==s->total1 && r->bet[0]==s->bet0 && r->bet[1]==s->bet1
        && r->stack[0]==s->stack0 && r->stack[1]==s->stack1 && r->reopen[0]==s->reopen0 && r->reopen[1]==s->reopen1;
}
int pc_observe(PcContext* c,const unsigned char owner[32],uint64_t gen,const PcPublic* b,int32_t action,const PcPublic* a) {
    if(!pc_envelope(c,owner,gen))return 0;
    if(!c->started || !c->own_bound || !b || memcmp(b,&c->state,sizeof(*b)) || !pc_public_transition(b,action,a))
        return pc_error(c,"public event or transition mismatch");
    for(int i=0;i<a->board_count;i++)if(a->board[i]==c->own[0] || a->board[i]==c->own[1])return pc_error(c,"new board collides with own holding");
    if(b->actor==c->seat && (!c->pending || c->pending_action!=action || memcmp(&c->pending_state,b,sizeof(*b))))
        return pc_error(c,"own event does not acknowledge pending action");
    if(b->actor!=c->seat && c->pending)return pc_error(c,"opponent event before own acknowledgement");
    if(c->phase==PC_FOLLOWING) {
        RcfrNodeInfo n;if(!rcfr_node_info(c->game,c->node,&n) || n.kind || !pc_rcfr_state_matches(&n.state,b))
            return pc_error(c,"retained node does not match before state");
        int index=-1;for(int k=0;k<n.action_count;k++)if(n.action[k]==action)index=k;
        if(index<0)c->phase=PC_DISABLED;
        else {
            RcfrNodeInfo child;if(!rcfr_node_info(c->game,n.child[index],&child) || !pc_rcfr_state_matches(&child.state,a))
                return pc_error(c,"retained child does not match checked public transition");
            c->node=n.child[index];
        }
    }
    PcSha256 hash;pc_sha_domain(&hash,"poker-continuation-prefix-event-v1");pc_sha_add(&hash,c->prefix_hash,32);
    pc_sha_u64(&hash,c->events);pc_sha_public(&hash,b);pc_sha_u64(&hash,(uint64_t)(int64_t)action);pc_sha_public(&hash,a);pc_sha_finish(&hash,c->prefix_hash);
    c->events++;c->state=*a;
    if(b->actor==c->seat)c->pending=0;
    return 1;
}
static int pc_validate_averages(const RcfrGame* game) {
    double values[3*POKER_CFR_HANDS];
    for(int node=0;node<rcfr_node_count(game);node++) {
        RcfrNodeInfo n;if(!rcfr_node_info(game,node,&n))return 0;
        if(n.kind)continue;
        if(n.action_count<1 || n.action_count>3 || !rcfr_average(game,node,values))return 0;
        for(int a=0;a<n.action_count;a++)if(n.action[a]<-1 || n.action[a]>INT32_MAX)return 0;
        for(int h=0;h<POKER_CFR_HANDS;h++) {
            double sum=0;
            for(int a=0;a<n.action_count;a++){double v=values[a*POKER_CFR_HANDS+h];if(!isfinite(v)||v<0||v>1)return 0;sum+=v;}
            if(!isfinite(sum)||fabs(sum-1)>1e-12)return 0;
        }
    }
    return 1;
}
static void pc_sha_rcfr_public(PcSha256* hash,const RcfrPublic* s) {
    pc_sha_u64(hash,(uint64_t)s->pot);
    for(int p=0;p<2;p++){pc_sha_u64(hash,(uint64_t)s->total[p]);pc_sha_u64(hash,(uint64_t)s->bet[p]);pc_sha_u64(hash,(uint64_t)s->stack[p]);pc_sha_u64(hash,(uint64_t)s->reopen[p]);}
    pc_sha_u64(hash,(uint64_t)s->high);pc_sha_u64(hash,(uint64_t)s->increment);pc_sha_u64(hash,(uint64_t)s->pending);pc_sha_u64(hash,(uint64_t)s->actor);pc_sha_double(hash,s->chips_per_bb);
}
static void pc_sha_certificate(PcSha256* h,const RcfrCertificate* c) {
    pc_sha_double(h,c->value_chips);pc_sha_double(h,c->upper_chips);pc_sha_double(h,c->lower_chips);pc_sha_double(h,c->nashconv_chips);pc_sha_double(h,c->gap_over_two_chips);
    pc_sha_double(h,c->value_bb);pc_sha_double(h,c->upper_bb);pc_sha_double(h,c->lower_bb);pc_sha_double(h,c->nashconv_bb);pc_sha_double(h,c->gap_over_two_bb);
    pc_sha_double(h,c->chance_normalizer);pc_sha_u64(h,c->iterations);
}
static int pc_strategy_digest(const RcfrGame* game,unsigned char out[32]) {
    PcSha256 hash;pc_sha_domain(&hash,"poker-continuation-all-node-average-v1");
    pc_sha_u64(&hash,(uint64_t)rcfr_hand_count(game));pc_sha_u64(&hash,(uint64_t)rcfr_node_count(game));
    double average[3*POKER_CFR_HANDS];
    for(int i=0;i<rcfr_node_count(game);i++) {
        RcfrNodeInfo node;if(!rcfr_node_info(game,i,&node))return 0;
        pc_sha_u64(&hash,(uint64_t)i);pc_sha_rcfr_public(&hash,&node.state);
        pc_sha_u64(&hash,(uint64_t)node.kind);pc_sha_u64(&hash,(uint64_t)node.folder);pc_sha_u64(&hash,(uint64_t)node.action_count);
        for(int a=0;a<3;a++){pc_sha_u64(&hash,(uint64_t)node.child[a]);pc_sha_u64(&hash,(uint64_t)node.action[a]);pc_sha_double(&hash,node.payoff0[a]);}
        if(node.kind)continue;
        if(!rcfr_average(game,i,average))return 0;
        for(int j=0;j<node.action_count*POKER_CFR_HANDS;j++)pc_sha_double(&hash,average[j]);
    }
    pc_sha_finish(&hash,out);return 1;
}
static int pc_anchor(PcContext* c,const Poker* g,int lane,const PokerProfile* profiles) {
    PokerCfrInputs in;
    if(!poker_cfr_make_inputs(g,lane,profiles,&in))return 0;
    c->creates++;
    RcfrGame* game=PC_CREATE(in.hand,POKER_CFR_HANDS,in.prior[0],in.prior[1],&in.state,NULL,0);
    if(!game){c->solve_status=POKER_CFR_CREATE_FAILED;return 0;}
    const int checkpoints[5]={100,200,400,800,1000};int previous=0,accepted=0;
    for(int k=0;k<5;k++) {
        c->checkpoint=checkpoints[k];c->iterate_calls++;
        if(!PC_ITERATE(game,(uint64_t)(checkpoints[k]-previous))){c->solve_status=POKER_CFR_ITERATION_FAILED;break;}
        c->iterations+=(uint64_t)(checkpoints[k]-previous);previous=checkpoints[k];
        if(!PC_CERTIFICATE(game,&c->certificate) || c->certificate.iterations!=(uint64_t)checkpoints[k]
            || !poker_cfr_certificate_usable(&c->certificate)){c->solve_status=POKER_CFR_CERTIFICATE_FAILED;break;}
        if(c->certificate.nashconv_bb<=.05){accepted=1;break;}
        c->solve_status=POKER_CFR_NOT_CONVERGED;
    }
    if(accepted && (!pc_validate_averages(game) || !pc_strategy_digest(game,c->strategy_hash))) {
        accepted=0;c->solve_status=POKER_CFR_ROW_FAILED;
    }
    if(!accepted){rcfr_free(game);return 0;}
    c->game=game;c->node=0;c->phase=PC_FOLLOWING;c->anchors++;c->solve_status=POKER_CFR_ACCEPTED;
    PcSha256 hash;pc_sha_domain(&hash,"poker-continuation-anchor-v1");pc_sha_add(&hash,c->prefix_hash,32);
    pc_sha_public(&hash,&c->state);pc_sha_rcfr_public(&hash,&in.state);
    for(int h=0;h<POKER_CFR_HANDS;h++){pc_sha_u64(&hash,in.hand[h].c0);pc_sha_u64(&hash,in.hand[h].c1);pc_sha_u64(&hash,in.hand[h].rank);pc_sha_double(&hash,in.prior[0][h]);pc_sha_double(&hash,in.prior[1][h]);}
    pc_sha_u64(&hash,(uint64_t)c->checkpoint);pc_sha_certificate(&hash,&c->certificate);pc_sha_add(&hash,c->strategy_hash,32);pc_sha_finish(&hash,c->anchor_hash);
    return 1;
}
/* Read-only private selection: the solve and every row validation preceded it.
 * Production callers cannot mutate context identity to reach additional rows. */
static int pc_select_average(const RcfrGame* game,int node,int a,int b,uint64_t* simulations,int32_t* action) {
    if(!game || !simulations || !action || a<0 || b<0 || a>=52 || b>=52 || a==b)return 0;
    if(a>b){int t=a;a=b;b=t;}
    int h=0;while(h<game->n && (game->hand[h].c0!=a || game->hand[h].c1!=b))h++;
    RcfrNodeInfo info;double average[3*POKER_CFR_HANDS];
    if(h==game->n || !rcfr_node_info(game,node,&info) || info.kind || !rcfr_average(game,node,average))return 0;
    double draw=poker_random(simulations)/4294967296.0,cumulative=0;
    for(int k=0;k<info.action_count;k++){cumulative+=average[k*game->n+h];if(draw<cumulative || k+1==info.action_count){*action=(int32_t)info.action[k];return 1;}}
    return 0;
}
/* Exact original equity-policy tail, starting after its external three draws.
 * No adapter call and no second top-level RNG seed draw can occur here. */
static int32_t pc_original_tail(Poker* g,int lane,uint64_t* simulations,unsigned roll,int samples,const PokerProfile* profiles) {
    PokerEquity e;poker_equity(g,lane,simulations,samples?samples:POKER_EQUITY_SAMPLES,1,profiles,&e);
    int p=g->actor[lane],live=__builtin_popcount(g->live[lane]);uint32_t call=g->high[lane]-g->bet[p][lane];
    if(call>g->stack[p][lane])call=g->stack[p][lane];
    float margin=g->street[lane]==3?0:.025f;
    if(call && e.payout<call+margin*e.eligible_pot)return -1;
    float value=call?(live==2?.78f:.68f):(live==2?.58f:.48f);
    int raise=e.share>value && roll<850;
    if(!call && live==2 && e.share>.25f && roll<45)raise=1;
    if(raise && poker_can_raise(g,lane)) {
        uint32_t target=g->high[lane]+2*(g->pot[lane]+call)/3;
        uint32_t min=poker_min_raise(g,lane),max=g->bet[p][lane]+g->stack[p][lane];
        if(target<min)target=min;
        return target<max?target:max;
    }
    return 0;
}
int pc_decide(PcContext* c,const unsigned char owner[32],uint64_t gen,void* ptr,int lane,uint64_t* rng,const void* profiles,uint64_t request,int32_t* action) {
    Poker* g=ptr;PcPublic observed;
    if(!pc_envelope(c,owner,gen))return 0;
    if(!c->started || !c->own_bound || !rng || !*rng || !action || !request
        || !pc_public_from_game(g,lane,&observed) || memcmp(&observed,&c->state,sizeof(observed)) || observed.actor!=c->seat)
        return pc_error(c,"decision state, RNG or request mismatch");
    int a=g->hole[c->seat][0][lane],b=g->hole[c->seat][1][lane];if(a>b){int t=a;a=b;b=t;}
    if(a!=c->own[0] || b!=c->own[1])return pc_error(c,"decision own holding changed");
    if(c->profile_mode>=0 && c->profile_mode!=!!profiles)return pc_error(c,"profile configuration changed within hand");
    if(c->phase==PC_FOLLOWING && profiles)return pc_error(c,"profiles are excluded from retained game");
    if(c->pending) {
        if(request!=c->pending_request || *rng!=c->pending_rng_after || memcmp(&observed,&c->pending_state,sizeof(observed)))return pc_error(c,"pending request is not an exact duplicate");
        *action=c->pending_action;return 1;
    }
    if(request<=c->requests)return pc_error(c,"request IDs must increase");
    c->profile_mode=!!profiles;
    uint64_t before=*rng;int32_t chosen=0;
    if(c->policy!=7 || !g->street[lane]) {chosen=poker_decide(g,lane,rng,c->policy,c->samples,profiles);c->fallbacks++;}
    else {
        uint64_t simulations=(uint64_t)poker_random(rng)<<32;simulations|=poker_random(rng);if(!simulations)simulations=1;
        unsigned roll=poker_random(rng)%1000;int selected=0;
        if(c->phase==PC_UNANCHORED && !profiles)pc_anchor(c,g,lane,NULL);
        if(c->phase==PC_FOLLOWING) {
            RcfrNodeInfo n;
            if(profiles || !rcfr_node_info(c->game,c->node,&n) || n.kind || !pc_rcfr_state_matches(&n.state,&observed))return pc_error(c,"retained decision state mismatch");
            selected=pc_select_average(c->game,c->node,c->own[0],c->own[1],&simulations,&chosen);
            if(!selected)return pc_error(c,"validated retained row unavailable");
            c->hits++;
        }
        if(!selected){chosen=pc_original_tail(g,lane,&simulations,roll,c->samples,profiles);c->fallbacks++;}
    }
    c->requests=request;c->pending=1;c->pending_request=request;c->pending_action=chosen;c->pending_state=observed;
    c->pending_rng_before=before;c->pending_rng_after=*rng;*action=chosen;return 1;
}
int pc_end(PcContext* c,const unsigned char owner[32],uint64_t gen,int reason) {
    if(!pc_envelope(c,owner,gen))return 0;
    if(reason<PC_TERMINAL || reason>PC_POLICY_CHANGE)return pc_error(c,"unknown end reason");
    if(reason==PC_TERMINAL && (!c->started || c->state.actor!=255 || c->pending))return pc_error(c,"terminal close before terminal acknowledgement");
    rcfr_free(c->game);c->game=NULL;c->phase=PC_ENDED;c->pending=0;c->end_reason=reason;return 1;
}
const char* pc_last_error(const PcContext* c){return c?c->error:"null context";}
int pc_protocol_error(PcContext* c,const char* message){return pc_error(c,message?message:"transport protocol error");}
int pc_audit(const PcContext* c,PcAudit* out) {
    if(!c || !out)return 0;
    memset(out,0,sizeof(*out));
    out->generation=c->generation;out->events=c->events;out->requests=c->requests;out->creates=c->creates;out->iterate_calls=c->iterate_calls;out->iterations=c->iterations;
    out->pending_request=c->pending_request;out->pending_rng_before=c->pending_rng_before;out->pending_rng_after=c->pending_rng_after;
    out->anchors=c->anchors;out->hits=c->hits;out->fallbacks=c->fallbacks;out->phase=c->phase;out->seat=c->seat;out->node=c->node;
    out->checkpoint=c->checkpoint;out->pending=c->pending;out->pending_action=c->pending_action;out->solve_status=c->solve_status;
    memcpy(out->owner,c->owner,32);pc_public_hash(&c->state,out->public_hash);memcpy(out->prefix_hash,c->prefix_hash,32);
    memcpy(out->anchor_hash,c->anchor_hash,32);memcpy(out->strategy_hash,c->strategy_hash,32);
    PcSha256 hash;pc_sha_domain(&hash,"poker-continuation-whole-audit-v1");pc_sha_add(&hash,c->owner,32);
    const uint64_t scalars[]={c->generation,c->events,c->requests,c->creates,c->iterate_calls,c->iterations,c->pending_request,c->pending_rng_before,c->pending_rng_after,
        c->anchors,c->hits,c->fallbacks,(uint64_t)c->phase,(uint64_t)c->seat,(uint64_t)c->node,(uint64_t)c->checkpoint,(uint64_t)c->pending,(uint64_t)(int64_t)c->pending_action,(uint64_t)c->solve_status};
    for(unsigned i=0;i<sizeof(scalars)/sizeof(scalars[0]);i++)pc_sha_u64(&hash,scalars[i]);
    pc_sha_add(&hash,out->public_hash,32);pc_sha_add(&hash,c->prefix_hash,32);pc_sha_add(&hash,c->anchor_hash,32);pc_sha_add(&hash,c->strategy_hash,32);
    pc_sha_finish(&hash,out->whole_hash);return 1;
}
int pc_certificate(const PcContext* c,RcfrCertificate* out){if(!c || !out || !c->anchors)return 0;*out=c->certificate;return 1;}
int pc_node_count(const PcContext* c){return c&&c->game?rcfr_node_count(c->game):0;}
size_t pc_allocated_bytes(const PcContext* c){return c?sizeof(*c)+(c->game?rcfr_allocated_bytes(c->game):0):0;}
int pc_node_info(const PcContext* c,int node,RcfrNodeInfo* out){return c&&c->game?rcfr_node_info(c->game,node,out):0;}
int pc_average(const PcContext* c,int node,double* out,size_t capacity) {
    RcfrNodeInfo n;if(!c || !c->game || !out || !rcfr_node_info(c->game,node,&n) || n.kind || capacity<(size_t)n.action_count*POKER_CFR_HANDS)return 0;
    return rcfr_average(c->game,node,out);
}
#ifdef PC_TESTING
int pc_test_select(const PcContext* c,int node,int a,int b,uint64_t* rng,int32_t* action) {
    return c&&c->game?pc_select_average(c->game,node,a,b,rng,action):0;
}
int pc_test_strategy_hash(const PcContext* c,unsigned char out[32]) {return c&&c->game?pc_strategy_digest(c->game,out):0;}
void pc_test_sha256(const void* data,size_t n,unsigned char out[32]){PcSha256 s;pc_sha_init(&s);pc_sha_add(&s,data,n);pc_sha_finish(&s,out);}
#endif
