#pragma once
#include "bridge.h"
#include <assert.h>
#include <math.h>
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
    // Forced move pairs (move exploration, like forced species cores): when force_moves[p] is set at reset,
    // each of p's drafted Pokemon gets 2 random legal moves dealt for its first slots; p picks the rest.
    int force_moves[2];
    uint8_t forced_moves[2][6][2], forced_count[2][6];
    uint64_t move_rng;
    // Exact sets (team "sets:", e.g. Metamon's competitive teams): per player up to 6 entries of species then its
    // four moves; a drafted Pokemon with an entry gets exactly those moves, in order (the policy still acts on
    // one-legal-action masks). Species 0 = no entry. Appended in 2026-10: older state banks no longer load.
    uint8_t set_moves[2][6][5];
    // In-game trainer battles (env.trainer_file, 2026-10-30): no draft; each game starts from two real trainer
    // parties (pk_trainers). trainer_mirror of games are a trainer against itself, the rest against a trainer whose
    // cartridge-AI rating is within trainer_window (pk_trainers.elo), so most games are not foregone conclusions.
    int trainer_mode;
    float trainer_mirror, trainer_window;
    int trainer_perfect; // 1: Showdown-standard DVs and stat experience instead of the cartridge trainers'
    // Pick three (env.trainer_pick, Pokemon Stadium's cup format): both players see both parties and each picks up
    // to three in order (the first leads); parties of one or two bring everything. Picks are species actions.
    int trainer_pick;
    PKTrainerMon trainer_party[2][6];
    uint8_t trainer_size[2], picked[2][6], pick_count[2];
    int trainer_ids[2];
} PKGame;

