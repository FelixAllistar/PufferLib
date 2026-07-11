// Native Pokemon TCG AI Battle environment for PufferLib.
//
// First milestone: one learning agent plays player 0 against an internal
// random player 1. The hot loop uses the official C++ engine directly; no
// Python, JSON, or ctypes are involved during rollout.

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

// The official engine also defines a struct named Log. Rename it inside this
// translation unit so Puffer's float-only Log can keep the expected name.
#define Log PtcgEngineLog
#include "engine/All.h"
#undef Log

#define PTCG_OBS_VERSION 5
#define PTCG_STATE_DIM 224
#define PTCG_OPTION_DIM 128
#define PTCG_MAX_OPTIONS 128
#define PTCG_STOP_ACTION PTCG_MAX_OPTIONS
#define PTCG_ACTIONS (PTCG_MAX_OPTIONS + 1)
#define PTCG_OBS_SIZE (PTCG_STATE_DIM + PTCG_ACTIONS * PTCG_OPTION_DIM)
#define PTCG_MAX_EPISODE_STEPS 512
#define PTCG_LEARNER 0
#define PTCG_OPPONENT_MIXED_POOL -1
#define PTCG_OPPONENT_LEGACY_EXACT_POOL -2
#define PTCG_OPPONENT_NEW_GENERIC_POOL -3
#define PTCG_OPPONENT_META_GENERIC_POOL -4
#define PTCG_OPPONENT_CUSTOM_POOL -99
#define PTCG_OPPONENT_SELFPLAY -100
#define PTCG_OPPONENT_STARTER_RANDOM 0
#define PTCG_OPPONENT_KIYOTA_ABOMASNOW 2
#define PTCG_OPPONENT_KIYOTA_IONO 3
#define PTCG_OPPONENT_KIYOTA_LUCARIO 4
#define PTCG_OPPONENT_DRAGAPULT_GENERIC 5
#define PTCG_OPPONENT_DASHIMAKI_CRUSTLE 6
#define PTCG_OPPONENT_ALAKAZAM_GENERIC 7
#define PTCG_OPPONENT_ABOMASNOW_SAMPLE_GENERIC 8
#define PTCG_OPPONENT_ABOMASNOW_RL_GENERIC 9
#define PTCG_OPPONENT_LUCARIO_RETUNED_GENERIC 10
#define PTCG_OPPONENT_LUCARIO_MULTIPLY_GENERIC 11
#define PTCG_OPPONENT_LUCARIO_BEGINNER_GENERIC 12
#define PTCG_OPPONENT_IONO_GENERIC 13
#define PTCG_OPPONENT_SAMPLE_GENERIC 14
#define PTCG_OPPONENT_FIRE_ZARD_X_GENERIC 15
#define PTCG_OPPONENT_KANGASKHAN_BOX 16
#define PTCG_OPPONENT_HYDRAPPLE_EX 17
#define PTCG_OPPONENT_CRUSTLE_LIMITLESS 18
#define PTCG_OPPONENT_DRAGAPULT_EXACT 19
#define PTCG_OPPONENT_ARCHALUDON_EXACT 20
#define PTCG_OPPONENT_MAGCARGO_EXACT 21
#define PTCG_OPPONENT_IRON_THORNS_EXACT 22
#define PTCG_OPPONENT_XINPW8_ARCHALUDON 23
#define PTCG_OPPONENT_GREAT_TUSK_CRUSTLE 24
#define PTCG_ATTACK_REWARD 0.0f
#define PTCG_KO_ATTACK_REWARD_DEFAULT 0.05f
#define PTCG_ARCHALUDON_SHAPING_SCALE 0.25f
#define PTCG_ARCHALUDON_SHAPING_GAMMA 0.998f

typedef struct {
    float perf;
    float score;
    float win_rate;
    float episode_return;
    float reward_shaping;
    float reward_attack_bonus;
    float reward_ko_attack_bonus;
    float reward_archaludon_potential;
    float diag_attack_prize_gain;
    float diag_attack_prize_rate;
    float episode_length;
    float games;
    float invalid_actions;
    float timeouts;
    float option_truncations;
    float max_option_count_seen;
    float stop_mask_errors;
    float invalid_mask_legal;
    float invalid_mask_illegal;
    float invalid_stop_actions;
    float invalid_repeat_actions;
    float invalid_range_actions;
    float invalid_action_sum;
    float invalid_action_max;
    float invalid_option_limit_sum;
    float micro_selects;
    float submitted_selects;
    float action_total;
    float action_internal_stop;
    float action_official_end;
    float action_official_yes;
    float action_official_no;
    float action_play;
    float action_attach;
    float action_evolve;
    float action_ability;
    float action_discard;
    float action_retreat;
    float action_attack;
    float action_target_select;
    float action_other;
    float diag_learner_decisions;
    float diag_attack_available;
    float diag_attack_chosen_when_available;
    float diag_ko_available;
    float diag_ko_chosen;
    float diag_ko_miss_play;
    float diag_ko_miss_attach;
    float diag_ko_miss_evolve;
    float diag_ko_miss_ability;
    float diag_ko_miss_end;
    float diag_ko_miss_retreat;
    float diag_ko_miss_other;
    float diag_archaludon_in_play;
    float diag_cinderace_in_play;
    float diag_metal_energy_in_play;
    float diag_metal_energy_in_hand;
    float diag_full_metal_lab_in_play;
    float diag_archaludon_attack_available;
    float diag_archaludon_attack_chosen;
    float diag_cinderace_attack_available;
    float diag_cinderace_attack_chosen;
    float diag_judge_available;
    float diag_judge_play;
    float diag_jumbo_ice_cream_available;
    float diag_jumbo_ice_cream_play;
    float diag_duraludon_bench_available;
    float diag_duraludon_bench_chosen;
    float diag_metal_discard_available;
    float diag_metal_discard_chosen;
    float diag_archaludon_evolve_available;
    float diag_archaludon_evolve_chosen;
    float diag_assemble_alloy_available;
    float diag_assemble_alloy_chosen;
    float diag_assemble_alloy_selectable_available;
    float diag_assemble_alloy_selectable_chosen;
    float diag_metal_defender_available;
    float diag_metal_defender_chosen;
    float diag_turns;
    float diag_turn_attack_available;
    float diag_turn_attacked_when_available;
    float diag_turn_ko_available;
    float diag_turn_ko_taken_when_available;
    float diag_turn_ended_with_attack_available;
    float diag_turn_ended_with_ko_available;
    float diag_turn_best_attack_prizes_available;
    float diag_turn_best_attack_prizes_taken;
    float diag_turn_metal_defender_available;
    float diag_turn_metal_defender_chosen_when_available;
    float diag_attach_available;
    float diag_attach_chosen;
    float diag_play_available;
    float diag_play_chosen;
    float diag_evolve_available;
    float diag_evolve_chosen;
    float diag_ability_available;
    float diag_ability_chosen;
    float diag_end_available;
    float diag_end_chosen;
    float slot_0_score;
    float slot_1_score;
    float hist_score;
    float hist_n;
    float hist_score_bank[8];
    float hist_n_bank[8];
    float n;
} Log;

typedef struct {
    Log log;
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    unsigned char* action_mask;
    unsigned char* obs_ptr[2];
    float* action_ptr[2];
    float* reward_ptr[2];
    float* terminal_ptr[2];
    unsigned char* action_mask_ptr[2];
    int num_agents;
    int opponent;
    int current_opponent;
    int player_deck;
    int opponent_pool[8];
    float opponent_weights[8];
    int opponent_pool_size;
    float opponent_weight_sum;
    float ko_attack_reward;
    float ko_attack_mask;
    unsigned int rng;

    BattleData* battle;
    int episode_length;
    float episode_return;
    bool pending_attack_action;
    int pending_attack_prizes_taken;
    int pending_attack_turn;
    float previous_archaludon_potential;
    bool diag_turn_open;
    int diag_turn_id;
    bool diag_turn_attack_available_seen;
    bool diag_turn_attack_taken;
    bool diag_turn_ko_available_seen;
    bool diag_turn_ko_taken;
    bool diag_turn_metal_defender_available_seen;
    bool diag_turn_metal_defender_taken;
    float diag_turn_best_attack_prizes_available_seen;
    float diag_turn_best_attack_prizes_taken_seen;
    int tag;
    int boundary_reached;
} PTCG;

#define OBS_SIZE PTCG_OBS_SIZE
#define NUM_ATNS 1
#define ACT_SIZES {PTCG_ACTIONS}
#define OBS_TENSOR_T ByteTensor
#define MY_ACTION_MASK PTCG_ACTIONS
#define MY_VEC_INIT
#define MY_USES_PERM
#define MY_USES_TAGS
#define Env PTCG

void c_reset(Env* env);
void c_step(Env* env);
void c_render(Env* env);
void c_close(Env* env);

#include "vecenv.h"
#include "kiyota_abomasnow_scorer.h"

void my_init(Env* env, Dict* kwargs);

void my_setup_perm(StaticVec* vec, Env* env, int slot_base) {
    size_t obs_elem_size = obs_element_size();
    for (int s = 0; s < env->num_agents; s++) {
        int phys = vec->agent_perm ? vec->agent_perm[slot_base + s] : (slot_base + s);
        env->obs_ptr[s] = (unsigned char*)vec->observations + (size_t)phys * PTCG_OBS_SIZE * obs_elem_size;
        env->action_mask_ptr[s] = vec->action_mask + (size_t)phys * PTCG_ACTIONS;
        env->action_ptr[s] = vec->actions + (size_t)phys * NUM_ATNS;
        env->reward_ptr[s] = vec->rewards + phys;
        env->terminal_ptr[s] = vec->terminals + phys;
    }
}

Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
                 Dict* vec_kwargs, Dict* env_kwargs) {
    int total_agents = (int)dict_get(vec_kwargs, "total_agents")->value;
    int num_buffers = (int)dict_get(vec_kwargs, "num_buffers")->value;
    DictItem* opponent = dict_get_unsafe(env_kwargs, "opponent");
    int opponent_id = opponent == nullptr ? 0 : (int)opponent->value;
    int agents_per_env = opponent_id == PTCG_OPPONENT_SELFPLAY ? 2 : 1;
    int num_envs = std::max(1, total_agents / agents_per_env);
    Env* envs = (Env*)calloc(num_envs, sizeof(Env));
    for (int i = 0; i < num_envs; i++) {
        my_init(&envs[i], env_kwargs);
        envs[i].rng = i;
    }

    int agents_per_buffer = total_agents / num_buffers;
    int buf = 0;
    int buf_agents = 0;
    buffer_env_starts[0] = 0;
    buffer_env_counts[0] = 0;
    for (int i = 0; i < num_envs; i++) {
        buf_agents += agents_per_env;
        buffer_env_counts[buf]++;
        if (buf_agents >= agents_per_buffer && buf < num_buffers - 1) {
            buf++;
            buffer_env_starts[buf] = i + 1;
            buffer_env_counts[buf] = 0;
            buf_agents = 0;
        }
    }

    *num_envs_out = num_envs;
    return envs;
}

