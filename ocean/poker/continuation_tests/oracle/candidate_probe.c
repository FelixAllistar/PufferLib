/* Proof-only counters and deterministic failure/branch construction hooks.
 * This is not the production playing binary. Frozen base math is unchanged. */
#include "../../native.c"

typedef struct ProbeCounts {
    uint64_t creates, iterate_calls, iterations, certificate_calls;
    uint64_t external_draws, row_draws;
    uint64_t last_row_before, last_row_after, last_row_return;
} ProbeCounts;
static ProbeCounts probe_counts;
static int probe_fault_mode, probe_fault_remaining;
static uint64_t *probe_external_rng;
static int probe_forced_local_enabled;
static uint32_t probe_forced_local_value;

static RcfrGame *probe_create(const RcfrHand *h,int n,const double *p0,
    const double *p1,const RcfrPublic *s,char *error,size_t length) {
    probe_counts.creates++;
    if(probe_fault_mode==1 && probe_fault_remaining>0) {
        probe_fault_remaining--;return NULL;
    }
    return rcfr_create(h,n,p0,p1,s,error,length);
}
static int probe_iterate(RcfrGame *g,uint64_t iterations) {
    probe_counts.iterate_calls++;
    if(probe_fault_mode==4 && probe_fault_remaining>0) {
        probe_fault_remaining--;return 0;
    }
    int ok=rcfr_iterate(g,iterations);
    if(ok)probe_counts.iterations+=iterations;
    return ok;
}
static int probe_certificate(RcfrGame *g,RcfrCertificate *out) {
    probe_counts.certificate_calls++;
    if(probe_fault_mode==2 && probe_fault_remaining>0) {
        probe_fault_remaining--;return 0;
    }
    if(!rcfr_certificate(g,out))return 0;
    if(probe_fault_mode==3 && probe_fault_remaining>0) {
        probe_fault_remaining--;
        out->lower_chips=out->value_chips-3;out->upper_chips=out->value_chips+3;
        out->nashconv_chips=6;out->gap_over_two_chips=3;
        out->lower_bb=out->lower_chips/100;out->upper_bb=out->upper_chips/100;
        out->nashconv_bb=.06;out->gap_over_two_bb=.03;
    }
    return 1;
}
static uint32_t probe_random(uint64_t *rng) {
    uint64_t before=*rng;
    uint32_t result=poker_random(rng);
    if(rng==probe_external_rng)probe_counts.external_draws++;
    else {
        probe_counts.row_draws++;
        if(probe_forced_local_enabled)result=probe_forced_local_value;
        probe_counts.last_row_before=before;probe_counts.last_row_after=*rng;
        probe_counts.last_row_return=result;
    }
    return result;
}
#define PC_CREATE probe_create
#define PC_ITERATE probe_iterate
#define PC_CERTIFICATE probe_certificate
#define poker_random probe_random
#define PC_TESTING 1
#include "../../river_continuation_impl.h"
#undef poker_random

void probe_reset(void) {
    memset(&probe_counts,0,sizeof(probe_counts));
    probe_fault_mode=probe_fault_remaining=probe_forced_local_enabled=0;
    probe_external_rng=NULL;
}
void probe_fault(int mode) {
    probe_fault_mode=mode;
    probe_fault_remaining=mode==3?5:mode?1:0;
}
void probe_force_local(int enabled,uint32_t value) {
    probe_forced_local_enabled=enabled;probe_forced_local_value=value;
}
void probe_read_counts(ProbeCounts *out) {if(out)*out=probe_counts;}
int probe_decide(PcContext *c,const unsigned char owner[32],uint64_t generation,
    void *game,int lane,uint64_t *rng,const void *profiles,uint64_t request,int32_t *action) {
    probe_external_rng=rng;
    int result=pc_decide(c,owner,generation,game,lane,rng,profiles,request,action);
    probe_external_rng=NULL;
    return result;
}