#define PK_PHASE_PICK 3
#define PK_MAX_TRAINERS 512
typedef struct {
    int count;
    PKTrainerMon teams[PK_MAX_TRAINERS][6];
    uint8_t sizes[PK_MAX_TRAINERS];
    float elo[PK_MAX_TRAINERS];
} PKTrainerTable;
static PKTrainerTable pk_trainers;

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
// Deal up to 2 distinct moves that are legal together, uniformly over the species' legal pairs.
static inline int pk_deal_move_pair(int species, uint64_t* rng, uint8_t out[2]) {
    uint8_t legal[164]; int n=0;
    for(int move=1;move<=164;move++) if(pk_move_learnable(species,move)) legal[n++]=(uint8_t)move;
    if(n==0) return 0;
    if(n>1) for(int attempt=0;attempt<256;attempt++) {
        uint8_t pair[2]={legal[pk_random(rng)%(unsigned)n],legal[pk_random(rng)%(unsigned)n]};
        if(pair[0]!=pair[1] && pk_moves_legal(species,pair,2)) { out[0]=pair[0]; out[1]=pair[1]; return 2; }
    }
    out[0]=legal[pk_random(rng)%(unsigned)n];
    return 1;
}
static inline void pk_deal_forced_moves(PKGame* g) {
    for(int p=0;p<2;p++) for(int i=0;i<6;i++) {
        g->forced_count[p][i]=0;
        // Fixed catalog sets keep their own moves.
        if(!g->force_moves[p] || pk_required_set(g,p,g->teams[p][i].species)>=0) continue;
        g->forced_count[p][i]=(uint8_t)pk_deal_move_pair(g->teams[p][i].species,&g->move_rng,g->forced_moves[p][i]);
    }
}
static inline void pk_draft_masks(PKGame* g) {
    memset(g->masks, 0, sizeof(g->masks));
    for (int p = 0; p < 2; p++) {
        if (g->phase == PK_PHASE_MOVES) {
            int index=g->move_index[p];
            if(index==6 || g->move_stopped[p]) { g->masks[p][PK_MOVE_DONE]=1; continue; }
            PKMon* mon=&g->teams[p][index];
            int n=pk_move_count(mon), fixed=pk_required_set(g,p,mon->species), exact=-1;
            for(int j=0;j<6 && exact<0;j++) if(g->set_moves[p][j][0]==mon->species) exact=j;
            if(exact>=0) {
                int move=n<4?g->set_moves[p][exact][1+n]:0;
                g->masks[p][move?move-1:PK_MOVE_DONE]=1;
            } else if(fixed>=0) {
                int move=n<4?pk_set_move(fixed,n):0;
                g->masks[p][move?move-1:PK_MOVE_DONE]=1;
            } else if(n<g->forced_count[p][index]) {
                g->masks[p][g->forced_moves[p][index][n]-1]=1;
            } else {
                int any=0;
                for(int move=1;move<=164;move++) any|=g->masks[p][move-1]=(uint8_t)pk_move_allowed(mon,move);
                // A drafted Pokemon fills all four slots; it stops early only when no legal move is left
                // (Ditto, Magikarp, Caterpie and the like, or a forbidden-combination dead end).
                g->masks[p][PK_MOVE_DONE]=(uint8_t)(n>0 && !any);
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
// Pick phase: a party member not yet picked, as its species action; PK_MOVE_DONE once the party has nothing left.
static inline void pk_pick_masks(PKGame* g) {
    memset(g->masks, 0, sizeof(g->masks));
    for (int p = 0; p < 2; p++) {
        int any = 0;
        for (int i = 0; i < g->trainer_size[p]; i++) {
            int taken = 0;
            for (int k = 0; k < g->pick_count[p]; k++) taken |= g->picked[p][k] == i;
            if (!taken) any = g->masks[p][g->trainer_party[p][i].species - 1] = 1;
        }
        if (!any) g->masks[p][PK_MOVE_DONE] = 1;
    }
}
// Pick phase observation: both parties in the battle layout (species, level, types; own moves and base stats too),
// own picks marked in byte 7 of each slot with their pick order.
static inline void pk_pick_observe(PKGame* g) {
    for (int p = 0; p < 2; p++) {
        memset(g->obs[p], 0, PK_OBS);
        g->obs[p][0] = PK_PHASE_PICK;
        g->obs[p][1] = (uint8_t)g->picks;
        for (int side = 0; side < 2; side++) {
            int q = p ^ side;
            for (int i = 0; i < g->trainer_size[q]; i++) {
                const PKTrainerMon* mon = &g->trainer_party[q][i];
                uint8_t* out = g->obs[p] + 16 + side * 192 + i * 32;
                out[0] = mon->species; out[1] = 255; out[4] = mon->level;
                out[5] = pk_species_types[mon->species][0]; out[6] = pk_species_types[mon->species][1];
                if (side == 0) {
                    for (int k = 0; k < g->pick_count[p]; k++) if (g->picked[p][k] == i) out[7] = (uint8_t)(k + 1);
                    for (int m = 0; m < 4; m++) out[8 + m] = mon->moves[m];
                    for (int stat = 0; stat < 5; stat++) out[16 + stat] = (uint8_t)(pk_species_stats[mon->species][stat] / 4);
                }
            }
        }
        memcpy(g->obs[p] + 480, g->masks[p], PK_ACTIONS);
    }
}
static inline void pk_trainer_start(PKGame* g) {
    PKTrainerMon teams[12];
    memset(teams, 0, sizeof(teams));
    uint8_t sizes[2];
    for (int p = 0; p < 2; p++) {
        sizes[p] = g->pick_count[p];
        for (int k = 0; k < g->pick_count[p]; k++) teams[p * 6 + k] = g->trainer_party[p][g->picked[p][k]];
    }
    g->phase = PK_PHASE_BATTLE;
    g->battle_seed = pk_random(&g->rng);
    g->result = pk_start_trainers(&g->battle, g->battle_seed, teams, sizes, g->trainer_perfect);
}
static inline void pk_trainer_reset(PKGame* g) {
    int a = (int)(pk_random(&g->rng) % (uint64_t)pk_trainers.count), b = a;
    if ((pk_random(&g->rng) >> 40) * 0x1p-24f >= g->trainer_mirror) {
        for (int tries = 0; tries < 256; tries++) {
            b = (int)(pk_random(&g->rng) % (uint64_t)pk_trainers.count);
            if (b != a && fabsf(pk_trainers.elo[a] - pk_trainers.elo[b]) <= g->trainer_window) break;
        }
    }
    g->trainer_ids[0] = a;
    g->trainer_ids[1] = b;
    for (int p = 0; p < 2; p++) {
        int id = g->trainer_ids[p];
        memcpy(g->trainer_party[p], pk_trainers.teams[id], sizeof(pk_trainers.teams[id]));
        g->trainer_size[p] = pk_trainers.sizes[id];
        g->pick_count[p] = 0;
        // Without the pick phase the whole party battles
        if (!g->trainer_pick) for (int i = 0; i < g->trainer_size[p]; i++) g->picked[p][g->pick_count[p]++] = (uint8_t)i;
    }
    g->picks = 0;
    if (g->trainer_pick) {
        g->phase = PK_PHASE_PICK;
        pk_pick_masks(g);
    } else {
        pk_trainer_start(g);
    }
}
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
    memset(g->forced_count, 0, sizeof(g->forced_count));
    memset(&g->battle, 0, sizeof(g->battle));
    if (g->trainer_mode) {
        pk_trainer_reset(g);
        if (g->phase == PK_PHASE_PICK) pk_pick_observe(g); else pk_game_observe(g);
        return;
    }
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
    if (g->phase == PK_PHASE_PICK) {
        for (int p = 0; p < 2; p++) {
            if (actions[p] == PK_MOVE_DONE) continue;
            for (int i = 0; i < g->trainer_size[p]; i++) {
                int taken = 0;
                for (int k = 0; k < g->pick_count[p]; k++) taken |= g->picked[p][k] == i;
                if (!taken && g->trainer_party[p][i].species == actions[p] + 1) {
                    g->picked[p][g->pick_count[p]++] = (uint8_t)i;
                    break;
                }
            }
        }
        if (++g->picks == 3) {
            pk_trainer_start(g);
            pk_game_observe(g);
        } else {
            pk_pick_masks(g);
            pk_pick_observe(g);
        }
        return g->result;
    }
    if (g->phase == PK_PHASE_SPECIES) {
        for(int p=0;p<2;p++) g->teams[p][g->picks].species=(uint8_t)(actions[p]+1);
        if(++g->picks==6) { g->phase=PK_PHASE_MOVES; pk_deal_forced_moves(g); }
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
