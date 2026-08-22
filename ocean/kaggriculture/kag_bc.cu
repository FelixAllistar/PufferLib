// Standalone behavioral-cloning trainer for Kaggriculture.
// Two modes:
//   gen   - run bc.games seeded prefixes where both players use a strong bot
//           (bc.bot selects the expert profile below) and write bc.steps of
//           (obs, expert action-heads, packed mask) to bc.data.
//   train - load bc.data and minimize -log pi(a_expert | obs) over the whole
//           dataset with mini-batch SGD on the same MinGRU policy as training.
// Usage: ./kag_bc gen bc.games=200 bc.bot=1 bc.data=file
//        ./kag_bc train bc.data=file bc.epochs=2000 bc.lr=5e-5

#include "kaggriculture.h"
#include "ini.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/pufferl_preamble.h"
#include "../../src/algo.cu"

#define KAG_BC_MAGIC 0x4b414742u  // "KAGB"
#define KAG_BC_VERSION 2u

// Expert profile IDs. Values 0..4 are kept stable for older scripts; the
// remaining entries expose the public planner/tape bots added later.
enum {
    BC_BOT_RULES = 0,
    BC_BOT_TOP = 1,
    BC_BOT_STRUCTURED = 2,
    BC_BOT_PULSE = 3,
    BC_BOT_FRONTIER = 4,
    BC_BOT_TRIAD = 5,
    BC_BOT_THUNDER = 6,
    BC_BOT_LUGOVOY = 7,
    BC_BOT_THUNDER25 = 8,
    BC_BOT_V20 = 9,
    BC_BOT_MOON = 10,
    BC_BOT_HAMBURGER = 11,
    BC_BOT_COUNT = 12,
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t row_obs;
    uint32_t row_expert;
    uint32_t row_mask;
    uint32_t games;
    uint32_t steps;
} KagBCHeader;

#define KAG_BC_STATS 16

__device__ __forceinline__ bool kag_bc_head_active(
        const float* actions, int action_base, int head) {
    if (head < PUFFER_CONDITIONAL_PREFIX_HEADS) return true;
    int relative = head - PUFFER_CONDITIONAL_PREFIX_HEADS;
    int slot = relative / PUFFER_CONDITIONAL_HEADS_PER_SLOT;
    int node = relative % PUFFER_CONDITIONAL_HEADS_PER_SLOT;
    for (int previous = 0; previous < slot; previous++) {
        int continue_head = PUFFER_CONDITIONAL_PREFIX_HEADS
            + PUFFER_CONDITIONAL_HEADS_PER_SLOT * previous;
        if ((int)actions[action_base + continue_head]
                != PUFFER_CONDITIONAL_CONTINUE) return false;
    }
    if (node == 0) return true;
    int continue_head = PUFFER_CONDITIONAL_PREFIX_HEADS
        + PUFFER_CONDITIONAL_HEADS_PER_SLOT * slot;
    if ((int)actions[action_base + continue_head]
            != PUFFER_CONDITIONAL_CONTINUE) return false;
    if (node == 1) return true;
    return (int)actions[action_base + continue_head + 1]
        < PUFFER_CONDITIONAL_QUANTITY_COMMANDS;
}

/* Cross-entropy on conditionally reached expert heads. Padded rows use an
 * expert action of -1. Each four-float stats group is raw loss, active heads,
 * correct heads, exact rows: all/opening/post-opening/root. Opening and root
 * rows may carry extra optimization weight without contaminating the reported
 * accuracies. */
__global__ void kag_bc_loss_kernel(
        const precision_t* __restrict__ logits,   // (B, fused_cols)
        const float* __restrict__ expert,         // (B, num_atns)
        const unsigned char* __restrict__ mask,   // (B, packed) bits
        float* __restrict__ grad_logits,          // (B, A_total)
        float* __restrict__ stats,                // 4 floats
        const int* __restrict__ act_sizes,
        int rows, float valid_weight, int A_total, int num_atns,
        int mask_stride, int sequence_steps, int opening_steps,
        float opening_weight, float root_weight) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows || (int)expert[idx * num_atns] < 0) return;
    int logits_base = idx * (A_total + 1);
    int mask_base = idx * mask_stride;
    int offset = 0;
    float loss = 0.0f;
    int active = 0;
    int correct = 0;
    int exact = 1;
    int sequence_step = idx % sequence_steps;
    int opening = sequence_step < opening_steps;
    int root = sequence_step == 0;
    float row_weight = root ? root_weight
        : (opening ? opening_weight : 1.0f);
    for (int h = 0; h < num_atns; h++) {
        int A = act_sizes[h];
        int expert_action = (int)expert[idx * num_atns + h];
        if (!kag_bc_head_active(expert, idx * num_atns, h)) {
            offset += A;
            continue;
        }
        if (expert_action < 0 || expert_action >= A
                || !puf_mask_bit(mask, mask_base, offset + expert_action)) {
            exact = 0;
            offset += A;
            continue;
        }
        float max_val = -INFINITY;
        int prediction = 0;
        for (int a = 0; a < A; a++) {
            if (puf_mask_bit(mask, mask_base, offset + a)) {
                float l = to_float(logits[logits_base + offset + a]);
                if (l > max_val) {
                    max_val = l;
                    prediction = a;
                }
            }
        }
        if (max_val == -INFINITY) {
            offset += A;
            continue;
        }
        float sum_exp = 0.0f;
        for (int a = 0; a < A; a++) {
            if (puf_mask_bit(mask, mask_base, offset + a)) {
                sum_exp += __expf(
                    to_float(logits[logits_base + offset + a]) - max_val);
            }
        }
        float logsumexp = max_val + __logf(sum_exp);
        float expert_logit = to_float(
            logits[logits_base + offset + expert_action]);
        loss += logsumexp - expert_logit;
        active++;
        if (prediction == expert_action) correct++;
        else exact = 0;
        for (int a = 0; a < A; a++) {
            if (!puf_mask_bit(mask, mask_base, offset + a)) continue;
            float p = __expf(
                to_float(logits[logits_base + offset + a]) - logsumexp);
            grad_logits[idx * A_total + offset + a] =
                ((a == expert_action) ? (p - 1.0f) : p)
                    * row_weight / valid_weight;
        }
        offset += A;
    }
    if (stats) {
        atomicAdd(stats, loss);
        atomicAdd(stats + 1, (float)active);
        atomicAdd(stats + 2, (float)correct);
        atomicAdd(stats + 3, exact ? 1.0f : 0.0f);
        int group = opening ? 4 : 8;
        atomicAdd(stats + group, loss);
        atomicAdd(stats + group + 1, (float)active);
        atomicAdd(stats + group + 2, (float)correct);
        atomicAdd(stats + group + 3, exact ? 1.0f : 0.0f);
        if (root) {
            atomicAdd(stats + 12, loss);
            atomicAdd(stats + 13, (float)active);
            atomicAdd(stats + 14, (float)correct);
            atomicAdd(stats + 15, exact ? 1.0f : 0.0f);
        }
    }
}

