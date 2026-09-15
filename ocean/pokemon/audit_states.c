#define _POSIX_C_SOURCE 200809L
#include "state_bank.h"
#include <assert.h>
#include <inttypes.h>
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5) { fprintf(stderr,"Usage: audit_states BANK COLLECTION_SEED COLLECTION_GAMES [POLICY_COUNT=6]\n"); return 1; }
    unsigned policies=argc==5?(unsigned)strtoul(argv[4],NULL,10):6;
    if(policies<1 || policies>16) return 1;
    uint64_t seed=strtoull(argv[2],NULL,10);
    unsigned games=(unsigned)strtoul(argv[3],NULL,10);
    if(!games || games>50000 || !pk_state_load(&pk_state_bank,argv[1])) return 1;
    uint64_t increment=UINT64_C(0x9e3779b97f4a7c15), inverse=1;
    for(int i=0;i<6;i++) inverse*=2-increment*inverse;
    assert(inverse*increment==1);
    unsigned* counts=(unsigned*)calloc((size_t)games*3,sizeof(unsigned));
    if(!counts) return 1;
    const unsigned caps[3]={1,4,3};
    printf("{\"version\":1,\"states\":%zu,\"phases\":[",pk_state_bank.count);
    for(int k=0;k<3;k++) {
        unsigned species[150]={0}, exploratory=0, policy_pairs[16][16]={{0}};
        int min_turn=100000,max_turn=0,min_alive[2]={6,6},max_alive[2]={0};
        double alive_sum[2]={0},turn_sum=0;
        for(unsigned i=0;i<pk_state_bank.header.counts[k];i++) {
            const PKGame* g=&pk_state_bank.states[pk_state_bank.offsets[k]+i];
            uint64_t number=(g->rng-seed)*inverse-1;
            if(number>=games || ++counts[number*3+k]>caps[k]) { fprintf(stderr,"Invalid collection provenance/cap\n"); return 1; }
            uint64_t stream=seed+number*increment;
            if(pk_random(&stream)!=g->battle_seed || stream!=g->rng) return 1;
            exploratory+=number%8==7;
            policy_pairs[number%policies][(number/policies)%policies]++;
            int alive[2]={0};
            for(int p=0;p<2;p++) for(int j=0;j<6;j++) {
                species[g->teams[p][j].species]++;
                alive[p]+=g->obs[p][16+32*j+1]>0;
            }
            int expected=g->updates==0?0:alive[0]<=3 || alive[1]<=3?2:1;
            if(expected!=k) { fprintf(stderr,"Wrong phase partition\n"); return 1; }
            for(int p=0;p<2;p++) {
                if(alive[p]<min_alive[p]) min_alive[p]=alive[p];
                if(alive[p]>max_alive[p]) max_alive[p]=alive[p];
                alive_sum[p]+=alive[p];
            }
            int turn=pk_turn(&g->battle);
            if(turn<min_turn) min_turn=turn;
            if(turn>max_turn) max_turn=turn;
            turn_sum+=turn;
        }
        unsigned distinct=0,pairs=0;
        for(int i=1;i<=149;i++) distinct+=species[i]>0;
        for(unsigned a=0;a<policies;a++) for(unsigned b=0;b<policies;b++) pairs+=policy_pairs[a][b]>0;
        double n=pk_state_bank.header.counts[k];
        printf("%s{\"phase\":%d,\"states\":%.0f,\"distinct_species\":%u,\"policy_pairs\":%u,"
               "\"exploratory_states\":%u,\"mean_turn\":%.3f,\"min_turn\":%d,\"max_turn\":%d,"
               "\"mean_alive\":[%.3f,%.3f],\"min_alive\":[%d,%d],\"max_alive\":[%d,%d]}",
               k?",":"",k,n,distinct,pairs,exploratory,turn_sum/n,min_turn,max_turn,
               alive_sum[0]/n,alive_sum[1]/n,min_alive[0],min_alive[1],max_alive[0],max_alive[1]);
    }
    printf("],\"provenance_checked\":true,\"per_game_caps_checked\":true,\"phase_partition_checked\":true}\n");
    free(counts); free(pk_state_bank.states);
}
