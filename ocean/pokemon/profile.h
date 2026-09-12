#pragma once
#include "personality.h"
// Evaluation-only episode descriptors. Never changes observations or rewards.
typedef struct {
    int species[149], leads[149], types[32];
    double hp_sum, personality_sum[PK_PERSONALITY_DIM];
    int samples;
} PKProfile;
typedef struct {
    int species[149], leads[149], games;
    double hp, survivors, turns;
} PKProfileSummary;
static void pk_profile_accumulate(PKProfileSummary* s, const PKProfile* p, const PKGame* g, int side) {
    for (int i=0;i<149;i++) { s->species[i]+=p->species[i]; s->leads[i]+=p->leads[i]; }
    s->games++;
    s->hp += p->samples ? p->hp_sum/p->samples : 0;
    for (int i=0;i<6;i++) s->survivors += !g->obs[side][16+32*i+3];
    s->turns += pk_turn(&g->battle);
}
static void pk_profile_summary(const PKProfileSummary* s) {
    if (!s->games) return;
    int species[149], leads[149];
    memcpy(species,s->species,sizeof(species)); memcpy(leads,s->leads,sizeof(leads));
    printf("\nModel A profile (%d games)\n",s->games);
    printf("%-39s %s\n","Top species (% of teams)","Top leads (% of games)");
    for (int row=0;row<6;row++) {
        int a=0,b=0;
        for (int i=1;i<149;i++) { if(species[i]>species[a]) a=i; if(leads[i]>leads[b]) b=i; }
        if(!species[a] && !leads[b]) break;
        char left[80]="",right[80]="";
        if(species[a]) snprintf(left,sizeof(left),"%d. %-20s %5.1f%%",row+1,pk_species_labels[a+1],100.0*species[a]/s->games);
        if(leads[b]) snprintf(right,sizeof(right),"%d. %-20s %5.1f%%",row+1,pk_species_labels[b+1],100.0*leads[b]/s->games);
        printf("%-39s %s\n",left,right); species[a]=leads[b]=0;
    }
    printf("Mean battle turns: %.1f | Survivors at end: %.2f/6\n",s->turns/s->games,s->survivors/s->games);
    printf("Mean team HP during battle: %.1f%% (equal weight per game)\n",100*s->hp/s->games);
}
static void pk_profile_step(PKProfile* p, const PKGame* g, int side) {
    if (g->picks != 6) return;
    const uint8_t* obs = g->obs[side];
    if (!p->samples) {
        for (int i=0;i<6;i++) {
            p->species[pk_species(g->teams[side][i])-1]++;
            const uint8_t* mon=obs+16+32*i;
            if (mon[5]<32) p->types[mon[5]]++;
            if (mon[6]<32 && mon[6]!=mon[5]) p->types[mon[6]]++;
        }
        p->leads[pk_species(g->teams[side][0])-1]++;
    }
    for (int i=0;i<6;i++) p->hp_sum += obs[16+32*i+1]/(255.0*6);
    double features[PK_PERSONALITY_DIM];
    pk_personality_features(obs,features);
    for(int k=0;k<PK_PERSONALITY_DIM;k++) p->personality_sum[k]+=features[k];
    p->samples++;
}
static void pk_profile_array(const int* values, int n) {
    putchar('[');
    for(int i=0;i<n;i++) printf("%s%d",i?",":"",values[i]);
    putchar(']');
}
static void pk_profile_emit(const PKProfile* p, const PKGame* g, int side, double score) {
    printf("PK_PROFILE {\"score\":%.1f,\"descriptors\":{\"species\":",score);
    pk_profile_array(p->species,149);
    printf(",\"leads\":"); pk_profile_array(p->leads,149);
    printf(",\"types\":"); pk_profile_array(p->types,32);
    double alive=0;
    for(int i=0;i<6;i++) alive += !g->obs[side][16+32*i+3];
    printf(",\"mean_team_hp\":%.9g,\"survivors\":%.9g,\"duration\":%.9g,\"timeout\":%d}",
        p->samples?p->hp_sum/p->samples:0,alive/6,
        (double)g->updates/g->max_updates,g->result==5);
    /* Additive fields: existing profile_population consumers keep their schema.
     * Exact ordered sets permit joint-core diagnostics, not just marginal usage.
     */
    printf(",\"qd_version\":1,\"species_ids\":[");
    for(int i=0;i<6;i++) printf("%s%d",i?",":"",pk_species(g->teams[side][i]));
    printf("],\"set_ids\":[");
    for(int i=0;i<6;i++) printf("%s%d",i?",":"",(int)g->teams[side][i]);
    printf("],\"personality\":[");
    for(int k=0;k<PK_PERSONALITY_DIM;k++) printf("%s%.9g",k?",":"",p->samples?p->personality_sum[k]/p->samples:0);
    printf("]}\n");
}
