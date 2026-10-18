/* Independent retained-tree reference from the recovered, unmodified V5 base.
 * No continuation implementation, private holding, match or transport code is
 * used to build the expected public range game. Compile and run only in an explicit test build; this file is not part
 * of the production policy library. */
#include "../../native.c"

typedef struct ReferenceGame {
    PokerCfrInputs inputs;
    RcfrGame *game;
    RcfrCertificate certificate;
    int status, checkpoint, iterate_calls;
} ReferenceGame;

/* Canonical 31-field fixture format is implemented independently of PcPublic. */
static int reference_load(Poker *g, const int64_t *v, const int *holes) {
    if (!g || !v) return 0;
    memset(g, 0, sizeof(*g));
    g->actor[0] = (uint8_t)v[0]; g->street[0] = (uint8_t)v[1];
    g->button[0] = (uint8_t)v[2]; g->decisions[0] = (uint16_t)v[3];
    g->pot[0] = (uint32_t)v[4]; g->high[0] = (uint16_t)v[5];
    g->increment[0] = (uint16_t)v[6]; g->live[0] = (uint16_t)v[7];
    g->active[0] = (uint16_t)v[8]; g->pending[0] = (uint16_t)v[9];
    g->raises[0] = (uint8_t)v[10]; g->last_raiser[0] = (uint8_t)v[11];
    for (int b = 0; b < 5; b++) g->board[b][0] = (uint8_t)v[13+b];
    g->line[0][0] = (uint8_t)v[18]; g->line[1][0] = (uint8_t)v[19];
    for (int p = 0; p < 2; p++) {
        int offset = 20 + 5*p;
        g->stack[p][0] = (uint16_t)v[offset];
        g->initial[p][0] = (uint16_t)v[offset+1];
        g->total[p][0] = (uint16_t)v[offset+2];
        g->bet[p][0] = (uint16_t)v[offset+3];
        g->reopen[p][0] = (uint32_t)v[offset+4];
        if (holes) for (int c = 0; c < 2; c++)
            g->hole[p][c][0] = (uint8_t)holes[2*p+c];
    }
    return 1;
}

size_t reference_native_size(void) { return sizeof(Poker); }
void *reference_native_create(const int64_t *v, const int *holes) {
    Poker *g = malloc(sizeof(*g));
    if (g && !reference_load(g, v, holes)) { free(g); return NULL; }
    return g;
}
void reference_native_free(void *g) { free(g); }
void reference_native_step(void *ptr, int32_t action) { poker_step(ptr, 0, action); }

/* Read native post-action state. Restore only terminal pre-payout money using
 * the independently known identity initial = stack_before_payout + total.
 * Native rank/settlement is NOT the independent actual-card payout oracle. */
int reference_unsettled(const void *ptr, int64_t *v) {
    const Poker *g = ptr;
    if (!g || !v) return 0;
    v[0]=g->actor[0];v[1]=g->street[0];v[2]=g->button[0];v[3]=g->decisions[0];
    v[4]=g->pot[0];v[5]=g->high[0];v[6]=g->increment[0];
    v[7]=g->live[0];v[8]=g->active[0];v[9]=g->pending[0];
    v[10]=g->raises[0];v[11]=g->last_raiser[0];v[12]=v[1]?v[1]+2:0;
    for(int b=0;b<5;b++)v[13+b]=b<v[12]?g->board[b][0]:-1;
    v[18]=g->line[0][0];v[19]=g->line[1][0];
    for(int p=0;p<2;p++) {
        int offset=20+5*p;
        v[offset]=g->actor[0]==POKER_DONE?
            (int64_t)g->initial[p][0]-g->total[p][0]:g->stack[p][0];
        v[offset+1]=g->initial[p][0];v[offset+2]=g->total[p][0];
        v[offset+3]=g->bet[p][0];v[offset+4]=g->reopen[p][0];
    }
    if(g->actor[0]==POKER_DONE)v[4]=(int64_t)g->total[0][0]+g->total[1][0];
    v[30]=POKER_BB;
    return 1;
}

ReferenceGame *reference_create(const int64_t *fields) {
    Poker *g=calloc(1,sizeof(*g));
    ReferenceGame *r=calloc(1,sizeof(*r));
    if(!g || !r) {free(g);free(r);return NULL;}
    reference_load(g,fields,NULL);
    int valid=poker_cfr_make_inputs(g,0,NULL,&r->inputs);
    free(g);
    if(!valid){free(r);return NULL;}
    r->game=rcfr_create(r->inputs.hand,POKER_CFR_HANDS,r->inputs.prior[0],
                        r->inputs.prior[1],&r->inputs.state,NULL,0);
    if(!r->game){free(r);return NULL;}
    const int checkpoint[5]={100,200,400,800,1000};
    int previous=0;
    for(int i=0;i<5;i++) {
        r->checkpoint=checkpoint[i];r->iterate_calls++;
        if(!rcfr_iterate(r->game,(uint64_t)(checkpoint[i]-previous))) {
            r->status=POKER_CFR_ITERATION_FAILED;return r;
        }
        previous=checkpoint[i];
        if(!rcfr_certificate(r->game,&r->certificate)
            || r->certificate.iterations!=(uint64_t)checkpoint[i]
            || !poker_cfr_certificate_usable(&r->certificate)) {
            r->status=POKER_CFR_CERTIFICATE_FAILED;return r;
        }
        if(r->certificate.nashconv_bb<=0.05) {
            r->status=POKER_CFR_ACCEPTED;return r;
        }
        r->status=POKER_CFR_NOT_CONVERGED;
    }
    return r;
}
void reference_free(ReferenceGame *r) {if(r){rcfr_free(r->game);free(r);}}
int reference_status(const ReferenceGame *r) {return r?r->status:-1;}
int reference_checkpoint(const ReferenceGame *r) {return r?r->checkpoint:-1;}
int reference_iterate_calls(const ReferenceGame *r) {return r?r->iterate_calls:-1;}
int reference_node_count(const ReferenceGame *r) {return r?rcfr_node_count(r->game):0;}
int reference_node(const ReferenceGame *r,int index,RcfrNodeInfo *out) {
    return r?rcfr_node_info(r->game,index,out):0;
}
int reference_average(const ReferenceGame *r,int index,double *out,size_t capacity) {
    RcfrNodeInfo n;
    if(!r || !out || !rcfr_node_info(r->game,index,&n) || n.kind
        || capacity<(size_t)n.action_count*POKER_CFR_HANDS)return 0;
    return rcfr_average(r->game,index,out);
}
int reference_certificate(const ReferenceGame *r,RcfrCertificate *out) {
    if(!r || !out)return 0;
    *out=r->certificate;return 1;
}
int reference_inputs(const ReferenceGame *r,PokerCfrInputs *out) {
    if(!r || !out)return 0;
    *out=r->inputs;return 1;
}