static constexpr std::array<int, DECK_SIZE> SAMPLE_DECK = {
    1158, 721, 721, 722, 722, 722, 722, 723, 723, 723,
    723, 1145, 1145, 1145, 1145, 1205, 1205, 1227, 1227, 1227,
    1227, 1235, 1235, 1235, 1235, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

static constexpr std::array<int, DECK_SIZE> KIYOTA_ABOMASNOW_DECK = {
    721, 721,
    722, 722, 722, 722,
    723, 723, 723, 723,
    1121, 1121, 1121, 1121,
    1126,
    1192, 1192, 1192, 1192,
    1227, 1227, 1227, 1227,
    1262, 1262, 1262,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3,
};

static constexpr std::array<int, DECK_SIZE> KIYOTA_IONO_DECK = {
    265, 265, 265,
    268, 268, 268,
    269, 269, 269,
    270, 270, 270,
    271, 271, 271,
    1086, 1086, 1086,
    1097, 1097,
    1110,
    1118,
    1121, 1121, 1121,
    1152, 1152,
    1227, 1227, 1227, 1227,
    1233, 1233, 1233, 1233,
    1254, 1254, 1254,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

static constexpr std::array<int, DECK_SIZE> KIYOTA_LUCARIO_DECK = {
    673, 673,
    674, 674,
    675, 675,
    676, 676, 676,
    677, 677, 677,
    678, 678, 678, 678,
    1102, 1102, 1102, 1102,
    1123, 1123,
    1141, 1141, 1141, 1141,
    1142, 1142, 1142, 1142,
    1152, 1152, 1152, 1152,
    1159,
    1182, 1182,
    1192, 1192, 1192, 1192,
    1227, 1227, 1227, 1227,
    1252, 1252,
    6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6,
};

static constexpr std::array<int, DECK_SIZE> DRAGAPULT_DECK = {
    119, 119, 119, 119,
    120, 120, 120, 120,
    121, 121, 121,
    140,
    184,
    235, 235,
    1071,
    1079, 1079,
    1080,
    1086, 1086, 1086, 1086,
    1097, 1097,
    1120, 1120, 1120, 1120,
    1121, 1121, 1121, 1121,
    1152, 1152, 1152,
    1156,
    1182, 1182, 1182,
    1198, 1198, 1198, 1198,
    1210, 1210,
    1227, 1227, 1227, 1227,
    1256, 1256,
    2, 2, 2, 2,
    5, 5, 5, 5,
};

static constexpr std::array<int, DECK_SIZE> ARCHALUDON_DECK = {
    169, 169, 169, 169,       // Duraludon
    190, 190, 190, 190,       // Archaludon ex
    666, 666, 666, 666,       // Cinderace
    1244,                     // Full Metal Lab
    57,                       // Relicanth
    1152, 1152, 1152, 1152,   // Poke Pad
    1121, 1121, 1121, 1121,   // Ultra Ball
    1213, 1213,               // Judge
    1122, 1122,               // Pokegear 3.0
    1097, 1097, 1097,         // Night Stretcher
    1147, 1147, 1147, 1147,   // Jumbo Ice Cream
    1159,                     // Hero's Cape
    1182, 1182, 1182, 1182,   // Boss's Orders
    1185, 1185, 1185, 1185,   // Explorer's Guidance
    1227, 1227, 1227, 1227,   // Lillie's Determination
    1244, 1244, 1244,         // Full Metal Lab
    8, 8, 8, 8, 8, 8,         // Basic Metal Energy
    8, 8, 8, 8, 8,
};

static constexpr std::array<int, DECK_SIZE> XINPW8_ARCHALUDON_DECK = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8,                       // Basic Metal Energy
    57,                      // Relicanth
    169, 169, 169, 169,      // Duraludon
    190, 190, 190, 190,      // Archaludon ex
    666, 666, 666, 666,      // Cinderace
    1097, 1097, 1097,        // Night Stretcher
    1121, 1121, 1121, 1121,  // Ultra Ball
    1122, 1122, 1122, 1122,  // Pokegear 3.0
    1147, 1147, 1147, 1147,  // Jumbo Ice Cream
    1152, 1152, 1152, 1152,  // Poke Pad
    1159,                    // Hero's Cape
    1182, 1182, 1182,        // Boss's Orders
    1185, 1185, 1185, 1185,  // Explorer's Guidance
    1213, 1213, 1213, 1213,  // Judge
    1227, 1227, 1227, 1227,  // Lillie's Determination
    1244,                    // Full Metal Lab
};

static constexpr std::array<int, DECK_SIZE> GREAT_TUSK_CRUSTLE_DECK = {
    58, 58, 58, 58,          // Great Tusk
    344, 344, 344, 344,      // Dwebble
    345, 345,                // Crustle
    1142, 1142, 1142, 1142,  // Fight Gong
    1152, 1152, 1152, 1152,  // Poke Pad
    1086, 1086, 1086, 1086,  // Buddy-Buddy Poffin
    1122, 1122, 1122, 1122,  // Pokegear 3.0
    1121,                    // Ultra Ball
    1123, 1123, 1123, 1123,  // Switch
    1197, 1197, 1197, 1197,  // Xerosic's Machinations
    1185, 1185, 1185, 1185,  // Explorer's Guidance
    1182, 1182, 1182, 1182,  // Boss's Orders
    1204, 1204,              // Lisia's Appeal
    1194, 1194,              // Colress's Tenacity
    1247,                    // Neutral Center
    1147,                    // Jumbo Ice Cream
    20, 20, 20, 20,          // Rock Fighting Energy
    11, 11, 11, 11,          // Mist Energy
    345, 345,                // Crustle
    607,                     // Terrakion
};

static constexpr std::array<int, DECK_SIZE> MAGCARGO_DECK = {
    30, 30, 30, 30,
    76, 76, 76, 76,
    572, 572, 572,
    358, 358, 358,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1165,
    1248, 1248, 1248, 1248,
    1127, 1127, 1127, 1127,
    1121, 1121, 1121, 1121,
    1102, 1102,
    1225, 1225, 1225, 1225,
    1227,
    1192,
    1182, 1182,
    1123, 1123,
    1097,
};

static constexpr std::array<int, DECK_SIZE> IRON_THORNS_DECK = {
    37, 37, 37, 37,
    87, 87, 87, 87,
    313, 313, 313, 313,
    80, 80, 80, 80,
    1121, 1121, 1121, 1121,
    1192, 1192, 1192, 1192,
    1227, 1227, 1227, 1227,
    1182, 1182, 1182,
    1123, 1123, 1123, 1123,
    1116, 1116, 1116,
    1163, 1163,
    1118,
    1097,
    1119,
    1089,
    11, 11,
    4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_DECK = {
    344, 344, 344, 344,
    345, 345, 345, 345,
    1147, 1147, 1147, 1147,
    1159,
    1264, 1264, 1264, 1264,
    1212, 1212, 1212, 1212,
    1224, 1224, 1224, 1224,
    18, 18, 18, 18,
    11, 11, 11, 11,
    1086, 1086, 1086, 1086,
    14, 14, 14, 14,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_LIMITLESS_DECK = {
    // Pokemon: 10
    756, 756, 756, 756,       // Mega Kangaskhan ex
    344, 344, 344,            // Dwebble
    345, 345, 345,            // Crustle

    // Trainers: 37
    1227, 1227, 1227, 1227,   // Lillie's Determination
    1182, 1182, 1182, 1182,   // Boss's Orders
    1219, 1219, 1219, 1219,   // Team Rocket's Petrel
    1225, 1225,               // Hilda
    1186, 1186,               // Eri
    1197,                     // Xerosic's Machinations
    1212,                     // Cook (Pokemon Center Lady replacement)
    1190,                     // Bianca's Devotion
    1204,                     // Lisia's Appeal
    1147, 1147, 1147, 1147,   // Jumbo Ice Cream
    1122, 1122, 1122,         // Pokegear 3.0
    1086, 1086,               // Buddy-Buddy Poffin
    1121,                     // Ultra Ball
    1123,                     // Switch
    1087,                     // Hand Trimmer
    1159,                     // Hero's Cape
    1161,                     // Handheld Fan
    1257,                     // Team Rocket's Factory
    1242,                     // Community Center
    1245,                     // Festival Grounds

    // Energy: 13
    14, 14, 14, 14,           // Spiky Energy
    18, 18, 18, 18,           // Grow Grass Energy
    11, 11, 11, 11,           // Mist Energy
    1,                        // Basic Grass Energy
};

// BEGIN GENERATED CRUSTLE SEARCH DECKS
static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_000_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_001_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1182, 1182,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_002_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1227, 1227, 1227, 1227,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_003_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1227, 1227,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_004_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1182, 1122,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_005_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1122, 1122,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_006_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1123, 1123,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_007_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1186, 1186,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_008_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1081, 1081,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_009_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1219, 1219, 1219, 1219,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_010_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1182, 1182, 1219, 1219,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_011_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1227, 1227, 1227, 1227, 1182, 1182,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_012_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1227, 1227, 1227, 1227, 1122, 1122,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_013_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1122, 1122, 1122, 1122, 1182, 1182,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_014_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1182, 1182, 1123, 1161, 1197,
};

static constexpr std::array<int, DECK_SIZE> CRUSTLE_SEARCH_015_DECK = {
    344, 344, 344, 344, 345, 345, 345, 345, 1147, 1147,
    1147, 1147, 1159, 1264, 1264, 1264, 1264, 1212, 1212, 1212,
    1212, 1224, 1224, 1224, 1224, 18, 18, 18, 18, 11,
    11, 11, 11, 1086, 1086, 1086, 1086, 14, 14, 14,
    14, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1182, 1182, 1186, 1087, 1081,
};
// END GENERATED CRUSTLE SEARCH DECKS

static constexpr std::array<int, DECK_SIZE> ALAKAZAM_DECK = {
    741, 741, 741, 741,
    742, 742, 742, 742,
    743, 743, 743,
    305, 305, 305,
    66, 66,
    140,
    142,
    858,
    343,
    1152, 1152, 1152, 1152,
    1086, 1086, 1086, 1086,
    1079, 1079, 1079,
    1097,
    1129,
    1156, 1156, 1156,
    1081, 1081, 1081,
    1182, 1182,
    1231, 1231, 1231, 1231,
    1225, 1225, 1225, 1225,
    1264, 1264, 1264, 1264,
    5, 5,
    19, 19, 19, 19,
    13,
};

static constexpr std::array<int, DECK_SIZE> ABOMASNOW_SAMPLE_DECK = {
    1158,
    721, 721,
    722, 722, 722, 722,
    723, 723, 723, 723,
    1145, 1145, 1145, 1145,
    1205, 1205,
    1227, 1227, 1227, 1227,
    1235, 1235, 1235, 1235,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3,
};

static constexpr std::array<int, DECK_SIZE> ABOMASNOW_RL_DECK = {
    721, 721,
    722, 722, 722, 722,
    723, 723, 723, 723,
    1092,
    1121, 1121,
    1145, 1145,
    1163, 1163,
    1219, 1219, 1219, 1219,
    1227, 1227, 1227, 1227,
    1262, 1262,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3,
};

static constexpr std::array<int, DECK_SIZE> LUCARIO_RETUNED_DECK = {
    678, 678, 678, 678,
    677, 677, 677, 677,
    673, 673, 673,
    674, 674, 674,
    676, 676, 676,
    675, 675,
    1102, 1102, 1102, 1102,
    1152, 1152, 1152, 1152,
    1192, 1192, 1192, 1192,
    1142, 1142, 1142,
    1123, 1123, 1123,
    1141, 1141,
    1227, 1227, 1227,
    1252, 1252,
    1182, 1182,
    1159,
    6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6,
};

static constexpr std::array<int, DECK_SIZE> LUCARIO_MULTIPLY_DECK = {
    673, 673,
    674, 674,
    675, 675,
    676, 676, 676,
    677, 677, 677, 677,
    678, 678, 678, 678,
    1102, 1102, 1102, 1102,
    1123, 1123,
    1141, 1141, 1141, 1141,
    1142, 1142, 1142, 1142,
    1152, 1152,
    1159,
    1182, 1182, 1182,
    1192, 1192, 1192, 1192,
    1227, 1227, 1227, 1227,
    1252,
    6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6,
};

static constexpr std::array<int, DECK_SIZE> LUCARIO_NUR_DECK = {
    673, 673, 673,
    674, 674, 674,
    675, 675,
    676, 676, 676,
    677, 677, 677, 677,
    678, 678, 678, 678,
    1102, 1102, 1102, 1102,
    1123, 1123, 1123,
    1141, 1141,
    1142, 1142, 1142,
    1152, 1152, 1152, 1152,
    1159,
    1182, 1182,
    1192, 1192, 1192, 1192,
    1227, 1227, 1227,
    1252, 1252,
    6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6,
};

static constexpr std::array<int, DECK_SIZE> FIRE_ZARD_X_TROLLEY_DECK = {
    // Pokemon: 16
    788, 788, 788, 788,
    789, 789, 789,
    790, 790, 790,
    795, 795,
    31, 31,
    794,
    140,

    // Trainers: 27
    1227, 1227, 1227, 1227,
    1121, 1121, 1121, 1121,
    1079, 1079, 1079, 1079,
    1232, 1232, 1232, 1232,
    1122, 1122, 1122,
    1152, 1152,
    1182, 1182,
    1123, 1123,
    1097,
    1126,

    // Energy: 17
    2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
};

static constexpr std::array<int, DECK_SIZE> KANGASKHAN_BOX_DECK = {
    // Pokemon: 19
    756, 756, 756,          // Mega Kangaskhan ex
    1071, 1071, 1071,       // Meowth ex
    96, 96, 96,             // Teal Mask Ogerpon ex
    63, 63,                 // Raging Bolt ex
    184, 184,               // Latias ex
    272,                    // Lillie's Clefairy ex
    108,                    // Wellspring Mask Ogerpon ex
    75,                     // Iron Leaves ex
    140,                    // Fezandipiti ex
    978,                    // Passimian
    209,                    // Chien-Pao

    // Trainers: 27
    1198, 1198, 1198, 1198, // Crispin
    1182, 1182,             // Boss's Orders
    1205, 1205,             // Cyrano
    1188,                   // Ciphermaniac's Codebreaking
    1227,                   // Lillie's Determination
    1121, 1121, 1121, 1121, // Ultra Ball
    1116, 1116, 1116, 1116, // Energy Switch
    1097, 1097,             // Night Stretcher
    1098, 1098,             // Glass Trumpet
    1080,                   // Unfair Stamp
    1250, 1250, 1250, 1250, // Area Zero Underdepths

    // Energy: 14
    1, 1, 1, 1, 1, 1, 1,    // Grass
    4, 4,                   // Lightning
    6, 6,                   // Fighting
    5, 5,                   // Psychic
    3,                      // Water
};

static constexpr std::array<int, DECK_SIZE> HYDRAPPLE_EX_DECK = {
    // Pokemon: 22
    96, 96, 96, 96,
    92, 92,
    93, 93,
    150, 150,
    917,
    708,
    918,
    709,
    710, 710,
    172,
    173,
    1071,
    140,
    655,
    920,

    // Trainers: 24
    1227, 1227, 1227, 1227,
    1182, 1182,
    1231, 1231,
    1201,
    1184,
    1094, 1094, 1094, 1094,
    1152, 1152,
    1121, 1121,
    1097,
    1080,
    1213,
    1261, 1261, 1261,

    // Energy: 14
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
};

static std::once_flag ptcg_init_once;

static void ensure_ptcg_initialized() {
    std::call_once(ptcg_init_once, []() {
        InitializeAll();
    });
}

static unsigned char byte_clamp(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (unsigned char)value;
}

static unsigned char flag_byte(bool value) {
    return value ? 1 : 0;
}

static void put_byte(unsigned char* obs, int* idx, int limit, int value) {
    if (*idx >= limit) return;
    obs[(*idx)++] = byte_clamp(value);
}

static void put_u16(unsigned char* obs, int* idx, int limit, int value) {
    if (*idx + 1 >= limit) return;
    if (value < 0) value = 0;
    obs[(*idx)++] = (unsigned char)(value & 0xff);
    obs[(*idx)++] = (unsigned char)((value >> 8) & 0xff);
}

static void put_s16(unsigned char* obs, int* idx, int limit, int value) {
    if (*idx + 1 >= limit) return;
    unsigned short encoded = (unsigned short)((short)value);
    obs[(*idx)++] = (unsigned char)(encoded & 0xff);
    obs[(*idx)++] = (unsigned char)((encoded >> 8) & 0xff);
}

static bool selected_contains(const State& state, int option_index) {
    return std::find(state.selected.begin(), state.selected.end(), option_index) != state.selected.end();
}

static bool can_stop_now(const State& state) {
    return (int)state.selected.size() >= state.selectMin;
}

static CardRef safe_get_card_ref(const State& state, AreaType area, int area_index, int player_index) {
    try {
        if (player_index < 0 || player_index > 1) return CardRef();
        return state.getCardRef(area, area_index, player_index);
    } catch (...) {
        return CardRef();
    }
}

static CardRef primary_card_ref_for_option(const State& state, const SelectOption& option) {
    int player = state.selectPlayer;
    try {
        switch (option.type) {
            case SelectOptionType::Card:
            case SelectOptionType::ToolCard:
            case SelectOptionType::EnergyCard:
                return state.getCardRef(option);
            case SelectOptionType::Play:
                return state.getPlayCardRef(option, player);
            case SelectOptionType::Attach:
                return state.getAttachCardRef(option, player);
            case SelectOptionType::Evolve:
                return safe_get_card_ref(state, AreaType::Hand, option.param1, player);
            case SelectOptionType::Ability:
                return state.getAbilityCardRef(option, player);
            case SelectOptionType::Discard:
                return safe_get_card_ref(state, (AreaType)option.param0, option.param1, player);
            case SelectOptionType::Attack:
                if (!state.players[player].active.empty()) return state.players[player].active.at(0);
                return CardRef();
            default:
                return CardRef();
        }
    } catch (...) {
        return CardRef();
    }
}

static CardRef target_card_ref_for_option(const State& state, const SelectOption& option) {
    int player = state.selectPlayer;
    try {
        switch (option.type) {
            case SelectOptionType::Attach:
            case SelectOptionType::Evolve:
                return safe_get_card_ref(state, (AreaType)option.param2, option.param3, player);
            case SelectOptionType::Card:
            case SelectOptionType::ToolCard:
            case SelectOptionType::EnergyCard:
                return state.getCardRef(option);
            default:
                return CardRef();
        }
    } catch (...) {
        return CardRef();
    }
}

static void put_card_summary(const State& state, CardRef ref, unsigned char* obs, int* idx, int limit) {
    if (ref.isNull()) {
        put_u16(obs, idx, limit, 0);
        put_byte(obs, idx, limit, 0);
        put_byte(obs, idx, limit, 0);
        return;
    }

    const Card& card = state.getCard(ref);
    put_u16(obs, idx, limit, card.cardId);
    put_byte(obs, idx, limit, card.damage / 10);
    int hp = 0;
    try {
        hp = card.getMaster().hp;
    } catch (...) {
        hp = 0;
    }
    put_byte(obs, idx, limit, hp / 10);
}

static bool learner_active_has_id(const State& state, int card_id);
static int learner_energy_in_play(const State& state);
static bool option_type_available(const State& state, SelectOptionType type);

static void put_option_card_features(
    const State& state,
    CardRef ref,
    unsigned char* row,
    int card_id_lo_idx,
    int card_type_idx
) {
    if (ref.isNull()) return;
    try {
        const Card& card = state.getCard(ref);
        row[card_id_lo_idx] = (unsigned char)(card.cardId & 0xff);
        row[card_id_lo_idx + 1] = (unsigned char)((card.cardId >> 8) & 0xff);
        const CardMaster& master = card.getMaster();
        row[card_type_idx] = byte_clamp((int)master.cardType);
        row[card_type_idx + 1] = byte_clamp((int)master.pokemonType);
        row[card_type_idx + 2] = byte_clamp((int)master.evolutionType);
        row[card_type_idx + 3] = byte_clamp(master.hp / 10);
        row[card_type_idx + 4] = byte_clamp(card.damage / 10);
        int energy_count = 0;
        try {
            energy_count = state.getEnergyCount(card.playerIndex, ref);
        } catch (...) {
            energy_count = 0;
        }
        row[card_type_idx + 5] = byte_clamp(energy_count);
        int retreat_cost = 0;
        try {
            retreat_cost = state.retreatCost(card);
        } catch (...) {
            retreat_cost = master.retreatCost;
        }
        row[card_type_idx + 6] = byte_clamp(retreat_cost + 16);
    } catch (...) {
    }
}

static int card_hp_remaining(const State& state, CardRef ref) {
    if (ref.isNull()) return 0;
    try {
        const Card& card = state.getCard(ref);
        return std::max(0, state.getHp(card) - card.damage);
    } catch (...) {
        return 0;
    }
}

static int card_energy_count(const State& state, CardRef ref) {
    if (ref.isNull()) return 0;
    try {
        const Card& card = state.getCard(ref);
        return state.getEnergyCount(card.playerIndex, ref);
    } catch (...) {
        return 0;
    }
}

static int card_prize_count(const State& state, CardRef ref) {
    if (ref.isNull()) return 0;
    try {
        return state.getPrizeCount(state.getCard(ref));
    } catch (...) {
        return 0;
    }
}

static int player_total_energy_in_play(const State& state, int player) {
    int total = 0;
    if (player < 0 || player > 1) return 0;
    const PlayerState& ps = state.players[player];
    try {
        if (!ps.active.empty()) total += state.getEnergyCount(player, ps.active.at(0));
        for (CardRef ref : ps.bench) total += state.getEnergyCount(player, ref);
    } catch (...) {
    }
    return total;
}

static int basic_energy_card_id_for_slot(int slot) {
    // Slot order: Grass, Fire, Water, Lightning, Psychic, Fighting, Darkness, Metal.
    return slot + 1;
}

static EnergyType basic_energy_type_for_slot(int slot) {
    switch (slot) {
        case 0: return EnergyType::Grass;
        case 1: return EnergyType::Fire;
        case 2: return EnergyType::Water;
        case 3: return EnergyType::Lightning;
        case 4: return EnergyType::Psychic;
        case 5: return EnergyType::Fighting;
        case 6: return EnergyType::Darkness;
        case 7: return EnergyType::Metal;
        default: return EnergyType::Colorless;
    }
}

static int count_player_basic_energy_in_area(const State& state, int player, AreaType area, int energy_slot) {
    if (player < 0 || player > 1) return 0;
    int card_id = basic_energy_card_id_for_slot(energy_slot);
    auto count_refs = [&](const auto& refs) {
        int count = 0;
        for (CardRef ref : refs) {
            try {
                if (!ref.isNull() && state.getCard(ref).cardId == card_id) count++;
            } catch (...) {
            }
        }
        return count;
    };
    try {
        switch (area) {
            case AreaType::Hand:
                return count_refs(state.players[player].hand);
            case AreaType::Trash:
                return count_refs(state.players[player].trash);
            default:
                return 0;
        }
    } catch (...) {
        return 0;
    }
}

static int type_energy_on_ref(const State& state, int player, CardRef ref, EnergyType type) {
    if (player < 0 || player > 1 || ref.isNull()) return 0;
    try {
        return state.typeEnergyCount(player, ref, type);
    } catch (...) {
        return 0;
    }
}

static int type_energy_in_play(const State& state, int player, EnergyType type) {
    if (player < 0 || player > 1) return 0;
    int count = 0;
    const PlayerState& ps = state.players[player];
    if (!ps.active.empty()) count += type_energy_on_ref(state, player, ps.active.at(0), type);
    for (CardRef ref : ps.bench) count += type_energy_on_ref(state, player, ref, type);
    return count;
}

static int type_energy_on_active(const State& state, int player, EnergyType type) {
    if (player < 0 || player > 1) return 0;
    const PlayerState& ps = state.players[player];
    if (ps.active.empty()) return 0;
    return type_energy_on_ref(state, player, ps.active.at(0), type);
}

static int type_energy_on_bench_slot(const State& state, int player, int slot, EnergyType type) {
    if (player < 0 || player > 1 || slot < 0) return 0;
    const PlayerState& ps = state.players[player];
    if (slot >= (int)ps.bench.size()) return 0;
    return type_energy_on_ref(state, player, ps.bench.at(slot), type);
}

static CardRef player_active_ref(const State& state, int player) {
    if (player < 0 || player > 1) return CardRef();
    const PlayerState& ps = state.players[player];
    if (ps.active.empty()) return CardRef();
    return ps.active.at(0);
}

static int attack_base_damage(const SelectOption& option) {
    if (option.type != SelectOptionType::Attack) return 0;
    try {
        auto it = AttackTable.find(option.param0);
        if (it == AttackTable.end()) return 0;
        return std::max(0, it->second.damage);
    } catch (...) {
        return 0;
    }
}

static int estimate_attack_damage(const State& state, const SelectOption& option) {
    int damage = attack_base_damage(option);
    // Mega Charizard X ex has dynamic damage by discarded Fire energy. Give the
    // policy a conservative visible estimate for this known high-impact attacker.
    if (option.type == SelectOptionType::Attack && learner_active_has_id(state, 790)) {
        damage = std::max(damage, std::min(450, learner_energy_in_play(state) * 90));
    }
    return damage;
}

static CardRef option_attack_target_ref(const State& state, const SelectOption& option) {
    if (option.type != SelectOptionType::Attack) return CardRef();
    return player_active_ref(state, 1 - PTCG_LEARNER);
}

static bool option_attack_can_ko(const State& state, const SelectOption& option) {
    if (option.type != SelectOptionType::Attack) return false;
    CardRef target = option_attack_target_ref(state, option);
    int hp = card_hp_remaining(state, target);
    return hp > 0 && estimate_attack_damage(state, option) >= hp;
}

struct TacticalOptionRoles {
    bool damage = false;
    bool heal = false;
    bool draw_search = false;
    bool gust = false;
    bool self_switch = false;
    bool energy_accel = false;
    bool energy_move = false;
    bool disruption = false;
    bool defense = false;
    bool recursion = false;
    bool setup = false;
    bool prize_bonus = false;
};

static bool effect_targets_enemy(const Effect& effect) {
    return effect.enemySelect
        || effect.target.targetPlayer == TargetPlayer::Enemy
        || effect.target.targetPlayer == TargetPlayer::Both;
}

static bool effect_touches_area(const Effect& effect, AreaType area) {
    for (AreaType target_area : effect.target.areas) {
        if (target_area == area) return true;
    }
    return false;
}

static void collect_effect_roles(const Effect& effect, TacticalOptionRoles& roles) {
    switch (effect.effectType) {
        case EffectType::AttackDamage:
        case EffectType::AttackDamageMulti:
        case EffectType::AttackDamageCoin:
        case EffectType::DamageCounter:
        case EffectType::DamageCounterDamaged:
        case EffectType::DamageCounterAny:
        case EffectType::DamageCounterDouble:
        case EffectType::DamageCounterHp:
        case EffectType::DamageCounterSwitchAny:
        case EffectType::Ko:
            roles.damage = true;
            if (effect_targets_enemy(effect)) roles.disruption = true;
            break;

        case EffectType::Heal:
        case EffectType::HealAll:
        case EffectType::HealSand:
        case EffectType::ResetHp:
        case EffectType::Drain:
        case EffectType::DamageCounterRemoved:
        case EffectType::RemoveDamageCounter:
        case EffectType::RemoveDamageCounterAll:
        case EffectType::RecoverSpecialCondition:
        case EffectType::RecoverSpecialConditionSingle:
            roles.heal = true;
            break;

        case EffectType::Draw:
        case EffectType::DrawTargetCount:
        case EffectType::DrawPrizeCount:
        case EffectType::DrawUntil:
        case EffectType::DrawUntilPsychic:
        case EffectType::DrawMirror:
        case EffectType::LookDeck:
        case EffectType::LookDeckReverse:
        case EffectType::LookDeckBottom:
        case EffectType::LookAndReturn:
        case EffectType::ToHand:
        case EffectType::ToHandReverse:
        case EffectType::ToHandWithAttach:
        case EffectType::PrizeToHand:
            roles.draw_search = true;
            if (effect_touches_area(effect, AreaType::Trash)) roles.recursion = true;
            if (effect_touches_area(effect, AreaType::Deck) || effect_touches_area(effect, AreaType::Looking)) roles.setup = true;
            break;

        case EffectType::ToBench:
        case EffectType::SelectEvolvesFrom:
        case EffectType::EvolvesToEach:
        case EffectType::SelectEvolvesTo:
        case EffectType::EvolvesFromEach:
            roles.setup = true;
            break;

        case EffectType::Switch:
            if (effect_targets_enemy(effect)) {
                roles.gust = true;
                roles.disruption = true;
            } else {
                roles.self_switch = true;
            }
            break;

        case EffectType::SelectAttachFrom:
        case EffectType::AttachToEach:
        case EffectType::SelectAttachTo:
        case EffectType::AttachEnergyMe:
        case EffectType::AttachSelectedCard:
        case EffectType::AttachFromEach:
            roles.energy_accel = true;
            break;

        case EffectType::SelectSwitchEnergy:
        case EffectType::SelectSwitchEnergyCard:
        case EffectType::EnergySwitchEach:
        case EffectType::SwitchSelectedCard:
            roles.energy_move = true;
            break;

        case EffectType::ToTrash:
        case EffectType::ToDeck:
        case EffectType::ToDeckReverse:
        case EffectType::ToDeckAndShuffle:
        case EffectType::ToDeckReverseAndShuffle:
        case EffectType::ToDeckBottom:
        case EffectType::ToDeckBottomReverse:
        case EffectType::ToDeckBottomClose:
        case EffectType::DeckToTrash:
        case EffectType::DeckToTrashCoinUntilTail:
        case EffectType::DeckBottomToTrash:
        case EffectType::Shuffle:
        case EffectType::Burn:
        case EffectType::Poison:
        case EffectType::Poison8:
        case EffectType::Poison16:
        case EffectType::Sleep:
        case EffectType::Confuse:
        case EffectType::Paralyze:
        case EffectType::Devolve:
        case EffectType::DevolveAny:
        case EffectType::CannotAttackNextTurn:
        case EffectType::CannotRetreatNextTurn:
        case EffectType::CannotPlayItemNextTurn:
        case EffectType::CannotPlaySupporterNextTurn:
        case EffectType::CannotPlayStadiumNextTurn:
        case EffectType::CannotPlaySpecialEnergyNextTurn:
        case EffectType::CannotEvolveNextTurn:
            if (effect_targets_enemy(effect)) roles.disruption = true;
            if (effect_touches_area(effect, AreaType::Trash)) roles.recursion = true;
            break;

        case EffectType::MaxHpChange:
        case EffectType::TakeDamageChangeNextEnemyTurn:
        case EffectType::NoDamageLessEqualAttackNextEnemyTurn:
        case EffectType::NoDamageAndEffectAttackNextEnemyTurn:
        case EffectType::NoDamageAndEffectEnemyAttackNextEnemyTurn:
        case EffectType::NoDamageEnemyAttack:
        case EffectType::NoDamageEnemyAbilityPokemonAttack:
        case EffectType::NoDamageEnemyExAttack:
        case EffectType::NoDamageEnemyBasicExAttack:
        case EffectType::NoDamageAndEffectEnemyTerastalAttack:
        case EffectType::NoDamageAndEffectEnemySpecialEnergyAttack:
        case EffectType::NoEffectEnemyAttack:
        case EffectType::NoDamageAndEffectEnemyAttack:
        case EffectType::NoSpecialCondition:
        case EffectType::NoSleepParalyzeConfuse:
        case EffectType::NoSleep:
        case EffectType::NoPrizeEx:
            roles.defense = true;
            break;

        case EffectType::TakePrizeCountChangeTerastalAttackKoActive:
        case EffectType::TakePrizeCountChangeNAttackKoActive:
        case EffectType::KoPrizeChangeAlways:
        case EffectType::KoPrizeChange:
        case EffectType::KoPrizeDecreaseOnce:
        case EffectType::BasicPrizePlus1:
            roles.prize_bonus = true;
            break;

        default:
            break;
    }
}

static void collect_skill_roles(const Skill* skill, TacticalOptionRoles& roles) {
    if (skill == nullptr) return;
    for (const Effect& effect : skill->effects) {
        collect_effect_roles(effect, roles);
    }
}

static void collect_attack_roles(const Attack* attack, TacticalOptionRoles& roles) {
    if (attack == nullptr) return;
    if (attack->damage > 0) roles.damage = true;
    if (attack->prizePlus1) roles.prize_bonus = true;
    for (const Effect& effect : attack->preEffects) {
        collect_effect_roles(effect, roles);
    }
    for (const Effect& effect : attack->postEffects) {
        collect_effect_roles(effect, roles);
    }
}

static void apply_card_id_role_hints(int card_id, TacticalOptionRoles& roles) {
    switch (card_id) {
        case 1086: // Buddy-Buddy Poffin
        case 1092: // Secret Box
        case 1097: // Night Stretcher
        case 1121: // Ultra Ball
        case 1122: // Pokegear 3.0
        case 1126: // Precious Trolley
        case 1152: // Poke Pad
        case 1205: // Cyrano
        case 1219: // Team Rocket's Petrel
        case 1225: // Hilda
        case 1227: // Lillie's Determination
        case 1231: // Dawn
        case 1233: // Canari
        case 1235: // Waitress
            roles.draw_search = true;
            roles.setup = true;
            break;
        case 1079: // Rare Candy
            roles.setup = true;
            break;
        case 1147: // Jumbo Ice Cream
        case 1212: // Cook
        case 1224: // Cheren
            roles.heal = true;
            roles.defense = true;
            break;
        case 1123: // Switch
        case 1161: // Air Balloon
            roles.self_switch = true;
            break;
        case 1182: // Boss's Orders
            roles.gust = true;
            roles.disruption = true;
            break;
        case 1108: // Energy Switch
        case 1134: // Glass Trumpet
            roles.energy_move = true;
            roles.energy_accel = true;
            break;
        case 1115: // Crushing Hammer
        case 1116: // Enhanced Hammer
        case 1117: // Hand Trimmer
        case 1127: // Unfair Stamp
        case 1131: // Special Red Card
        case 1222: // Eri
            roles.disruption = true;
            break;
        default:
            break;
    }
}

static TacticalOptionRoles option_tactical_roles(
    const State& state,
    const SelectOption& option,
    CardRef primary
) {
    TacticalOptionRoles roles;
    try {
        if (option.type == SelectOptionType::Attack) {
            auto it = AttackTable.find(option.param0);
            if (it != AttackTable.end()) {
                collect_attack_roles(&it->second, roles);
            }
        }
        if (!primary.isNull()) {
            const Card& card = state.getCard(primary);
            const CardMaster& master = card.getMaster();
            apply_card_id_role_hints(card.cardId, roles);
            if (master.play != nullptr && option.type == SelectOptionType::Play) {
                collect_skill_roles(master.play, roles);
            }
            if (master.ability != nullptr && option.type == SelectOptionType::Ability) {
                collect_skill_roles(master.ability, roles);
            }
            if (master.cardType == CardType::Tool || master.cardType == CardType::Stadium) {
                roles.defense = roles.defense || master.cardType == CardType::Tool;
                roles.disruption = roles.disruption || master.cardType == CardType::Stadium;
            }
            if (master.cardType == CardType::Pokemon && master.evolutionType != EvolutionType::NoEvolutionType) {
                roles.setup = true;
            }
            if (IsEnergy(master.cardType)) {
                roles.energy_accel = true;
            }
        }
    } catch (...) {
    }
    return roles;
}

static void put_option_tactical_features(
    const State& state,
    const SelectOption& option,
    CardRef primary,
    CardRef target,
    int attack_damage,
    int target_hp,
    int target_prizes,
    unsigned char* row
) {
    TacticalOptionRoles roles = option_tactical_roles(state, option, primary);
    int primary_card_type = 0;
    int primary_pokemon_type = 0;
    int primary_evolution_type = 0;
    int primary_is_rule = 0;
    int primary_ace_spec = 0;
    int primary_team_rocket = 0;
    int primary_has_ability = 0;
    int primary_has_attack = 0;
    int primary_damaged = 0;
    int target_is_enemy = 0;
    int target_is_rule = 0;
    int target_damage = 0;
    int target_energy = 0;

    try {
        if (!primary.isNull()) {
            const Card& card = state.getCard(primary);
            const CardMaster& master = card.getMaster();
            primary_card_type = (int)master.cardType;
            primary_pokemon_type = (int)master.pokemonType;
            primary_evolution_type = (int)master.evolutionType;
            primary_is_rule = master.isRulePokemon() ? 1 : 0;
            primary_ace_spec = master.aceSpec ? 1 : 0;
            primary_team_rocket = master.teamRocket ? 1 : 0;
            primary_has_ability = master.ability != nullptr ? 1 : 0;
            primary_has_attack = master.attacks.empty() ? 0 : 1;
            primary_damaged = card.damage > 0 ? 1 : 0;
        }
        if (!target.isNull()) {
            const Card& card = state.getCard(target);
            const CardMaster& master = card.getMaster();
            target_is_enemy = card.playerIndex != PTCG_LEARNER ? 1 : 0;
            target_is_rule = master.isRulePokemon() ? 1 : 0;
            target_damage = card.damage;
            target_energy = state.getEnergyCount(card.playerIndex, target);
        }
    } catch (...) {
    }

    int estimated_prizes = 0;
    int overkill = 0;
    if (option.type == SelectOptionType::Attack && attack_damage > 0 && target_hp > 0) {
        if (attack_damage >= target_hp) {
            estimated_prizes = target_prizes + (roles.prize_bonus ? 1 : 0);
            overkill = attack_damage - target_hp;
        }
    }

    row[64] = byte_clamp(primary_card_type);
    row[65] = byte_clamp(primary_pokemon_type);
    row[66] = byte_clamp(primary_evolution_type);
    row[67] = flag_byte(primary_is_rule != 0);
    row[68] = flag_byte(primary_ace_spec != 0);
    row[69] = flag_byte(primary_team_rocket != 0);
    row[70] = flag_byte(primary_has_ability != 0);
    row[71] = flag_byte(primary_has_attack != 0);
    row[72] = flag_byte(primary_damaged != 0);
    row[73] = flag_byte(target_is_enemy != 0);
    row[74] = flag_byte(target_is_rule != 0);
    row[75] = byte_clamp(target_damage / 10);
    row[76] = byte_clamp(target_energy);
    row[77] = flag_byte(roles.damage);
    row[78] = flag_byte(roles.heal);
    row[79] = flag_byte(roles.draw_search);
    row[80] = flag_byte(roles.gust);
    row[81] = flag_byte(roles.self_switch);
    row[82] = flag_byte(roles.energy_accel);
    row[83] = flag_byte(roles.energy_move);
    row[84] = flag_byte(roles.disruption);
    row[85] = flag_byte(roles.defense);
    row[86] = flag_byte(roles.recursion);
    row[87] = flag_byte(roles.setup);
    row[88] = flag_byte(roles.prize_bonus);
    row[89] = byte_clamp(estimated_prizes);
    row[90] = byte_clamp(overkill / 10);
    row[91] = byte_clamp((int)state.players[PTCG_LEARNER].prize.size());
    row[92] = byte_clamp((int)state.players[1 - PTCG_LEARNER].prize.size());
    row[93] = byte_clamp((int)state.players[PTCG_LEARNER].hand.size());
    row[94] = byte_clamp((int)state.players[1 - PTCG_LEARNER].hand.size());
    row[95] = byte_clamp((int)state.turn);
}

struct ArchaludonOptionOutcome {
    bool would_bench_duraludon = false;
    bool would_evolve_to_archaludon = false;
    bool would_trigger_assemble_alloy = false;
    bool would_discard_basic_metal = false;
    bool would_attach_metal_from_discard = false;
    bool would_reach_3_metal_on_archaludon = false;
    bool would_enable_metal_defender = false;
    bool would_enable_attack_this_turn = false;
    bool would_enable_attack_next_turn = false;
    bool would_create_ko_option = false;
    int metal_from_discard = 0;
};

static int count_learner_card_in_play(const State& state, int card_id);
static int count_learner_card_in_hand(const State& state, int card_id);
static int count_learner_card_in_trash(const State& state, int card_id);
static int count_learner_card_that_can_evolve(const State& state, int card_id);
static bool card_ref_has_id(const State& state, CardRef ref, int card_id);
static bool stadium_has_id(const State& state, int card_id);
static bool play_card_available(const State& state, int card_id);
static bool ability_card_available(const State& state, int card_id);
static bool attack_with_card_available(const State& state, int card_id);
static bool evolve_card_available(const State& state, int card_id);
static bool bench_card_available(const State& state, int card_id);
static bool discard_basic_metal_available(const State& state);
static int archaludon_post_assemble_best_target_energy_count(const State& state);
static bool metal_defender_ready_now(const State& state);
static bool metal_defender_ready_after_assemble(const State& state);
static bool metal_defender_ready_next_turn_estimate(const State& state);
static ArchaludonOptionOutcome archaludon_option_outcome(
    const State& state,
    const SelectOption& option,
    CardRef primary,
    CardRef target);
static float archaludon_potential(const State& state);

static void write_observation_slot(PTCG* env, int slot) {
    unsigned char* obs = env->num_agents > 1 ? env->obs_ptr[slot] : (unsigned char*)env->observations;
    std::memset(obs, 0, PTCG_OBS_SIZE);
    if (env->battle == nullptr) return;

    const State& state = env->battle->state;
    int idx = 0;
    int option_count = (int)state.options.size();
    int option_limit = std::min(option_count, PTCG_MAX_OPTIONS);
    env->log.max_option_count_seen = std::max(env->log.max_option_count_seen, (float)option_count);
    if (option_count > PTCG_MAX_OPTIONS) env->log.option_truncations += 1.0f;

    put_byte(obs, &idx, PTCG_STATE_DIM, PTCG_OBS_VERSION);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.turn);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.turnActionCount);
    put_byte(obs, &idx, PTCG_STATE_DIM, (int)state.phase);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.selectPlayer);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.firstPlayer);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.activePlayerIndex());
    put_byte(obs, &idx, PTCG_STATE_DIM, (int)state.selectType);
    put_byte(obs, &idx, PTCG_STATE_DIM, (int)state.selectContext);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.selectMin);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.selectMax);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_count);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_limit);
    put_byte(obs, &idx, PTCG_STATE_DIM, (int)state.selected.size());
    put_byte(obs, &idx, PTCG_STATE_DIM, can_stop_now(state) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.apiResult() + 1);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.supporterPlayed ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.stadiumPlayed ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.energyPlayed ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.retreated ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.remainDamageCounter);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.energyCost);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.remainEnergyCost);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.selectedEnergyCardCount);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.turnAttackCount);
    put_byte(obs, &idx, PTCG_STATE_DIM, state.effectActionCount);
    put_u16(obs, &idx, PTCG_STATE_DIM, state.currentAttackId);

    CardRef learner_active = player_active_ref(state, PTCG_LEARNER);
    CardRef opponent_active = player_active_ref(state, 1 - PTCG_LEARNER);
    put_byte(obs, &idx, PTCG_STATE_DIM, card_hp_remaining(state, learner_active) / 10);
    put_byte(obs, &idx, PTCG_STATE_DIM, card_hp_remaining(state, opponent_active) / 10);
    put_byte(obs, &idx, PTCG_STATE_DIM, card_energy_count(state, learner_active));
    put_byte(obs, &idx, PTCG_STATE_DIM, card_energy_count(state, opponent_active));
    put_byte(obs, &idx, PTCG_STATE_DIM, player_total_energy_in_play(state, PTCG_LEARNER));
    put_byte(obs, &idx, PTCG_STATE_DIM, player_total_energy_in_play(state, 1 - PTCG_LEARNER));
    put_byte(obs, &idx, PTCG_STATE_DIM, card_prize_count(state, learner_active));
    put_byte(obs, &idx, PTCG_STATE_DIM, card_prize_count(state, opponent_active));
    put_byte(obs, &idx, PTCG_STATE_DIM, option_type_available(state, SelectOptionType::Attack) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_type_available(state, SelectOptionType::Attach) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_type_available(state, SelectOptionType::Play) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_type_available(state, SelectOptionType::Evolve) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_type_available(state, SelectOptionType::Ability) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, option_type_available(state, SelectOptionType::End) ? 1 : 0);
    int best_attack_damage = 0;
    int best_attack_ko_prizes = 0;
    int best_attack_can_finish = 0;
    for (int i = 0; i < option_limit; i++) {
        const SelectOption& option = state.options[i];
        if (selected_contains(state, i) || option.type != SelectOptionType::Attack) continue;
        int damage = estimate_attack_damage(state, option);
        best_attack_damage = std::max(best_attack_damage, damage);
        if (option_attack_can_ko(state, option)) {
            int prizes = card_prize_count(state, opponent_active);
            best_attack_ko_prizes = std::max(best_attack_ko_prizes, prizes);
            best_attack_can_finish = std::max(best_attack_can_finish, prizes >= (int)state.players[PTCG_LEARNER].prize.size() ? 1 : 0);
        }
    }
    put_byte(obs, &idx, PTCG_STATE_DIM, best_attack_damage / 10);
    put_byte(obs, &idx, PTCG_STATE_DIM, best_attack_ko_prizes);
    put_byte(obs, &idx, PTCG_STATE_DIM, best_attack_can_finish);

    for (int p = 0; p < 2; p++) {
        const PlayerState& ps = state.players[p];
        put_byte(obs, &idx, PTCG_STATE_DIM, (int)ps.deck.size());
        put_byte(obs, &idx, PTCG_STATE_DIM, (int)ps.hand.size());
        put_byte(obs, &idx, PTCG_STATE_DIM, (int)ps.trash.size());
        put_byte(obs, &idx, PTCG_STATE_DIM, (int)ps.prize.size());
        put_byte(obs, &idx, PTCG_STATE_DIM, (int)ps.bench.size());
        put_byte(obs, &idx, PTCG_STATE_DIM, ps.isPoisoned() ? 1 : 0);
        put_byte(obs, &idx, PTCG_STATE_DIM, ps.isBurned() ? 1 : 0);
        put_byte(obs, &idx, PTCG_STATE_DIM, (int)ps.badStatus);

        if (ps.active.size() > 0) {
            put_card_summary(state, ps.active.at(0), obs, &idx, PTCG_STATE_DIM);
        } else {
            put_card_summary(state, CardRef(), obs, &idx, PTCG_STATE_DIM);
        }
        for (int b = 0; b < BENCH_SIZE_MAX; b++) {
            if (b < ps.bench.size()) {
                put_card_summary(state, ps.bench.at(b), obs, &idx, PTCG_STATE_DIM);
            } else {
                put_card_summary(state, CardRef(), obs, &idx, PTCG_STATE_DIM);
            }
        }
    }

    for (int slot = 0; slot < 8; slot++) {
        put_byte(obs, &idx, PTCG_STATE_DIM,
            count_player_basic_energy_in_area(state, PTCG_LEARNER, AreaType::Hand, slot));
    }
    for (int slot = 0; slot < 8; slot++) {
        put_byte(obs, &idx, PTCG_STATE_DIM,
            count_player_basic_energy_in_area(state, PTCG_LEARNER, AreaType::Trash, slot));
    }
    for (int slot = 0; slot < 8; slot++) {
        put_byte(obs, &idx, PTCG_STATE_DIM,
            type_energy_in_play(state, PTCG_LEARNER, basic_energy_type_for_slot(slot)));
    }
    for (int slot = 0; slot < 8; slot++) {
        put_byte(obs, &idx, PTCG_STATE_DIM,
            type_energy_on_active(state, PTCG_LEARNER, basic_energy_type_for_slot(slot)));
    }
    for (int bench = 0; bench < BENCH_SIZE_MAX; bench++) {
        for (int slot = 0; slot < 8; slot++) {
            put_byte(obs, &idx, PTCG_STATE_DIM,
                type_energy_on_bench_slot(state, PTCG_LEARNER, bench, basic_energy_type_for_slot(slot)));
        }
    }
    for (int slot = 0; slot < 8; slot++) {
        put_byte(obs, &idx, PTCG_STATE_DIM,
            type_energy_in_play(state, 1 - PTCG_LEARNER, basic_energy_type_for_slot(slot)));
    }

    put_byte(obs, &idx, PTCG_STATE_DIM, count_learner_card_in_play(state, 169));
    put_byte(obs, &idx, PTCG_STATE_DIM, count_learner_card_that_can_evolve(state, 169));
    put_byte(obs, &idx, PTCG_STATE_DIM, count_learner_card_in_hand(state, 190));
    put_byte(obs, &idx, PTCG_STATE_DIM, evolve_card_available(state, 190) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, ability_card_available(state, 190) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, count_learner_card_in_trash(state, 8));
    put_byte(obs, &idx, PTCG_STATE_DIM, archaludon_post_assemble_best_target_energy_count(state));
    put_byte(obs, &idx, PTCG_STATE_DIM, metal_defender_ready_now(state) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, metal_defender_ready_after_assemble(state) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, metal_defender_ready_next_turn_estimate(state) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, count_learner_card_in_play(state, 190));
    put_byte(obs, &idx, PTCG_STATE_DIM, count_learner_card_in_play(state, 666));
    put_byte(obs, &idx, PTCG_STATE_DIM, stadium_has_id(state, 1244) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, play_card_available(state, 1213) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, play_card_available(state, 1147) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, discard_basic_metal_available(state) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, bench_card_available(state, 169) ? 1 : 0);
    put_byte(obs, &idx, PTCG_STATE_DIM, attack_with_card_available(state, 190) ? 1 : 0);

    for (int i = 0; i < option_limit; i++) {
        const SelectOption& option = state.options[i];
        unsigned char* row = obs + PTCG_STATE_DIM + i * PTCG_OPTION_DIM;
        row[0] = 1;
        row[1] = 0;
        row[2] = selected_contains(state, i) ? 1 : 0;
        row[3] = byte_clamp((int)option.type);
        row[4] = byte_clamp((int)state.selectType);
        row[5] = byte_clamp((int)state.selectContext);
        row[6] = byte_clamp(state.selectMin);
        row[7] = byte_clamp(state.selectMax);
        row[8] = byte_clamp((int)state.selected.size());
        row[9] = byte_clamp(i);
        int ridx = 10;
        put_s16(row, &ridx, PTCG_OPTION_DIM, option.param0);
        put_s16(row, &ridx, PTCG_OPTION_DIM, option.param1);
        put_s16(row, &ridx, PTCG_OPTION_DIM, option.param2);
        put_s16(row, &ridx, PTCG_OPTION_DIM, option.param3);
        put_s16(row, &ridx, PTCG_OPTION_DIM, option.param4);

        CardRef primary = primary_card_ref_for_option(state, option);
        CardRef target = target_card_ref_for_option(state, option);
        put_option_card_features(state, primary, row, 20, 30);
        if (!target.isNull()) {
            try {
                const Card& target_card = state.getCard(target);
                row[22] = (unsigned char)(target_card.cardId & 0xff);
                row[23] = (unsigned char)((target_card.cardId >> 8) & 0xff);
            } catch (...) {
            }
        }
        row[24] = byte_clamp(option.param0);
        row[25] = byte_clamp(option.param1);
        row[26] = byte_clamp(option.param2 + 1);
        row[27] = byte_clamp(option.param2);
        row[28] = byte_clamp(option.param3);
        row[29] = byte_clamp(state.selectPlayer + 1);
        row[37] = flag_byte(option.type == SelectOptionType::Play);
        row[38] = flag_byte(option.type == SelectOptionType::Attach);
        row[39] = flag_byte(option.type == SelectOptionType::Evolve);
        row[40] = flag_byte(option.type == SelectOptionType::Ability);
        row[41] = flag_byte(option.type == SelectOptionType::Discard);
        row[42] = flag_byte(option.type == SelectOptionType::Retreat);
        row[43] = flag_byte(option.type == SelectOptionType::Attack);
        row[44] = flag_byte(option.type == SelectOptionType::End);
        row[45] = flag_byte(option.type == SelectOptionType::Yes);
        row[46] = flag_byte(option.type == SelectOptionType::No);
        row[47] = flag_byte(state.selectDeck);
        CardRef attack_target = option_attack_target_ref(state, option);
        int attack_damage = estimate_attack_damage(state, option);
        int target_hp = card_hp_remaining(state, attack_target);
        int target_prizes = card_prize_count(state, attack_target);
        row[48] = byte_clamp(attack_damage / 10);
        row[49] = byte_clamp(attack_base_damage(option) / 10);
        row[50] = flag_byte(option_attack_can_ko(state, option));
        row[51] = byte_clamp(target_hp / 10);
        row[52] = byte_clamp(target_prizes);
        row[53] = byte_clamp(card_energy_count(state, attack_target));
        row[54] = byte_clamp(card_hp_remaining(state, primary) / 10);
        row[55] = byte_clamp(card_energy_count(state, primary));
        row[56] = byte_clamp(card_prize_count(state, primary));
        row[57] = flag_byte(option.type == SelectOptionType::Attack && target_prizes >= (int)state.players[PTCG_LEARNER].prize.size());
        row[58] = flag_byte(option.type == SelectOptionType::Attack && attack_damage >= 180);
        row[59] = flag_byte(option.type == SelectOptionType::Attack && attack_damage >= 270);
        row[60] = flag_byte(option.type == SelectOptionType::Attack && attack_damage >= 360);
        row[61] = byte_clamp(player_total_energy_in_play(state, PTCG_LEARNER));
        row[62] = byte_clamp(player_total_energy_in_play(state, 1 - PTCG_LEARNER));
        row[63] = byte_clamp((int)state.players[PTCG_LEARNER].prize.size());
        put_option_tactical_features(state, option, primary, target, attack_damage, target_hp, target_prizes, row);

        ArchaludonOptionOutcome arch = archaludon_option_outcome(state, option, primary, target);
        row[96] = flag_byte(arch.would_bench_duraludon);
        row[97] = flag_byte(arch.would_evolve_to_archaludon);
        row[98] = flag_byte(arch.would_trigger_assemble_alloy);
        row[99] = flag_byte(arch.would_discard_basic_metal);
        row[100] = flag_byte(arch.would_attach_metal_from_discard);
        row[101] = byte_clamp(arch.metal_from_discard);
        row[102] = flag_byte(arch.would_reach_3_metal_on_archaludon);
        row[103] = flag_byte(arch.would_enable_metal_defender);
        row[104] = flag_byte(arch.would_enable_attack_this_turn);
        row[105] = flag_byte(arch.would_enable_attack_next_turn);
        row[106] = flag_byte(arch.would_create_ko_option);
        row[107] = flag_byte(card_ref_has_id(state, primary, 169));
        row[108] = flag_byte(card_ref_has_id(state, primary, 190));
        row[109] = flag_byte(card_ref_has_id(state, primary, 666));
        row[110] = flag_byte(card_ref_has_id(state, primary, 8));
        row[111] = byte_clamp(type_energy_on_ref(state, PTCG_LEARNER, primary, EnergyType::Metal));
        row[112] = byte_clamp(type_energy_on_ref(state, PTCG_LEARNER, target, EnergyType::Metal));
        row[113] = byte_clamp(count_learner_card_in_trash(state, 8));
        row[114] = byte_clamp(archaludon_post_assemble_best_target_energy_count(state));
        row[115] = flag_byte(metal_defender_ready_now(state));
        row[116] = flag_byte(metal_defender_ready_after_assemble(state));
        row[117] = flag_byte(metal_defender_ready_next_turn_estimate(state));
        row[118] = byte_clamp(count_learner_card_in_play(state, 169));
        row[119] = byte_clamp(count_learner_card_in_play(state, 190));
        row[120] = byte_clamp(count_learner_card_in_play(state, 666));
        row[121] = byte_clamp(count_learner_card_in_hand(state, 190));
        row[122] = flag_byte(stadium_has_id(state, 1244));
        row[123] = flag_byte(card_ref_has_id(state, primary, 1213));
        row[124] = flag_byte(card_ref_has_id(state, primary, 1147));
        row[125] = flag_byte(card_ref_has_id(state, primary, 1182));
        row[126] = flag_byte(card_ref_has_id(state, primary, 1121) || card_ref_has_id(state, primary, 1122)
            || card_ref_has_id(state, primary, 1152) || card_ref_has_id(state, primary, 1185));
        row[127] = byte_clamp((int)state.turn);
    }

    unsigned char* stop = obs + PTCG_STATE_DIM + PTCG_STOP_ACTION * PTCG_OPTION_DIM;
    stop[0] = 1;
    stop[1] = 1;
    stop[3] = 255;
    stop[4] = byte_clamp((int)state.selectType);
    stop[5] = byte_clamp((int)state.selectContext);
    stop[6] = byte_clamp(state.selectMin);
    stop[7] = byte_clamp(state.selectMax);
    stop[8] = byte_clamp((int)state.selected.size());
    stop[9] = byte_clamp(PTCG_STOP_ACTION);
    stop[95] = byte_clamp((int)state.turn);
    stop[127] = byte_clamp((int)state.turn);
}

