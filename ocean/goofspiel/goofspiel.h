#pragma once

#include <math.h>

#include "pufferenv.h"

#define GS_OBS_T_DEFINED
typedef uint8_t obs_t;

#include "goofspiel_obs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goofspiel_exploit.h"

#ifdef __CUDACC__
void gs_gpu_exact_upload(const GSExactTable* tables, int count);
#endif

#ifndef GS_ENV_HD
#ifdef __CUDACC__
#define GS_ENV_HD __host__ __device__
#else
#define GS_ENV_HD
#endif
#endif

#define NUM_ATNS 1
#define ACT_SIZES {GS_NUM_CARDS}

struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float slot_0_points;
    float slot_0_score;
    float slot_1_score;
    float draw_rate;
    float n;
};

typedef struct Client {
    int width;
    int height;
} Client;

struct Env {
    Log log;
    GSConfig cfg;
    GSState state;
    unsigned int rng;
    int num_agents;
    int tag;
    int boundary_reached;
    uint64_t exact_node;
    int exact_depth;
    int exact_table;
    Agent agents[GS_MAX_PLAYERS];
    GSHistory history;
    Client* client;
};

#define GS_EXACT_POOL_MAX 256

static GSExactTable gs_exact_tables[GS_EXACT_POOL_MAX];
static int gs_exact_enabled;
static int gs_exact_banks;
static int gs_exact_history;
static int gs_exact_count;
static uint64_t gs_exact_seen;
static float gs_exact_current_prob;
static int gs_exact_restored;
static float gs_sweep_best = INFINITY;

static inline int gs_kw(Dict* kwargs, const char* key) {
    return (int)dict_get(kwargs, key);
}

static inline GSConfig gs_load_config(Dict* kwargs) {
    GSConfig cfg = {0};
    cfg.num_players = (uint8_t)gs_kw(kwargs, "num_players");
    cfg.num_cards = (uint8_t)gs_kw(kwargs, "num_cards");
    cfg.num_turns = (uint8_t)gs_kw(kwargs, "num_turns");
    cfg.prize_order = (uint8_t)gs_kw(kwargs, "prize_order");
    cfg.information = (uint8_t)gs_kw(kwargs, "information");
    cfg.egocentric = (uint8_t)gs_kw(kwargs, "egocentric");
    cfg.return_type = (uint8_t)gs_kw(kwargs, "return_type");
    cfg.tie_rule = (uint8_t)gs_kw(kwargs, "tie_rule");
    cfg.auto_forced_last = (uint8_t)gs_kw(kwargs, "auto_forced_last");
    cfg.open_spiel_obs = (uint8_t)gs_kw(kwargs, "open_spiel_obs");
    if (cfg.num_cards != GS_NUM_CARDS) {
        fprintf(stderr,
            "goofspiel env.num_cards=%d does not match compiled ABI "
            "GS_NUM_CARDS=%d (rebuild with GS_NUM_CARDS=%d for this config)\n",
            cfg.num_cards, GS_NUM_CARDS, cfg.num_cards);
        exit(1);
    }
    cfg.full_hand = (1u << cfg.num_cards) - 1u;
    cfg.total_points = cfg.num_cards * (cfg.num_cards + 1) / 2;
    return cfg;
}

static inline float gs_cpu_exploit(const char* checkpoint, Ini* ini) {
    char command[8192];
    snprintf(command, sizeof(command),
        "./goofspiel_exploit '%s' env.num_cards=%d env.num_turns=%d "
        "env.prize_order=%d env.information=%d env.egocentric=%d "
        "env.return_type=%d env.tie_rule=%d env.auto_forced_last=%d "
        "env.open_spiel_obs=%d policy.hidden_size=%d policy.num_layers=%d",
        checkpoint,
        (int)puf_ini_get(ini, "env", "num_cards"),
        (int)puf_ini_get(ini, "env", "num_turns"),
        (int)puf_ini_get(ini, "env", "prize_order"),
        (int)puf_ini_get(ini, "env", "information"),
        (int)puf_ini_get(ini, "env", "egocentric"),
        (int)puf_ini_get(ini, "env", "return_type"),
        (int)puf_ini_get(ini, "env", "tie_rule"),
        (int)puf_ini_get(ini, "env", "auto_forced_last"),
        (int)puf_ini_get(ini, "env", "open_spiel_obs"),
        (int)puf_ini_get(ini, "policy", "hidden_size"),
        (int)puf_ini_get(ini, "policy", "num_layers"));

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "failed to launch exact Goofspiel sweep evaluator\n");
        exit(1);
    }
    float score = NAN;
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        char* value = strstr(line, "exploitability=");
        if (value) score = strtof(value + 15, NULL);
    }
    int status = pclose(pipe);
    if (status != 0 || !isfinite(score)) {
        fprintf(stderr, "exact Goofspiel sweep evaluator failed\n");
        exit(1);
    }
    return score;
}

