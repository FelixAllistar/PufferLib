#pragma once
// Trusted, local, versioned simulator snapshots. No neural activations/pointers.
#include "pokemon_core.h"
#include "catalog_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define PK_STATE_VERSION 2
#define PK_STATE_SCHEMA "pkmn-9b88fd6c5467f703c38951d5b2e8a660314d410b-freepick-states-v2"
typedef struct {
    char magic[8];
    uint32_t version, record_bytes, counts[3], reserved;
    uint64_t checksum;
    char rules[72], schema[80];
} PKStateHeader;
typedef struct {
    PKStateHeader header;
    PKGame* states;
    size_t offsets[3], count;
} PKStateBank;
static PKStateBank pk_state_bank;
static inline uint64_t pk_state_hash(const void* data, size_t bytes) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i=0;i<bytes;i++) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}
static inline int pk_state_valid(const PKGame* g) {
    if (g->picks!=6 || g->phase!=PK_PHASE_BATTLE || g->result || g->invalid_actions || g->reset_source ||
            g->updates<0 || g->updates>=g->max_updates || g->max_updates!=512 ||
            g->draft!=1 || g->fixed_enabled[0] || g->fixed_enabled[1] || g->fixed_lead[0] || g->fixed_lead[1]) return 0;
    for (int p=0;p<2;p++) for (int i=0;i<6;i++) {
        if (!pk_mon_legal(&g->teams[p][i])) return 0;
        for(int j=0;j<i;j++) if(g->teams[p][i].species==g->teams[p][j].species) return 0;
    }
    PKGame check = *g;
    pk_game_observe(&check);
    if (memcmp(check.obs,g->obs,sizeof(g->obs)) || memcmp(check.masks,g->masks,sizeof(g->masks))) return 0;
    for (int p=0;p<2;p++) {
        int n=0;
        for (int a=0;a<PK_ACTIONS;a++) { if(g->masks[p][a]>1) return 0; n+=g->masks[p][a]; }
        if (!n) return 0;
    }
    return 1;
}
static inline int pk_state_load(PKStateBank* bank, const char* path) {
    FILE* f=fopen(path,"rb");
    if(!f) return 0;
    PKStateBank b={0};
    int ok=fread(&b.header,sizeof(b.header),1,f)==1;
    if(!ok || memcmp(b.header.magic,"PKSTATE2",8) || b.header.version!=PK_STATE_VERSION ||
            b.header.record_bytes!=sizeof(PKGame) || b.header.reserved ||
            memcmp(b.header.rules,PK_RULES_SHA,sizeof(PK_RULES_SHA)) ||
            memcmp(b.header.schema,PK_STATE_SCHEMA,sizeof(PK_STATE_SCHEMA))) { fclose(f); return 0; }
    for(int k=0;k<3;k++) {
        if(!b.header.counts[k] || b.header.counts[k]>1000000) { fclose(f); return 0; }
        b.offsets[k]=b.count; b.count+=b.header.counts[k];
    }
    b.states=(PKGame*)malloc(b.count*sizeof(PKGame));
    if(!b.states) { fclose(f); return 0; }
    ok=fread(b.states,sizeof(PKGame),b.count,f)==b.count && fgetc(f)==EOF && !ferror(f);
    fclose(f);
    if(ok) ok=pk_state_hash(b.states,b.count*sizeof(PKGame))==b.header.checksum;
    for(size_t i=0;ok && i<b.count;i++) ok=pk_state_valid(&b.states[i]);
    if(!ok) { free(b.states); return 0; }
    free(bank->states); *bank=b;
    return 1;
}
static inline void pk_state_restore(PKGame* g, const PKGame* source, uint64_t future_seed) {
    // The caller's episode/bank sampling stream must not rewind with a snapshot.
    uint64_t rng=g->rng;
    *g=*source;
    g->rng=rng;
    g->battle_seed=future_seed;
    g->reset_source=1;
    pk_reseed(&g->battle,future_seed);
    pk_game_observe(g);
}