static uint32_t bc_rng_state;
static uint32_t bc_rand(void) {
    uint32_t x = bc_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    bc_rng_state = x;
    return x;
}

static void bc_perturb_action(const KGState* game, int player,
        KGAction* action) {
    const KGPlayer* farm = &game->players[player];
    int unit = farm->unit_count > 0
        ? (int)(bc_rand() % (uint32_t)farm->unit_count) : 0;
    KGUnitAction* command = unit == 0
        ? &action->farmer : &action->hands[unit - 1];
    int candidates[5], count = 0;
    for (int op = KG_OP_NORTH; op <= KG_OP_PASS; op++) {
        KGPolicyUnitSpec spec = {op, -1, 1};
        if (kag_unit_action_legal(game, farm, unit, spec)) {
            candidates[count++] = op;
        }
    }
    if (count > 0) {
        *command = (KGUnitAction){
            candidates[bc_rand() % (uint32_t)count], -1, 1};
    }
    /* Sometimes remove the economic bundle too. The next observation is then
     * an off-demonstration state for which the expert supplies a recovery. */
    if ((bc_rand() & 3u) == 0) action->market_count = 0;
}

// Pick the bot action for a player. Each profile maps to a native opponent so
// the clone captures the exact behavior that now runs in the self-play lane.
static void bc_bot_action(const KGState* game, int player, int profile,
        KGAction* action) {
    switch (profile) {
        case BC_BOT_TOP:
            if (game->step < 26) {
                kag_script_action(game, player, KG_SCRIPT_TOP, action);
                kag_script_repair(game, player, KG_SCRIPT_TOP, action);
            } else {
                kag_bot_action(game, player, -1, action);
            }
            return;
        case BC_BOT_STRUCTURED:
            kag_adaptive_action(game, player, KAG_ADAPTIVE_STRUCTURED, action);
            return;
        case BC_BOT_PULSE:
            kag_adaptive_action(game, player, KAG_ADAPTIVE_HARVEST_PULSE, action);
            return;
        case BC_BOT_TRIAD:
            kag_adaptive_action(game, player, KAG_ADAPTIVE_TRIAD, action);
            return;
        case BC_BOT_THUNDER:
            kag_adaptive_action(game, player, KAG_ADAPTIVE_THUNDER, action);
            return;
        case BC_BOT_FRONTIER:
            kag_script_action(game, player, KG_SCRIPT_FRONTIER, action);
            kag_script_repair(game, player, KG_SCRIPT_FRONTIER, action);
            return;
        case BC_BOT_V20:
            kag_script_action(game, player, KG_SCRIPT_V20, action);
            kag_script_repair(game, player, KG_SCRIPT_V20, action);
            return;
        case BC_BOT_MOON:
            kag_script_action(game, player, KG_SCRIPT_MOON, action);
            kag_script_repair(game, player, KG_SCRIPT_MOON, action);
            return;
        case BC_BOT_HAMBURGER:
            kag_script_action(game, player, KG_SCRIPT_HAMBURGER, action);
            kag_script_repair(game, player, KG_SCRIPT_HAMBURGER, action);
            return;
        case BC_BOT_LUGOVOY:
            kag_script_action(game, player, KG_SCRIPT_LUGOVOY, action);
            kag_script_repair(game, player, KG_SCRIPT_LUGOVOY, action);
            return;
        case BC_BOT_THUNDER25:
            kag_script_action(game, player, KG_SCRIPT_THUNDER25, action);
            kag_script_repair(game, player, KG_SCRIPT_THUNDER25, action);
            return;
        case BC_BOT_RULES:
        default:
            kag_bot_action(game, player, -1, action);
            return;
    }
}

// Encode a KGAction into policy action-heads on the agent.
static void bc_set_expert(Env* env, int player, const KGAction* expert) {
    kag_clear_policy_actions(&env->agents[player]);
    kag_set_policy_unit(&env->agents[player], 0,
        expert->farmer.op, expert->farmer.arg, expert->farmer.n);
    for (int h = 0; h < expert->hand_count && h < KG_POLICY_DIRECT_HANDS; h++) {
        kag_set_policy_unit(&env->agents[player], h + 1,
            expert->hands[h].op, expert->hands[h].arg, expert->hands[h].n);
    }
    for (int o = 0; o < expert->market_count && o < KG_POLICY_MARKET_SLOTS; o++) {
        kag_set_policy_market(&env->agents[player], o,
            expert->market[o].op, expert->market[o].item, expert->market[o].n);
    }
}

static void bc_pack_mask(Env* env, int player, unsigned char* out, int mask_size,
        int packed_stride) {
    for (int byte = 0; byte < packed_stride; byte++) {
        unsigned char bits = 0;
        for (int bit = 0; bit < 8; bit++) {
            int action = byte * 8 + bit;
            if (action < mask_size && env->agents[player].action_mask[action]) {
                bits |= (unsigned char)(1u << bit);
            }
        }
        out[byte] = bits;
    }
}

static Env bc_make_env(Ini* ini, uint32_t rng) {
    Env env = {};
    env.rng = rng;
    puf_init(&env, puf_ini_section(ini, "env", 0));
    int mask_size = KG_POLICY_ACTION_MASK_SIZE;
    for (int p = 0; p < KG_NUM_PLAYERS; p++) {
        env.agents[p].observations = (obs_t*)calloc(OBS_SIZE, 1);
        env.agents[p].actions = (float*)calloc(NUM_ATNS, sizeof(float));
        env.agents[p].rewards = (float*)calloc(1, sizeof(float));
        env.agents[p].terminals = (float*)calloc(1, sizeof(float));
        env.agents[p].action_mask = (unsigned char*)calloc(mask_size, 1);
    }
    puf_reset(&env);
    return env;
}