static inline float gs_checkpoint_score(const char* checkpoint, Ini* ini) {
#ifdef __CUDACC__
    float score = (float)gs_cuda_exploit(checkpoint, ini, NULL, NULL);
#else
    float score = gs_cpu_exploit(checkpoint, ini);
#endif
    if (!isfinite(score) || score < -1e-6f) {
        fprintf(stderr, "exact Goofspiel sweep evaluator returned invalid score\n");
        exit(1);
    }
    score = fmaxf(score, 0.0f);
    if (puf_ini_get(ini, "train", "emag_kl_coef") > 0.0) {
        char magnet[8192];
        snprintf(magnet, sizeof(magnet), "%s.emag", checkpoint);
#ifdef __CUDACC__
        float magnet_score = (float)gs_cuda_exploit(
            magnet, ini, NULL, NULL);
#else
        float magnet_score = gs_cpu_exploit(magnet, ini);
#endif
        if (!isfinite(magnet_score) || magnet_score < -1e-6f) {
            fprintf(stderr, "exact Goofspiel sweep evaluator returned invalid magnet score\n");
            exit(1);
        }
        magnet_score = fmaxf(magnet_score, 0.0f);
        printf("EMAg exact: last=%.9f magnet=%.9f\n", score, magnet_score);
        score = fminf(score, magnet_score);
    }
    return score;
}

static inline float gs_sweep_score(const char* checkpoint, Ini* ini) {
    float score = gs_checkpoint_score(checkpoint, ini);
    gs_sweep_best = fminf(gs_sweep_best, score);
    return gs_sweep_best;
}

#if GS_NUM_CARDS <= GS_EXACT_MAX_CARDS
#define PUF_SWEEP_SCORE(checkpoint, ini) gs_sweep_score(checkpoint, ini)
#else
#define PUF_SWEEP_SCORE(checkpoint, ini) 0.0f
#endif

GS_ENV_HD static inline void gs_observe(Env* env) {
    for (int observer = 0; observer < env->cfg.num_players; observer++) {
        gs_observe_player(&env->cfg, &env->state, observer,
            (obs_t*)env->agents[observer].observations);
    }
}

GS_ENV_HD static inline void gs_write_masks(Env* env) {
    for (int p = 0; p < env->cfg.num_players; p++) {
        unsigned char* mask = env->agents[p].action_mask;
        memset(mask, 0, GS_NUM_CARDS);
        memset(mask, 1, env->cfg.num_cards);
    }
}

static inline void gs_reset_state(Env* env) {
    gs_reset(&env->state, &env->history, &env->cfg, &env->rng);
    gs_write_masks(env);
    gs_observe(env);
    env->exact_depth = 0;
    uint32_t exact_draw = gs_mix32(env->rng ^ 0x85ebca6bu);
    env->exact_table = gs_exact_count <= 1
        || (double)exact_draw / 4294967296.0 < gs_exact_current_prob
        ? 0 : 1 + (int)(exact_draw % (uint32_t)(gs_exact_count - 1));
    env->exact_node = env->cfg.prize_order == GS_PRIZES_RANDOM
        ? env->state.prizes[0] : 0;
}