static void write_observation(PTCG* env) {
    for (int slot = 0; slot < env->num_agents; slot++) {
        write_observation_slot(env, slot);
    }
}

static void write_action_mask_slot(PTCG* env, int slot) {
    unsigned char* mask = env->num_agents > 1 ? env->action_mask_ptr[slot] : env->action_mask;
    if (mask == nullptr) return;
    std::memset(mask, 0, PTCG_ACTIONS);
    if (env->battle == nullptr) return;

    const State& state = env->battle->state;
    int acting_player = env->num_agents > 1 ? slot : PTCG_LEARNER;
    if (state.isFinish() || state.selectPlayer != acting_player) return;

    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (env->ko_attack_mask > 0.0f && can_choose_more) {
        int ko_valid = 0;
        for (int i = 0; i < option_limit; i++) {
            if (selected_contains(state, i)) continue;
            if (!option_attack_can_ko(state, state.options[i])) continue;
            mask[i] = 1;
            ko_valid++;
        }
        if (ko_valid > 0) return;
    }

    int valid = 0;
    for (int i = 0; i < option_limit; i++) {
        if (can_choose_more && !selected_contains(state, i)) {
            mask[i] = 1;
            valid++;
        }
    }
    if (can_stop_now(state)) {
        mask[PTCG_STOP_ACTION] = 1;
        valid++;
    }

    if (valid == 0) {
        env->log.stop_mask_errors += 1.0f;
        if (option_limit > 0) {
            mask[0] = 1;
        } else {
            mask[PTCG_STOP_ACTION] = 1;
        }
    }
}