// ---- Mode 1: generate a dataset file ----
static int bc_gen(Ini* ini) {
    int games = (int)puf_ini_get(ini, "bc", "games");
    int requested_steps = (int)puf_ini_get(ini, "bc", "steps");
    int profile = (int)puf_ini_get(ini, "bc", "bot");
    int opp = (int)puf_ini_get(ini, "bc", "opponent");
    int configured_seat = (int)puf_ini_get(ini, "bc", "seat");
    float rollout_noise = (float)puf_ini_get(ini, "bc", "rollout_noise");
    int bc_seed = (int)puf_ini_get(ini, "bc", "seed");
    const char* data_path = puf_ini_get_str(ini, "bc", "data");
    if (games <= 0) games = 50;
    if (requested_steps <= 0 || requested_steps > 720) {
        fprintf(stderr, "bc.steps must be in [1, 720]\n");
        return 1;
    }
    if (!data_path || !data_path[0]) data_path = "saved/kaggriculture_bc_data.bin";
    bc_rng_state = (uint32_t)bc_seed * 2654435761u + 1u;

    int mask_size = KG_POLICY_ACTION_MASK_SIZE;
    int packed_stride = (mask_size + 7) / 8;
    size_t row_obs = OBS_SIZE;
    size_t row_expert = NUM_ATNS;
    size_t row_mask = packed_stride;
    size_t cap = (size_t)games * (size_t)requested_steps;
    obs_t* obs = (obs_t*)malloc(cap * row_obs);
    float* expert = (float*)malloc(cap * row_expert * sizeof(float));
    unsigned char* mask = (unsigned char*)malloc(cap * row_mask);
    if (!obs || !expert || !mask) { perror("malloc"); return 1; }

    size_t count = 0;
    int sequence_steps = requested_steps;
    int end_animals = 0;
    for (int g = 0; g < games; g++) {
        Env env = bc_make_env(ini, (uint32_t)g);
        int learner = configured_seat < 0 ? (int)(bc_rand() & 1u)
            : configured_seat;
        if (learner < 0 || learner >= KG_NUM_PLAYERS) {
            fprintf(stderr, "bc.seat must be -1, 0, or 1\n");
            return 1;
        }
        int game_opp = opp < 0 ? (int)(bc_rand() % BC_BOT_COUNT) : opp;
        int steps = 0;
        while (!env.game_storage.done && steps < sequence_steps) {
            KGAction a0 = {}, a1 = {};
            bc_bot_action(&env.game_storage, learner, profile,
                learner == 0 ? &a0 : &a1);
            bc_bot_action(&env.game_storage, 1 - learner, game_opp,
                learner == 0 ? &a1 : &a0);
            KGAction* learner_action = learner == 0 ? &a0 : &a1;
            kag_compact_animal_repair(&env.game_storage, learner,
                learner_action);
            bc_set_expert(&env, learner, learner_action);
            memcpy(obs + count * row_obs,
                env.agents[learner].observations, row_obs);
            memcpy(expert + count * row_expert,
                env.agents[learner].actions,
                row_expert * sizeof(float));
            bc_pack_mask(&env, learner, mask + count * row_mask, mask_size,
                packed_stride);
            count++;
            /* Labels live in the compact policy action space. Advance with
             * that label decoded back into a game action—not the richer bot
             * command—so recurrent training never observes an impossible
             * transition (for example teacher quantity 12 labeled as bucket
             * 10, or PICKUP 1 represented by the policy's PICKUP ALL). */
            KGAction projected = {};
            kag_decode_action(&projected, &env.agents[learner],
                &env.game_storage, learner);
            *learner_action = projected;
            if (rollout_noise > 0.0f) {
                uint32_t cutoff = (uint32_t)(rollout_noise * 16777216.0f);
                if ((bc_rand() >> 8) < cutoff) {
                    bc_perturb_action(&env.game_storage, learner,
                        learner_action);
                }
            }
            KGAction pair[KG_NUM_PLAYERS] = {a0, a1};
            kg_step(&env.game_storage, pair);
            kag_write_all_observations_from_tapes(&env, kag_script_tapes);
            steps++;
        }
        /* A nominal 720-turn Kaggle episode exposes 719 decision observations:
         * the final core transition is terminal and has no next action. Infer
         * that exact recurrent length once, then require every game to match. */
        if (g == 0 && env.game_storage.done && steps < sequence_steps) {
            sequence_steps = steps;
        }
        if (steps != sequence_steps) {
            fprintf(stderr,
                "BC game %d ended at %d before requested prefix %d\n",
                g, steps, sequence_steps);
            return 1;
        }
        for (int word = 0; word < KG_TILE_WORDS; word++) {
            end_animals += __builtin_popcountll(
                env.game_storage.players[learner].animal_bits[word]);
        }
        for (int p = 0; p < KG_NUM_PLAYERS; p++) {
            free(env.agents[p].observations);
            free(env.agents[p].actions);
            free(env.agents[p].rewards);
            free(env.agents[p].terminals);
            free(env.agents[p].action_mask);
        }
        if ((g + 1) % 10 == 0 || g == games - 1) {
            printf("gen game %d/%d steps=%d total=%zu\n", g + 1, games,
                steps, count);
        }
    }

    FILE* fp = fopen(data_path, "wb");
    if (!fp) { perror(data_path); return 1; }
    KagBCHeader header = {
        KAG_BC_MAGIC, KAG_BC_VERSION, (uint32_t)count,
        (uint32_t)row_obs, (uint32_t)row_expert, (uint32_t)row_mask,
        (uint32_t)games, (uint32_t)sequence_steps,
    };
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(obs, 1, count * row_obs, fp);
    fwrite(expert, sizeof(float), count * row_expert, fp);
    fwrite(mask, 1, count * row_mask, fp);
    fclose(fp);
    printf("BC dataset written: %s (%zu steps, %.1f MB)\n", data_path,
        count, (double)(count * (row_obs + row_expert * 4 + row_mask))
            / 1048576.0);
    printf("BC oracle end animals: %.3f/game\n",
        (float)end_animals / (float)games);
    free(obs); free(expert); free(mask);
    return 0;
}