void puf_init(Env* env, Dict* kwargs) {
    env->cfg = gs_load_config(kwargs);
    if (!gs_config_valid(&env->cfg)) {
        fprintf(stderr,
            "invalid goofspiel config: players=%d cards=%d turns=%d "
            "prizes=%d info=%d returns=%d ties=%d\n",
            env->cfg.num_players, env->cfg.num_cards, env->cfg.num_turns,
            env->cfg.prize_order, env->cfg.information,
            env->cfg.return_type, env->cfg.tie_rule);
        exit(1);
    }
    if (env->cfg.open_spiel_obs
            && gs_open_spiel_obs_size(&env->cfg) > OBS_SIZE) {
        fprintf(stderr,
            "OpenSpiel observation needs %d floats, fixed buffer has %d\n",
            gs_open_spiel_obs_size(&env->cfg), OBS_SIZE);
        exit(1);
    }
    if (env->cfg.open_spiel_obs
            && env->cfg.information != GS_INFO_PERFECT) {
        fprintf(stderr,
            "env.open_spiel_obs currently implements the perfect-information "
            "OpenSpiel tensor\n");
        exit(1);
    }

    uint32_t stream = env->rng;
    uint32_t seed = (uint32_t)gs_kw(kwargs, "seed");
    env->rng = gs_mix32(seed ^ (0x9e3779b9u * (stream + 1u)));
    env->num_agents = env->cfg.num_players;
    env->tag = 0;
    env->boundary_reached = 0;
    env->client = NULL;
    memset(&env->log, 0, sizeof(env->log));
    gs_exact_enabled = gs_kw(kwargs, "exact_exploiter");
    gs_exact_banks = gs_kw(kwargs, "exact_exploiter_banks");
    gs_exact_history = gs_kw(kwargs, "exact_exploiter_history");
    gs_exact_current_prob = (float)dict_get(kwargs,
        "exact_exploiter_current_prob");
    if (gs_exact_enabled
            && (gs_exact_history < 1 || gs_exact_history > GS_EXACT_POOL_MAX)) {
        fprintf(stderr, "exact_exploiter_history must be 1..%d\n",
            GS_EXACT_POOL_MAX);
        exit(1);
    }
    if (gs_exact_enabled
            && (gs_exact_current_prob < 0.0f
                || gs_exact_current_prob > 1.0f)) {
        fprintf(stderr, "exact_exploiter_current_prob must be in [0, 1]\n");
        exit(1);
    }

    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].policy = p == 0 ? 0 : 1;
        env->agents[p].action_mask = NULL;
    }
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "slot_0_points", log->slot_0_points);
    dict_set(out, "slot_0_score", log->slot_0_score);
    dict_set(out, "slot_1_score", log->slot_1_score);
    dict_set(out, "draw_rate", log->draw_rate);
}

void puf_reset(Env* env) {
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].rewards[0] = 0.0f;
        env->agents[p].terminals[0] = 0.0f;
    }
    gs_reset_state(env);
}

static inline void gs_log_game(Env* env, const float* returns) {
    int max_score = -1;
    int winners = 0;
    for (int p = 0; p < env->num_agents; p++) {
        int score = env->state.scores[p];
        if (score > max_score) {
            max_score = score;
            winners = 1;
        } else if (score == max_score) {
            winners++;
        }
    }

    float draw = winners == env->num_agents;
    float slot_0 = draw ? 0.5f
        : env->state.scores[0] == max_score ? 1.0f : 0.0f;
    float slot_1 = env->num_agents > 1
        ? (draw ? 0.5f : env->state.scores[1] == max_score ? 1.0f : 0.0f)
        : 0.0f;
    int decisions = env->cfg.num_turns;
    if (env->cfg.auto_forced_last
            && env->cfg.num_turns == env->cfg.num_cards) {
        decisions--;
    }

    env->log.perf += slot_0;
    env->log.score += env->state.scores[0];
    env->log.episode_return += returns[0];
    env->log.episode_length += decisions;
    env->log.slot_0_points += env->state.scores[0];
    env->log.slot_0_score += slot_0;
    env->log.slot_1_score += slot_1;
    env->log.draw_rate += draw;
    env->log.n += 1.0f;
}

