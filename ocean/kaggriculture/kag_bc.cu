// Standalone behavioral-cloning trainer for Kaggriculture.
// Two modes:
//   gen   - run bc.games seeded games where both players use a strong bot
//           (bc.bot: 0=rules econ, 1=top hybrid, 2=adaptive structured,
//           3=adaptive harvest-pulse, 4=frontier tape) and write every step's
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
#define KAG_BC_VERSION 1u

// Cross-entropy on the expert action heads; invalid/padded rows contribute 0.
__global__ void kag_bc_loss_kernel(
        const precision_t* __restrict__ logits,   // (B, fused_cols)
        const float* __restrict__ expert,         // (B, num_atns)
        const unsigned char* __restrict__ mask,   // (B, packed) bits
        float* __restrict__ grad_logits,          // (B, A_total)
        float* __restrict__ loss_acc,             // scalar
        const int* __restrict__ act_sizes,
        int B, int A_total, int num_atns, int mask_stride) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B) return;
    int logits_base = idx * (A_total + 1);
    int mask_base = idx * mask_stride;
    int offset = 0;
    float loss = 0.0f;
    for (int h = 0; h < num_atns; h++) {
        int A = act_sizes[h];
        int expert_action = (int)expert[idx * num_atns + h];
        if (expert_action < 0 || expert_action >= A) {
            offset += A;
            continue;
        }
        float max_val = -INFINITY;
        for (int a = 0; a < A; a++) {
            if (puf_mask_bit(mask, mask_base, offset + a)) {
                float l = to_float(logits[logits_base + offset + a]);
                max_val = fmaxf(max_val, l);
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
        for (int a = 0; a < A; a++) {
            if (!puf_mask_bit(mask, mask_base, offset + a)) continue;
            float p = __expf(
                to_float(logits[logits_base + offset + a]) - logsumexp);
            grad_logits[idx * A_total + offset + a] =
                (a == expert_action) ? (p - 1.0f) : p;
        }
        offset += A;
    }
    if (loss_acc) atomicAdd(loss_acc, loss);
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

// Pick the bot action for a player. profile 1 = top hybrid (tape opening then
// the rules economy continuation, matching the bench "top" opponent).
static void bc_bot_action(const KGState* game, int player, int profile,
        KGAction* action) {
    if (profile == 1 && game->step < 26) {
        kag_script_action(game, player, KG_SCRIPT_TOP, action);
        kag_script_repair(game, player, KG_SCRIPT_TOP, action);
        return;
    }
    if (profile == 1) {
        kag_bot_action(game, player, -1, action);
        return;
    }
    if (profile == 2) {
        kag_adaptive_action(game, player, KAG_ADAPTIVE_STRUCTURED, action);
        return;
    }
    if (profile == 3) {
        kag_adaptive_action(game, player, KAG_ADAPTIVE_HARVEST_PULSE, action);
        return;
    }
    if (profile == 4) {
        kag_script_action(game, player, KG_SCRIPT_FRONTIER, action);
        kag_script_repair(game, player, KG_SCRIPT_FRONTIER, action);
        return;
    }
    kag_bot_action(game, player, -1, action);
}

// Encode a KGAction into policy action-heads on the agent.
static void bc_set_expert(Env* env, const KGAction* expert) {
    kag_clear_policy_actions(&env->agents[0]);
    kag_set_policy_unit(&env->agents[0], 0,
        expert->farmer.op, expert->farmer.arg, expert->farmer.n);
    for (int h = 0; h < expert->hand_count && h < KG_POLICY_DIRECT_HANDS; h++) {
        kag_set_policy_unit(&env->agents[0], h + 1,
            expert->hands[h].op, expert->hands[h].arg, expert->hands[h].n);
    }
    for (int o = 0; o < expert->market_count && o < KG_POLICY_MARKET_SLOTS; o++) {
        kag_set_policy_market(&env->agents[0], o,
            expert->market[o].op, expert->market[o].item, expert->market[o].n);
    }
}

static void bc_pack_mask(Env* env, unsigned char* out, int mask_size,
        int packed_stride) {
    for (int byte = 0; byte < packed_stride; byte++) {
        unsigned char bits = 0;
        for (int bit = 0; bit < 8; bit++) {
            int action = byte * 8 + bit;
            if (action < mask_size && env->agents[0].action_mask[action]) {
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
    int profile = (int)puf_ini_get(ini, "bc", "bot");
    int bc_seed = (int)puf_ini_get(ini, "bc", "seed");
    const char* data_path = puf_ini_get_str(ini, "bc", "data");
    if (games <= 0) games = 50;
    if (!data_path || !data_path[0]) data_path = "saved/kaggriculture_bc_data.bin";
    bc_rng_state = (uint32_t)bc_seed * 2654435761u + 1u;

    int mask_size = KG_POLICY_ACTION_MASK_SIZE;
    int packed_stride = (mask_size + 7) / 8;
    size_t row_obs = OBS_SIZE;
    size_t row_expert = NUM_ATNS;
    size_t row_mask = packed_stride;
    size_t cap = (size_t)games * 720;
    obs_t* obs = (obs_t*)malloc(cap * row_obs);
    float* expert = (float*)malloc(cap * row_expert * sizeof(float));
    unsigned char* mask = (unsigned char*)malloc(cap * row_mask);
    if (!obs || !expert || !mask) { perror("malloc"); return 1; }

    size_t count = 0;
    for (int g = 0; g < games; g++) {
        Env env = bc_make_env(ini, (uint32_t)g);
        int steps = 0;
        while (!env.game_storage.done && steps < 720) {
            KGAction a0 = {}, a1 = {};
            bc_bot_action(&env.game_storage, 0, profile, &a0);
            bc_bot_action(&env.game_storage, 1, profile, &a1);
            bc_set_expert(&env, &a0);
            memcpy(obs + count * row_obs, env.agents[0].observations, row_obs);
            memcpy(expert + count * row_expert, env.agents[0].actions,
                row_expert * sizeof(float));
            bc_pack_mask(&env, mask + count * row_mask, mask_size,
                packed_stride);
            count++;
            KGAction pair[KG_NUM_PLAYERS] = {a0, a1};
            kg_step(&env.game_storage, pair);
            kag_write_all_observations_from_tapes(&env, kag_script_tapes);
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
    uint32_t header[6] = {
        KAG_BC_MAGIC, KAG_BC_VERSION, (uint32_t)count,
        (uint32_t)row_obs, (uint32_t)row_expert, (uint32_t)row_mask,
    };
    fwrite(header, sizeof(uint32_t), 6, fp);
    fwrite(obs, 1, count * row_obs, fp);
    fwrite(expert, sizeof(float), count * row_expert, fp);
    fwrite(mask, 1, count * row_mask, fp);
    fclose(fp);
    printf("BC dataset written: %s (%zu steps, %.1f MB)\n", data_path,
        count, (double)(count * (row_obs + row_expert * 4 + row_mask))
            / 1048576.0);
    free(obs); free(expert); free(mask);
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
    if (bc_epochs <= 0) bc_epochs = 2000;
    if (bc_lr <= 0.0f) bc_lr = 0.00005f;
    if (hidden <= 0) hidden = 128;
    if (layers <= 0) layers = 2;
    if (batch <= 0) batch = 128;
    bc_rng_state = (uint32_t)bc_seed * 2654435761u + 1u;
    if (!data_path || !data_path[0]) {
        fprintf(stderr, "bc.data is required for train mode\n");
        return 1;
    }
    if (!out_path || !out_path[0]) out_path = "saved/kaggriculture_bc_anchor.bin";

    FILE* fp = fopen(data_path, "rb");
    if (!fp) { perror(data_path); return 1; }
    uint32_t header[6];
    if (fread(header, sizeof(uint32_t), 6, fp) != 6
            || header[0] != KAG_BC_MAGIC || header[1] != KAG_BC_VERSION) {
        fprintf(stderr, "bad dataset header\n");
        return 1;
    }
    uint32_t count = header[2], row_obs = header[3],
        row_expert = header[4], row_mask = header[5];
    obs_t* obs = (obs_t*)malloc((size_t)count * row_obs);
    float* expert = (float*)malloc((size_t)count * row_expert * sizeof(float));
    unsigned char* mask = (unsigned char*)malloc((size_t)count * row_mask);
    if (!obs || !expert || !mask) { perror("malloc"); return 1; }
    fread(obs, 1, (size_t)count * row_obs, fp);
    fread(expert, sizeof(float), (size_t)count * row_expert, fp);
    fread(mask, 1, (size_t)count * row_mask, fp);
    fclose(fp);
    printf("BC train: %u steps, batch=%d epochs=%d lr=%g hidden=%d layers=%d\n",
        count, batch, bc_epochs, bc_lr, hidden, layers);

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
        layers, act_n, false, 1);

    Allocator params = {0}, acts = {0}, grads = {0};
    PolicyWeights weights = policy_weights_create(&policy, &params);
    PolicyActivations train_acts = policy_reg_train(&policy, weights,
        &acts, &grads, batch);
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
    int act_sizes[42];
    {
        int tmp[] = ACT_SIZES;
        memcpy(act_sizes, tmp, sizeof(act_sizes));
    }
    cudaMemcpy(act_sizes_puf.data, act_sizes, sizeof(act_sizes),
        cudaMemcpyHostToDevice);
    uint64_t seed = 42;
    policy_init_weights(&policy, weights, &seed, 0);
    cudaDeviceSynchronize();
    // Seed the float master from the initialized bf16 params, then keep the
    // master as the trainable copy (bf16 params are refreshed each iteration).
    cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0, 0>>>(
        master_weights.data, param_puf, (int)params.total_elems);
    cudaDeviceSynchronize();

    int packed_stride = (mask_size + 7) / 8;
    int A_total = act_n;
    precision_t* d_obs = (precision_t*)xcuda(
        (size_t)batch * OBS_SIZE * sizeof(precision_t));
    obs_t* h_obs_chunk = (obs_t*)malloc((size_t)batch * OBS_SIZE);
    float* h_expert_chunk = (float*)malloc(
        (size_t)batch * NUM_ATNS * sizeof(float));
    unsigned char* h_mask_chunk = (unsigned char*)malloc(
        (size_t)batch * packed_stride);
    obs_t* d_obs_raw = (obs_t*)xcuda((size_t)batch * OBS_SIZE);
    float* d_expert = (float*)xcuda(
        (size_t)batch * NUM_ATNS * sizeof(float));
    unsigned char* d_mask = (unsigned char*)xcuda(
        (size_t)batch * packed_stride);
    float* grad_logits = (float*)xcuda(
        (size_t)batch * act_n * sizeof(float));
    float* grad_value = (float*)xcuda((size_t)batch * sizeof(float));
    float* loss_acc = (float*)xcuda(sizeof(float));

    PrecisionTensor obs_t = {.data = d_obs,
        .shape = {batch, 1, OBS_SIZE}};
    PrecisionTensor terminals = {.data = (precision_t*)xcuda(
        (size_t)batch * sizeof(precision_t)), .shape = {batch}};

    // Index shuffle for dataset order variation per epoch.
    uint32_t* order = (uint32_t*)malloc((size_t)count * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) order[i] = i;
    precision_t* host_grad_bf = (precision_t*)malloc(
        (size_t)params.total_elems * sizeof(precision_t));
    float* host_master = (float*)malloc(
        (size_t)params.total_elems * sizeof(float));

    for (int ep = 0; ep < bc_epochs; ep++) {
        // Fisher-Yates shuffle.
        for (uint32_t i = count - 1; i > 0; i--) {
            uint32_t j = (uint32_t)(bc_rand() % (i + 1));
            uint32_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        float epoch_loss = 0.0f;
        int epoch_steps = 0;
        for (uint32_t start = 0; start < count; start += (uint32_t)batch) {
            int B = (int)((count - start) < (uint32_t)batch
                ? (count - start) : (uint32_t)batch);
            // Fill a padded batch (invalid expert = -1 on the tail).
            for (int i = 0; i < B; i++) {
                uint32_t row = order[start + i];
                memcpy(h_obs_chunk + (size_t)i * OBS_SIZE,
                    obs + (size_t)row * OBS_SIZE, OBS_SIZE);
                memcpy(h_expert_chunk + (size_t)i * NUM_ATNS,
                    expert + (size_t)row * NUM_ATNS,
                    NUM_ATNS * sizeof(float));
                memcpy(h_mask_chunk + (size_t)i * packed_stride,
                    mask + (size_t)row * packed_stride, packed_stride);
            }
            for (int i = B; i < batch; i++) {
                for (int h = 0; h < NUM_ATNS; h++) {
                    h_expert_chunk[(size_t)i * NUM_ATNS + h] = -1.0f;
                }
            }
            cudaMemcpy(d_obs_raw, h_obs_chunk, (size_t)batch * OBS_SIZE,
                cudaMemcpyHostToDevice);
            cudaMemcpy(d_expert, h_expert_chunk,
                (size_t)batch * NUM_ATNS * sizeof(float),
                cudaMemcpyHostToDevice);
            cudaMemcpy(d_mask, h_mask_chunk, (size_t)batch * packed_stride,
                cudaMemcpyHostToDevice);
            cast<<<grid_size(batch * OBS_SIZE), BLOCK_SIZE, 0, bc_stream>>>(
                d_obs, d_obs_raw, batch * OBS_SIZE);
            cudaMemsetAsync(grad_logits, 0,
                (size_t)batch * A_total * sizeof(float), bc_stream);
            cudaMemsetAsync(loss_acc, 0, sizeof(float), bc_stream);
            cudaMemsetAsync(state.data, 0,
                numel(state.shape) * sizeof(precision_t), bc_stream);
            PrecisionTensor dec_out = policy_forward_train(&policy, weights,
                train_acts, obs_t, state, terminals, bc_stream);
            PrecisionTensor dec_flat = *puf_squeeze(&dec_out, 0);
            if (start == 0) {
                DecoderActivations* da = (DecoderActivations*)train_acts.decoder;
                float sv[4];
                cudaMemcpy(sv, da->saved_input.data, 4 * sizeof(float),
                    cudaMemcpyDeviceToHost);
                fprintf(stderr, "decoder saved_input[0..3]=%g,%g,%g,%g\n",
                    sv[0], sv[1], sv[2], sv[3]);
                EncoderActivations* ea = (EncoderActivations*)train_acts.encoder;
                cudaMemcpy(sv, ea->saved_input.data, 4 * sizeof(float),
                    cudaMemcpyDeviceToHost);
                fprintf(stderr, "encoder saved_input[0..3]=%g,%g,%g,%g\n",
                    sv[0], sv[1], sv[2], sv[3]);
            }
            kag_bc_loss_kernel<<<1, 256, 0, bc_stream>>>(
                dec_flat.data, d_expert, d_mask, grad_logits, loss_acc,
                act_sizes_puf.data, batch, A_total, num_atns, packed_stride);
            if (start == 0) {
                float gl[6];
                cudaMemcpy(gl, grad_logits, 6 * sizeof(float),
                    cudaMemcpyDeviceToHost);
                fprintf(stderr, "grad_logits[0..5]=%g,%g,%g,%g,%g,%g\n",
                    gl[0], gl[1], gl[2], gl[3], gl[4], gl[5]);
            }
            cudaError_t ker_err = cudaGetLastError();
            if (ker_err != cudaSuccess) {
                fprintf(stderr, "chunk %u kernel launch: %s\n", start,
                    cudaGetErrorString(ker_err));
                return 1;
            }
            FloatTensor grad_logits_t = {.data = grad_logits,
                .shape = {batch, 1, A_total}};
            FloatTensor grad_value_t = {.data = grad_value,
                .shape = {batch, 1}};
            policy_backward(&policy, weights, train_acts, grad_logits_t,
                FloatTensor(), grad_value_t, bc_stream);
            cudaDeviceSynchronize();
            if (start == 0) {
                DecoderActivations* da = (DecoderActivations*)train_acts.decoder;
                EncoderActivations* ea = (EncoderActivations*)train_acts.encoder;
                float go[4];
                cudaMemcpy(go, ea->wgrad_scratch.data, 4 * sizeof(float),
                    cudaMemcpyDeviceToHost);
                fprintf(stderr, "enc wgrad ptr=%p val=%g,%g,%g,%g\n",
                    (void*)ea->wgrad_scratch.data,
                    go[0], go[1], go[2], go[3]);
                fprintf(stderr, "grads reg0 ptr=%p\n",
                    (void*)(*(precision_t**)grads.regs[0].data_ptr));
            }
            cudaError_t sync_err = cudaGetLastError();
            if (sync_err != cudaSuccess) {
                fprintf(stderr, "chunk %u sync: %s\n", start,
                    cudaGetErrorString(sync_err));
                return 1;
            }
            float chunk_loss = 0.0f;
            cudaMemcpy(&chunk_loss, loss_acc, sizeof(float),
                cudaMemcpyDeviceToHost);
            epoch_loss += chunk_loss;
            epoch_steps += B;

            // Host-side SGD on the float master using per-reg bf16 grads.
            cudaMemcpy(host_master, master_weights.data,
                (size_t)params.total_elems * sizeof(float),
                cudaMemcpyDeviceToHost);
            long grad_off = 0;
            for (int r = 0; r < params.num_regs; r++) {
                long ne = numel(params.regs[r].shape);
                if (ne > 0) {
                    precision_t* gr = *(precision_t**)grads.regs[r].data_ptr;
                    if (start == 0 && r < 4) {
                        float gv[4];
                        cudaMemcpy(gv, gr, 4 * sizeof(float),
                            cudaMemcpyDeviceToHost);
                        fprintf(stderr, "grads reg%d ne=%ld first=%g,%g,%g,%g\n",
                            r, ne, gv[0], gv[1], gv[2], gv[3]);
                    }
                    cudaMemcpy(host_grad_bf + grad_off, gr,
                        (size_t)ne * sizeof(precision_t),
                        cudaMemcpyDeviceToHost);
                }
                grad_off += ne;
            }
            for (long i = 0; i < params.total_elems; i++) {
                host_master[i] -= bc_lr * __bfloat162float(host_grad_bf[i]);
            }
            cudaMemcpy(master_weights.data, host_master,
                (size_t)params.total_elems * sizeof(float),
                cudaMemcpyHostToDevice);
            // Refresh bf16 inference params from the updated float master.
            cast<<<grid_size((int)params.total_elems), BLOCK_SIZE, 0,
                bc_stream>>>(param_puf, master_weights.data,
                (int)params.total_elems);
            cudaDeviceSynchronize();
        }
        if ((ep + 1) % 50 == 0) {
            printf("BC epoch %d loss=%.4f\n", ep + 1,
                epoch_loss / (epoch_steps > 0 ? epoch_steps : 1));
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
    free(host_grad_bf); free(host_master);
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
    if (mode && strcmp(mode, "train") == 0) {
        return bc_train(&ini);
    }
    fprintf(stderr, "usage: kag_bc bc.mode=gen|train ...\n");
    return 1;
}
