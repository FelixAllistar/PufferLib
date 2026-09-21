#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "catalog_meta.h"
#include "freepick.h"
#define PK_ACTIONS 168
#define PK_OBS 648
#define PK_ABI_VERSION 3
#define PK_MOVE_DONE 164
#define PK_PHASE_SPECIES 0
#define PK_PHASE_MOVES 1
#define PK_PHASE_BATTLE 2
typedef struct { uint64_t words[64]; } PKBattle;
// Results: 0 ongoing, 1 P1 wins, 2 P2 wins, 3 tie, 4 engine error.
int pk_start(PKBattle*, uint64_t seed, const uint16_t teams[12]);
int pk_start_free(PKBattle*, uint64_t seed, const PKMon teams[12]);
void pk_move_engine_info(int move, uint16_t out[6]);
int pk_update(PKBattle*, int p1, int p2);
void pk_mask(const PKBattle*, int player, uint8_t out[PK_ACTIONS]);
void pk_observe(const PKBattle*, int player, uint8_t out[PK_OBS]);
int pk_turn(const PKBattle*);
void pk_reseed(PKBattle*, uint64_t seed);
void pk_behavior(const PKBattle*, int player, float out[2]);
int pk_species(int set);
const char* pk_set_name(int set);
const char* pk_move_name(int move);
int pk_set_move(int set, int slot);
int pk_species_set(int species, int variant);
int pk_species_count(int species);
#ifdef __cplusplus
}
#endif