void puf_step(Env* env) {
    uint8_t bids[GS_MAX_PLAYERS];
    for (int p = 0; p < env->num_agents; p++) {
        bids[p] = (uint8_t)env->agents[p].actions[0];
        env->agents[p].rewards[0] = 0.0f;
        env->agents[p].terminals[0] = 0.0f;
    }

    GSExactTable* exact_table = gs_exact_tables + env->exact_table;
    int exact = gs_exact_enabled && gs_exact_count && env->tag > 0
        && env->tag <= gs_exact_banks
        && env->exact_depth < exact_table->decisions;
    if (exact) {
        bids[1] = exact_table->actions[env->exact_depth][env->exact_node];
    }
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].action_mask[bids[p]] = 0;
    }

    uint64_t next_exact_node = 0;
    if (exact && env->exact_depth + 1 < exact_table->decisions) {
        int hand_size = env->cfg.num_cards - env->state.round;
        int prize_choices = env->cfg.prize_order == GS_PRIZES_RANDOM
            ? env->cfg.num_cards - env->state.round - 1 : 1;
        uint32_t below_response = (1u << bids[1]) - 1u;
        uint32_t below_opponent = (1u << bids[0]) - 1u;
        int response_rank = __builtin_popcount(
            (unsigned int)(env->state.hands[1] & below_response));
        int opponent_rank = __builtin_popcount(
            (unsigned int)(env->state.hands[0] & below_opponent));
        int prize_rank = 0;
        if (env->cfg.prize_order == GS_PRIZES_RANDOM) {
            int next_prize = env->state.prizes[env->state.round + 1];
            uint32_t below_prize = (1u << next_prize) - 1u;
            prize_rank = __builtin_popcount((unsigned int)(
                env->state.remaining_prizes & below_prize));
        }
        int stride = hand_size * hand_size * prize_choices;
        next_exact_node = env->exact_node * stride
            + response_rank * hand_size * prize_choices
            + opponent_rank * prize_choices + prize_rank;
    }

    if (!gs_step(&env->state, &env->history, &env->cfg, bids)) {
        if (exact) {
            env->exact_node = next_exact_node;
            env->exact_depth++;
        }
        gs_observe(env);
        return;
    }

    float returns[GS_MAX_PLAYERS];
    gs_returns(&env->state, &env->cfg, returns);
    float scale = env->cfg.return_type == GS_RETURN_WIN_LOSS
        ? 1.0f : (float)env->cfg.total_points;
    for (int p = 0; p < env->num_agents; p++) {
        returns[p] /= scale;
        env->agents[p].rewards[0] = returns[p];
        env->agents[p].terminals[0] = 1.0f;
    }

    gs_log_game(env, returns);
    if (env->tag > 0) {
        env->boundary_reached = 1;
    }

    // Prepare the next episode immediately while preserving the completed
    // transition's rewards and terminal flags in Puffer's external buffers.
    gs_reset_state(env);
}

#ifdef __CUDACC__
#if GS_NUM_CARDS <= GS_EXACT_MAX_CARDS
static inline void gs_exact_save(const char* checkpoint) {
    gs_exact_pool_save(checkpoint, gs_exact_tables, gs_exact_count,
        gs_exact_history, gs_exact_seen);
}

static inline void gs_exact_load(const char* checkpoint, Ini* ini) {
    if (!puf_ini_get(ini, "env", "exact_exploiter")) return;
    if (!gs_exact_pool_load(checkpoint, gs_exact_tables, gs_exact_history,
            (int)puf_ini_get(ini, "env", "num_cards"),
            (int)puf_ini_get(ini, "env", "prize_order"),
            &gs_exact_count, &gs_exact_seen)) {
        printf("No exact response pool beside %s; starting a new pool\n",
            checkpoint);
        return;
    }
    gs_exact_restored = 1;
    printf("Restored exact response pool: pool=%d/%d seen=%llu from %s.exact\n",
        gs_exact_count, gs_exact_history,
        (unsigned long long)gs_exact_seen, checkpoint);
}

static inline void gs_load_hook(const char* checkpoint, Ini* ini) {
    gs_exact_load(checkpoint, ini);
}
#define PUF_LOAD_HOOK(checkpoint, ini) gs_load_hook(checkpoint, ini)