static void write_action_mask(PTCG* env) {
    for (int slot = 0; slot < env->num_agents; slot++) {
        write_action_mask_slot(env, slot);
    }
}

static const std::array<int, DECK_SIZE>& deck_for_id(int deck_id) {
    switch (deck_id) {
        case PTCG_OPPONENT_KIYOTA_ABOMASNOW:
            return KIYOTA_ABOMASNOW_DECK;
        case PTCG_OPPONENT_KIYOTA_IONO:
            return KIYOTA_IONO_DECK;
        case PTCG_OPPONENT_KIYOTA_LUCARIO:
            return KIYOTA_LUCARIO_DECK;
        case PTCG_OPPONENT_DRAGAPULT_GENERIC:
            return DRAGAPULT_DECK;
        case PTCG_OPPONENT_DASHIMAKI_CRUSTLE:
            return CRUSTLE_DECK;
        case PTCG_OPPONENT_ALAKAZAM_GENERIC:
            return ALAKAZAM_DECK;
        case PTCG_OPPONENT_ABOMASNOW_SAMPLE_GENERIC:
            return ABOMASNOW_SAMPLE_DECK;
        case PTCG_OPPONENT_ABOMASNOW_RL_GENERIC:
            return ABOMASNOW_RL_DECK;
        case PTCG_OPPONENT_LUCARIO_RETUNED_GENERIC:
            return LUCARIO_RETUNED_DECK;
        case PTCG_OPPONENT_LUCARIO_MULTIPLY_GENERIC:
            return LUCARIO_MULTIPLY_DECK;
        case PTCG_OPPONENT_LUCARIO_BEGINNER_GENERIC:
            return LUCARIO_NUR_DECK;
        case PTCG_OPPONENT_IONO_GENERIC:
            return KIYOTA_IONO_DECK;
        case PTCG_OPPONENT_SAMPLE_GENERIC:
            return SAMPLE_DECK;
        case PTCG_OPPONENT_FIRE_ZARD_X_GENERIC:
            return FIRE_ZARD_X_TROLLEY_DECK;
        case PTCG_OPPONENT_KANGASKHAN_BOX:
            return KANGASKHAN_BOX_DECK;
        case PTCG_OPPONENT_HYDRAPPLE_EX:
            return HYDRAPPLE_EX_DECK;
        case PTCG_OPPONENT_CRUSTLE_LIMITLESS:
            return CRUSTLE_LIMITLESS_DECK;
        case PTCG_OPPONENT_DRAGAPULT_EXACT:
            return DRAGAPULT_DECK;
        case PTCG_OPPONENT_ARCHALUDON_EXACT:
            return ARCHALUDON_DECK;
        case PTCG_OPPONENT_MAGCARGO_EXACT:
            return MAGCARGO_DECK;
        case PTCG_OPPONENT_IRON_THORNS_EXACT:
            return IRON_THORNS_DECK;
        case PTCG_OPPONENT_XINPW8_ARCHALUDON:
            return XINPW8_ARCHALUDON_DECK;
        case PTCG_OPPONENT_GREAT_TUSK_CRUSTLE:
            return GREAT_TUSK_CRUSTLE_DECK;
        // BEGIN GENERATED CRUSTLE SEARCH CASES
        case 1000: return CRUSTLE_SEARCH_000_DECK;
        case 1001: return CRUSTLE_SEARCH_001_DECK;
        case 1002: return CRUSTLE_SEARCH_002_DECK;
        case 1003: return CRUSTLE_SEARCH_003_DECK;
        case 1004: return CRUSTLE_SEARCH_004_DECK;
        case 1005: return CRUSTLE_SEARCH_005_DECK;
        case 1006: return CRUSTLE_SEARCH_006_DECK;
        case 1007: return CRUSTLE_SEARCH_007_DECK;
        case 1008: return CRUSTLE_SEARCH_008_DECK;
        case 1009: return CRUSTLE_SEARCH_009_DECK;
        case 1010: return CRUSTLE_SEARCH_010_DECK;
        case 1011: return CRUSTLE_SEARCH_011_DECK;
        case 1012: return CRUSTLE_SEARCH_012_DECK;
        case 1013: return CRUSTLE_SEARCH_013_DECK;
        case 1014: return CRUSTLE_SEARCH_014_DECK;
        case 1015: return CRUSTLE_SEARCH_015_DECK;
        // END GENERATED CRUSTLE SEARCH CASES

        case PTCG_OPPONENT_STARTER_RANDOM:
        default:
            return SAMPLE_DECK;
    }
}

