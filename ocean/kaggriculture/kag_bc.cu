// Standalone behavioral-cloning trainer for Kaggriculture script tapes.
// Builds the same MinGRU policy as the train path, plays a script tape on the
// CPU env, and minimizes -log pi(a_expert | obs) with a pure CUDA loss kernel.
// Usage: ./kag_bc [bc.steps=N] [bc.profile=4] [bc.epochs=N] [bc.lr=X]
//        [policy.hidden_size=N] [policy.num_layers=N] [bc.output=path]

#include "kaggriculture.h"
#include "ini.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---- Minimal policy plumbing (mirrors the puffer train path) ----
#include "../../src/pufferl_preamble.h"
#include "../../src/algo.cu"

// Same semantics as the sampler: skip heads whose expert action is invalid or
// that have no legal actions. Padded rows (expert = -1) contribute nothing.
__global__ void kag_bc_loss_kernel(
        const precision_t* __restrict__ logits,   // (B, fused_cols)
        const float* __restrict__ expert,         // (B, num_atns)
        const unsigned char* __restrict__ mask,   // (B, packed) bits
        float* __restrict__ grad_logits,          // (B, A_total)
        float* __restrict__ loss_acc,             // scalar
        float* __restrict__ debug_out,            // (B) per-cell loss
        const int* __restrict__ act_sizes,
        int B, int A_total, int num_atns, int mask_stride) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B) return;
    if (debug_out) debug_out[idx] = 9999.0f;  // sentinel: kernel ran
    int logits_base = idx * (A_total + 1);
    int mask_base = idx * mask_stride;
    int offset = 0;
    float loss = 0.0f;
    int heads_done = 0;
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
        heads_done++;
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
    if (debug_out) debug_out[idx] = loss + 1000.0f * heads_done;
}

// Plain SGD on the flat master weights: wb -= lr * grad.
__global__ void kag_bc_sgd(float* __restrict__ wb,
        const precision_t* __restrict__ grad, float lr, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) wb[i] -= lr * to_float(grad[i]);
}