static inline void gs_checkpoint_hook(const char* checkpoint, Ini* ini) {
    if (puf_ini_get(ini, "base", "result_fd") > 0) {
        float score = gs_checkpoint_score(checkpoint, ini);
        if (score < gs_sweep_best) {
            gs_sweep_best = score;
            printf("Sweep exact best: exploitability=%.9f checkpoint=%s\n",
                score, checkpoint);
        }
    }
    if (!puf_ini_get(ini, "env", "exact_exploiter")) {
        return;
    }
    if (gs_exact_restored) {
        gs_exact_restored = 0;
        gs_exact_save(checkpoint);
        printf("Exact response pool continued unchanged at %s\n", checkpoint);
        return;
    }
    uint64_t nodes = 0;
    double milliseconds = 0.0;
    double exploitability = gs_cuda_pool_response(checkpoint, ini,
        gs_exact_tables, &gs_exact_count, gs_exact_history, &gs_exact_seen,
        &nodes, &milliseconds);
    gs_gpu_exact_upload(gs_exact_tables, gs_exact_count);
    gs_exact_save(checkpoint);
    printf("Exact response: exploitability=%.9f pool=%d/%d seen=%llu nodes=%llu milliseconds=%.3f\n",
        exploitability, gs_exact_count, gs_exact_history,
        (unsigned long long)gs_exact_seen,
        (unsigned long long)nodes, milliseconds);
}
#define PUF_CHECKPOINT_HOOK(checkpoint, ini) gs_checkpoint_hook(checkpoint, ini)
#else
/* The exact solver is 4-card-only. In a 13-card ABI build, disable the
 * solver-backed sweep/checkpoint hooks rather than abort on every save. */
#define PUF_SWEEP_SCORE(checkpoint, ini) 0.0f
#define PUF_CHECKPOINT_HOOK(checkpoint, ini) ((void)0)
#endif
#endif

#if defined(__CUDACC__) && defined(PUFFERLIB_BUILD_MAIN) \
    && GS_NUM_CARDS <= GS_EXACT_MAX_CARDS
#define GS_EXPLOIT_NO_MAIN
#include "goofspiel_exploit.cu"
#endif

void puf_close(Env* env) {
    if (env->client) {
        if (IsWindowReady()) {
            CloseWindow();
        }
        free(env->client);
        env->client = NULL;
    }
}

void puf_render(Env* env) {
    if (!env->client) {
        env->client = (Client*)calloc(1, sizeof(Client));
        env->client->width = 980;
        env->client->height = 180 + 48 * env->num_agents;
        InitWindow(env->client->width, env->client->height,
            "PufferLib Goofspiel");
        SetTargetFPS(12);
    }

    static const Color colors[GS_MAX_PLAYERS] = {SKYBLUE, ORANGE};

    BeginDrawing();
    ClearBackground((Color){14, 20, 28, 255});
    DrawText(TextFormat("Round %d/%d   Prize %d   Pot %d   Discarded %d",
        env->state.round + 1, env->cfg.num_turns,
        env->state.prizes[env->state.round] + 1,
        env->state.pot, env->state.discarded), 20, 16, 24, RAYWHITE);
    DrawText(TextFormat("%s information   %s ties   %s prizes",
        env->cfg.information == GS_INFO_PERFECT ? "perfect" : "hidden-bid",
        env->cfg.tie_rule == GS_TIE_CARRY ? "carry" : "discard",
        env->cfg.prize_order == GS_PRIZES_RANDOM ? "random"
            : env->cfg.prize_order == GS_PRIZES_ASCENDING
                ? "ascending" : "descending"),
        20, 50, 18, LIGHTGRAY);

    for (int p = 0; p < env->num_agents; p++) {
        int y = 90 + 48 * p;
        DrawText(TextFormat("P%d  score=%d", p, env->state.scores[p]),
            20, y + 8, 20, colors[p]);
        for (int card = 0; card < env->cfg.num_cards; card++) {
            int x = 170 + 47 * card;
            int held = (env->state.hands[p] >> card) & 1u;
            Color fill = held ? colors[p] : (Color){45, 50, 58, 255};
            DrawRectangle(x, y, 39, 38, fill);
            DrawRectangleLines(x, y, 39, 38, held ? RAYWHITE : DARKGRAY);
            DrawText(TextFormat("%d", card + 1), x + (card < 9 ? 14 : 8),
                y + 9, 18, held ? BLACK : GRAY);
        }
    }

    int history_y = 108 + 48 * env->num_agents;
    DrawText("Previous bids:", 20, history_y, 18, LIGHTGRAY);
    if (env->state.round == 0) {
        DrawText("new game", 175, history_y, 18, GRAY);
    } else {
        for (int p = 0; p < env->num_agents; p++) {
            DrawText(TextFormat("P%d=%d", p, env->state.last_bids[p] + 1),
                175 + 72 * p, history_y, 18, colors[p]);
        }
    }
    EndDrawing();
}
