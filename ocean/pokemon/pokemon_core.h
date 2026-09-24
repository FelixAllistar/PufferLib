#pragma once
#include "bridge.h"
#include <assert.h>
#include <string.h>

typedef struct {
    PKBattle battle;
    uint64_t rng;
    uint64_t battle_seed;
    PKMon teams[2][6];
    int fixed_team[2][6];
    int fixed_enabled[2]; // 1 ordered catalog control, 2 required catalog control, 3 assigned core, 4 required species.
    int required_count[2], fixed_lead[2];
    uint8_t masks[2][PK_ACTIONS];
    uint8_t obs[2][PK_OBS];
    int draft;       // 0 random legal construction, 1 private learned construction.
    int phase;       // species, moves, battle; picks==6 alone does NOT mean battle.
    int picks;
    int move_index[2], move_turn, move_stopped[2];
    int max_updates;
    int updates;
    int invalid_actions;
    int result;      // Engine results; 5 means wrapper time limit (draw).
    int reset_source; // 0 original game, 1 auxiliary state-bank game; public to both.
} PKGame;

static inline uint64_t pk_random(uint64_t* rng) {
    // SplitMix64: per-environment stream, no global RNG.
    uint64_t z = (*rng += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
static inline int pk_random_action(const uint8_t* mask, uint64_t* rng) {
    int n = 0;
    for (int a = 0; a < PK_ACTIONS; a++) n += mask[a] != 0;
    assert(n > 0);
    int selected = (int)(pk_random(rng) % (unsigned)n);
    for (int a = 0; a < PK_ACTIONS; a++) if (mask[a] && selected-- == 0) return a;
    return -1;
}
static inline int pk_required_set(const PKGame* g, int p, int species) {
    if (g->fixed_enabled[p] != 1 && g->fixed_enabled[p] != 2) return -1;
    for (int i = 0; i < g->required_count[p]; i++)
        if (pk_species(g->fixed_team[p][i]) == species) return g->fixed_team[p][i];
    return -1;
}
static inline int pk_required_species(const PKGame* g, int p, int species) {
    if (g->fixed_enabled[p] == 3 || g->fixed_enabled[p] == 4) {
        for(int i=0;i<g->required_count[p];i++) if(g->fixed_team[p][i]==species) return 1;
        return 0;
    }
    return pk_required_set(g,p,species)>=0;
}
static inline void pk_draft_masks(PKGame* g) {
    memset(g->masks, 0, sizeof(g->masks));
    for (int p = 0; p < 2; p++) {
        if (g->phase == PK_PHASE_MOVES) {
            int index=g->move_index[p];
            if(index==6 || g->move_stopped[p]) { g->masks[p][PK_MOVE_DONE]=1; continue; }
            PKMon* mon=&g->teams[p][index];
            int n=pk_move_count(mon), fixed=pk_required_set(g,p,mon->species);
            if(fixed>=0) {
                int move=n<4?pk_set_move(fixed,n):0;
                g->masks[p][move?move-1:PK_MOVE_DONE]=1;
            } else {
                for(int move=1;move<=164;move++) g->masks[p][move-1]=(uint8_t)pk_move_allowed(mon,move);
                g->masks[p][PK_MOVE_DONE]=(uint8_t)(n>0);
            }
            continue;
        }
        if (g->fixed_enabled[p] == 1) {
            int set = g->fixed_team[p][g->picks];
            g->masks[p][pk_species(set)-1] = 1;
            continue;
        }
        int missing = g->fixed_enabled[p] ? g->required_count[p] : 0;
        for (int i = 0; i < g->picks; i++)
            if (pk_required_species(g,p,g->teams[p][i].species)) missing--;
        for (int a = 0; a < 149; a++) {
            // Reserve enough slots for all not-yet-picked required species.
            int allowed = missing < 6-g->picks || pk_required_species(g,p,a+1);
            for (int i = 0; i < g->picks; i++)
                if (a + 1 == g->teams[p][i].species) allowed = 0;
            if(g->picks==0 && g->fixed_lead[p] && a+1!=g->fixed_lead[p]) allowed=0;
            g->masks[p][a] = (uint8_t)allowed;
        }
    }
}
static inline void pk_game_observe(PKGame* g) {
    for (int p = 0; p < 2; p++) {
        if (g->phase == PK_PHASE_BATTLE) {
            pk_observe(&g->battle, p, g->obs[p]);
            memcpy(g->masks[p], g->obs[p] + 480, PK_ACTIONS);
        } else {
            memset(g->obs[p], 0, PK_OBS);
            g->obs[p][0]=(uint8_t)g->phase;
            g->obs[p][1] = (uint8_t)g->picks;
            g->obs[p][12] = (uint8_t)g->move_index[p];
            g->obs[p][13] = (uint8_t)(g->move_index[p]<6?pk_move_count(&g->teams[p][g->move_index[p]]):0);
            g->obs[p][15] = (uint8_t)g->move_stopped[p];
            for (int i = 0; i < g->picks; i++) {
                const PKMon* mon=&g->teams[p][i];
                uint8_t* out=g->obs[p]+16+i*32;
                out[0]=mon->species; out[1]=255; out[4]=100;
                out[5]=pk_species_types[mon->species][0]; out[6]=pk_species_types[mon->species][1];
                for(int m=0;m<4;m++) {
                    out[8+m]=mon->moves[m];
                    int pp=pk_move_pp[mon->moves[m]]/5*8;
                    out[12+m]=(uint8_t)(pp>61?61:pp);
                    g->obs[p][14]+=(uint8_t)(mon->moves[m]!=0);
                }
                for(int stat=0;stat<5;stat++) out[16+stat]=(uint8_t)(pk_species_stats[mon->species][stat]/4);
            }
            for(int i=0;i<g->required_count[p];i++) g->obs[p][464+i]=(uint8_t)(
                g->fixed_enabled[p]<=2?pk_species(g->fixed_team[p][i]):g->fixed_team[p][i]);
            g->obs[p][470]=(uint8_t)g->required_count[p];
            g->obs[p][471]=(uint8_t)g->fixed_lead[p];
        }
        memcpy(g->obs[p] + 480, g->masks[p], PK_ACTIONS);
        g->obs[p][476] = (uint8_t)g->reset_source;
        // Only the player's own assigned core is public to that player. Core
        // conditioning is draft-only; ordinary games and frozen experts see 0.
        for(int i=0;i<3;i++) g->obs[p][477+i] = (uint8_t)(
            g->phase!=PK_PHASE_BATTLE && g->fixed_enabled[p]==3 ? g->fixed_team[p][i] : 0);
    }
}
static inline int pk_game_step(PKGame* g, int a0, int a1);
static inline void pk_game_reset(PKGame* g) {
    g->reset_source = 0;
    g->picks = 0;
    g->phase = PK_PHASE_SPECIES;
    g->move_index[0] = g->move_index[1] = 0;
    g->move_turn=0;g->move_stopped[0]=g->move_stopped[1]=0;
    g->updates = 0;
    g->invalid_actions = 0;
    g->result = 0;
    memset(g->teams, 0, sizeof(g->teams));
    memset(&g->battle, 0, sizeof(g->battle));
    pk_draft_masks(g);
    while(!g->draft && g->phase!=PK_PHASE_BATTLE && !g->result) {
        int a0=pk_random_action(g->masks[0],&g->rng), a1=pk_random_action(g->masks[1],&g->rng);
        pk_game_step(g,a0,a1);
    }
    pk_game_observe(g);
}
static inline int pk_game_step(PKGame* g, int a0, int a1) {
    if (g->result) return g->result;
    int actions[2] = {a0, a1};
    for (int p = 0; p < 2; p++) {
        int a = actions[p];
        if (a < 0 || a >= PK_ACTIONS || !g->masks[p][a]) {
            g->invalid_actions++;
            // Invalid policy actions never reach libpkmn (undefined behavior).
            for (a = 0; a < PK_ACTIONS && !g->masks[p][a]; a++) {}
            if (a == PK_ACTIONS) return g->result = 4;
            actions[p] = a;
        }
    }
    if (g->phase == PK_PHASE_SPECIES) {
        for(int p=0;p<2;p++) g->teams[p][g->picks].species=(uint8_t)(actions[p]+1);
        if(++g->picks==6) g->phase=PK_PHASE_MOVES;
        pk_draft_masks(g);
    } else if(g->phase==PK_PHASE_MOVES) {
        for(int p=0;p<2;p++) if(!g->move_stopped[p]) {
            PKMon* mon=&g->teams[p][g->move_index[p]];
            int n=pk_move_count(mon);
            if(actions[p]==PK_MOVE_DONE) g->move_stopped[p]=1;
            else {
                assert(n<4 && pk_move_allowed(mon,actions[p]+1));
                mon->moves[n]=(uint8_t)(actions[p]+1);
            }
        }
        // Four fixed decision slots per Pokemon hide the opponent's number of
        // selected moves. Early completion gets forced no-ops, not a timing leak.
        if(++g->move_turn%4==0) {
            for(int p=0;p<2;p++) {g->move_index[p]++;g->move_stopped[p]=0;}
        }
        if(g->move_index[0]==6 && g->move_index[1]==6) {
            g->phase=PK_PHASE_BATTLE;
            g->battle_seed = pk_random(&g->rng);
            g->result = pk_start_free(&g->battle, g->battle_seed, &g->teams[0][0]);
        } else pk_draft_masks(g);
    } else {
        g->updates++;
        g->result = pk_update(&g->battle, actions[0], actions[1]);
        if (g->result < 0) g->result = 4;
        if (!g->result && g->updates >= g->max_updates) g->result = 5;
    }
    pk_game_observe(g);
    return g->result;
}
