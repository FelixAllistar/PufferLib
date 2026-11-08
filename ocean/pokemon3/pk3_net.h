// Structured Gen 3 network layout (2026-11-07), shared by the CUDA encoder/decoder (pokemon3_encoder.cu), the CPU
// reference forward (pk3_net_cpu.h) and the env's config hook (pokemon3.h). Gen 1's design (ocean/pokemon/
// pokemon_encoder.cu) over Gen 3's observation as it is (engine/src/lib.rs observe): shared move and Pokemon MLPs, a
// fusion MLP, and a decoder that scores battle actions by pointing at the move or Pokemon they mean.
//
// Observation (POKEMON3_OBS 5343 floats): 12 Pokemon x 404 (own six in party order, then the opponent's), 2 sides x 158,
// field 11 (weather 5, weather turns, turn; draft: drafting, picking species, picking set, drafted / 6), matchup 12 x 14
// (per move slot: effectiveness / 4, damage / max HP, KO; then the most damage and highest effectiveness the other
// active would deal it). Per Pokemon: 0 known, 1 active, 2 fainted, 3 HP, 4 max HP, 5 level, 6-10 stats, 11-30 types,
// 31-37 status, 38-317 four moves x 70 (0 = move known), 318-389 ability, 390-403 item.
#pragma once
#define P3_MON 404
#define P3_MONS 12
#define P3_MOVE 70
#define P3_MOVE_AT 38
#define P3_ABILITY_AT 318
#define P3_BASE (P3_MOVE_AT + (P3_MON - P3_ABILITY_AT))  // 124 Pokemon features without the move block
#define P3_SIDE_AT (P3_MONS * P3_MON)                      // 4848
#define P3_SIDES 316
#define P3_FIELD_AT (P3_SIDE_AT + P3_SIDES)                // 5164
#define P3_FIELD 11
#define P3_DRAFTING (P3_FIELD_AT + 7)                       // 1 while drafting
#define P3_MATCHUP_AT (P3_FIELD_AT + P3_FIELD)              // 5175
#define P3_MATCHUP 14
#define P3_OBS (P3_MATCHUP_AT + P3_MONS * P3_MATCHUP)       // 5343
#define P3_ACTIONS 128
#define P3_MOVE_IN (P3_MOVE + 3)                            // move features + its eff/dmg/KO
#define P3_MON_IN(e) (P3_BASE + (e) + 3)                    // + mean move embedding + 2 defensive + own flag
#define P3_FUSION(e) (P3_MONS * (e) + P3_SIDES + P3_FIELD)
#define P3_OWN_MOVES 24                                      // the decoder embeds own Pokemon's moves only
#define P3_AUG(n) (((n) + 8) & ~7)                          // input + bias column, padded to 8 (Gen 1's PK_AUG)

// policy.pokemon3_net = 1 turns the structured network on (0: PufferLib's default linear encoder/decoder, the old
// checkpoints). policy.pokemon_embed / pokemon_mid: embedding and entity-MLP widths (multiples of 8).
static int pk3_net_on = 0, pk3_embed = 32, pk3_mid = 64;

#ifdef __CUDACC__
#define P3_FN template<typename T> __host__ __device__ static inline
#define P3_T T
#else
#define P3_FN static inline
#define P3_T float
#endif
P3_FN float p3_obs(const P3_T* o, int i) { return (float)o[i]; }

// Move slot j of Pokemon m: feature f of the encoder's move input (70 move features, then eff, dmg, KO).
P3_FN float p3_move_input(const P3_T* o, int m, int j, int f) {
    if (f < P3_MOVE) return p3_obs(o, m * P3_MON + P3_MOVE_AT + j * P3_MOVE + f);
    return p3_obs(o, P3_MATCHUP_AT + m * P3_MATCHUP + 3 * j + f - P3_MOVE);
}
P3_FN int p3_move_known(const P3_T* o, int m, int j) { return p3_obs(o, m * P3_MON + P3_MOVE_AT + j * P3_MOVE) > 0.5f; }
P3_FN int p3_move_count(const P3_T* o, int m) {
    int n = 0;
    for (int j = 0; j < 4; j++) n += p3_move_known(o, m, j);
    return n;
}
// Pokemon m: feature f of the encoder's Pokemon input after the mean move embedding (base 124 first, then the mean,
// then these): 0-1 defensive matchup, 2 own side.
P3_FN float p3_mon_base(const P3_T* o, int m, int f) {
    return p3_obs(o, m * P3_MON + (f < P3_MOVE_AT ? f : f - P3_MOVE_AT + P3_ABILITY_AT));
}
P3_FN float p3_mon_tail(const P3_T* o, int m, int f) {
    if (f < 2) return p3_obs(o, P3_MATCHUP_AT + m * P3_MATCHUP + 12 + f);
    return m < 6;
}
P3_FN int p3_battle(const P3_T* o) { return p3_obs(o, P3_DRAFTING) < 0.5f; }
// Own active party slot (0-5), or -1 (draft, or nothing active).
P3_FN int p3_active(const P3_T* o) {
    for (int m = 0; m < 6; m++) if (p3_obs(o, m * P3_MON + 1) > 0.5f) return m;
    return -1;
}
#undef P3_FN
#undef P3_T
