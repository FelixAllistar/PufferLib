#define _POSIX_C_SOURCE 200809L
#include "pokemon.h"
#include "policy_view.h"

// Sparse, per-game sampling followed by capped coarse-stratum admission.
// No outcomes, values, species rankings or hidden-state features reach the actor.
#define CELLS (16*11*4)
#define HASH_SLOTS (1u<<20)
typedef struct {
    PKGame* games;
    unsigned* cells;
    unsigned counts[CELLS], count, capacity, seen, occupied;
} Collection;
static uint64_t fingerprints[HASH_SLOTS];
static uint64_t collection_rng;
static unsigned state_cell(const PKGame* g) {
    int alive[2]={0}, status[2]={0};
    uint64_t team_hash=0;
    for(int p=0;p<2;p++) {
        uint64_t h=0;
        for(int i=0;i<6;i++) {
            int base=16+32*i;
            alive[p]+=g->obs[p][base+1]>0;
            status[p]|=g->obs[p][base+2]!=0;
            uint64_t species=(uint64_t)pk_species(g->teams[p][i]);
            h+=species*species*UINT64_C(0x9e3779b97f4a7c15)+species;
        }
        team_hash^=h;
    }
    return (unsigned)((team_hash%16)*44+(alive[0]-alive[1]+5)*4+status[0]*2+status[1]);
}
static unsigned phase(const PKGame* g) {
    if(!g->updates) return 0;
    int alive[2]={0};
    for(int p=0;p<2;p++) for(int i=0;i<6;i++) alive[p]+=g->obs[p][16+32*i+1]>0;
    return alive[0]<=3 || alive[1]<=3 ? 2:1;
}
static int unique_state(const PKGame* g) {
    PKGame copy=*g;
    copy.rng=copy.battle_seed=0;
    pk_reseed(&copy.battle,0);
    uint64_t h=pk_state_hash(&copy,sizeof(copy));
    if(!h) h=1;
    unsigned slot=(unsigned)h & (HASH_SLOTS-1);
    for(unsigned probe=0;probe<HASH_SLOTS;probe++,slot=(slot+1)&(HASH_SLOTS-1)) {
        if(fingerprints[slot]==h) return 0;
        if(!fingerprints[slot]) { fingerprints[slot]=h; return 1; }
    }
    fprintf(stderr,"State fingerprint table full\n"); exit(1);
}
static void admit(Collection* bank, const PKGame* game) {
    if(!pk_state_valid(game)) { fprintf(stderr,"Invalid collected state\n"); exit(1); }
    if(!unique_state(game)) return;
    unsigned cell=state_cell(game);
    assert(cell<CELLS);
    // A single team-hash / material / status cell cannot fill an entire bank.
    unsigned limit=bank->capacity/32;
    if(limit<8) limit=8;
    if(bank->counts[cell]>=limit) return;
    bank->seen++;
    unsigned index=bank->count;
    if(index==bank->capacity) {
        index=(unsigned)(pk_random(&collection_rng)%bank->seen);
        if(index>=bank->capacity) return;
        unsigned old=bank->cells[index];
        if(!--bank->counts[old]) bank->occupied--;
    } else bank->count++;
    if(!bank->counts[cell]++) bank->occupied++;
    bank->cells[index]=cell;
    bank->games[index]=*game;
}
int main(int argc,char** argv) {
    if(argc<6) { fprintf(stderr,"Usage: collect_states OUT GAMES SEED CHECKPOINT CHECKPOINT [...]\n"); return 1; }
    const char* path=argv[1];
    int games=atoi(argv[2]), policies=argc-4;
    uint64_t seed=strtoull(argv[3],NULL,10);
    if(games<1 || games>50000 || policies>16) return 1;
    if(access(path,F_OK)==0) { fprintf(stderr,"Output exists: %s\n",path); return 1; }
    PKPolicy* players=(PKPolicy*)calloc(2*(size_t)policies,sizeof(PKPolicy));
    for(int side=0;side<2;side++) for(int i=0;i<policies;i++) {
        pk_load_policy(&players[side*policies+i],argv[4+i],0,0,NULL);
        if(strcmp(players[side*policies+i].team,"None")) { fprintf(stderr,"Collectors must use unrestricted teams\n"); return 1; }
    }
    Collection banks[3]={0};
    const unsigned capacities[3]={8192,32768,24576};
    for(int k=0;k<3;k++) {
        banks[k].capacity=capacities[k];
        banks[k].games=(PKGame*)calloc(capacities[k],sizeof(PKGame));
        banks[k].cells=(unsigned*)calloc(capacities[k],sizeof(unsigned));
        if(!banks[k].games || !banks[k].cells) return 1;
    }
    collection_rng=seed ^ UINT64_C(0xa3c59093fcb1375d);
    uint64_t decisions=0;
    int outcomes[6]={0}, perturbed=0;
    for(int n=0;n<games;n++) {
        PKPolicy* pair[2]={&players[n%policies],&players[policies+(n/policies)%policies]};
        PKGame g={0}; g.draft=1; g.max_updates=512;
        g.rng=seed+(uint64_t)n*UINT64_C(0x9e3779b97f4a7c15);
        pk_game_reset(&g);
        for(int p=0;p<2;p++) pk_reset_policy(pair[p],seed+2*(uint64_t)n+p);
        // One eighth of games explores legal drafts; other games retain archive styles.
        int explore=n%8==7; perturbed+=explore;
        PKGame samples[3][4]; unsigned seen[3]={0}, used[3]={0};
        const unsigned per_game[3]={1,4,3};
        while(!g.result) {
            if(g.picks==6) {
                unsigned k=phase(&g), index=seen[k]++;
                if(index>=per_game[k]) index=(unsigned)(pk_random(&collection_rng)%seen[k]);
                if(index<per_game[k]) { samples[k][index]=g; if(used[k]<per_game[k]) used[k]++; }
            }
            int actions[2];
            for(int p=0;p<2;p++) {
                // Forward even on exploratory choices to keep the collector's history current.
                actions[p]=pk_policy_action(pair[p],g.obs[p],g.masks[p],0);
                if((explore && g.picks<6) || (explore && pk_random(&collection_rng)%20==0))
                    actions[p]=pk_random_action(g.masks[p],&collection_rng);
            }
            if(pk_game_step(&g,actions[0],actions[1])==4) return 2;
            decisions++;
        }
        outcomes[g.result]++;
        for(int k=0;k<3;k++) for(unsigned i=0;i<used[k];i++) admit(&banks[k],&samples[k][i]);
        if((n+1)%256==0) {
            fprintf(stderr,"Collected games=%d states=%u,%u,%u cells=%u,%u,%u\n",n+1,
                banks[0].count,banks[1].count,banks[2].count,banks[0].occupied,banks[1].occupied,banks[2].occupied);
        }
    }
    PKStateHeader header={0};
    memcpy(header.magic,"PKSTATE1",8); header.version=PK_STATE_VERSION; header.record_bytes=sizeof(PKGame);
    strcpy(header.catalog,PK_CATALOG_SHA); strcpy(header.schema,PK_STATE_SCHEMA);
    header.checksum=UINT64_C(14695981039346656037);
    for(int k=0;k<3;k++) {
        if(!banks[k].count) { fprintf(stderr,"Empty phase bank %d\n",k); return 1; }
        header.counts[k]=banks[k].count;
        const uint8_t* data=(const uint8_t*)banks[k].games;
        for(size_t i=0;i<banks[k].count*sizeof(PKGame);i++) { header.checksum^=data[i]; header.checksum*=UINT64_C(1099511628211); }
    }
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0) { perror(path); return 1; }
    FILE* f=fdopen(fd,"wb"); if(!f) return 1;
    int ok=fwrite(&header,sizeof(header),1,f)==1;
    for(int k=0;k<3;k++) ok=ok && fwrite(banks[k].games,sizeof(PKGame),banks[k].count,f)==banks[k].count;
    if(fclose(f)) ok=0;
    if(!ok) { fprintf(stderr,"Failed bank write\n"); return 1; }
    printf("{\"version\":1,\"games\":%d,\"seed\":%llu,\"joint_decisions\":%llu,\"perturbed_games\":%d,"
        "\"counts\":[%u,%u,%u],\"strata\":[%u,%u,%u],\"record_bytes\":%zu,\"timeouts\":%d}\n",
        games,(unsigned long long)seed,(unsigned long long)decisions,perturbed,banks[0].count,banks[1].count,banks[2].count,
        banks[0].occupied,banks[1].occupied,banks[2].occupied,sizeof(PKGame),outcomes[5]);
    for(int k=0;k<3;k++) { free(banks[k].games); free(banks[k].cells); }
    for(int i=0;i<2*policies;i++) { if(players[i].net) free_puffernet(players[i].net); free(players[i].weights); puf_ini_free(&players[i].ini); }
    free(players);
    return 0;
}