// On-policy DAgger rollout. The current clone (bc.student) acts in the
// environment; the expert bot (bc.bot) labels every state the clone reaches.
// With probability bc.beta the expert's action is executed instead of the
// clone's, bounding compounding drift while keeping the state distribution
// close to the clone's.
static int bc_gen_dagger(Ini* ini) {
    int games = (int)puf_ini_get(ini, "bc", "games");
    int requested_steps = (int)puf_ini_get(ini, "bc", "steps");
    int profile = (int)puf_ini_get(ini, "bc", "bot");
    int opp = (int)puf_ini_get(ini, "bc", "opponent");
    int configured_seat = (int)puf_ini_get(ini, "bc", "seat");
    float beta = (float)puf_ini_get(ini, "bc", "beta");
    int dagger_batch = (int)puf_ini_get(ini, "bc", "dagger_batch");
    int bc_seed = (int)puf_ini_get(ini, "bc", "seed");
    const char* student_path = puf_ini_get_str(ini, "bc", "student");
    const char* data_path = puf_ini_get_str(ini, "bc", "data");
    int hidden = (int)puf_ini_get(ini, "policy", "hidden_size");
    int layers = (int)puf_ini_get(ini, "policy", "num_layers");
    if (games <= 0) games = 50;
    if (dagger_batch <= 0) dagger_batch = 16;
    if (beta < 0.0f) beta = 0.0f;
    if (beta > 1.0f) beta = 1.0f;
    if (!student_path || !student_path[0]
            || strcmp(student_path, "None") == 0) {
        fprintf(stderr, "bc.student is required for DAgger generation\n");
        return 1;
    }
    if (!data_path || !data_path[0]) data_path = "saved/kaggriculture_bc_data.bin";
    int episode_steps = (int)puf_ini_get(ini, "env", "episode_steps");
    int sequence_steps = requested_steps > 0
        && requested_steps <= episode_steps - 1 ? requested_steps
        : episode_steps - 1;
    bc_rng_state = (uint32_t)bc_seed * 2654435761u + 1u;

    int num_atns = NUM_ATNS;
    int mask_size = KG_POLICY_ACTION_MASK_SIZE;
    int packed_stride = (mask_size + 7) / 8;
    int act_n = 0;
    {
        int sizes[] = ACT_SIZES;
        for (int i = 0; i < num_atns; i++) act_n += sizes[i];
    }
    int fused_cols = act_n + 1;

    // Reuse the rollout path, not the training path: one decoder row per game
    // and a carried MinGRU state tensor, exactly like pufferl_forward.
    Policy policy = build_policy("kaggriculture", OBS_SIZE, hidden,
        layers, act_n, false, 1);
    Allocator params = {0}, acts = {0};
    PolicyWeights weights = policy_weights_create(&policy, &params);
    PolicyActivations inf_acts = policy_reg_rollout(&policy, weights,
        &acts, dagger_batch);
    PrecisionTensor state = {.shape = {layers, dagger_batch, hidden}};
    alloc_register(&acts, &state);
    create_allocator_or_die("params", &params);
    create_allocator_or_die("acts", &acts);
    precision_t* param_puf = (precision_t*)params.mem;
    FloatTensor master_weights = {
        .data = (float*)xcuda((size_t)params.total_elems * sizeof(float)),
        .shape = {params.total_elems}};
    uint64_t init_seed = 42;
    policy_init_weights(&policy, weights, &init_seed, 0);
    cudaDeviceSynchronize();
    cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0, 0>>>(
        master_weights.data, param_puf, (int)params.total_elems);
    cudaDeviceSynchronize();
    float* host_weights = (float*)malloc(
        (size_t)params.total_elems * sizeof(float));
    if (!host_weights) { perror("malloc"); return 1; }
    FILE* student_file = fopen(student_path, "rb");
    if (!student_file) { perror(student_path); return 1; }
    if (fseek(student_file, 0, SEEK_END) != 0
            || ftell(student_file) != (long)(params.total_elems * sizeof(float))
            || fseek(student_file, 0, SEEK_SET) != 0
            || fread(host_weights, sizeof(float),
                (size_t)params.total_elems, student_file)
                != (size_t)params.total_elems) {
        fprintf(stderr, "incompatible DAgger student checkpoint: %s\n",
            student_path);
        fclose(student_file);
        return 1;
    }
    fclose(student_file);
    cudaStream_t stream;
    cudaStreamCreate(&stream);
    cudaMemcpyAsync(master_weights.data, host_weights,
        (size_t)params.total_elems * sizeof(float),
        cudaMemcpyHostToDevice, stream);
    cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0, stream>>>(
        param_puf, master_weights.data, (int)params.total_elems);
    cudaStreamSynchronize(stream);

    obs_t* d_obs_raw = (obs_t*)xcuda((size_t)dagger_batch * OBS_SIZE);
    precision_t* d_obs = (precision_t*)xcuda(
        (size_t)dagger_batch * OBS_SIZE * sizeof(precision_t));
    float* d_logits = (float*)xcuda(
        (size_t)dagger_batch * fused_cols * sizeof(float));
    obs_t* h_obs = (obs_t*)malloc((size_t)dagger_batch * OBS_SIZE);
    float* h_logits = (float*)malloc(
        (size_t)dagger_batch * fused_cols * sizeof(float));
    if (!d_obs_raw || !d_obs || !d_logits || !h_obs || !h_logits) {
        perror("malloc"); return 1;
    }
    PrecisionTensor obs_tensor = {.data = d_obs,
        .shape = {dagger_batch, OBS_SIZE}};

    size_t row_obs = OBS_SIZE;
    size_t row_expert = NUM_ATNS;
    size_t row_mask = packed_stride;
    size_t cap = (size_t)games * (size_t)sequence_steps;
    obs_t* all_obs = (obs_t*)malloc(cap * row_obs);
    float* expert = (float*)malloc(cap * row_expert * sizeof(float));
    unsigned char* mask = (unsigned char*)malloc(cap * row_mask);
    Env* envs = (Env*)calloc((size_t)dagger_batch, sizeof(Env));
    int* learner_seat = (int*)malloc((size_t)dagger_batch * sizeof(int));
    int* game_opp = (int*)malloc((size_t)dagger_batch * sizeof(int));
    if (!all_obs || !expert || !mask || !envs || !learner_seat || !game_opp) {
        perror("malloc"); return 1;
    }

    size_t count = 0;
    for (int batch_start = 0; batch_start < games;
            batch_start += dagger_batch) {
        int B = games - batch_start < dagger_batch
            ? games - batch_start : dagger_batch;
        for (int b = 0; b < B; b++) {
            envs[b] = bc_make_env(ini, (uint32_t)(batch_start + b));
            learner_seat[b] = configured_seat < 0
                ? (int)(bc_rand() & 1u) : configured_seat;
            if (learner_seat[b] < 0 || learner_seat[b] >= KG_NUM_PLAYERS) {
                fprintf(stderr, "bc.seat must be -1, 0, or 1\n");
                return 1;
            }
            game_opp[b] = opp < 0 ? (int)(bc_rand() % BC_BOT_COUNT) : opp;
        }
        cudaMemsetAsync(state.data, 0,
            numel(state.shape) * sizeof(precision_t), stream);
        for (int t = 0; t < sequence_steps; t++) {
            for (int b = 0; b < B; b++) {
                memcpy(h_obs + b * OBS_SIZE,
                    envs[b].agents[learner_seat[b]].observations, OBS_SIZE);
            }
            cudaMemcpyAsync(d_obs_raw, h_obs,
                (size_t)B * OBS_SIZE, cudaMemcpyHostToDevice, stream);
            cast<<<grid_size(B * OBS_SIZE), BLOCK_SIZE, 0, stream>>>(
                d_obs, d_obs_raw, B * OBS_SIZE);
            PrecisionTensor dec = policy_forward(&policy, weights, inf_acts,
                obs_tensor, state, stream);
            cast<<<grid_size(B * fused_cols), BLOCK_SIZE, 0, stream>>>(
                d_logits, dec.data, B * fused_cols);
            cudaMemcpyAsync(h_logits, d_logits,
                (size_t)B * fused_cols * sizeof(float),
                cudaMemcpyDeviceToHost, stream);
            cudaStreamSynchronize(stream);

            for (int b = 0; b < B; b++) {
                int learner = learner_seat[b];
                Env* env = &envs[b];
                /* Greedy student decode from the live semantic mask. Market
                 * tails beyond a STOP are ignored later by kag_decode_action. */
                int offset = 0;
                for (int h = 0; h < NUM_ATNS; h++) {
                    int A = KG_ACTION_SIZES[h];
                    float best = -INFINITY;
                    int best_a = 0;
                    for (int a = 0; a < A; a++) {
                        if (env->agents[learner].action_mask[offset + a]) {
                            float l = h_logits[(size_t)b * fused_cols
                                + offset + a];
                            if (l > best) { best = l; best_a = a; }
                        }
                    }
                    env->agents[learner].actions[h] = (float)best_a;
                    offset += A;
                }
                KGAction student_act = {};
                kag_decode_action(&student_act, &env->agents[learner],
                    &env->game_storage, learner);

                KGAction expert_act = {};
                bc_bot_action(&env->game_storage, learner, profile,
                    &expert_act);
                kag_compact_animal_repair(&env->game_storage, learner,
                    &expert_act);
                bc_set_expert(env, learner, &expert_act);

                memcpy(all_obs + count * row_obs,
                    env->agents[learner].observations, row_obs);
                memcpy(expert + count * row_expert,
                    env->agents[learner].actions,
                    row_expert * sizeof(float));
                bc_pack_mask(env, learner, mask + count * row_mask, mask_size,
                    packed_stride);
                count++;

                KGAction step_act = {};
                if (beta > 0.0f
                        && (bc_rand() >> 8) < (uint32_t)(beta * 16777216.0f)) {
                    kag_decode_action(&step_act, &env->agents[learner],
                        &env->game_storage, learner);
                } else {
                    step_act = student_act;
                }
                KGAction opp_act = {};
                bc_bot_action(&env->game_storage, 1 - learner,
                    game_opp[b], &opp_act);
                KGAction pair[KG_NUM_PLAYERS];
                pair[learner] = step_act;
                pair[1 - learner] = opp_act;
                kg_step(&env->game_storage, pair);
                kag_write_all_observations_from_tapes(env, kag_script_tapes);
            }
        }
        for (int b = 0; b < B; b++) {
            for (int p = 0; p < KG_NUM_PLAYERS; p++) {
                free(envs[b].agents[p].observations);
                free(envs[b].agents[p].actions);
                free(envs[b].agents[p].rewards);
                free(envs[b].agents[p].terminals);
                free(envs[b].agents[p].action_mask);
            }
            memset(&envs[b], 0, sizeof(envs[b]));
        }
        printf("dagger batch %d/%d total=%zu\n",
            batch_start / dagger_batch + 1,
            (games + dagger_batch - 1) / dagger_batch, count);
    }

    FILE* fp = fopen(data_path, "wb");
    if (!fp) { perror(data_path); return 1; }
    KagBCHeader header = {
        KAG_BC_MAGIC, KAG_BC_VERSION, (uint32_t)count,
        (uint32_t)row_obs, (uint32_t)row_expert, (uint32_t)row_mask,
        (uint32_t)games, (uint32_t)sequence_steps,
    };
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(all_obs, 1, count * row_obs, fp);
    fwrite(expert, sizeof(float), count * row_expert, fp);
    fwrite(mask, 1, count * row_mask, fp);
    fclose(fp);
    printf("DAgger dataset written: %s (%zu steps, %.1f MB)\n", data_path,
        count, (double)(count * (row_obs + row_expert * 4 + row_mask))
            / 1048576.0);
    free(all_obs); free(expert); free(mask); free(envs);
    free(learner_seat); free(game_opp); free(host_weights);
    free(h_obs); free(h_logits);
    cudaFree(d_obs_raw); cudaFree(d_obs); cudaFree(d_logits);
    cudaStreamDestroy(stream);
    return 0;
}