static GameConfig make_config(PTCG* env) {
    GameConfig config = {};
    config.seed = env->rng;
    config.recordLog = false;
    config.deviceRand = true;
    const std::array<int, DECK_SIZE>& learner_deck = deck_for_id(env->player_deck);
    const std::array<int, DECK_SIZE>& opponent_deck = deck_for_id(env->current_opponent);
    for (int i = 0; i < DECK_SIZE; i++) {
        if (learner_deck[i] <= 0 || opponent_deck[i] <= 0) {
            throw std::runtime_error("PTCG deck contains an invalid non-positive card id");
        }
        config.decks[PTCG_LEARNER].cards[i] = learner_deck[i];
        config.decks[1 - PTCG_LEARNER].cards[i] = opponent_deck[i];
    }
    return config;
}

static void selected_random(State& state, unsigned int* rng) {
    int option_count = (int)state.options.size();
    int count = std::min(state.selectMax, option_count);
    state.selected.clear();
    if (count <= 0) return;

    std::vector<int> choices(option_count);
    for (int i = 0; i < option_count; i++) choices[i] = i;
    for (int i = option_count - 1; i > 0; i--) {
        int j = (int)(rand_r(rng) % (unsigned int)(i + 1));
        std::swap(choices[i], choices[j]);
    }
    for (int i = 0; i < count; i++) {
        state.selected.push_back(choices[i]);
    }
}

static bool card_ref_has_id(const State& state, CardRef ref, int card_id) {
    if (ref.isNull()) return false;
    try {
        return state.getCard(ref).cardId == card_id;
    } catch (...) {
        return false;
    }
}

template <typename Refs>
static int count_card_refs_with_id(const State& state, const Refs& refs, int card_id) {
    int count = 0;
    for (const CardRef& ref : refs) {
        if (card_ref_has_id(state, ref, card_id)) count++;
    }
    return count;
}

static int count_learner_card_in_play(const State& state, int card_id) {
    const PlayerState& ps = state.players[PTCG_LEARNER];
    return count_card_refs_with_id(state, ps.active, card_id)
        + count_card_refs_with_id(state, ps.bench, card_id);
}

static int count_learner_card_in_hand(const State& state, int card_id) {
    return count_card_refs_with_id(state, state.players[PTCG_LEARNER].hand, card_id);
}

static bool stadium_has_id(const State& state, int card_id) {
    return count_card_refs_with_id(state, state.stadium, card_id) > 0;
}

static int learner_energy_in_play(const State& state) {
    int energy = 0;
    const PlayerState& ps = state.players[PTCG_LEARNER];
    auto add_energy = [&](const auto& refs) {
        for (const CardRef& ref : refs) {
            try {
                energy += state.getEnergyCount(PTCG_LEARNER, ref);
            } catch (...) {
            }
        }
    };
    add_energy(ps.active);
    add_energy(ps.bench);
    return energy;
}

static int opponent_active_hp_remaining(const State& state) {
    const PlayerState& opp = state.players[1 - PTCG_LEARNER];
    if (opp.active.empty()) return 0;
    try {
        const Card& active = state.getCard(opp.active.at(0));
        return std::max(0, active.getMaster().hp - active.damage);
    } catch (...) {
        return 0;
    }
}

static bool learner_active_has_id(const State& state, int card_id) {
    const PlayerState& ps = state.players[PTCG_LEARNER];
    return !ps.active.empty() && card_ref_has_id(state, ps.active.at(0), card_id);
}

static bool option_type_available(const State& state, SelectOptionType type) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (!selected_contains(state, i) && state.options[i].type == type) return true;
    }
    return false;
}

static bool play_card_available(const State& state, int card_id) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i) || state.options[i].type != SelectOptionType::Play) continue;
        CardRef ref = primary_card_ref_for_option(state, state.options[i]);
        if (card_ref_has_id(state, ref, card_id)) return true;
    }
    return false;
}

static bool ability_card_available(const State& state, int card_id) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i) || state.options[i].type != SelectOptionType::Ability) continue;
        CardRef ref = primary_card_ref_for_option(state, state.options[i]);
        if (card_ref_has_id(state, ref, card_id)) return true;
    }
    return false;
}

