#pragma once
#include "bridge.h"
#include <assert.h>
#include <string.h>

typedef struct {
    PKBattle battle;
    uint64_t rng;
    uint64_t battle_seed;
    uint16_t teams[2][6];
    int fixed_team[2][6];
    int fixed_enabled[2];
    int required_count[2]; // mode 2: required sets, arbitrary order, free remaining slots
    uint8_t masks[2][PK_ACTIONS];
    uint8_t obs[2][PK_OBS];
    int draft;       // 0 sampled teams, 1 independent private set drafting.
    int picks;
    int selecting_set;
    int pending_species[2];
    int max_updates;
    int updates;
    int invalid_actions;
    int result;      // Engine results; 5 means wrapper time limit (draw).
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
    if (g->fixed_enabled[p] != 2) return -1;
    for (int i = 0; i < g->required_count[p]; i++)
        if (pk_species(g->fixed_team[p][i]) == species) return g->fixed_team[p][i];
    return -1;
}
static inline void pk_draft_masks(PKGame* g) {
    memset(g->masks, 0, sizeof(g->masks));
    for (int p = 0; p < 2; p++) {
        if (g->fixed_enabled[p] == 1) {
            int set = g->fixed_team[p][g->picks];
            int species = pk_species(set), action = species - 1;
            if (g->selecting_set) {
                for (action = 0; action < pk_species_count(species); action++)
                    if (pk_species_set(species, action) == set) break;
                assert(action < pk_species_count(species));
            }
            g->masks[p][action] = 1;
            continue;
        }
        if (g->selecting_set) {
            int species = g->pending_species[p];
            int required = pk_required_set(g,p,species);
            for (int a = 0; a < pk_species_count(species); a++)
                g->masks[p][a] = required < 0 || pk_species_set(species,a) == required;
            continue;
        }
        int missing = g->fixed_enabled[p] == 2 ? g->required_count[p] : 0;
        for (int i = 0; i < g->picks; i++)
            if (pk_required_set(g,p,pk_species(g->teams[p][i])) >= 0) missing--;
        for (int a = 0; a < 149; a++) {
            // Reserve enough slots for all not-yet-picked required species.
            int allowed = missing < 6-g->picks || pk_required_set(g,p,a+1) >= 0;
            for (int i = 0; i < g->picks; i++)
                if (a + 1 == pk_species(g->teams[p][i])) allowed = 0;
            g->masks[p][a] = (uint8_t)allowed;
        }
    }
}
static inline void pk_game_observe(PKGame* g) {
    for (int p = 0; p < 2; p++) {
        if (g->picks == 6) {
            pk_observe(&g->battle, p, g->obs[p]);
            memcpy(g->masks[p], g->obs[p] + 480, PK_ACTIONS);
        } else {
            memset(g->obs[p], 0, PK_OBS);
            g->obs[p][1] = (uint8_t)g->picks;
            g->obs[p][12] = (uint8_t)g->selecting_set;
            g->obs[p][13] = g->selecting_set ? (uint8_t)g->pending_species[p] : 0;
            for (int i = 0; i < g->picks; i++) {
                g->obs[p][16 + i * 32] = (uint8_t)pk_species(g->teams[p][i]);
                for (int m = 0; m < 4; m++) g->obs[p][24 + i * 32 + m] = (uint8_t)pk_set_move(g->teams[p][i], m);
            }
        }
        // Own chosen set IDs are known in both phases. Never reveal foe picks.
        for (int i = 0; i < g->picks; i++) {
            unsigned set = g->teams[p][i] + 1;
            g->obs[p][464 + 2*i] = (uint8_t)set;
            g->obs[p][465 + 2*i] = (uint8_t)(set >> 8);
        }
        memcpy(g->obs[p] + 480, g->masks[p], PK_ACTIONS);
    }
}
static inline void pk_game_reset(PKGame* g) {
    g->picks = 0;
    g->selecting_set = 0;
    g->pending_species[0] = g->pending_species[1] = 0;
    g->updates = 0;
    g->invalid_actions = 0;
    g->result = 0;
    memset(g->teams, 0, sizeof(g->teams));
    memset(&g->battle, 0, sizeof(g->battle));
    if (!g->draft) {
        for (; g->picks < 6; g->picks++) {
            pk_draft_masks(g);
            for (int p = 0; p < 2; p++) {
                int species = pk_random_action(g->masks[p], &g->rng) + 1;
                int variant = (int)(pk_random(&g->rng) % (unsigned)pk_species_count(species));
                int required = pk_required_set(g,p,species);
                g->teams[p][g->picks] = g->fixed_enabled[p] == 1
                    ? g->fixed_team[p][g->picks] : required >= 0 ? required : (uint16_t)pk_species_set(species, variant);
            }
        }
        g->battle_seed = pk_random(&g->rng);
        g->result = pk_start(&g->battle, g->battle_seed, &g->teams[0][0]);
        assert(g->result == 0);
    } else pk_draft_masks(g);
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
    if (g->picks < 6) {
        if (!g->selecting_set) {
            for (int p = 0; p < 2; p++) g->pending_species[p] = actions[p] + 1;
            g->selecting_set = 1;
            pk_draft_masks(g);
        } else {
        for (int p = 0; p < 2; p++) g->teams[p][g->picks] = (uint16_t)pk_species_set(g->pending_species[p], actions[p]);
        g->selecting_set = 0;
        if (++g->picks == 6) {
            g->battle_seed = pk_random(&g->rng);
            g->result = pk_start(&g->battle, g->battle_seed, &g->teams[0][0]);
        } else pk_draft_masks(g);
        }
    } else {
        g->updates++;
        g->result = pk_update(&g->battle, actions[0], actions[1]);
        if (g->result < 0) g->result = 4;
        if (!g->result && g->updates >= g->max_updates) g->result = 5;
    }
    pk_game_observe(g);
    return g->result;
}
