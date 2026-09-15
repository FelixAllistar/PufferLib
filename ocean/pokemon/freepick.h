#pragma once
#include "freepick_rules.h"
#include <stddef.h>
#include <string.h>
#include <ctype.h>

// This is a moveset, not an index into a recommended-set catalog.
typedef struct { uint8_t species, moves[4]; } PKMon;

static inline int pk_move_learnable(int species, int move) {
    return species >= 1 && species <= 149 && move >= 1 && move <= 164 &&
        (int)((pk_legal_moves[species][move/64] >> (move%64)) & 1);
}
static inline int pk_move_count(const PKMon* mon) {
    int n=0; while(n<4 && mon->moves[n]) n++; return n;
}
static inline int pk_moves_legal(int species, const uint8_t* moves, int count) {
    if(species<1 || species>149 || count<1 || count>4) return 0;
    for(int i=0;i<count;i++) {
        if(!pk_move_learnable(species,moves[i])) return 0;
        for(int j=0;j<i;j++) if(moves[j]==moves[i]) return 0;
    }
    // Minimal forbidden subsets were derived from an exhaustive full-set
    // validator audit, not inferred from pairwise compatibility alone.
    for(int i=0;i<PK_FORBIDDEN_COUNT;i++) {
        const PKForbiddenSet* bad=&pk_forbidden_sets[i];
        if(bad->species!=species || bad->count>count) continue;
        int found=0;
        for(int j=0;j<bad->count;j++)
            for(int k=0;k<count;k++) if(bad->moves[j]==moves[k]) found++;
        if(found==bad->count) return 0;
    }
    return 1;
}
static inline int pk_mon_legal(const PKMon* mon) {
    int n=pk_move_count(mon);
    for(int i=n;i<4;i++) if(mon->moves[i]) return 0;
    return pk_moves_legal(mon->species,mon->moves,n);
}
static inline int pk_move_allowed(const PKMon* mon, int move) {
    int n=pk_move_count(mon);
    if(n==4 || move<1 || move>164) return 0;
    uint8_t chosen[4]; memcpy(chosen,mon->moves,4); chosen[n]=(uint8_t)move;
    // Every legal non-empty prefix may stop. No mask can lead to a dead end.
    return pk_moves_legal(mon->species,chosen,n+1);
}
static inline int pk_species_id(const char* name) {
    if(!name || !*name) return 0;
    char wanted[40]; int n=0;
    for(const char* p=name;*p && n<39;p++) if(isalnum((unsigned char)*p)) wanted[n++]=(char)tolower((unsigned char)*p);
    wanted[n]=0;
    if(!n) return 0;
    for(int species=1;species<=149;species++) {
        char normalized[40]; n=0;
        for(const char* p=pk_species_names[species];*p && n<39;p++)
            if(isalnum((unsigned char)*p))normalized[n++]=(char)tolower((unsigned char)*p);
        normalized[n]=0;
        if(!strcmp(normalized,wanted))return species;
    }
    return 0;
}