static bool attack_with_card_available(const State& state, int card_id) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i) || state.options[i].type != SelectOptionType::Attack) continue;
        CardRef ref = primary_card_ref_for_option(state, state.options[i]);
        if (card_ref_has_id(state, ref, card_id)) return true;
    }
    return false;
}

static bool chosen_option_primary_card_id(const State& state, int action, int card_id) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    if (action < 0 || action >= option_limit) return false;
    CardRef ref = primary_card_ref_for_option(state, state.options[action]);
    return card_ref_has_id(state, ref, card_id);
}

static bool attack_ko_available(const State& state) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i)) continue;
        if (option_attack_can_ko(state, state.options[i])) return true;
    }
    return false;
}

static int option_attack_prizes_estimate(const State& state, const SelectOption& option) {
    if (!option_attack_can_ko(state, option)) return 0;
    CardRef target = option_attack_target_ref(state, option);
    return card_prize_count(state, target);
}

static int best_attack_prizes_available(const State& state) {
    int best = 0;
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return 0;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i)) continue;
        best = std::max(best, option_attack_prizes_estimate(state, state.options[i]));
    }
    return best;
}

static int count_learner_card_in_trash(const State& state, int card_id) {
    return count_card_refs_with_id(state, state.players[PTCG_LEARNER].trash, card_id);
}

static int count_learner_card_that_can_evolve(const State& state, int card_id) {
    int count = 0;
    const PlayerState& ps = state.players[PTCG_LEARNER];
    auto add_if_ready = [&](CardRef ref) {
        try {
            if (card_ref_has_id(state, ref, card_id) && !state.getCard(ref).appear) count++;
        } catch (...) {
        }
    };
    for (CardRef ref : ps.active) add_if_ready(ref);
    for (CardRef ref : ps.bench) add_if_ready(ref);
    return count;
}

static bool evolve_card_available(const State& state, int card_id) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i) || state.options[i].type != SelectOptionType::Evolve) continue;
        CardRef ref = primary_card_ref_for_option(state, state.options[i]);
        if (card_ref_has_id(state, ref, card_id)) return true;
    }
    return false;
}

static bool bench_card_available(const State& state, int card_id) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i)) continue;
        const SelectOption& option = state.options[i];
        CardRef ref = primary_card_ref_for_option(state, option);
        if (!card_ref_has_id(state, ref, card_id)) continue;
        if (option.type == SelectOptionType::Play) return true;
        if (option.type == SelectOptionType::Card &&
            (state.selectContext == SelectContext::SetupBenchPokemon ||
             state.selectContext == SelectContext::ToBench)) return true;
    }
    return false;
}

static bool discard_basic_metal_available(const State& state) {
    int option_limit = std::min((int)state.options.size(), PTCG_MAX_OPTIONS);
    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    if (!can_choose_more) return false;
    for (int i = 0; i < option_limit; i++) {
        if (selected_contains(state, i)) continue;
        const SelectOption& option = state.options[i];
        CardRef ref = primary_card_ref_for_option(state, option);
        if (!card_ref_has_id(state, ref, 8)) continue;
        try {
            const Card& card = state.getCard(ref);
            if (option.type == SelectOptionType::Discard) return true;
            if ((option.type == SelectOptionType::Card || option.type == SelectOptionType::EnergyCard)
                && (state.selectContext == SelectContext::Discard || card.area == AreaType::Hand)) return true;
        } catch (...) {
        }
    }
    return false;
}

static int best_attached_metal_on_learner_ids(const State& state) {
    int best = 0;
    const PlayerState& ps = state.players[PTCG_LEARNER];
    auto consider = [&](CardRef ref) {
        if (card_ref_has_id(state, ref, 169) || card_ref_has_id(state, ref, 190)) {
            best = std::max(best, type_energy_on_ref(state, PTCG_LEARNER, ref, EnergyType::Metal));
        }
    };
    for (CardRef ref : ps.active) consider(ref);
    for (CardRef ref : ps.bench) consider(ref);
    return best;
}

static bool metal_defender_ready_now(const State& state) {
    if (attack_with_card_available(state, 190)) return true;
    const PlayerState& ps = state.players[PTCG_LEARNER];
    auto ready = [&](CardRef ref) {
        return card_ref_has_id(state, ref, 190)
            && type_energy_on_ref(state, PTCG_LEARNER, ref, EnergyType::Metal) >= 3;
    };
    for (CardRef ref : ps.active) if (ready(ref)) return true;
    for (CardRef ref : ps.bench) if (ready(ref)) return true;
    return false;
}

static int archaludon_post_assemble_best_target_energy_count(const State& state) {
    int metal_discard = count_learner_card_in_trash(state, 8);
    int best = best_attached_metal_on_learner_ids(state);
    return std::min(3, best + std::min(2, metal_discard));
}

static bool metal_defender_ready_after_assemble(const State& state) {
    return count_learner_card_in_play(state, 190) > 0
        && archaludon_post_assemble_best_target_energy_count(state) >= 3;
}

static bool metal_defender_ready_next_turn_estimate(const State& state) {
    int metal_discard = count_learner_card_in_trash(state, 8);
    int best = best_attached_metal_on_learner_ids(state);
    int hand_archaludon = count_learner_card_in_hand(state, 190);
    return (hand_archaludon > 0 || count_learner_card_in_play(state, 190) > 0)
        && count_learner_card_in_play(state, 169) > 0
        && best + std::min(2, metal_discard) + 1 >= 3;
}

static ArchaludonOptionOutcome archaludon_option_outcome(
    const State& state,
    const SelectOption& option,
    CardRef primary,
    CardRef target
) {
    ArchaludonOptionOutcome out;
    int primary_id = 0;
    AreaType primary_area = AreaType::All;
    try {
        if (!primary.isNull()) {
            const Card& card = state.getCard(primary);
            primary_id = card.cardId;
            primary_area = card.area;
        }
    } catch (...) {
    }

    out.would_bench_duraludon = primary_id == 169
        && (option.type == SelectOptionType::Play
            || (option.type == SelectOptionType::Card
                && (state.selectContext == SelectContext::SetupBenchPokemon
                    || state.selectContext == SelectContext::ToBench)));
    out.would_evolve_to_archaludon = primary_id == 190 && option.type == SelectOptionType::Evolve;
    out.would_trigger_assemble_alloy = out.would_evolve_to_archaludon
        || (primary_id == 190 && option.type == SelectOptionType::Ability);
    out.would_discard_basic_metal = primary_id == 8
        && (option.type == SelectOptionType::Discard
            || state.selectContext == SelectContext::Discard
            || (option.type == SelectOptionType::EnergyCard && primary_area == AreaType::Energy));
    out.would_attach_metal_from_discard = primary_id == 8
        && primary_area == AreaType::Trash
        && (option.type == SelectOptionType::Attach
            || option.type == SelectOptionType::Card
            || option.type == SelectOptionType::EnergyCard);
    out.metal_from_discard = out.would_attach_metal_from_discard ? 1 : 0;
    if (out.would_trigger_assemble_alloy) {
        out.metal_from_discard = std::max(out.metal_from_discard, std::min(2, count_learner_card_in_trash(state, 8)));
    }

    int target_metal = 0;
    bool target_arch_or_duraludon = false;
    if (!target.isNull()) {
        target_arch_or_duraludon = card_ref_has_id(state, target, 169) || card_ref_has_id(state, target, 190);
        target_metal = type_energy_on_ref(state, PTCG_LEARNER, target, EnergyType::Metal);
    }
    int best_after = std::max(archaludon_post_assemble_best_target_energy_count(state),
        target_metal + out.metal_from_discard);
    out.would_reach_3_metal_on_archaludon =
        (target_arch_or_duraludon && target_metal + out.metal_from_discard >= 3)
        || (out.would_evolve_to_archaludon && best_after >= 3);
    out.would_enable_metal_defender = out.would_reach_3_metal_on_archaludon
        || (primary_id == 190 && option.type == SelectOptionType::Attack);
    out.would_enable_attack_this_turn = out.would_enable_metal_defender
        && (option.type == SelectOptionType::Attack || !state.players[PTCG_LEARNER].active.empty());
    out.would_enable_attack_next_turn = out.would_enable_metal_defender
        || metal_defender_ready_next_turn_estimate(state);
    out.would_create_ko_option = option_attack_can_ko(state, option);
    return out;
}

static float archaludon_potential(const State& state) {
    float duraludon = count_learner_card_in_play(state, 169) > 0 ? 1.0f : 0.0f;
    float metal_discard = std::min(2, count_learner_card_in_trash(state, 8));
    float archaludon = count_learner_card_in_play(state, 190) > 0 ? 1.0f : 0.0f;
    float alloy = (ability_card_available(state, 190) || evolve_card_available(state, 190)) ? 1.0f : 0.0f;
    float defender = metal_defender_ready_now(state) ? 1.0f : 0.0f;
    float prize_advantage = (float)((int)state.players[1 - PTCG_LEARNER].prize.size()
        - (int)state.players[PTCG_LEARNER].prize.size());
    return 0.03f * duraludon
        + 0.04f * metal_discard
        + 0.08f * archaludon
        + 0.06f * alloy
        + 0.08f * defender
        + 0.12f * prize_advantage;
}

static float ratio_stat(float numerator, float denominator, float n) {
    if (denominator <= 0.0f || n <= 0.0f) return 0.0f;
    return (numerator / denominator) * n;
}

static void reset_turn_diagnostics(PTCG* env, int turn) {
    env->diag_turn_open = true;
    env->diag_turn_id = turn;
    env->diag_turn_attack_available_seen = false;
    env->diag_turn_attack_taken = false;
    env->diag_turn_ko_available_seen = false;
    env->diag_turn_ko_taken = false;
    env->diag_turn_metal_defender_available_seen = false;
    env->diag_turn_metal_defender_taken = false;
    env->diag_turn_best_attack_prizes_available_seen = 0.0f;
    env->diag_turn_best_attack_prizes_taken_seen = 0.0f;
}

static void flush_turn_diagnostics(PTCG* env) {
    if (!env->diag_turn_open) return;
    env->log.diag_turns += 1.0f;
    env->log.diag_turn_attack_available += env->diag_turn_attack_available_seen ? 1.0f : 0.0f;
    env->log.diag_turn_attacked_when_available +=
        (env->diag_turn_attack_available_seen && env->diag_turn_attack_taken) ? 1.0f : 0.0f;
    env->log.diag_turn_ko_available += env->diag_turn_ko_available_seen ? 1.0f : 0.0f;
    env->log.diag_turn_ko_taken_when_available +=
        (env->diag_turn_ko_available_seen && env->diag_turn_ko_taken) ? 1.0f : 0.0f;
    env->log.diag_turn_ended_with_attack_available +=
        (env->diag_turn_attack_available_seen && !env->diag_turn_attack_taken) ? 1.0f : 0.0f;
    env->log.diag_turn_ended_with_ko_available +=
        (env->diag_turn_ko_available_seen && !env->diag_turn_ko_taken) ? 1.0f : 0.0f;
    env->log.diag_turn_best_attack_prizes_available += env->diag_turn_best_attack_prizes_available_seen;
    env->log.diag_turn_best_attack_prizes_taken += env->diag_turn_best_attack_prizes_taken_seen;
    env->log.diag_turn_metal_defender_available +=
        env->diag_turn_metal_defender_available_seen ? 1.0f : 0.0f;
    env->log.diag_turn_metal_defender_chosen_when_available +=
        (env->diag_turn_metal_defender_available_seen && env->diag_turn_metal_defender_taken) ? 1.0f : 0.0f;
    env->diag_turn_open = false;
}

static void ensure_turn_diagnostics(PTCG* env, const State& state) {
    if (!env->diag_turn_open) {
        reset_turn_diagnostics(env, state.turn);
        return;
    }
    if (env->diag_turn_id != state.turn) {
        flush_turn_diagnostics(env);
        reset_turn_diagnostics(env, state.turn);
    }
}

