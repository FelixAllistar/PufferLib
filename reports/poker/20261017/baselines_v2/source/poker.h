#pragma once
#include "core.h"
typedef float obs_t;
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"

#define OBS_SIZE POKER_OBS
#define NUM_ATNS 1
#define ACT_SIZES {POKER_ACTIONS}

struct Log {
    float perf, score, episode_return, episode_length, n;
};

typedef struct Client { int unused; } Client;

// Immutable, shared PufferNet snapshots. Same fp32 checkpoint layout and
// MinGRU equations as src/puffercpu.c. No per-decision allocation or search.
typedef struct PokerLeague {
    float* weights;
    int count, hidden, layers, stride;
} PokerLeague;

PokerLeague* poker_load_league(Dict* kwargs) {
    const char* path = dict_get_str(kwargs, "native_league");
    if (!strcmp(path, "None")) {
        return NULL;
    }
    FILE* manifest = fopen(path, "r");
    assert(manifest);
    PokerLeague* bank = (PokerLeague*)calloc(1, sizeof(PokerLeague));
    int players;
    char line[4096];
    char* read = fgets(line, sizeof(line), manifest);
    assert(read);
    int fields = sscanf(line, "poker_v1 %d %d %d %d", &players, &bank->hidden,
        &bank->layers, &bank->count);
    assert(fields == 4 && players == POKER_PLAYERS);
    assert(bank->hidden > 0 && bank->hidden <= 128 && bank->hidden % 8 == 0);
    assert(bank->layers > 0 && bank->layers <= 4 && bank->count > 0 && bank->count <= 16);
    int h = bank->hidden;
    bank->stride = h * (POKER_OBS + POKER_ACTIONS + 1) + bank->layers * 3 * h * h;
    bank->weights = (float*)malloc(bank->count * bank->stride * sizeof(float));
    for (int i = 0; i < bank->count; i++) {
        read = fgets(line, sizeof(line), manifest);
        assert(read);
        line[strcspn(line, "\r\n")] = 0;
        FILE* file = fopen(line, "rb");
        assert(file);
        size_t size = fread(bank->weights + i * bank->stride, sizeof(float),
            bank->stride, file);
        int extra = fgetc(file);
        assert(size == (size_t)bank->stride && extra == EOF);
        fclose(file);
    }
    fclose(manifest);
    printf("Poker league: %d frozen H%d/L%d banks, %zu weight bytes\n",
        bank->count, h, bank->layers, bank->count * bank->stride * sizeof(float));
    return bank;
}

void poker_linear(float* out, const float* x, const float* weights, int in, int n) {
    for (int o = 0; o < n; o++) {
        float sum = 0;
        int i = 0;
#if defined(__AVX2__)
        __m256 acc = _mm256_setzero_ps();
        for (; i + 8 <= in; i += 8) {
            acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_loadu_ps(x + i),
                _mm256_loadu_ps(weights + o * in + i)));
        }
        float lanes[8];
        _mm256_storeu_ps(lanes, acc);
        for (int k = 0; k < 8; k++) {
            sum += lanes[k];
        }
#endif
        for (; i < in; i++) {
            sum += x[i] * weights[o * in + i];
        }
        out[o] = sum;
    }
}

void poker_forward(PokerLeague* bank, int policy, float* state,
    const float* observations, float* logits) {
    int h = bank->hidden;
    float x[128], projected[384];
    const float* weights = bank->weights + policy * bank->stride;
    poker_linear(x, observations, weights, POKER_OBS, h);
    const float* recurrent = weights + h * (POKER_OBS + POKER_ACTIONS + 1);
    for (int l = 0; l < bank->layers; l++) {
        poker_linear(projected, x, recurrent + l * 3 * h * h, h, 3 * h);
        for (int i = 0; i < h; i++) {
            float value = projected[i] >= 0 ? projected[i] + 0.5f
                : 1.0f / (1.0f + expf(-projected[i]));
            float gate = 1.0f / (1.0f + expf(-projected[h + i]));
            float highway = 1.0f / (1.0f + expf(-projected[2 * h + i]));
            float s = state[l * h + i];
            s += gate * (value - s);
            state[l * h + i] = s;
            x[i] = highway * s + (1 - highway) * x[i];
        }
    }
    poker_linear(logits, x, weights + h * POKER_OBS, h, POKER_ACTIONS + 1);
}

// One fixed learner seat; only acting opponents run inference. Each seat owns
// recurrent state, and each hand samples a field held fixed until settlement.
struct Env {
    Poker* game;
    PokerLeague* league;
    float* memories;
    float rules_fraction;
    uint64_t policy_rng;
    uint32_t starting_stack;
    unsigned int rng;
    int lane, num_agents, tag, boundary_reached, bot_policy;
    int8_t opponents[POKER_PLAYERS];
    uint8_t league_owner;
    Agent agents[1];
    Log log;
    Client* client;
};

void puf_init(Env* env, Dict* kwargs) {
    if (!env->game) {
        env->game = (Poker*)calloc(1, sizeof(Poker));
        env->league = poker_load_league(kwargs);
        env->league_owner = 1;
    }
    int seed = dict_get(kwargs, "seed");
    env->game->rng[env->lane] = ((uint64_t)(unsigned)seed << 32) + env->rng + 1;
    env->policy_rng = env->game->rng[env->lane] ^ UINT64_C(0xd1b54a32d192ed03);
    env->starting_stack = dict_get(kwargs, "starting_stack");
    env->bot_policy = dict_get(kwargs, "bot_policy");
    env->rules_fraction = dict_get(kwargs, "rules_fraction");
    assert(env->starting_stack >= POKER_BB && env->starting_stack <= 65535);
    assert((int)dict_get(kwargs, "num_players") == POKER_PLAYERS);
    assert(poker_bot_supported(env->bot_policy));
    assert(env->rules_fraction >= 0 && env->rules_fraction <= 1);
    if (env->league) {
        env->memories = (float*)calloc((POKER_PLAYERS - 1) * env->league->hidden
            * env->league->layers, sizeof(float));
    }
    env->num_agents = 1;
    env->agents[0].policy = 0;
}