int main(int argc, char** argv) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", argc - 1, argv + 1);

    int bc_steps = (int)puf_ini_get(&ini, "bc", "steps");
    int bc_profile = (int)puf_ini_get(&ini, "bc", "profile");
    int bc_epochs = (int)puf_ini_get(&ini, "bc", "epochs");
    float bc_lr = (float)puf_ini_get(&ini, "bc", "learning_rate");
    int hidden = (int)puf_ini_get(&ini, "policy", "hidden_size");
    int layers = (int)puf_ini_get(&ini, "policy", "num_layers");
    const char* out_path = puf_ini_get_str(&ini, "bc", "output");
    if (bc_steps <= 0 || bc_steps > 720) bc_steps = 26;
    if (bc_epochs <= 0) bc_epochs = 2000;
    if (bc_lr <= 0.0f) bc_lr = 0.0003f;
    if (hidden <= 0) hidden = 128;
    if (layers <= 0) layers = 2;
    if (!out_path || !out_path[0]) {
        out_path = "saved/kaggriculture_bc_anchor.bin";
    }

    printf("KAG BC: steps=%d profile=%d epochs=%d lr=%g hidden=%d layers=%d\n",
        bc_steps, bc_profile, bc_epochs, bc_lr, hidden, layers);

    cublas_init_handle();
    cudaStream_t bc_stream;
    cudaStreamCreate(&bc_stream);
    cudaError_t init_err = cudaGetLastError();
    if (init_err != cudaSuccess) {
        fprintf(stderr, "cublas init failed: %s\n",
            cudaGetErrorString(init_err));
        return 1;
    }

    // Build the policy. OBS_SIZE / NUM_ATNS come from the env header.
    int input_size = OBS_SIZE;
    int num_atns = NUM_ATNS;
    int mask_size = KG_POLICY_ACTION_MASK_SIZE;
    int act_n = 0;
    {
        int act_sizes[] = ACT_SIZES;
        for (int i = 0; i < num_atns; i++) act_n += act_sizes[i];
    }
    Policy policy = build_policy("kaggriculture", input_size, hidden,
        layers, act_n, false, 1);
    fprintf(stderr, "policy: enc in=%d out=%d dec out=%d net hidden=%d layers=%d\n",
        policy.encoder.in_dim, policy.encoder.out_dim,
        policy.decoder.output_dim, policy.network.hidden,
        policy.network.num_layers);

    Allocator params = {0}, acts = {0}, grads = {0};
    PolicyWeights weights = policy_weights_create(&policy, &params);
    PolicyActivations train_acts = policy_reg_train(&policy, weights,
        &acts, &grads, bc_steps);
    IntTensor act_sizes_puf = {.shape = {num_atns}};
    alloc_register(&acts, &act_sizes_puf);
    PrecisionTensor state = {.shape = {layers, bc_steps, hidden}};
    alloc_register(&acts, &state);
    create_allocator_or_die("params", &params);
    create_allocator_or_die("grads", &grads);
    create_allocator_or_die("acts", &acts);
    cudaError_t alloc_err = cudaGetLastError();
    fprintf(stderr, "after allocs: %s\n", cudaGetErrorString(alloc_err));
    fprintf(stderr, "pools: params_elems=%ld grads_elems=%ld acts_bytes=%ld\n",
        (long)params.total_elems, (long)grads.total_elems,
        (long)acts.total_bytes);

    FloatTensor master_weights = {
        .data = (float*)params.mem, .shape = {params.total_elems}};
    PrecisionTensor grad_puf = {
        .data = (precision_t*)grads.mem, .shape = {grads.total_elems}};
    fprintf(stderr, "pools ptr: params=%p grads=%p acts=%p\n",
        params.mem, grads.mem, acts.mem);
    fprintf(stderr, "grad_puf.data=%p master.data=%p\n",
        grad_puf.data, master_weights.data);
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
    cudaError_t init_w_err = cudaGetLastError();
    if (init_w_err != cudaSuccess) {
        fprintf(stderr, "policy_init_weights failed: %s\n",
            cudaGetErrorString(init_w_err));
        return 1;
    }
    float lr = bc_lr;

    // Play the tape on a CPU env and record (obs, expert action, mask).
    Env env = {};
    env.rng = 0;
    puf_init(&env, puf_ini_section(&ini, "env", 0));
    for (int p = 0; p < KG_NUM_PLAYERS; p++) {
        env.agents[p].observations = (obs_t*)calloc(OBS_SIZE, 1);
        env.agents[p].actions = (float*)calloc(NUM_ATNS, sizeof(float));
        env.agents[p].rewards = (float*)calloc(1, sizeof(float));
        env.agents[p].terminals = (float*)calloc(1, sizeof(float));
        env.agents[p].action_mask =
            (unsigned char*)calloc(mask_size, 1);
    }
    puf_reset(&env);

    int packed_stride = (mask_size + 7) / 8;
    obs_t* host_obs = (obs_t*)calloc((size_t)bc_steps * OBS_SIZE, 1);
    float* host_expert = (float*)calloc(
        (size_t)bc_steps * NUM_ATNS, sizeof(float));
    unsigned char* host_mask = (unsigned char*)calloc(
        (size_t)bc_steps * packed_stride, 1);
    for (int t = 0; t < bc_steps && !env.game_storage.done; t++) {
        KGAction expert = {};
        kag_script_action_from_tapes(&env.game_storage, 0, bc_profile,
            &expert, kag_script_tapes);
        kag_script_repair(&env.game_storage, 0, bc_profile, &expert);
        kag_clear_policy_actions(&env.agents[0]);
        kag_set_policy_unit(&env.agents[0], 0,
            expert.farmer.op, expert.farmer.arg, expert.farmer.n);
        for (int h = 0; h < expert.hand_count && h < KG_POLICY_DIRECT_HANDS; h++) {
            kag_set_policy_unit(&env.agents[0], h + 1,
                expert.hands[h].op, expert.hands[h].arg, expert.hands[h].n);
        }
        for (int o = 0; o < expert.market_count && o < KG_POLICY_MARKET_SLOTS; o++) {
            kag_set_policy_market(&env.agents[0], o,
                expert.market[o].op, expert.market[o].item, expert.market[o].n);
        }
        memcpy(host_obs + (size_t)t * OBS_SIZE,
            env.agents[0].observations, OBS_SIZE);
        memcpy(host_expert + (size_t)t * NUM_ATNS,
            env.agents[0].actions, NUM_ATNS * sizeof(float));
        for (int byte = 0; byte < packed_stride; byte++) {
            unsigned char bits = 0;
            for (int bit = 0; bit < 8; bit++) {
                int action = byte * 8 + bit;
                if (action < mask_size
                        && env.agents[0].action_mask[action]) {
                    bits |= (unsigned char)(1u << bit);
                }
            }
            host_mask[(size_t)t * packed_stride + byte] = bits;
        }
        KGAction actions[KG_NUM_PLAYERS] = {expert, {}};
        actions[1].farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
        actions[1].hand_count = env.game_storage.players[1].hand_count;
        for (int h = 0; h < actions[1].hand_count; h++) {
            actions[1].hands[h] = (KGUnitAction){KG_OP_PASS, -1, 1};
        }
        kg_step(&env.game_storage, actions);
        kag_write_all_observations_from_tapes(&env, kag_script_tapes);
    }
    int valid_cells = 0, mask_bits = 0;
    for (int t = 0; t < bc_steps; t++) {
        int valid = 1;
        for (int h = 0; h < num_atns; h++) {
            if ((int)host_expert[(size_t)t * NUM_ATNS + h] < 0) valid = 0;
        }
        valid_cells += valid;
        for (int byte = 0; byte < packed_stride; byte++) {
            mask_bits += __builtin_popcount(
                host_mask[(size_t)t * packed_stride + byte]);
        }
    }
    fprintf(stderr, "BC data: %d/%d valid cells, %d mask bits, head0=%g\n",
        valid_cells, bc_steps, mask_bits,
        bc_steps > 0 ? host_expert[0] : -1.0f);

    // Upload.
    precision_t* d_obs = (precision_t*)xcuda(
        (size_t)bc_steps * OBS_SIZE * sizeof(precision_t));
    float* d_expert = (float*)xcuda(
        (size_t)bc_steps * NUM_ATNS * sizeof(float));
    unsigned char* d_mask = (unsigned char*)xcuda(
        (size_t)bc_steps * packed_stride);
    obs_t* d_obs_raw = (obs_t*)xcuda((size_t)bc_steps * OBS_SIZE);
    float* grad_logits = (float*)xcuda(
        (size_t)bc_steps * act_n * sizeof(float));
    float* grad_value = (float*)xcuda((size_t)bc_steps * sizeof(float));
    float* loss_acc = (float*)xcuda(sizeof(float));
    float* debug_out = (float*)xcuda((size_t)bc_steps * sizeof(float));
    cudaMemcpy(d_obs_raw, host_obs, (size_t)bc_steps * OBS_SIZE,
        cudaMemcpyHostToDevice);
    cudaDeviceSynchronize();
    cast<<<grid_size(bc_steps * OBS_SIZE), BLOCK_SIZE, 0, bc_stream>>>(
        d_obs, d_obs_raw, bc_steps * OBS_SIZE);
    cudaStreamSynchronize(bc_stream);
    init_w_err = cudaGetLastError();
    if (init_w_err != cudaSuccess) {
        fprintf(stderr, "obs cast failed: %s\n",
            cudaGetErrorString(init_w_err));
        return 1;
    }
    cudaMemcpy(d_expert, host_expert,
        (size_t)bc_steps * NUM_ATNS * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_mask, host_mask, (size_t)bc_steps * packed_stride,
        cudaMemcpyHostToDevice);
    cudaError_t up_err = cudaGetLastError();
    fprintf(stderr, "after upload: %s\n", cudaGetErrorString(up_err));

    PrecisionTensor obs_t = {.data = d_obs,
        .shape = {bc_steps, 1, OBS_SIZE}};
    PrecisionTensor terminals = {.data = (precision_t*)xcuda(
        (size_t)bc_steps * sizeof(precision_t)), .shape = {bc_steps}};
    int A_total = act_n;
    int mask_stride = packed_stride;

    for (int ep = 0; ep < bc_epochs; ep++) {
        cudaMemsetAsync(grad_logits, 0,
            (size_t)bc_steps * A_total * sizeof(float), bc_stream);
        cudaMemsetAsync(loss_acc, 0, sizeof(float), bc_stream);
        cudaMemsetAsync(state.data, 0,
            numel(state.shape) * sizeof(precision_t), bc_stream);
        PrecisionTensor enc_in = obs_t;
        puf_squeeze(&enc_in, 0);
        PrecisionTensor enc_out = policy.encoder.forward(weights.encoder,
            train_acts.encoder, enc_in, bc_stream);
        cudaError_t e1 = cudaGetLastError();
        if (e1 != cudaSuccess) {
            fprintf(stderr, "encoder forward failed: %s\n",
                cudaGetErrorString(e1));
            return 1;
        }
        PrecisionTensor net_in = enc_out;
        puf_unsqueeze(&net_in, 0, bc_steps, 1);
        PrecisionTensor net_out = policy.network.forward_train(weights.network,
            net_in, state, terminals, train_acts.network, bc_stream);
        cudaError_t e2 = cudaGetLastError();
        if (e2 != cudaSuccess) {
            fprintf(stderr, "network forward failed: %s\n",
                cudaGetErrorString(e2));
            return 1;
        }
        PrecisionTensor dec_in = net_out;
        puf_squeeze(&dec_in, 0);
        PrecisionTensor dec_out = policy.decoder.forward(weights.decoder,
            train_acts.decoder, dec_in, bc_stream);
        cudaError_t e3 = cudaGetLastError();
        if (e3 != cudaSuccess) {
            fprintf(stderr, "decoder forward failed: %s\n",
                cudaGetErrorString(e3));
            return 1;
        }
        PrecisionTensor dec_flat = dec_out;
        if (ep == 0) {
            int dev_sizes[4];
            cudaMemcpy(dev_sizes, act_sizes_puf.data, 4 * sizeof(int),
                cudaMemcpyDeviceToHost);
            unsigned char dev_mask[8];
            cudaMemcpy(dev_mask, d_mask, 8, cudaMemcpyDeviceToHost);
            float dev_logits[4];
            cudaMemcpy(dev_logits, dec_flat.data, 4 * sizeof(float),
                cudaMemcpyDeviceToHost);
            fprintf(stderr, "BC dev: sizes=%d,%d,%d,%d mask=%02x,%02x,%02x "
                "logits=%g,%g,%g,%g\n",
                dev_sizes[0], dev_sizes[1], dev_sizes[2], dev_sizes[3],
                dev_mask[0], dev_mask[1], dev_mask[2], dev_logits[0],
                dev_logits[1], dev_logits[2], dev_logits[3]);
        }
        cudaError_t fwd_err = cudaGetLastError();
        if (fwd_err != cudaSuccess) {
            fprintf(stderr, "forward failed: %s\n",
                cudaGetErrorString(fwd_err));
            return 1;
        }
        if (ep == 0) {
            cudaPointerAttributes da = {};
            cudaPointerGetAttributes(&da, dec_flat.data);
            fprintf(stderr, "dec_flat: data=%p type=%d dev=%d\n",
                dec_flat.data, (int)da.type, da.device);
            cudaPointerAttributes oa = {};
            cudaPointerGetAttributes(&oa, obs_t.data);
            fprintf(stderr, "obs_t: data=%p type=%d dev=%d\n",
                obs_t.data, (int)oa.type, oa.device);
        }
        kag_bc_loss_kernel<<<1, 256, 0, bc_stream>>>(
            dec_flat.data, d_expert, d_mask, grad_logits, loss_acc,
            debug_out, act_sizes_puf.data, bc_steps, A_total, num_atns,
            mask_stride);
        cudaError_t err = cudaGetLastError();
        if (err != cudaSuccess) {
            fprintf(stderr, "BC kernel launch failed: %s\n",
                cudaGetErrorString(err));
            return 1;
        }
        FloatTensor grad_logits_t = {.data = grad_logits,
            .shape = {bc_steps, 1, A_total}};
        FloatTensor grad_value_t = {.data = grad_value,
            .shape = {bc_steps, 1}};
        policy_backward(&policy, weights, train_acts, grad_logits_t,
            FloatTensor(), grad_value_t, bc_stream);
        kag_bc_sgd<<<grid_size(params.total_elems), BLOCK_SIZE, 0,
            bc_stream>>>(master_weights.data, grad_puf.data, lr,
            (int)params.total_elems);
        cudaDeviceSynchronize();
        if ((ep + 1) % 200 == 0) {
            float loss = 0.0f;
            cudaMemcpy(&loss, loss_acc, sizeof(float), cudaMemcpyDeviceToHost);
            float dbg[4];
            cudaMemcpy(dbg, debug_out, 4 * sizeof(float),
                cudaMemcpyDeviceToHost);
            printf("BC epoch %d loss=%.4f dbg=%g,%g,%g,%g\n", ep + 1,
                loss / bc_steps, dbg[0], dbg[1], dbg[2], dbg[3]);
        }
    }

    // Save master weights in the same flat float format as the train path.
    int64_t nbytes = numel(master_weights.shape) * sizeof(float);
    float* host = (float*)malloc((size_t)nbytes);
    cudaMemcpy(host, master_weights.data, nbytes, cudaMemcpyDeviceToHost);
    FILE* fp = fopen(out_path, "wb");
    if (!fp) { perror(out_path); return 1; }
    fwrite(host, 1, (size_t)nbytes, fp);
    fclose(fp);
    free(host);
    printf("BC anchor saved to %s (%lld bytes)\n", out_path,
        (long long)nbytes);
    return 0;
}