static void record_tactical_diagnostics(PTCG* env, int action, bool valid_action) {
    State& state = env->battle->state;
    if (state.selectPlayer != PTCG_LEARNER || state.isFinish()) return;
    ensure_turn_diagnostics(env, state);

    bool attack_available = option_type_available(state, SelectOptionType::Attack);
    bool attach_available = option_type_available(state, SelectOptionType::Attach);
    bool play_available = option_type_available(state, SelectOptionType::Play);
    bool evolve_available = option_type_available(state, SelectOptionType::Evolve);
    bool ability_available = option_type_available(state, SelectOptionType::Ability);
    bool end_available = option_type_available(state, SelectOptionType::End);
    bool archaludon_attack_available = attack_with_card_available(state, 190);
    bool cinderace_attack_available = attack_with_card_available(state, 666);
    bool judge_available = play_card_available(state, 1213);
    bool jumbo_ice_cream_available = play_card_available(state, 1147);
    bool duraludon_bench_available = bench_card_available(state, 169);
    bool metal_discard_available = discard_basic_metal_available(state);
    bool archaludon_evolve_available = evolve_card_available(state, 190);
    bool assemble_alloy_available = ability_card_available(state, 190) || archaludon_evolve_available;
    bool assemble_alloy_selectable_available = ability_card_available(state, 190);
    bool metal_defender_available = attack_with_card_available(state, 190);
    bool attack_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Attack;
    bool attach_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Attach;
    bool play_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Play;
    bool evolve_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Evolve;
    bool ability_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Ability;
    bool end_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::End;
    bool retreat_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Retreat;
    bool archaludon_attack_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Attack
        && chosen_option_primary_card_id(state, action, 190);
    bool cinderace_attack_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Attack
        && chosen_option_primary_card_id(state, action, 666);
    bool judge_play = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Play
        && chosen_option_primary_card_id(state, action, 1213);
    bool jumbo_ice_cream_play = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Play
        && chosen_option_primary_card_id(state, action, 1147);
    bool assemble_alloy_selectable_chosen = valid_action
        && action >= 0
        && action < (int)state.options.size()
        && state.options[action].type == SelectOptionType::Ability
        && chosen_option_primary_card_id(state, action, 190);
    bool ko_available = attack_ko_available(state);
    int best_attack_prizes = best_attack_prizes_available(state);
    ArchaludonOptionOutcome arch_outcome;
    if (valid_action && action >= 0 && action < (int)state.options.size()) {
        const SelectOption& option = state.options[action];
        arch_outcome = archaludon_option_outcome(
            state,
            option,
            primary_card_ref_for_option(state, option),
            target_card_ref_for_option(state, option));
    }

    env->log.diag_learner_decisions += 1.0f;
    env->diag_turn_attack_available_seen = env->diag_turn_attack_available_seen || attack_available;
    env->diag_turn_attack_taken = env->diag_turn_attack_taken || attack_chosen;
    env->diag_turn_ko_available_seen = env->diag_turn_ko_available_seen || ko_available;
    env->diag_turn_metal_defender_available_seen =
        env->diag_turn_metal_defender_available_seen || metal_defender_available;
    env->diag_turn_metal_defender_taken =
        env->diag_turn_metal_defender_taken || archaludon_attack_chosen;
    env->diag_turn_best_attack_prizes_available_seen = std::max(
        env->diag_turn_best_attack_prizes_available_seen,
        (float)best_attack_prizes);
    env->log.diag_attack_available += attack_available ? 1.0f : 0.0f;
    env->log.diag_attack_chosen_when_available += (attack_available && attack_chosen) ? 1.0f : 0.0f;
    env->log.diag_ko_available += ko_available ? 1.0f : 0.0f;
    env->log.diag_ko_chosen += (ko_available && attack_chosen) ? 1.0f : 0.0f;
    if (ko_available && !attack_chosen) {
        if (play_chosen) env->log.diag_ko_miss_play += 1.0f;
        else if (attach_chosen) env->log.diag_ko_miss_attach += 1.0f;
        else if (evolve_chosen) env->log.diag_ko_miss_evolve += 1.0f;
        else if (ability_chosen) env->log.diag_ko_miss_ability += 1.0f;
        else if (end_chosen) env->log.diag_ko_miss_end += 1.0f;
        else if (retreat_chosen) env->log.diag_ko_miss_retreat += 1.0f;
        else env->log.diag_ko_miss_other += 1.0f;
    }
    env->log.diag_archaludon_in_play += count_learner_card_in_play(state, 190) > 0 ? 1.0f : 0.0f;
    env->log.diag_cinderace_in_play += count_learner_card_in_play(state, 666) > 0 ? 1.0f : 0.0f;
    env->log.diag_metal_energy_in_play += (float)player_total_energy_in_play(state, PTCG_LEARNER);
    env->log.diag_metal_energy_in_hand += (float)count_learner_card_in_hand(state, 8);
    env->log.diag_full_metal_lab_in_play += stadium_has_id(state, 1244) ? 1.0f : 0.0f;
    env->log.diag_archaludon_attack_available += archaludon_attack_available ? 1.0f : 0.0f;
    env->log.diag_archaludon_attack_chosen += (archaludon_attack_available && archaludon_attack_chosen) ? 1.0f : 0.0f;
    env->log.diag_cinderace_attack_available += cinderace_attack_available ? 1.0f : 0.0f;
    env->log.diag_cinderace_attack_chosen += (cinderace_attack_available && cinderace_attack_chosen) ? 1.0f : 0.0f;
    env->log.diag_judge_available += judge_available ? 1.0f : 0.0f;
    env->log.diag_judge_play += (judge_available && judge_play) ? 1.0f : 0.0f;
    env->log.diag_jumbo_ice_cream_available += jumbo_ice_cream_available ? 1.0f : 0.0f;
    env->log.diag_jumbo_ice_cream_play += (jumbo_ice_cream_available && jumbo_ice_cream_play) ? 1.0f : 0.0f;
    env->log.diag_duraludon_bench_available += duraludon_bench_available ? 1.0f : 0.0f;
    env->log.diag_duraludon_bench_chosen += (duraludon_bench_available && arch_outcome.would_bench_duraludon) ? 1.0f : 0.0f;
    env->log.diag_metal_discard_available += metal_discard_available ? 1.0f : 0.0f;
    env->log.diag_metal_discard_chosen += (metal_discard_available && arch_outcome.would_discard_basic_metal) ? 1.0f : 0.0f;
    env->log.diag_archaludon_evolve_available += archaludon_evolve_available ? 1.0f : 0.0f;
    env->log.diag_archaludon_evolve_chosen += (archaludon_evolve_available && arch_outcome.would_evolve_to_archaludon) ? 1.0f : 0.0f;
    env->log.diag_assemble_alloy_available += assemble_alloy_available ? 1.0f : 0.0f;
    env->log.diag_assemble_alloy_chosen += (assemble_alloy_available && arch_outcome.would_trigger_assemble_alloy) ? 1.0f : 0.0f;
    env->log.diag_assemble_alloy_selectable_available += assemble_alloy_selectable_available ? 1.0f : 0.0f;
    env->log.diag_assemble_alloy_selectable_chosen +=
        (assemble_alloy_selectable_available && assemble_alloy_selectable_chosen) ? 1.0f : 0.0f;
    env->log.diag_metal_defender_available += metal_defender_available ? 1.0f : 0.0f;
    env->log.diag_metal_defender_chosen += (metal_defender_available && archaludon_attack_chosen) ? 1.0f : 0.0f;
    env->log.diag_attach_available += attach_available ? 1.0f : 0.0f;
    env->log.diag_attach_chosen += (attach_available && attach_chosen) ? 1.0f : 0.0f;
    env->log.diag_play_available += play_available ? 1.0f : 0.0f;
    env->log.diag_play_chosen += (play_available && play_chosen) ? 1.0f : 0.0f;
    env->log.diag_evolve_available += evolve_available ? 1.0f : 0.0f;
    env->log.diag_evolve_chosen += (evolve_available && evolve_chosen) ? 1.0f : 0.0f;
    env->log.diag_ability_available += ability_available ? 1.0f : 0.0f;
    env->log.diag_ability_chosen += (ability_available && ability_chosen) ? 1.0f : 0.0f;
    env->log.diag_end_available += end_available ? 1.0f : 0.0f;
    env->log.diag_end_chosen += (end_available && end_chosen) ? 1.0f : 0.0f;
}

static bool selected_from_action(PTCG* env, int slot) {
    State& state = env->battle->state;
    int option_count = (int)state.options.size();
    int option_limit = std::min(option_count, PTCG_MAX_OPTIONS);
    float* action_ptr = env->num_agents > 1 ? env->action_ptr[slot] : env->actions;
    unsigned char* mask_ptr = env->num_agents > 1 ? env->action_mask_ptr[slot] : env->action_mask;
    int action = (int)action_ptr[0];
    bool action_in_range = action >= 0 && action < PTCG_ACTIONS;
    bool mask_legal = mask_ptr != nullptr && action_in_range && mask_ptr[action] != 0;
    bool learner_slot = slot == PTCG_LEARNER;

    auto log_invalid = [&]() {
        env->log.invalid_actions += 1.0f;
        if (mask_legal) env->log.invalid_mask_legal += 1.0f;
        else env->log.invalid_mask_illegal += 1.0f;
        env->log.invalid_action_sum += (float)action;
        env->log.invalid_action_max = std::max(env->log.invalid_action_max, (float)action);
        env->log.invalid_option_limit_sum += (float)option_limit;
    };

    if (action == PTCG_STOP_ACTION) {
        if (learner_slot) {
            env->log.action_total += 1.0f;
            env->log.action_internal_stop += 1.0f;
        }
        if (can_stop_now(state)) {
            env->log.submitted_selects += 1.0f;
            return true;
        }
        env->log.invalid_stop_actions += 1.0f;
        log_invalid();
    }

    bool can_choose_more = (int)state.selected.size() < state.selectMax;
    bool valid_action = action >= 0 && action < option_limit
        && can_choose_more && !selected_contains(state, action);

    if (!valid_action) {
        if (action != PTCG_STOP_ACTION) {
            if (action < 0 || action >= option_limit) {
                env->log.invalid_range_actions += 1.0f;
            } else if (selected_contains(state, action)) {
                env->log.invalid_repeat_actions += 1.0f;
            }
            log_invalid();
        }
        if (can_stop_now(state)) {
            env->log.submitted_selects += 1.0f;
            return true;
        }
        action = -1;
        for (int i = 0; i < option_limit; i++) {
            if (!selected_contains(state, i)) {
                action = i;
                break;
            }
        }
        if (action < 0) {
            env->log.submitted_selects += 1.0f;
            return true;
        }
    }

    if (learner_slot) record_tactical_diagnostics(env, action, valid_action);

    if (valid_action && learner_slot) {
        env->log.action_total += 1.0f;
        switch (state.options[action].type) {
            case SelectOptionType::End:
                env->log.action_official_end += 1.0f;
                break;
            case SelectOptionType::Yes:
                env->log.action_official_yes += 1.0f;
                break;
            case SelectOptionType::No:
                env->log.action_official_no += 1.0f;
                break;
            case SelectOptionType::Play:
                env->log.action_play += 1.0f;
                break;
            case SelectOptionType::Attach:
                env->log.action_attach += 1.0f;
                break;
            case SelectOptionType::Evolve:
                env->log.action_evolve += 1.0f;
                break;
            case SelectOptionType::Ability:
                env->log.action_ability += 1.0f;
                break;
            case SelectOptionType::Discard:
                env->log.action_discard += 1.0f;
                break;
            case SelectOptionType::Retreat:
                env->log.action_retreat += 1.0f;
                break;
            case SelectOptionType::Attack:
                env->log.action_attack += 1.0f;
                env->pending_attack_action = true;
                env->pending_attack_prizes_taken = state.takenPrizeCount(PTCG_LEARNER);
                env->pending_attack_turn = state.turn;
                env->reward_ptr[0][0] += PTCG_ATTACK_REWARD;
                env->episode_return += PTCG_ATTACK_REWARD;
                env->log.reward_shaping += PTCG_ATTACK_REWARD;
                env->log.reward_attack_bonus += PTCG_ATTACK_REWARD;
                break;
            case SelectOptionType::Card:
            case SelectOptionType::ToolCard:
            case SelectOptionType::EnergyCard:
            case SelectOptionType::Energy:
            case SelectOptionType::Number:
            case SelectOptionType::SpecialCondition:
            case SelectOptionType::Skill:
                env->log.action_target_select += 1.0f;
                break;
            default:
                env->log.action_other += 1.0f;
                break;
        }
    }

    state.selected.push_back(action);
    if (learner_slot) env->log.micro_selects += 1.0f;
    if ((int)state.selected.size() >= state.selectMax) {
        if (learner_slot) env->log.submitted_selects += 1.0f;
        return true;
    }
    return false;
}

static bool advance_one_selection(PTCG* env, bool learner_action) {
    State& state = env->battle->state;
    if (learner_action) {
        bool submit = selected_from_action(env, env->num_agents > 1 ? state.selectPlayer : PTCG_LEARNER);
        if (!submit) return false;
    } else if (ptcg_scorers::selected_public_agent(state, env->current_opponent)) {
    } else {
        // Official starter sample-submission baseline: legal random selections.
        selected_random(state, &env->rng);
    }

    int error = state.checkPlayerSelect();
    if (error != 0) {
        state.selected.clear();
        if (state.selectMin > 0 && !state.options.empty()) {
            state.selected.push_back(0);
        }
    }

    env->battle->next();
    while (!state.isFinish() && state.selectMax == 0) {
        state.selected.clear();
        env->battle->next();
    }
    if (env->pending_attack_action) {
        int prizes_taken = state.takenPrizeCount(PTCG_LEARNER);
        int prize_gain = std::max(0, prizes_taken - env->pending_attack_prizes_taken);
        if (prize_gain > 0) {
            float bonus = env->ko_attack_reward * (float)prize_gain;
            env->reward_ptr[0][0] += bonus;
            env->episode_return += bonus;
            env->log.reward_shaping += bonus;
            env->log.reward_ko_attack_bonus += bonus;
            env->log.diag_attack_prize_gain += (float)prize_gain;
            env->log.diag_attack_prize_rate += 1.0f;
            env->diag_turn_ko_taken = true;
            env->diag_turn_best_attack_prizes_taken_seen += (float)prize_gain;
            env->pending_attack_action = false;
            env->pending_attack_prizes_taken = 0;
            env->pending_attack_turn = -1;
        } else if (state.isFinish() || state.turn != env->pending_attack_turn) {
            env->pending_attack_action = false;
            env->pending_attack_prizes_taken = 0;
            env->pending_attack_turn = -1;
        }
    }
    return true;
}

static void advance_to_learner(PTCG* env) {
    State& state = env->battle->state;
    int guard = 0;
    while (!state.isFinish() && guard++ < 10000) {
        if (state.selectType == SelectType::None || state.selectMax == 0) {
            state.selected.clear();
            env->battle->next();
            continue;
        }
        if (state.selectPlayer == PTCG_LEARNER) {
            break;
        }
        advance_one_selection(env, false);
    }
}

static void advance_to_policy(PTCG* env) {
    if (env->num_agents <= 1) {
        advance_to_learner(env);
        return;
    }
    State& state = env->battle->state;
    int guard = 0;
    while (!state.isFinish() && guard++ < 10000) {
        if (state.selectType == SelectType::None || state.selectMax == 0) {
            state.selected.clear();
            env->battle->next();
            continue;
        }
        if (state.selectPlayer == 0 || state.selectPlayer == 1) {
            break;
        }
        state.selected.clear();
        env->battle->next();
    }
}

static void start_new_battle(PTCG* env) {
    ensure_ptcg_initialized();
    delete env->battle;
    env->battle = new BattleData();
    env->current_opponent = env->opponent;
    if (env->opponent == PTCG_OPPONENT_SELFPLAY) {
        env->current_opponent = env->player_deck;
    } else if (env->opponent < 0) {
        static constexpr std::array<int, 8> mixed_opponents = {
            PTCG_OPPONENT_KIYOTA_ABOMASNOW,
            PTCG_OPPONENT_KIYOTA_ABOMASNOW,
            PTCG_OPPONENT_KIYOTA_IONO,
            PTCG_OPPONENT_KIYOTA_IONO,
            PTCG_OPPONENT_KIYOTA_LUCARIO,
            PTCG_OPPONENT_KIYOTA_LUCARIO,
            PTCG_OPPONENT_DASHIMAKI_CRUSTLE,
            PTCG_OPPONENT_DASHIMAKI_CRUSTLE,
        };
        static constexpr std::array<int, 3> legacy_exact_opponents = {
            PTCG_OPPONENT_KIYOTA_ABOMASNOW,
            PTCG_OPPONENT_KIYOTA_IONO,
            PTCG_OPPONENT_KIYOTA_LUCARIO,
        };
        static constexpr std::array<int, 4> new_generic_opponents = {
            PTCG_OPPONENT_KIYOTA_ABOMASNOW,
            PTCG_OPPONENT_KIYOTA_IONO,
            PTCG_OPPONENT_KIYOTA_LUCARIO,
            PTCG_OPPONENT_DASHIMAKI_CRUSTLE,
        };
        static constexpr std::array<int, 11> meta_generic_opponents = {
            PTCG_OPPONENT_KIYOTA_ABOMASNOW,
            PTCG_OPPONENT_KIYOTA_IONO,
            PTCG_OPPONENT_KIYOTA_LUCARIO,
            PTCG_OPPONENT_DASHIMAKI_CRUSTLE,
            PTCG_OPPONENT_CRUSTLE_LIMITLESS,
            PTCG_OPPONENT_DRAGAPULT_EXACT,
            PTCG_OPPONENT_ARCHALUDON_EXACT,
            PTCG_OPPONENT_MAGCARGO_EXACT,
            PTCG_OPPONENT_IRON_THORNS_EXACT,
            PTCG_OPPONENT_XINPW8_ARCHALUDON,
            PTCG_OPPONENT_GREAT_TUSK_CRUSTLE,
        };
        if (env->opponent == PTCG_OPPONENT_CUSTOM_POOL && env->opponent_pool_size > 0) {
            float pick = ((float)rand_r(&env->rng) / (float)RAND_MAX) * env->opponent_weight_sum;
            float cumulative = 0.0f;
            env->current_opponent = env->opponent_pool[env->opponent_pool_size - 1];
            for (int i = 0; i < env->opponent_pool_size; i++) {
                cumulative += env->opponent_weights[i];
                if (pick <= cumulative) {
                    env->current_opponent = env->opponent_pool[i];
                    break;
                }
            }
        } else if (env->opponent == PTCG_OPPONENT_LEGACY_EXACT_POOL) {
            env->current_opponent = legacy_exact_opponents[rand_r(&env->rng) % legacy_exact_opponents.size()];
        } else if (env->opponent == PTCG_OPPONENT_NEW_GENERIC_POOL) {
            env->current_opponent = new_generic_opponents[rand_r(&env->rng) % new_generic_opponents.size()];
        } else if (env->opponent == PTCG_OPPONENT_META_GENERIC_POOL) {
            env->current_opponent = meta_generic_opponents[rand_r(&env->rng) % meta_generic_opponents.size()];
        } else {
            env->current_opponent = mixed_opponents[rand_r(&env->rng) % mixed_opponents.size()];
        }
    }
    env->battle->init(make_config(env));
    env->battle->game.rng = std::mt19937(env->rng);
    env->battle->start();
    env->battle->next();
    env->episode_length = 0;
    env->episode_return = 0.0f;
    env->pending_attack_action = false;
    env->pending_attack_prizes_taken = 0;
    env->pending_attack_turn = -1;
    env->diag_turn_open = false;
    env->diag_turn_id = -1;
    advance_to_policy(env);
    env->previous_archaludon_potential = env->player_deck == PTCG_OPPONENT_XINPW8_ARCHALUDON
        ? archaludon_potential(env->battle->state)
        : 0.0f;
    write_observation(env);
    write_action_mask(env);
}