static int bc_train(Ini* ini) {
    const char* data_path = puf_ini_get_str(ini, "bc", "data");
    int bc_epochs = (int)puf_ini_get(ini, "bc", "epochs");
    float bc_lr = (float)puf_ini_get(ini, "bc", "learning_rate");
    int hidden = (int)puf_ini_get(ini, "policy", "hidden_size");
    int layers = (int)puf_ini_get(ini, "policy", "num_layers");
    int batch = (int)puf_ini_get(ini, "bc", "batch");
    int bc_seed = (int)puf_ini_get(ini, "bc", "seed");
    const char* out_path = puf_ini_get_str(ini, "bc", "output");
    const char* load_path = puf_ini_get_str(ini, "bc", "load_model_path");
    int validation_games = (int)puf_ini_get(ini, "bc", "validation_games");
    int zero_reset_source = (int)puf_ini_get(
        ini, "bc", "zero_reset_source");
    float anchor_l2 = (float)puf_ini_get(ini, "bc", "anchor_l2");
    int opening_steps = (int)puf_ini_get(ini, "bc", "opening_steps");
    float opening_weight = (float)puf_ini_get(ini, "bc", "opening_weight");
    float root_weight = (float)puf_ini_get(ini, "bc", "root_weight");
    /* Zero is an intentional conversion-only pass (for example, zeroing a
     * newly assigned observation column in a legacy checkpoint). */
    if (bc_epochs < 0) bc_epochs = 2000;
    if (bc_lr <= 0.0f) bc_lr = 0.00005f;
    if (hidden <= 0) hidden = 128;
    if (layers <= 0) layers = 2;
    if (batch <= 0) batch = 32;
    if (opening_steps <= 0) opening_steps = 26;
    if (opening_weight <= 0.0f) opening_weight = 1.0f;
    if (root_weight <= 0.0f) root_weight = opening_weight;
    bc_rng_state = (uint32_t)bc_seed * 2654435761u + 1u;
    if (!data_path || !data_path[0]) {
        fprintf(stderr, "bc.data is required for train mode\n");
        return 1;
    }
    if (!out_path || !out_path[0]) out_path = "saved/kaggriculture_bc_anchor.bin";

    FILE* fp = fopen(data_path, "rb");
    if (!fp) { perror(data_path); return 1; }
    KagBCHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1
            || header.magic != KAG_BC_MAGIC
            || header.version != KAG_BC_VERSION) {
        fprintf(stderr,
            "bad or legacy BC dataset header; regenerate with bc.mode=gen\n");
        return 1;
    }
    uint32_t count = header.count, row_obs = header.row_obs,
        row_expert = header.row_expert, row_mask = header.row_mask;
    int games = (int)header.games;
    int sequence_steps = (int)header.steps;
    if (opening_steps > sequence_steps) opening_steps = sequence_steps;
    if (games < 2 || sequence_steps < 1
            || count != (uint32_t)(games * sequence_steps)
            || row_obs != OBS_SIZE || row_expert != NUM_ATNS
            || row_mask != (KG_POLICY_ACTION_MASK_SIZE + 7) / 8) {
        fprintf(stderr, "invalid BC v2 dimensions\n");
        return 1;
    }
    if (validation_games <= 0) validation_games = games / 5;
    if (validation_games < 1) validation_games = 1;
    if (validation_games >= games) validation_games = games - 1;
    int train_games = games - validation_games;
    if (batch > train_games) batch = train_games;
    obs_t* obs = (obs_t*)malloc((size_t)count * row_obs);
    float* expert = (float*)malloc((size_t)count * row_expert * sizeof(float));
    unsigned char* mask = (unsigned char*)malloc((size_t)count * row_mask);
    if (!obs || !expert || !mask) { perror("malloc"); return 1; }
    bool read_ok = fread(obs, 1, (size_t)count * row_obs, fp)
            == (size_t)count * row_obs
        && fread(expert, sizeof(float), (size_t)count * row_expert, fp)
            == (size_t)count * row_expert
        && fread(mask, 1, (size_t)count * row_mask, fp)
            == (size_t)count * row_mask;
    fclose(fp);
    if (!read_ok) {
        fprintf(stderr, "truncated BC dataset: %s\n", data_path);
        return 1;
    }
    printf("BC train: %d games x %d steps (%d train/%d validation), "
        "batch=%d epochs=%d lr=%g hidden=%d layers=%d "
        "opening=%d weight=%g root_weight=%g init=%s\n",
        games, sequence_steps, train_games, validation_games, batch,
        bc_epochs, bc_lr, hidden, layers, opening_steps, opening_weight,
        root_weight,
        load_path && load_path[0] && strcmp(load_path, "None")
            ? load_path : "random");

    cublas_init_handle();
    cudaStream_t bc_stream;
    cudaStreamCreate(&bc_stream);
    int num_atns = NUM_ATNS;
    int mask_size = KG_POLICY_ACTION_MASK_SIZE;
    int act_n = 0;
    {
        int act_sizes[] = ACT_SIZES;
        for (int i = 0; i < num_atns; i++) act_n += act_sizes[i];
    }
    Policy policy = build_policy("kaggriculture", OBS_SIZE, hidden,
        layers, act_n, false, sequence_steps);

    Allocator params = {0}, acts = {0}, grads = {0};
    PolicyWeights weights = policy_weights_create(&policy, &params);
    int batch_rows = batch * sequence_steps;
    PolicyActivations train_acts = policy_reg_train(&policy, weights,
        &acts, &grads, batch_rows);
    IntTensor act_sizes_puf = {.shape = {num_atns}};
    alloc_register(&acts, &act_sizes_puf);
    PrecisionTensor state = {.shape = {layers, batch, hidden}};
    alloc_register(&acts, &state);
    create_allocator_or_die("params", &params);
    create_allocator_or_die("grads", &grads);
    create_allocator_or_die("acts", &acts);
    precision_t* param_puf = (precision_t*)params.mem;
    FloatTensor master_weights = {
        .data = (float*)xcuda((size_t)params.total_elems * sizeof(float)),
        .shape = {params.total_elems}};
    int act_sizes[NUM_ATNS];
    {
        int tmp[] = ACT_SIZES;
        memcpy(act_sizes, tmp, sizeof(act_sizes));
    }
    cudaMemcpy(act_sizes_puf.data, act_sizes, sizeof(act_sizes),
        cudaMemcpyHostToDevice);
    uint64_t seed = 42;
    policy_init_weights(&policy, weights, &seed, 0);
    cudaDeviceSynchronize();
    /* Seed the float master from initialized bf16 params, then optionally
     * replace it with a compatible PPO checkpoint. */
    cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0, 0>>>(
        master_weights.data, param_puf, (int)params.total_elems);
    cudaDeviceSynchronize();
    float* host_anchor = (float*)malloc(
        (size_t)params.total_elems * sizeof(float));
    if (!host_anchor) { perror("malloc"); return 1; }
    if (load_path && load_path[0] && strcmp(load_path, "None")) {
        FILE* load = fopen(load_path, "rb");
        if (!load) { perror(load_path); return 1; }
        if (fseek(load, 0, SEEK_END) != 0
                || ftell(load) != (long)(params.total_elems * sizeof(float))
                || fseek(load, 0, SEEK_SET) != 0
                || fread(host_anchor, sizeof(float),
                    (size_t)params.total_elems, load)
                    != (size_t)params.total_elems) {
            fprintf(stderr, "incompatible BC initialization checkpoint: %s\n",
                load_path);
            fclose(load);
            return 1;
        }
        fclose(load);
    } else {
        cudaMemcpy(host_anchor, master_weights.data,
            (size_t)params.total_elems * sizeof(float),
            cudaMemcpyDeviceToHost);
    }
    if (zero_reset_source) {
        for (int h = 0; h < hidden; h++) {
            host_anchor[(size_t)h * OBS_SIZE
                + KAG_OBS_RESET_SOURCE_INDEX] = 0.0f;
        }
    }
    cudaMemcpy(master_weights.data, host_anchor,
        (size_t)params.total_elems * sizeof(float), cudaMemcpyHostToDevice);
    cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0, bc_stream>>>(
        param_puf, master_weights.data, (int)params.total_elems);
    cudaStreamSynchronize(bc_stream);

    int packed_stride = (mask_size + 7) / 8;
    int A_total = act_n;
    precision_t* d_obs = (precision_t*)xcuda(
        (size_t)batch_rows * OBS_SIZE * sizeof(precision_t));
    obs_t* h_obs_chunk = (obs_t*)malloc((size_t)batch_rows * OBS_SIZE);
    float* h_expert_chunk = (float*)malloc(
        (size_t)batch_rows * NUM_ATNS * sizeof(float));
    unsigned char* h_mask_chunk = (unsigned char*)malloc(
        (size_t)batch_rows * packed_stride);
    obs_t* d_obs_raw = (obs_t*)xcuda((size_t)batch_rows * OBS_SIZE);
    float* d_expert = (float*)xcuda(
        (size_t)batch_rows * NUM_ATNS * sizeof(float));
    unsigned char* d_mask = (unsigned char*)xcuda(
        (size_t)batch_rows * packed_stride);
    float* grad_logits = (float*)xcuda(
        (size_t)batch_rows * act_n * sizeof(float));
    float* grad_value = (float*)xcuda((size_t)batch_rows * sizeof(float));
    float* stats_acc = (float*)xcuda(KAG_BC_STATS * sizeof(float));

    PrecisionTensor obs_t = {.data = d_obs,
        .shape = {batch, sequence_steps, OBS_SIZE}};
    PrecisionTensor terminals = {.data = (precision_t*)xcuda(
        (size_t)batch_rows * sizeof(precision_t)),
        .shape = {batch, sequence_steps}};

    /* Shuffle once before splitting so validation is held out by episode, then
     * reshuffle only the training prefix each epoch. */
    uint32_t* order = (uint32_t*)malloc((size_t)games * sizeof(uint32_t));
    for (int i = 0; i < games; i++) order[i] = (uint32_t)i;
    for (int i = games - 1; i > 0; i--) {
        int j = (int)(bc_rand() % (uint32_t)(i + 1));
        uint32_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    precision_t* host_grad_bf = (precision_t*)malloc(
        (size_t)params.total_elems * sizeof(precision_t));
    float* host_master = (float*)malloc(
        (size_t)params.total_elems * sizeof(float));
    float* host_mom = (float*)calloc((size_t)params.total_elems,
        sizeof(float));
    float mom = (float)puf_ini_get(ini, "bc", "momentum");
    if (mom <= 0.0f || mom >= 1.0f) mom = 0.9f;
    int report_interval = (int)puf_ini_get(ini, "bc", "report_interval");
    if (report_interval <= 0) report_interval = 25;

    auto run_chunk = [&](int order_start, int B, bool update,
            float host_stats[KAG_BC_STATS]) -> bool {
        memset(h_obs_chunk, 0, (size_t)batch_rows * OBS_SIZE);
        memset(h_mask_chunk, 0, (size_t)batch_rows * packed_stride);
        for (int row = 0; row < batch_rows; row++) {
            for (int h = 0; h < NUM_ATNS; h++) {
                h_expert_chunk[(size_t)row * NUM_ATNS + h] = -1.0f;
            }
        }
        for (int sequence = 0; sequence < B; sequence++) {
            uint32_t game = order[order_start + sequence];
            for (int t = 0; t < sequence_steps; t++) {
                size_t source = (size_t)game * sequence_steps + t;
                size_t dest = (size_t)sequence * sequence_steps + t;
                memcpy(h_obs_chunk + dest * OBS_SIZE,
                    obs + source * OBS_SIZE, OBS_SIZE);
                memcpy(h_expert_chunk + dest * NUM_ATNS,
                    expert + source * NUM_ATNS,
                    NUM_ATNS * sizeof(float));
                memcpy(h_mask_chunk + dest * packed_stride,
                    mask + source * packed_stride, packed_stride);
            }
        }
        cudaMemcpyAsync(d_obs_raw, h_obs_chunk,
            (size_t)batch_rows * OBS_SIZE, cudaMemcpyHostToDevice, bc_stream);
        cudaMemcpyAsync(d_expert, h_expert_chunk,
            (size_t)batch_rows * NUM_ATNS * sizeof(float),
            cudaMemcpyHostToDevice, bc_stream);
        cudaMemcpyAsync(d_mask, h_mask_chunk,
            (size_t)batch_rows * packed_stride,
            cudaMemcpyHostToDevice, bc_stream);
        cast<<<grid_size(batch_rows * OBS_SIZE), BLOCK_SIZE, 0, bc_stream>>>(
            d_obs, d_obs_raw, batch_rows * OBS_SIZE);
        cudaMemsetAsync(grad_logits, 0,
            (size_t)batch_rows * A_total * sizeof(float), bc_stream);
        cudaMemsetAsync(grad_value, 0,
            (size_t)batch_rows * sizeof(float), bc_stream);
        cudaMemsetAsync(stats_acc, 0,
            KAG_BC_STATS * sizeof(float), bc_stream);
        cudaMemsetAsync(state.data, 0,
            numel(state.shape) * sizeof(precision_t), bc_stream);
        cudaMemsetAsync(terminals.data, 0,
            numel(terminals.shape) * sizeof(precision_t), bc_stream);
        PrecisionTensor dec_out = policy_forward_train(&policy, weights,
            train_acts, obs_t, state, terminals, bc_stream);
        PrecisionTensor dec_flat = *puf_squeeze(&dec_out, 0);
        float valid_weight = (float)B * (root_weight
            + (opening_steps - 1) * opening_weight
            + sequence_steps - opening_steps);
        kag_bc_loss_kernel<<<grid_size(batch_rows), BLOCK_SIZE, 0,
            bc_stream>>>(dec_flat.data, d_expert, d_mask, grad_logits,
            stats_acc, act_sizes_puf.data, batch_rows, valid_weight,
            A_total, num_atns, packed_stride, sequence_steps, opening_steps,
            opening_weight, root_weight);
        if (cudaGetLastError() != cudaSuccess) return false;
        if (update) {
            FloatTensor grad_logits_t = {.data = grad_logits,
                .shape = {batch, sequence_steps, A_total}};
            FloatTensor grad_value_t = {.data = grad_value,
                .shape = {batch, sequence_steps}};
            policy_backward(&policy, weights, train_acts, grad_logits_t,
                FloatTensor(), grad_value_t, bc_stream);
        }
        if (cudaStreamSynchronize(bc_stream) != cudaSuccess) return false;
        cudaMemcpy(host_stats, stats_acc, KAG_BC_STATS * sizeof(float),
            cudaMemcpyDeviceToHost);
        if (!update) return true;

        cudaMemcpy(host_master, master_weights.data,
            (size_t)params.total_elems * sizeof(float),
            cudaMemcpyDeviceToHost);
        long grad_off = 0;
        for (int r = 0; r < params.num_regs; r++) {
            long ne = numel(params.regs[r].shape);
            if (ne > 0) {
                precision_t* gr = *(precision_t**)grads.regs[r].data_ptr;
                cudaMemcpy(host_grad_bf + grad_off, gr,
                    (size_t)ne * sizeof(precision_t),
                    cudaMemcpyDeviceToHost);
            }
            grad_off += ne;
        }
        for (long i = 0; i < params.total_elems; i++) {
            float g = __bfloat162float(host_grad_bf[i]);
            if (anchor_l2 > 0.0f) {
                g += anchor_l2 * (host_master[i] - host_anchor[i]);
            }
            host_mom[i] = mom * host_mom[i] + g;
            host_master[i] -= bc_lr * host_mom[i];
        }
        cudaMemcpyAsync(master_weights.data, host_master,
            (size_t)params.total_elems * sizeof(float),
            cudaMemcpyHostToDevice, bc_stream);
        cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0,
            bc_stream>>>(param_puf, master_weights.data,
            (int)params.total_elems);
        return cudaStreamSynchronize(bc_stream) == cudaSuccess;
    };

    for (int ep = 0; ep < bc_epochs; ep++) {
        for (int i = train_games - 1; i > 0; i--) {
            int j = (int)(bc_rand() % (uint32_t)(i + 1));
            uint32_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        float train_stats[KAG_BC_STATS] = {0};
        int train_rows = 0;
        for (int start = 0; start < train_games; start += batch) {
            int B = train_games - start < batch ? train_games - start : batch;
            float chunk[KAG_BC_STATS];
            if (!run_chunk(start, B, true, chunk)) {
                fprintf(stderr, "BC train chunk %d failed: %s\n", start,
                    cudaGetErrorString(cudaGetLastError()));
                return 1;
            }
            for (int i = 0; i < KAG_BC_STATS; i++) {
                train_stats[i] += chunk[i];
            }
            train_rows += B * sequence_steps;
        }
        if (ep == 0 || (ep + 1) % report_interval == 0
                || ep + 1 == bc_epochs) {
            float val_stats[KAG_BC_STATS] = {0};
            int val_rows = 0;
            for (int start = train_games; start < games; start += batch) {
                int B = games - start < batch ? games - start : batch;
                float chunk[KAG_BC_STATS];
                if (!run_chunk(start, B, false, chunk)) {
                    fprintf(stderr, "BC validation chunk %d failed\n", start);
                    return 1;
                }
                for (int i = 0; i < KAG_BC_STATS; i++) {
                    val_stats[i] += chunk[i];
                }
                val_rows += B * sequence_steps;
            }
            int val_open_rows = validation_games * opening_steps;
            int val_post_rows = val_rows - val_open_rows;
            printf("BC epoch %d train_loss=%.5f train_head=%.4f "
                "train_exact=%.4f val_loss=%.5f val_head=%.4f "
                "val_exact=%.4f val_open_loss=%.5f val_open_head=%.4f "
                "val_open_exact=%.4f val_root_loss=%.5f "
                "val_root_head=%.4f val_root_exact=%.4f "
                "val_post_loss=%.5f val_post_head=%.4f "
                "val_post_exact=%.4f\n", ep + 1,
                train_stats[0] / (float)train_rows,
                train_stats[1] > 0.0f
                    ? train_stats[2] / train_stats[1] : 0.0f,
                train_stats[3] / (float)train_rows,
                val_stats[0] / (float)val_rows,
                val_stats[1] > 0.0f
                    ? val_stats[2] / val_stats[1] : 0.0f,
                val_stats[3] / (float)val_rows,
                val_open_rows ? val_stats[4] / (float)val_open_rows : 0.0f,
                val_stats[5] > 0.0f ? val_stats[6] / val_stats[5] : 0.0f,
                val_open_rows ? val_stats[7] / (float)val_open_rows : 0.0f,
                val_stats[12] / (float)validation_games,
                val_stats[13] > 0.0f
                    ? val_stats[14] / val_stats[13] : 0.0f,
                val_stats[15] / (float)validation_games,
                val_post_rows ? val_stats[8] / (float)val_post_rows : 0.0f,
                val_stats[9] > 0.0f ? val_stats[10] / val_stats[9] : 0.0f,
                val_post_rows ? val_stats[11] / (float)val_post_rows : 0.0f);
        }
    }

    int64_t nbytes = numel(master_weights.shape) * sizeof(float);
    float* host = (float*)malloc((size_t)nbytes);
    cudaMemcpy(host, master_weights.data, nbytes, cudaMemcpyDeviceToHost);
    FILE* out = fopen(out_path, "wb");
    if (!out) { perror(out_path); return 1; }
    fwrite(host, 1, (size_t)nbytes, out);
    fclose(out);
    free(host);
    printf("BC anchor saved to %s (%lld bytes)\n", out_path,
        (long long)nbytes);
    free(order); free(obs); free(expert); free(mask);
    free(host_grad_bf); free(host_master); free(host_mom);
    free(h_obs_chunk); free(h_expert_chunk); free(h_mask_chunk);
    return 0;
}

int main(int argc, char** argv) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", argc - 1, argv + 1);
    const char* mode = puf_ini_get_str(&ini, "bc", "mode");
    if (mode && strcmp(mode, "gen") == 0) {
        return bc_gen(&ini);
    }
    if (mode && strcmp(mode, "gen_dagger") == 0) {
        return bc_gen_dagger(&ini);
    }
    if (mode && strcmp(mode, "train") == 0) {
        return bc_train(&ini);
    }
    fprintf(stderr, "usage: kag_bc bc.mode=gen|gen_dagger|train ...\n");
    return 1;
}