#define MY_VEC_INIT
Env* my_vec_init(int* size, int* starts, int* counts, Dict* vk, Dict* ek) {
    int total = dict_get(vk, "total_agents"), buffers = dict_get(vk, "num_buffers");
    assert(total > 0 && buffers > 0 && total % buffers == 0);
    assert((int)dict_get(vk, "num_policies") == 1);
    Env* envs = (Env*)calloc(total, sizeof(Env));
    PokerLeague* league = poker_load_league(ek);
    for (int b = 0; b < buffers; b++) {
        starts[b] = b * (total / buffers);
        counts[b] = total / buffers;
        Poker* block = NULL;
        for (int i = 0; i < counts[b]; i++) {
            if (i % POKER_BATCH == 0) {
                block = (Poker*)calloc(1, sizeof(Poker));
            }
            Env* env = envs + starts[b] + i;
            env->game = block;
            env->league = league;
            env->league_owner = starts[b] + i == 0;
            env->lane = i % POKER_BATCH;
            env->rng = starts[b] + i;
            puf_init(env, ek);
        }
    }
    *size = total;
    return envs;
}

void poker_field(Env* env) {
    Poker* g = env->game;
    int t = env->lane;
    while (g->actor[t] != 0 && g->actor[t] != POKER_DONE) {
        int p = g->actor[t], policy = env->opponents[p];
        int32_t action;
        if (policy < 0) {
            action = poker_bot(g, t, &env->policy_rng, env->bot_policy);
        } else {
            float obs[POKER_OBS], logits[POKER_ACTIONS + 1];
            int32_t amounts[POKER_ACTIONS];
            poker_observe(g, t, p, obs);
            unsigned mask = poker_actions(g, t, amounts);
            poker_forward(env->league, policy, env->memories + (p - 1)
                * env->league->hidden * env->league->layers, obs, logits);
            float maximum = -INFINITY, total = 0;
            for (int a = 0; a < POKER_ACTIONS; a++) {
                if ((mask & (1u << a)) && logits[a] > maximum) {
                    maximum = logits[a];
                }
            }
            for (int a = 0; a < POKER_ACTIONS; a++) {
                logits[a] = mask & (1u << a) ? expf(logits[a] - maximum) : 0;
                total += logits[a];
            }
            float sample = (poker_random(&env->policy_rng) >> 8) * 0x1p-24f * total;
            int selected = 31 - __builtin_clz(mask);
            for (int a = 0; a < POKER_ACTIONS; a++) {
                sample -= logits[a];
                if (sample < 0) {
                    selected = a;
                    break;
                }
            }
            action = amounts[selected];
        }
        poker_step(g, t, action);
    }
}

float poker_log_hand(Env* env) {
    Poker* g = env->game;
    int t = env->lane;
    float reward = ((int32_t)g->stack[0][t] - (int32_t)g->initial[0][t]) / 100.0f;
    env->log.perf += reward;
    env->log.score += reward;
    env->log.episode_return += reward;
    env->log.episode_length += g->decisions[t];
    env->log.n++;
    return reward;
}

void poker_publish(Env* env) {
    poker_observe(env->game, env->lane, 0, env->agents[0].observations);
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(env->game, env->lane, amounts);
    for (int a = 0; a < POKER_ACTIONS; a++) {
        env->agents[0].action_mask[a] = (mask >> a) & 1;
    }
}

void puf_reset(Env* env) {
    uint32_t stacks[POKER_PLAYERS];
    for (int p = 0; p < POKER_PLAYERS; p++) {
        stacks[p] = env->starting_stack;
    }
    do {
        Poker* g = env->game;
        int t = env->lane;
        if (env->league) {
            memset(env->memories, 0, (POKER_PLAYERS - 1) * env->league->hidden
                * env->league->layers * sizeof(float));
        }
        for (int p = 1; p < POKER_PLAYERS; p++) {
            env->opponents[p] = -1;
            if (env->league && (poker_random(&env->policy_rng) >> 8) * 0x1p-24f
                >= env->rules_fraction) {
                env->opponents[p] = poker_random(&env->policy_rng) % env->league->count;
            }
        }
        poker_deal(g, t, (g->button[t] + 1) % POKER_PLAYERS, stacks);
        poker_field(env);
        if (g->actor[t] != POKER_DONE) {
            break;
        }
        // No learner decision in this hand: retain its result in raw game logs.
        poker_log_hand(env);
    } while (1);
    poker_publish(env);
}

void puf_step(Env* env) {
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(env->game, env->lane, amounts);
    int action = env->agents[0].actions[0];
    assert(action >= 0 && action < POKER_ACTIONS && (mask & (1u << action)));
    poker_step(env->game, env->lane, amounts[action]);
    poker_field(env);
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = env->game->actor[env->lane] == POKER_DONE;
    if (env->agents[0].terminals[0]) {
        env->agents[0].rewards[0] = poker_log_hand(env);
        puf_reset(env);
    } else {
        poker_publish(env);
    }
}

void puf_set_bot_policy(Env* env, int policy) {
    assert(poker_bot_supported(policy));
    env->bot_policy = policy;
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "n", log->n);
}

void puf_close(Env* env) {
    free(env->memories);
    if (env->league_owner && env->league) {
        free(env->league->weights);
        free(env->league);
    }
    if (env->lane == 0) {
        free(env->game);
    }
}

void puf_render(Env* env) {
}
