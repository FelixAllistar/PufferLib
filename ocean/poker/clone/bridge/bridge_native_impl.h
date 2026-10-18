#pragma once
#if POKER_PLAYERS != 2
#error "The enabled prospective bridge is HU only"
#endif
#include <stdio.h>
#include "clone_bridge.h"
#include "prospective_clone_api.h"
#include "bb_sha256.h"
#include "bridge_public_impl.h"

struct BbContext {
    unsigned char owner[32], prefix[32], pending_profile_hash[32];
    BbModelBinding binding;
    const void* native_game;
    int native_lane;
    BbPublic state;
    BcEvent ring[4];
    uint64_t generation, events, requests, last_request, hits, fallbacks;
    uint64_t pending_request, rng_before, rng_after;
    unsigned count, receipt_mask;
    int phase, pending, pending_seat, pending_own[2], pending_policy;
    int pending_samples, pending_profiles, pending_branch;
    int32_t pending_action;
    char error[160];
};

static int bb_hex_digest(const char value[65]) {
    if (!value || value[64]) return 0;
    int nonzero = 0;
    for (int i=0;i<64;i++) {
        if (!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f'))) return 0;
        nonzero |= value[i]!='0';
    }
    return nonzero;
}
static int bb_model_matches(const BbModelBinding* b) {
    if (!b || !memchr(b->model_domain,0,sizeof(b->model_domain)) || strcmp(b->model_domain,BB_MODEL_DOMAIN)
        || !bb_hex_digest(b->model_npz_sha256) || !bb_hex_digest(b->model_numeric_sha256)
        || !bb_hex_digest(b->module_source_closure_sha256) || !bb_hex_digest(b->module_binary_sha256)
        || b->numeric_bytes>524288 || !b->numeric_bytes || !b->nodes || b->nodes>4064) return 0;
    return bc_runtime_abi_version()==2 && bc_input_bytes()==sizeof(BcInput) && bc_model_version()==2
        && bc_model_domain_version()==2
        && bc_model_domain() && !strcmp(bc_model_domain(),b->model_domain)
        && bc_size_encoding() && !strcmp(bc_size_encoding(),BB_SIZE_ENCODING)
        && bc_feature_contract_sha256() && !strcmp(bc_feature_contract_sha256(),BB_FEATURE_SHA256)
        && bc_model_npz_sha256() && !strcmp(bc_model_npz_sha256(),b->model_npz_sha256)
        && bc_model_numeric_sha256() && !strcmp(bc_model_numeric_sha256(),b->model_numeric_sha256)
        && bc_model_numeric_bytes()==b->numeric_bytes && (uint64_t)bc_model_nodes()==b->nodes
        && bc_model_validate()==1;
}
int bb_protocol_error(BbContext* c,const char* reason) {
    if (c) { c->phase=BB_ERROR; snprintf(c->error,sizeof(c->error),"%s",reason?reason:"protocol error"); }
    return 0;
}
const char* bb_last_error(const BbContext* c) { return c?c->error:"missing context"; }
static void bb_hash_public(BbSha256* h,const BbPublic* p) {
    int64_t words[31]; memcpy(words,p,sizeof(words));
    for (int i=0;i<31;i++) bb_sha_u64(h,(uint64_t)words[i]);
}
static void bb_hash_profiles(const PokerProfile* profiles,unsigned char out[32]) {
    /* Receipt-only public-statistic binding, never a feature/model argument.
     * The frozen profile has seven uint64 counters and one binary64 number.
     * Hash fields explicitly so padding cannot create false equality/diffs. */
    BbSha256 h;bb_sha_domain(&h,"clone-bridge-profile-receipt-v1");bb_sha_u64(&h,!!profiles);
    if(profiles)for(int p=0;p<2;p++) {
        const PokerProfile* v=profiles+p;
        bb_sha_u64(&h,v->hands);bb_sha_u64(&h,v->vpip);bb_sha_u64(&h,v->pfr);
        bb_sha_u64(&h,v->faced);bb_sha_u64(&h,v->folded);bb_sha_u64(&h,v->post_actions);
        bb_sha_u64(&h,v->post_raises);bb_sha_double(&h,v->raise_pot_sum);
    }
    bb_sha_finish(&h,out);
}
BbContext* bb_create(const unsigned char owner[32],const BbModelBinding* b,unsigned receipt_mask) {
    if (!owner || receipt_mask>3 || !bb_model_matches(b)) return NULL;
    unsigned char any=0; for (int i=0;i<32;i++) any|=owner[i]; if (!any) return NULL;
    BbContext* c=calloc(1,sizeof(*c));
    if (c) {memcpy(c->owner,owner,32);c->binding=*b;c->receipt_mask=receipt_mask;c->phase=BB_NEW;}
    return c;
}
void bb_free(BbContext* c) { if(c){memset(c,0,sizeof(*c));free(c);} }
int bb_begin(BbContext* c,const unsigned char owner[32],uint64_t gen) {
    if (!c) return 0;
    if (!owner || memcmp(owner,c->owner,32) || !gen || gen<=c->generation
        || (c->phase!=BB_NEW && c->phase!=BB_TERMINAL && c->phase!=BB_ABANDONED && c->phase!=BB_ERROR))
        return bb_protocol_error(c,"new hand requires closed context, same owner and increasing generation");
    BbModelBinding binding=c->binding; unsigned mask=c->receipt_mask; unsigned char id[32];memcpy(id,c->owner,32);
    memset(c,0,sizeof(*c));c->binding=binding;c->receipt_mask=mask;memcpy(c->owner,id,32);
    c->generation=gen;c->phase=BB_RESERVED;return 1;
}
int bb_envelope(BbContext* c,const unsigned char owner[32],uint64_t gen,int terminal) {
    if (!c) return 0;
    if (!owner || memcmp(owner,c->owner,32) || !gen || gen!=c->generation
        || (c->phase!=BB_RESERVED && c->phase!=BB_ACTIVE && !(terminal && c->phase==BB_TERMINAL)))
        return bb_protocol_error(c,"owner, request generation or phase mismatch");
    return 1;
}
int bb_bind_native(BbContext* c,const unsigned char owner[32],uint64_t gen,const void* game,int lane) {
    if(!bb_envelope(c,owner,gen,0))return 0;
    if(c->phase!=BB_RESERVED || c->native_game || !game || lane<0 || lane>=POKER_BATCH)
        return bb_protocol_error(c,"native lane binding requires a fresh reserved generation");
    c->native_game=game;c->native_lane=lane;return 1;
}
int bb_start(BbContext* c,const unsigned char owner[32],uint64_t gen,const BbPublic* p) {
    if (!bb_envelope(c,owner,gen,0)) return 0;
    if (c->phase!=BB_RESERVED || !c->native_game || !bb_public_valid(p) || p->actor==255 || p->decisions || p->street
        || p->line[0] || p->line[1] || p->initial0<POKER_BB || p->initial1<POKER_BB
        || p->actor!=p->button || p->bet0!=(p->button?100:50) || p->bet1!=(p->button?50:100)
        || p->total0!=p->bet0 || p->total1!=p->bet1 || p->reopen0 || p->reopen1
        || p->pot!=150 || p->high!=100 || p->increment!=100 || p->raises || p->last_raiser!=255
        || p->pending!=p->active || p->live!=3)
        return bb_protocol_error(c,"start must be a fresh original nonterminal blind state");
    c->state=*p;c->phase=BB_ACTIVE;
    BbSha256 h;bb_sha_domain(&h,"clone-bridge-public-start-v1");bb_hash_public(&h,p);bb_sha_finish(&h,c->prefix);
    return 1;
}
int bb_observe(BbContext* c,const unsigned char owner[32],uint64_t gen,
               const BbPublic* before,int32_t action,const BbPublic* after) {
    if (!bb_envelope(c,owner,gen,0)) return 0;
    if (c->phase!=BB_ACTIVE || !before || memcmp(before,&c->state,sizeof(*before))
        || !bb_public_transition(before,action,after)) return bb_protocol_error(c,"public transition mismatch");
    int p=(int)before->actor;
    if (c->pending) {
        if (p!=c->pending_seat || action!=c->pending_action)
            return bb_protocol_error(c,"event is not exact pending action acknowledgement");
    } else if (c->receipt_mask&(1u<<p)) {
        return bb_protocol_error(c,"managed seat acted without a pending request");
    }
    int64_t call=before->high-bb_bet(before,p);
    BcEvent event={p,before->street,action<0?0:action?3:call?2:1,bb_bet(before,p)};
    if (action>=0) event.bet+=bb_minimum((action?action:before->high)-event.bet,bb_stack(before,p));
    if(c->count<4)c->ring[c->count++]=event;
    else {memmove(c->ring,c->ring+1,3*sizeof(*c->ring));c->ring[3]=event;}
    BbSha256 h;bb_sha_domain(&h,"clone-bridge-public-event-v1");bb_sha_add(&h,c->prefix,32);
    bb_sha_u64(&h,c->events);bb_hash_public(&h,before);bb_sha_u64(&h,(uint64_t)(int64_t)action);
    bb_hash_public(&h,after);bb_sha_finish(&h,c->prefix);
    c->events++;c->state=*after;c->pending=0;c->pending_own[0]=c->pending_own[1]=0;
    if(after->actor==255)c->phase=BB_TERMINAL;
    return 1;
}
int bb_abandon(BbContext* c,const unsigned char owner[32],uint64_t gen) {
    if(!bb_envelope(c,owner,gen,1))return 0;
    if(c->phase!=BB_TERMINAL)c->phase=BB_ABANDONED;
    return 1;
}
int bb_terminal_receipt(BbContext* c,const unsigned char owner[32],uint64_t gen,const BbPublic* terminal) {
    if(!bb_envelope(c,owner,gen,1))return 0;
    if(c->phase!=BB_TERMINAL || !terminal || memcmp(terminal,&c->state,sizeof(*terminal)))
        return bb_protocol_error(c,"terminal receipt changed");
    return 1;
}
int bb_audit(const BbContext* c,BbAudit* out) {
    if(!c || !out)return 0;
    memset(out,0,sizeof(*out));out->generation=c->generation;out->events=c->events;
    out->decisions_requested=c->requests;out->hits=c->hits;out->fallbacks=c->fallbacks;
    out->pending_request=c->pending_request;out->pending_rng_before=c->rng_before;out->pending_rng_after=c->rng_after;
    out->phase=c->phase;out->pending=c->pending;out->pending_seat=c->pending_seat;
    out->pending_action=c->pending_action;out->pending_branch=c->pending_branch;
    memcpy(out->owner,c->owner,32);memcpy(out->public_prefix_sha256,c->prefix,32);return 1;
}

int bb_make_input(const BbPublic* p,const BcEvent physical[4],unsigned count,
                  int own0,int own1,int policy,int samples,int profiles,BcInput* out) {
    if(!out || !physical || !bb_public_valid(p) || p->actor==255
        || count!=(unsigned)(p->decisions<4?p->decisions:4))return -1;
    if(policy!=7 || samples || profiles || p->street==3 || p->initial0!=20000 || p->initial1!=20000
        || ((p->line[p->actor]>>(2*p->street))&3))return 0;
    if(own0<0 || own0>51 || own1<0 || own1>51 || own0==own1)return -1;
    for(int i=0;i<p->board_count;i++)if(p->board[i]==own0 || p->board[i]==own1)return -1;
    BcInput v;memset(&v,0,sizeof(v));v.abi_version=1;v.players=2;v.policy=policy;
    v.samples=samples;v.has_profiles=profiles;v.actor=0;v.street=p->street;
    v.actor_button=p->button==p->actor;v.pot=p->pot;v.high=p->high;v.increment=p->increment;
    v.raises=p->raises;v.last_raiser=p->last_raiser==255?-1:p->last_raiser!=p->actor;v.decisions=p->decisions;
    for(int a=0;a<2;a++) {
        int seat=a?1-(int)p->actor:(int)p->actor;
        v.stack[a]=bb_stack(p,seat);v.initial[a]=bb_initial(p,seat);v.total[a]=bb_total(p,seat);
        v.bet[a]=bb_bet(p,seat);v.reopen[a]=bb_reopen(p,seat);
        v.live[a]=!!(p->live&(1<<seat));v.active[a]=!!(p->active&(1<<seat));v.pending[a]=!!(p->pending&(1<<seat));
        for(int street=0;street<3;street++)v.line[a][street]=(p->line[seat]>>(2*street))&3;
    }
    v.history_count=count;
    for(unsigned i=0;i<count;i++) {
        BcEvent e=physical[i];
        if(e.seat<0 || e.seat>1 || e.street<0 || e.street>p->street || e.kind<1 || e.kind>3
            || e.bet<0 || e.bet>20000)return -1;
        e.seat=e.seat!=p->actor;v.history[i]=e;
    }
    v.own[0]=own0;v.own[1]=own1;v.board_count=p->board_count;
    for(int i=0;i<4;i++)v.board[i]=i<p->board_count?p->board[i]:-1;
    *out=v;return 1;
}

static int bb_game_matches(const BbContext* c,const Poker* g,int lane) {
    BbPublic observed;
    if(g!=c->native_game || lane!=c->native_lane || !bb_public_from_game(g,lane,&observed)
        || memcmp(&observed,&c->state,sizeof(observed))
        || c->events!=(uint64_t)g->decisions[lane] || c->count!=(unsigned)(c->events<4?c->events:4))return 0;
    for(unsigned i=0;i<c->count;i++) {
        uint32_t raw=g->history[(g->decisions[lane]-c->count+i)%POKER_HISTORY][lane];
        BcEvent event={(raw>>16)&15,(raw>>20)&3,(raw>>22)&3,raw&65535};
        if(memcmp(&event,c->ring+i,sizeof(event)))return 0;
    }
    return 1;
}
int bb_game_status(const void* ptr,int lane,int64_t out[2]) {
    const Poker* g=ptr;
    if(!g || !out || lane<0 || lane>=POKER_BATCH)return 0;
    out[0]=g->actor[lane];out[1]=g->street[lane];return 1;
}
int bb_decide(BbContext* c,const unsigned char owner[32],uint64_t gen,void* ptr,int lane,
              uint64_t* rng,int policy,int samples,const void* profile_ptr,uint64_t request,int32_t* action) {
    Poker* g=ptr;const PokerProfile* profiles=profile_ptr;
    if(!g || !rng || !action || lane<0 || lane>=POKER_BATCH || g->actor[lane]==255
        || !poker_bot_supported(policy) || (samples!=0 && samples!=32 && samples!=64 && samples!=128))
        return bb_protocol_error(c,"invalid native decision request");
    if(!c){*action=poker_decide(g,lane,rng,policy,samples,profiles);return 1;}
    if(!bb_envelope(c,owner,gen,0))return 0;
    if(c->phase!=BB_ACTIVE || !request || !bb_game_matches(c,g,lane))return bb_protocol_error(c,"native state/ring mismatch");
    int p=g->actor[lane],a=g->hole[p][0][lane],b=g->hole[p][1][lane];
    unsigned char profile_hash[32];bb_hash_profiles(profiles,profile_hash);
    if(a>b){int temp=a;a=b;b=temp;}
    if(c->pending) {
        if(request!=c->pending_request || *rng!=c->rng_after || p!=c->pending_seat
            || a!=c->pending_own[0] || b!=c->pending_own[1] || policy!=c->pending_policy
            || samples!=c->pending_samples || !!profiles!=c->pending_profiles
            || memcmp(profile_hash,c->pending_profile_hash,32))
            return bb_protocol_error(c,"pending decision is not an exact duplicate");
        *action=c->pending_action;return 1;
    }
    if(request<=c->last_request)return bb_protocol_error(c,"request IDs must increase within a hand");
    BcInput input;int eligible=bb_make_input(&c->state,c->ring,c->count,a,b,policy,samples,!!profiles,&input);
    if(eligible<0)return bb_protocol_error(c,"invalid private/public input projection");
    BcInput immutable;if(eligible)immutable=input;
    int supported=eligible?bc_supported(&input):0;
    if(eligible && memcmp(&immutable,&input,sizeof(input)))
        return bb_protocol_error(c,"qualified support lookup mutated immutable input");
    if(supported<0 || supported>1)return bb_protocol_error(c,"qualified model rejected eligible typed input");
    uint64_t before=*rng;int32_t chosen=INT32_C(123456789);int branch=BB_ORIGINAL;
    if(supported) {
        if(bc_try_action(&input,rng,&chosen)!=1 || memcmp(&immutable,&input,sizeof(input)))
            return bb_protocol_error(c,"qualified active model violated immutable input/active contract");
        branch=BB_CLONE;c->hits++;
    } else {
        /* No draw has happened in the bridge. This is the whole original a4b
         * call on the original state/RNG, including its own preflop handling. */
        chosen=poker_decide(g,lane,rng,policy,samples,profiles);c->fallbacks++;
    }
    BbPublic legal;
    if(!bb_public_money_next(&c->state,chosen,&legal))return bb_protocol_error(c,"selected action is illegal; no late fallback");
    c->requests++;c->last_request=request;c->pending=1;c->pending_request=request;c->pending_action=chosen;
    c->pending_seat=p;c->pending_own[0]=a;c->pending_own[1]=b;c->pending_policy=policy;
    c->pending_samples=samples;c->pending_profiles=!!profiles;c->pending_branch=branch;
    memcpy(c->pending_profile_hash,profile_hash,32);
    c->rng_before=before;c->rng_after=*rng;*action=chosen;return 1;
}
int bb_step(BbContext* c,const unsigned char owner[32],uint64_t gen,void* ptr,int lane,const uint64_t* rng,int32_t action) {
    Poker* g=ptr;
    if(!g || lane<0 || lane>=POKER_BATCH)return bb_protocol_error(c,"invalid native step target");
    if(!c){poker_step(g,lane,action);return 1;}
    if(!bb_envelope(c,owner,gen,0))return 0;
    if(c->phase!=BB_ACTIVE || !bb_game_matches(c,g,lane))return bb_protocol_error(c,"unobserved native action or reset");
    if(c->pending && (!rng || *rng!=c->rng_after))
        return bb_protocol_error(c,"pending own-action RNG changed before native acknowledgement");
    if(c->pending) {
        int seat=g->actor[lane],a=g->hole[seat][0][lane],b=g->hole[seat][1][lane];
        if(a>b){int temp=a;a=b;b=temp;}
        if(seat!=c->pending_seat || a!=c->pending_own[0] || b!=c->pending_own[1])
            return bb_protocol_error(c,"pending own pair changed before native acknowledgement");
    }
    BbPublic before=c->state,money,after;
    if(!bb_public_money_next(&before,action,&money))return bb_protocol_error(c,"illegal native action");
    /* The action is fixed and checked. Read only the board prefix it reveals.
     * Terminal public cards are processed before the context closes; opponent
     * private cards and the original kernel's settlement remain outside it. */
    int count=money.street?(int)money.street+2:0;int64_t visible[5];
    for(int i=0;i<5;i++)visible[i]=i<count?g->board[i][lane]:-1;
    if(!bb_public_next(&before,action,visible,count,&after) || !bb_observe(c,owner,gen,&before,action,&after))return 0;
    poker_step(g,lane,action);
    if(after.actor!=255 && !bb_game_matches(c,g,lane))return bb_protocol_error(c,"original step differs from public transition");
    if(after.actor==255 && g->actor[lane]!=255)return bb_protocol_error(c,"original terminal transition differs");
    return 1;
}