static void add_episode_log(PTCG* env, float reward, bool timeout) {
    flush_turn_diagnostics(env);
    float win = reward > 0.0f ? 1.0f : 0.0f;
    float draw = reward == 0.0f ? 0.5f : 0.0f;
    env->log.perf += win + draw;
    env->log.win_rate += win;
    env->log.score += reward;
    env->log.episode_return += env->episode_return;
    env->log.episode_length += (float)env->episode_length;
    env->log.games += 1.0f;
    env->log.timeouts += timeout ? 1.0f : 0.0f;
    env->log.n += 1.0f;
    if (env->num_agents > 1) {
        float slot0_score = win + draw;
        float slot1_score = (reward < 0.0f ? 1.0f : 0.0f) + draw;
        env->log.slot_0_score += slot0_score;
        env->log.slot_1_score += slot1_score;
        if (env->tag > 0 && env->tag <= 8) {
            int bank_idx = env->tag - 1;
            env->log.hist_score_bank[bank_idx] += slot0_score;
            env->log.hist_n_bank[bank_idx] += 1.0f;
            env->log.hist_score += slot0_score;
            env->log.hist_n += 1.0f;
            env->boundary_reached = 1;
        }
    }
}

void my_init(Env* env, Dict* kwargs) {
    DictItem* opponent = dict_get_unsafe(kwargs, "opponent");
    env->opponent = opponent == nullptr ? 0 : (int)opponent->value;
    env->num_agents = env->opponent == PTCG_OPPONENT_SELFPLAY ? 2 : 1;
    env->current_opponent = env->opponent;
    env->opponent_pool_size = 0;
    env->opponent_weight_sum = 0.0f;
    for (int i = 0; i < 8; i++) {
        env->opponent_pool[i] = 0;
        env->opponent_weights[i] = 0.0f;
        char id_key[32];
        char weight_key[32];
        snprintf(id_key, sizeof(id_key), "opponent_pool_%d", i);
        snprintf(weight_key, sizeof(weight_key), "opponent_weight_%d", i);
        DictItem* id_item = dict_get_unsafe(kwargs, id_key);
        if (id_item == nullptr) continue;
        int id = (int)id_item->value;
        if (id <= 0) continue;
        DictItem* weight_item = dict_get_unsafe(kwargs, weight_key);
        float weight = weight_item == nullptr ? 1.0f : (float)weight_item->value;
        if (weight <= 0.0f) continue;
        env->opponent_pool[env->opponent_pool_size] = id;
        env->opponent_weights[env->opponent_pool_size] = weight;
        env->opponent_weight_sum += weight;
        env->opponent_pool_size++;
    }
    if (env->opponent == PTCG_OPPONENT_CUSTOM_POOL && env->opponent_pool_size == 0) {
        env->opponent = PTCG_OPPONENT_MIXED_POOL;
    }
    DictItem* player_deck = dict_get_unsafe(kwargs, "player_deck");
    env->player_deck = player_deck == nullptr
        ? PTCG_OPPONENT_KIYOTA_ABOMASNOW
        : (int)player_deck->value;
    DictItem* ko_attack_reward = dict_get_unsafe(kwargs, "ko_attack_reward");
    env->ko_attack_reward = ko_attack_reward == nullptr
        ? PTCG_KO_ATTACK_REWARD_DEFAULT
        : (float)ko_attack_reward->value;
    DictItem* ko_attack_mask = dict_get_unsafe(kwargs, "ko_attack_mask");
    env->ko_attack_mask = ko_attack_mask == nullptr
        ? 0.0f
        : (float)ko_attack_mask->value;
    env->battle = nullptr;
    env->episode_length = 0;
    env->episode_return = 0.0f;
    env->pending_attack_action = false;
    env->pending_attack_prizes_taken = 0;
    env->pending_attack_turn = -1;
    env->diag_turn_open = false;
    env->diag_turn_id = -1;
    env->tag = 0;
    env->boundary_reached = 0;
    ensure_ptcg_initialized();
}

void c_reset(Env* env) {
    for (int slot = 0; slot < env->num_agents; slot++) {
        env->reward_ptr[slot][0] = 0.0f;
        env->terminal_ptr[slot][0] = 0.0f;
    }
    start_new_battle(env);
}

static void apply_archaludon_potential_shaping(Env* env) {
    if (env->player_deck != PTCG_OPPONENT_XINPW8_ARCHALUDON || env->battle == nullptr) return;
    float next = archaludon_potential(env->battle->state);
    float bonus = PTCG_ARCHALUDON_SHAPING_SCALE
        * (PTCG_ARCHALUDON_SHAPING_GAMMA * next - env->previous_archaludon_potential);
    env->previous_archaludon_potential = next;
    if (bonus == 0.0f) return;
    env->reward_ptr[0][0] += bonus;
    env->episode_return += bonus;
    env->log.reward_shaping += bonus;
    env->log.reward_archaludon_potential += bonus;
}

void c_step(Env* env) {
    for (int slot = 0; slot < env->num_agents; slot++) {
        env->reward_ptr[slot][0] = 0.0f;
        env->terminal_ptr[slot][0] = 0.0f;
    }

    if (env->battle == nullptr || env->battle->state.isFinish()) {
        start_new_battle(env);
    }

    advance_to_policy(env);
    if (!env->battle->state.isFinish()) {
        bool submitted = advance_one_selection(env, true);
        env->episode_length++;
        if (submitted) {
            advance_to_policy(env);
        }
    }

    apply_archaludon_potential_shaping(env);

    bool timeout = !env->battle->state.isFinish()
        && env->episode_length >= PTCG_MAX_EPISODE_STEPS;
    if (env->battle->state.isFinish() || timeout) {
        int result = timeout ? 2 : env->battle->state.apiResult();
        float reward = result == PTCG_LEARNER ? 1.0f : (result == 2 ? 0.0f : -1.0f);
        env->reward_ptr[0][0] += reward;
        env->terminal_ptr[0][0] = 1.0f;
        if (env->num_agents > 1) {
            env->reward_ptr[1][0] += -reward;
            env->terminal_ptr[1][0] = 1.0f;
        }
        env->episode_return += reward;
        add_episode_log(env, reward, timeout);
        start_new_battle(env);
        return;
    }

    write_observation(env);
    write_action_mask(env);
}

void c_render(Env* env) {
    (void)env;
}

void c_close(Env* env) {
    delete env->battle;
    env->battle = nullptr;
}

void my_log(Log* log, Dict* out) {
    float n = std::max(log->n, 1.0f);

    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "win_rate", log->win_rate);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "reward_shaping", log->reward_shaping);
    dict_set(out, "reward_attack_bonus", log->reward_attack_bonus);
    dict_set(out, "reward_ko_attack_bonus", log->reward_ko_attack_bonus);
    dict_set(out, "reward_archaludon_potential", log->reward_archaludon_potential);
    dict_set(out, "diag_turn_ko_rate", ratio_stat(
        log->diag_turn_ko_taken_when_available, log->diag_turn_ko_available, n));
    dict_set(out, "diag_turn_ended_with_ko_available_rate", ratio_stat(
        log->diag_turn_ended_with_ko_available, log->diag_turn_ko_available, n));
    dict_set(out, "diag_turn_attack_rate", ratio_stat(
        log->diag_turn_attacked_when_available, log->diag_turn_attack_available, n));
    dict_set(out, "diag_turn_ended_with_attack_available_rate", ratio_stat(
        log->diag_turn_ended_with_attack_available, log->diag_turn_attack_available, n));
    dict_set(out, "diag_turn_metal_defender_rate", ratio_stat(
        log->diag_turn_metal_defender_chosen_when_available,
        log->diag_turn_metal_defender_available, n));
    dict_set(out, "diag_turn_best_attack_prizes_available",
        log->diag_turn_best_attack_prizes_available / n);
    dict_set(out, "diag_turn_best_attack_prizes_taken",
        log->diag_turn_best_attack_prizes_taken / n);
    dict_set(out, "diag_turns", log->diag_turns);
    dict_set(out, "diag_assemble_alloy_selectable_rate", ratio_stat(
        log->diag_assemble_alloy_selectable_chosen,
        log->diag_assemble_alloy_selectable_available, n));

    dict_set(out, "diag_attack_rate", ratio_stat(
        log->diag_attack_chosen_when_available, log->diag_attack_available, n));
    dict_set(out, "diag_attack_prize_rate", ratio_stat(
        log->diag_attack_prize_rate, log->action_attack, n));
    dict_set(out, "diag_ko_rate", ratio_stat(log->diag_ko_chosen, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_play_rate", ratio_stat(log->diag_ko_miss_play, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_attach_rate", ratio_stat(log->diag_ko_miss_attach, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_evolve_rate", ratio_stat(log->diag_ko_miss_evolve, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_ability_rate", ratio_stat(log->diag_ko_miss_ability, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_end_rate", ratio_stat(log->diag_ko_miss_end, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_retreat_rate", ratio_stat(log->diag_ko_miss_retreat, log->diag_ko_available, n));
    dict_set(out, "diag_ko_miss_other_rate", ratio_stat(log->diag_ko_miss_other, log->diag_ko_available, n));
    dict_set(out, "diag_attach_rate", ratio_stat(log->diag_attach_chosen, log->diag_attach_available, n));
    dict_set(out, "diag_play_rate", ratio_stat(log->diag_play_chosen, log->diag_play_available, n));
    dict_set(out, "diag_ability_rate", ratio_stat(log->diag_ability_chosen, log->diag_ability_available, n));
    dict_set(out, "diag_evolve_rate", ratio_stat(log->diag_evolve_chosen, log->diag_evolve_available, n));
    dict_set(out, "diag_end_rate", ratio_stat(log->diag_end_chosen, log->diag_end_available, n));
    dict_set(out, "diag_archaludon_rate", ratio_stat(
        log->diag_archaludon_in_play, log->diag_learner_decisions, n));
    dict_set(out, "diag_cinderace_rate", ratio_stat(
        log->diag_cinderace_in_play, log->diag_learner_decisions, n));
    dict_set(out, "diag_full_metal_lab_rate", ratio_stat(
        log->diag_full_metal_lab_in_play, log->diag_learner_decisions, n));
    dict_set(out, "diag_archaludon_attack_rate", ratio_stat(
        log->diag_archaludon_attack_chosen, log->diag_archaludon_attack_available, n));
    dict_set(out, "diag_cinderace_attack_rate", ratio_stat(
        log->diag_cinderace_attack_chosen, log->diag_cinderace_attack_available, n));
    dict_set(out, "diag_judge_rate", ratio_stat(
        log->diag_judge_play, log->diag_judge_available, n));
    dict_set(out, "diag_jumbo_ice_cream_rate", ratio_stat(
        log->diag_jumbo_ice_cream_play, log->diag_jumbo_ice_cream_available, n));
    dict_set(out, "diag_duraludon_bench_rate", ratio_stat(
        log->diag_duraludon_bench_chosen, log->diag_duraludon_bench_available, n));
    dict_set(out, "diag_metal_discard_rate", ratio_stat(
        log->diag_metal_discard_chosen, log->diag_metal_discard_available, n));
    dict_set(out, "diag_archaludon_evolve_rate", ratio_stat(
        log->diag_archaludon_evolve_chosen, log->diag_archaludon_evolve_available, n));
    dict_set(out, "diag_assemble_alloy_rate", ratio_stat(
        log->diag_assemble_alloy_chosen, log->diag_assemble_alloy_available, n));
    dict_set(out, "diag_metal_defender_rate", ratio_stat(
        log->diag_metal_defender_chosen, log->diag_metal_defender_available, n));

    dict_set(out, "diag_attack_available", log->diag_attack_available);
    dict_set(out, "diag_ko_available", log->diag_ko_available);
    dict_set(out, "diag_duraludon_bench_available", log->diag_duraludon_bench_available);
    dict_set(out, "diag_metal_discard_available", log->diag_metal_discard_available);
    dict_set(out, "diag_archaludon_evolve_available", log->diag_archaludon_evolve_available);
    dict_set(out, "diag_assemble_alloy_available", log->diag_assemble_alloy_available);
    dict_set(out, "diag_assemble_alloy_selectable_available",
        log->diag_assemble_alloy_selectable_available);
    dict_set(out, "diag_metal_defender_available", log->diag_metal_defender_available);
    dict_set(out, "diag_attack_prize_gain", log->diag_attack_prize_gain);
    dict_set(out, "diag_learner_decisions", log->diag_learner_decisions);

    dict_set(out, "micro_selects", log->micro_selects);
    dict_set(out, "submitted_selects", log->submitted_selects);
    dict_set(out, "action_total", log->action_total);
    dict_set(out, "action_play", log->action_play);
    dict_set(out, "action_attach", log->action_attach);
    dict_set(out, "action_ability", log->action_ability);
    dict_set(out, "action_evolve", log->action_evolve);
    dict_set(out, "action_attack", log->action_attack);
    dict_set(out, "action_official_end", log->action_official_end);
    dict_set(out, "action_internal_stop", log->action_internal_stop);
    dict_set(out, "action_retreat", log->action_retreat);
    dict_set(out, "diag_metal_energy_in_play", log->diag_metal_energy_in_play);
    dict_set(out, "diag_metal_energy_in_hand", log->diag_metal_energy_in_hand);
    dict_set(out, "diag_archaludon_attack_available", log->diag_archaludon_attack_available);
    dict_set(out, "diag_cinderace_attack_available", log->diag_cinderace_attack_available);
    dict_set(out, "slot_0_score", log->slot_0_score);
    dict_set(out, "slot_1_score", log->slot_1_score);
    dict_set(out, "hist_score", log->hist_score);
    dict_set(out, "hist_n", log->hist_n);
    dict_set(out, "hist_score_bank_0", log->hist_score_bank[0]);
    dict_set(out, "hist_score_bank_1", log->hist_score_bank[1]);
    dict_set(out, "hist_score_bank_2", log->hist_score_bank[2]);
    dict_set(out, "hist_score_bank_3", log->hist_score_bank[3]);
    dict_set(out, "hist_score_bank_4", log->hist_score_bank[4]);
    dict_set(out, "hist_score_bank_5", log->hist_score_bank[5]);
    dict_set(out, "hist_score_bank_6", log->hist_score_bank[6]);
    dict_set(out, "hist_score_bank_7", log->hist_score_bank[7]);
    dict_set(out, "hist_n_bank_0", log->hist_n_bank[0]);
    dict_set(out, "hist_n_bank_1", log->hist_n_bank[1]);
    dict_set(out, "hist_n_bank_2", log->hist_n_bank[2]);
    dict_set(out, "hist_n_bank_3", log->hist_n_bank[3]);
    dict_set(out, "hist_n_bank_4", log->hist_n_bank[4]);
    dict_set(out, "hist_n_bank_5", log->hist_n_bank[5]);
    dict_set(out, "hist_n_bank_6", log->hist_n_bank[6]);
    dict_set(out, "hist_n_bank_7", log->hist_n_bank[7]);

    dict_set(out, "n", log->n);
    dict_set(out, "games", log->games);
    dict_set(out, "timeouts", log->timeouts);
    dict_set(out, "max_option_count_seen", log->max_option_count_seen);
    dict_set(out, "invalid_actions", log->invalid_actions);
    dict_set(out, "option_truncations", log->option_truncations);
    dict_set(out, "stop_mask_errors", log->stop_mask_errors);
}
