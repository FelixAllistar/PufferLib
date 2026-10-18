#pragma once
/* Include after the original core.h and before Env. These hooks deliberately
 * do not choose or load a model from a path. A separately source-bound loader
 * supplies a root-qualified binding and explicit owner namespace at puf_init.
 * Existing builds never define POKER_CLONE_BRIDGE and retain the original calls.
 */
#ifndef POKER_CLONE_BRIDGE
#define POKER_CLONE_BRIDGE 0
#endif
#if POKER_CLONE_BRIDGE && POKER_PLAYERS == 2
#if defined(POKER_RIVER_CFR) && POKER_RIVER_CFR
#error "Do not combine clone and river-CFR policies"
#endif
#include "bridge_native_impl.h"

typedef struct BbPuffer {
    BbContext* context;
    unsigned char owner[32];
    uint64_t generation, request, abandoned_hands;
} BbPuffer;

static void bb_puffer_require(BbPuffer* p,int success) {
    if(!success) {
        fprintf(stderr,"clone bridge protocol failure: %s\n",bb_last_error(p->context));
        abort(); /* Never converts a protocol fault into a baseline action. */
    }
}
static void bb_puffer_configure(BbPuffer* p,int enabled,const unsigned char owner[32],const BbModelBinding* binding) {
    memset(p,0,sizeof(*p));
    if(!enabled)return;
    if(!owner){fprintf(stderr,"explicit clone owner namespace required\n");abort();}
    memcpy(p->owner,owner,32);p->context=bb_create(owner,binding,0);
    bb_puffer_require(p,p->context!=NULL);
}
static void bb_puffer_begin(BbPuffer* p,Poker* game,int lane) {
    if(!p->context)return;
    /* This hook is called only for a real puf_reset/deal. A forced reset of
     * an open hand explicitly abandons that generation before reserving next.
     * Full transport rebuilds never call this hook. */
    BbAudit audit;bb_puffer_require(p,bb_audit(p->context,&audit));
    if(audit.phase==BB_ACTIVE || audit.phase==BB_RESERVED) {
        bb_puffer_require(p,bb_abandon(p->context,p->owner,p->generation));
        p->abandoned_hands++;
    }
    if(p->generation==UINT64_MAX){fprintf(stderr,"clone generation exhausted\n");abort();}
    p->generation++;p->request=0;
    bb_puffer_require(p,bb_begin(p->context,p->owner,p->generation));
    bb_puffer_require(p,bb_bind_native(p->context,p->owner,p->generation,game,lane));
}
static void bb_puffer_start(BbPuffer* p,Poker* game,int lane) {
    if(!p->context)return;
    BbPublic public_state;
    bb_puffer_require(p,bb_public_from_game(game,lane,&public_state));
    bb_puffer_require(p,bb_start(p->context,p->owner,p->generation,&public_state));
}
static int32_t bb_puffer_decide(BbPuffer* p,Poker* game,int lane,uint64_t* rng,int policy,int samples,const PokerProfile* profiles) {
    if(!p->context)return poker_decide(game,lane,rng,policy,samples,profiles);
    if(p->request==UINT64_MAX){fprintf(stderr,"clone request ID exhausted\n");abort();}
    int32_t action=0;
    bb_puffer_require(p,bb_decide(p->context,p->owner,p->generation,game,lane,rng,policy,samples,
        profiles,++p->request,&action));
    return action;
}
static void bb_puffer_step(BbPuffer* p,Poker* game,int lane,const uint64_t* rng,int32_t action) {
    if(!p->context){poker_step(game,lane,action);return;}
    bb_puffer_require(p,bb_step(p->context,p->owner,p->generation,game,lane,rng,action));
}
static void bb_puffer_close(BbPuffer* p) {
    if(p->context) {
        BbAudit audit;
        if(bb_audit(p->context,&audit) && (audit.phase==BB_ACTIVE || audit.phase==BB_RESERVED))
            (void)bb_abandon(p->context,p->owner,p->generation);
        bb_free(p->context);p->context=NULL;
    }
}
#define BB_PUFFER_FIELDS BbPuffer clone_bridge;
#define BB_PUFFER_CONFIGURE(env,enabled,owner,binding) bb_puffer_configure(&(env)->clone_bridge,enabled,owner,binding)
#define BB_PUFFER_BEFORE_DEAL(env) bb_puffer_begin(&(env)->clone_bridge,(env)->game,(env)->lane)
#define BB_PUFFER_AFTER_DEAL(env) bb_puffer_start(&(env)->clone_bridge,(env)->game,(env)->lane)
#define BB_PUFFER_DECIDE(env,policy) bb_puffer_decide(&(env)->clone_bridge,(env)->game,(env)->lane,&(env)->policy_rng,policy,(env)->equity_samples,(env)->profiles)
#define BB_PUFFER_STEP(env,action) bb_puffer_step(&(env)->clone_bridge,(env)->game,(env)->lane,&(env)->policy_rng,action)
#define BB_PUFFER_CLOSE(env) bb_puffer_close(&(env)->clone_bridge)
#else
#define BB_PUFFER_FIELDS
#define BB_PUFFER_CONFIGURE(env,enabled,owner,binding) ((void)0)
#define BB_PUFFER_BEFORE_DEAL(env) ((void)0)
#define BB_PUFFER_AFTER_DEAL(env) ((void)0)
#define BB_PUFFER_DECIDE(env,policy) poker_decide((env)->game,(env)->lane,&(env)->policy_rng,policy,(env)->equity_samples,(env)->profiles)
#define BB_PUFFER_STEP(env,action) poker_step((env)->game,(env)->lane,action)
#define BB_PUFFER_CLOSE(env) ((void)0)
#endif
