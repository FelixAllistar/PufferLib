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
// In-game trainer parties (1-6 each, levels, trainer DVs, base PP): teams[0..6] P1, teams[6..12] P2.
typedef struct { uint8_t species, level, moves[4]; } PKTrainerMon;
int pk_start_trainers(PKBattle*, uint64_t seed, const PKTrainerMon teams[12], const uint8_t sizes[2], int perfect);
void pk_move_engine_info(int move, uint16_t out[6]);
int pk_update(PKBattle*, int p1, int p2);
// Single-player rules (cartridge build of our engine fork): the player (P1) against an in-game trainer (P2) of `class`
// under the cartridge trainer AI, with the player's badges; pk_ai_action is the AI's P2 action (-1 without one).
int pk_start_vs_ai(PKBattle*, uint64_t seed, const PKTrainerMon teams[12], const uint8_t sizes[2], int player_perfect,
    uint8_t cls, uint8_t badges);
int pk_ai_action(PKBattle*);
// Showdown-protocol text of the next battle started on this thread into buf (len bytes used); pk_text_len after updates.
void pk_text_next(char* buf, uint32_t cap, uint32_t len);
uint32_t pk_text_len(const PKBattle*);
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
// Benchmark-only full-state export. Never fed to the learned policy.
int pk_benchmark_state(const PKBattle*, uint8_t out[384]);
int pk_benchmark_choice(const PKBattle*, int player, int encoded_choice);
void pk_benchmark_fields(const PKBattle*, uint16_t out[192]);
#ifdef __cplusplus
}
#endif
