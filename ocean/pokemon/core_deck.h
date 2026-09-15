#pragma once
// A process-wide shuffled deck. Only the serial post-step/reset hook deals it.
// No worker races, no per-environment decks, no sampling with replacement.
#include "pokemon_core.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
typedef struct {
    uint32_t* cards;
    uint32_t count, cursor;
    uint64_t seed, cycle, assigned, drafted;
    int species[149], species_count;
} PKCoreDeck;
static PKCoreDeck pk_core_deck;

static inline uint64_t pk_core_bounded(uint64_t* rng, uint64_t bound) {
    uint64_t x, threshold = -bound % bound;
    do { x = pk_random(rng); } while (x < threshold);
    return x % bound;
}
static inline void pk_core_shuffle(PKCoreDeck* d) {
    uint32_t index = 0;
    for (int a=0; a<d->species_count-2; a++)
        for (int b=a+1; b<d->species_count-1; b++)
            for (int c=b+1; c<d->species_count; c++)
                d->cards[index++] = (uint32_t)d->species[a] |
                    ((uint32_t)d->species[b]<<8) | ((uint32_t)d->species[c]<<16);
    assert(index == d->count);
    uint64_t rng = d->seed + UINT64_C(0x9e3779b97f4a7c15)*d->cycle;
    for (uint32_t i=d->count-1; i>0; i--) {
        uint32_t j=(uint32_t)pk_core_bounded(&rng,(uint64_t)i+1);
        uint32_t tmp=d->cards[i]; d->cards[i]=d->cards[j]; d->cards[j]=tmp;
    }
    d->cursor=0;
}
static inline void pk_core_free(PKCoreDeck* d) {
    free(d->cards); memset(d,0,sizeof(*d));
}
static inline int pk_core_seek(PKCoreDeck* d,uint64_t assigned,uint64_t drafted) {
    if(!d->count || drafted>assigned)return 0;
    d->cycle=assigned/d->count;pk_core_shuffle(d);
    d->cursor=(uint32_t)(assigned%d->count);d->assigned=assigned;d->drafted=drafted;
    return 1;
}
// Pool is "all" or distinct national species IDs, comma separated. Sort it so
// equivalent pool orderings have identical seeded decks.
static inline int pk_core_init(PKCoreDeck* d, const char* pool, uint64_t seed) {
    pk_core_free(d);
    if (!pool || !strcmp(pool,"all")) {
        for (int s=1;s<=149;s++) d->species[d->species_count++]=s;
    } else {
        const char* p=pool;
        while (*p) {
            char* end; long s=strtol(p,&end,10);
            if (end==p || s<1 || s>149 || d->species_count>=149) goto invalid;
            while(isspace((unsigned char)*end)) end++;
            if (*end && *end!=',') goto invalid;
            for(int i=0;i<d->species_count;i++) if(d->species[i]==s) goto invalid;
            d->species[d->species_count++]=(int)s;
            if(!*end) break;
            p=end+1; if(!*p) goto invalid;
        }
        for(int i=1;i<d->species_count;i++) for(int j=i;j>0 && d->species[j]<d->species[j-1];j--) {
            int tmp=d->species[j]; d->species[j]=d->species[j-1]; d->species[j-1]=tmp;
        }
    }
    if(d->species_count<3) goto invalid;
    d->count=(uint32_t)(d->species_count*(d->species_count-1)*(d->species_count-2)/6);
    d->cards=(uint32_t*)malloc(d->count*sizeof(*d->cards));
    if(!d->cards) goto invalid;
    d->seed=seed; pk_core_shuffle(d); return 1;
invalid:
    pk_core_free(d); return 0;
}
static inline uint32_t pk_core_deal(PKCoreDeck* d) {
    assert(d->cards && d->count);
    if(d->cursor==d->count) { d->cycle++; pk_core_shuffle(d); }
    d->assigned++;
    return d->cards[d->cursor++];
}
