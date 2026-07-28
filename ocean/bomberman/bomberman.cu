// GPU Bomberman: one CUDA thread owns one match; agents pack into flat tensors.
#ifndef PUFFER_BOMBERMAN_GPU_CU
#define PUFFER_BOMBERMAN_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "bomberman.cu requires -DPUFFER_GPU_ENV (build with --gpu)"
#endif

#include "bm_sim.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef BM_CUDA_BLOCK
#define BM_CUDA_BLOCK 256
#endif

static BMConfig h_bcfg;
__constant__ BMConfig d_bcfg;

// Match pool: indexed by match id = agent_base / num_agents
static BMMatch* d_matches = nullptr;
static int g_num_matches = 0;
static int g_num_agents = 2;
static int g_total_agents = 0;

static inline int bm_grid(int n) {
    return (n + BM_CUDA_BLOCK - 1) / BM_CUDA_BLOCK;
}

static void bm_host_load_config(BMConfig* cfg, Dict* kwargs) {
    *cfg = bm_default_config();
    cfg->width = (int)dict_get(kwargs, "width");
    cfg->height = (int)dict_get(kwargs, "height");
    cfg->num_agents = (int)dict_get(kwargs, "num_agents");
    cfg->max_ticks = (int)dict_get(kwargs, "max_ticks");
    cfg->bomb_timer = (int)dict_get(kwargs, "bomb_timer");
    cfg->flame_duration = (int)dict_get(kwargs, "flame_duration");
    cfg->frames_per_cell = (int)dict_get(kwargs, "frames_per_cell");
    cfg->soft_density = (float)dict_get(kwargs, "soft_density");
    cfg->item_chance = (float)dict_get(kwargs, "item_chance");
    cfg->reward_soft = (float)dict_get(kwargs, "reward_soft");
    cfg->reward_kill = (float)dict_get(kwargs, "reward_kill");
    cfg->reward_death = (float)dict_get(kwargs, "reward_death");
    cfg->reward_self_kill = (float)dict_get(kwargs, "reward_self_kill");
    cfg->reward_win = (float)dict_get(kwargs, "reward_win");
    cfg->reward_alive = (float)dict_get(kwargs, "reward_alive");
    cfg->reward_timeout = (float)dict_get(kwargs, "reward_timeout");
    cfg->reward_bomb_threat = (float)dict_get(kwargs, "reward_bomb_threat");
    cfg->reward_bomb_escape = (float)dict_get(kwargs, "reward_bomb_escape");
    cfg->reward_curriculum_aim = (float)dict_get(kwargs, "reward_curriculum_aim");
    cfg->reward_curriculum_escape = (float)dict_get(kwargs, "reward_curriculum_escape");
    cfg->reward_curriculum_progress = (float)dict_get(kwargs, "reward_curriculum_progress");
    cfg->reverse_curriculum = (int)dict_get(kwargs, "reverse_curriculum");
    cfg->curriculum_steps = (int)dict_get(kwargs, "curriculum_steps");
    cfg->curriculum_window = (int)dict_get(kwargs, "curriculum_window");
    cfg->curriculum_success_rate = (float)dict_get(kwargs, "curriculum_success_rate");
    cfg->pillar_mode = (int)dict_get(kwargs, "pillar_mode");
    if (cfg->num_agents < 2) cfg->num_agents = 2;
    if (cfg->num_agents > BM_MAX_AGENTS) cfg->num_agents = BM_MAX_AGENTS;
}

__device__ void bm_gpu_fold_logs(Env* envs, BMMatch* match, int match_id, int num_agents) {
    // Pack onto agent-0's log shell only would under-count n for reduce; fold
    // into every agent index (same as CPU: +1 n per agent per episode) and
    // put match scores only on the first agent of the match so they aren't
    // multiplied by num_agents twice. Actually robocode puts scores once on
    // env->log then n += num_agents. Here each Env is one agent: put full
    // match accounting on agent 0, and only n/stats on others? Simpler: put
    // everything on agent 0 only with n += num_agents.
    int gi0 = match_id * num_agents;
    Log* log = &envs[gi0].log;
    int outcome = (match->winner == 0) ? 1 : (match->winner > 0) ? -1 : 0;
    float s0 = (outcome > 0) ? 1.0f : (outcome < 0) ? 0.0f : 0.5f;
    float na = (float)num_agents;
    log->slot_0_score += s0 * na;
    log->slot_1_score += (1.0f - s0) * na;
    if (outcome == 0) log->draw_rate += na;
    log->perf += s0 * na;
    log->slot_0_kills += (float)match->agents[0].kills * na;
    log->slot_0_self_kills += (float)match->agents[0].self_kills * na;
    int opponent_suicides = 0;
    for (int a = 1; a < num_agents; a++) {
        opponent_suicides += match->agents[a].self_kills;
    }
    log->slot_0_opponent_suicides += (float)opponent_suicides * na;
    log->curriculum_stage += (float)(match->curriculum_stage < 0 ? 4 : match->curriculum_stage) * na;
    log->curriculum_full_game += (match->curriculum_stage < 0 ? 1.0f : 0.0f) * na;

    int draw = (outcome == 0) ? 1 : 0;
    for (int a = 0; a < num_agents; a++) {
        BMAgent* ag = &match->agents[a];
        int win = (match->winner == a) ? 1 : 0;
        log->score += ag->ep_score;
        log->episode_return += ag->ep_return;
        log->episode_length += (float)match->tick;
        log->kills += (float)ag->kills;
        log->self_kills += (float)ag->self_kills;
        log->soft_breaks += (float)ag->soft_breaks;
        log->wins += (float)win;
        log->draws += (float)draw;
        log->deaths += ag->alive ? 0.0f : 1.0f;
        log->n += 1.0f;
    }
}

__global__ void bm_reset_kernel(Env* envs, BMMatch* matches, obs_t* observations,
        float* rewards, float* terminals, int num_matches, int num_agents) {
    int mid = blockIdx.x * blockDim.x + threadIdx.x;
    if (mid >= num_matches) return;

    BMMatch* m = &matches[mid];
    uint32_t seed = (uint32_t)(mid + 1) * 0x9E3779B9u;
    bm_reset_match(m, &d_bcfg, seed);

    for (int a = 0; a < num_agents; a++) {
        int gi = mid * num_agents + a;
        rewards[gi] = 0.0f;
        terminals[gi] = 0.0f;
        bm_write_obs(m, &d_bcfg, a,
            observations + (size_t)gi * OBS_SIZE);
    }
}

__global__ void bm_step_kernel(Env* envs, BMMatch* matches,
        const float* actions, obs_t* observations,
        float* rewards, float* terminals,
        int start_agent, int agent_count, int num_matches, int num_agents) {
    // Map agent-range to match-range. Full-rollout path uses start=0,count=total.
    int start_match = start_agent / num_agents;
    int end_agent = start_agent + agent_count;
    int end_match = (end_agent + num_agents - 1) / num_agents;
    if (end_match > num_matches) end_match = num_matches;

    int lane = blockIdx.x * blockDim.x + threadIdx.x;
    int mid = start_match + lane;
    if (mid >= end_match) return;

    BMMatch* m = &matches[mid];
    int acts[BM_MAX_AGENTS];
    float rew[BM_MAX_AGENTS];
    float term[BM_MAX_AGENTS];

    for (int a = 0; a < num_agents; a++) {
        int gi = mid * num_agents + a;
        acts[a] = (int)actions[(size_t)gi * NUM_ATNS];
        rew[a] = 0.0f;
        term[a] = 0.0f;
    }

    bm_step_match(m, &d_bcfg, acts, rew, term);

    if (m->done) {
        bm_gpu_fold_logs(envs, m, mid, num_agents);
        // Preserve terminal rewards, then auto-reset for next rollout step.
        float keep_r[BM_MAX_AGENTS];
        float keep_t[BM_MAX_AGENTS];
        for (int a = 0; a < num_agents; a++) {
            keep_r[a] = rew[a];
            keep_t[a] = term[a];
        }
        uint32_t seed = m->rng ^ (0x85ebca6bu * (uint32_t)(m->tick + 1));
        bm_reset_match(m, &d_bcfg, seed);
        for (int a = 0; a < num_agents; a++) {
            int gi = mid * num_agents + a;
            rewards[gi] = keep_r[a];
            terminals[gi] = keep_t[a];
            bm_write_obs(m, &d_bcfg, a,
                observations + (size_t)gi * OBS_SIZE);
        }
        return;
    }

    for (int a = 0; a < num_agents; a++) {
        int gi = mid * num_agents + a;
        rewards[gi] = rew[a];
        terminals[gi] = term[a];
        bm_write_obs(m, &d_bcfg, a,
            observations + (size_t)gi * OBS_SIZE);
    }
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    bm_host_load_config(&h_bcfg, env_kwargs);
    if (h_bcfg.reverse_curriculum) {
        fprintf(stderr, "Bomberman reverse curriculum currently requires vec.gpu_env=0\n");
        exit(1);
    }
    g_num_agents = h_bcfg.num_agents;
    if (total_agents % g_num_agents != 0) {
        fprintf(stderr,
            "Bomberman GPU: total_agents (%d) must be divisible by num_agents (%d)\n",
            total_agents, g_num_agents);
        exit(1);
    }
    g_total_agents = total_agents;
    g_num_matches = total_agents / g_num_agents;

    cudaMemcpyToSymbol(d_bcfg, &h_bcfg, sizeof(BMConfig));

    Env* envs = nullptr;
    cudaMalloc((void**)&envs, (size_t)total_agents * sizeof(Env));
    cudaMemset(envs, 0, (size_t)total_agents * sizeof(Env));

    cudaMalloc((void**)&d_matches, (size_t)g_num_matches * sizeof(BMMatch));
    cudaMemset(d_matches, 0, (size_t)g_num_matches * sizeof(BMMatch));

    return envs;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    if (total_agents != g_total_agents) {
        fprintf(stderr, "Bomberman GPU reset: total_agents mismatch\n");
        exit(1);
    }
    bm_reset_kernel<<<bm_grid(g_num_matches), BM_CUDA_BLOCK>>>(
        envs, d_matches, observations, rewards, terminals,
        g_num_matches, g_num_agents);
}

static void puf_envs_step(Env* envs, const float* actions, obs_t* observations,
        float* rewards, float* terminals, int start, int count, cudaStream_t stream) {
    int start_match = start / g_num_agents;
    int end_agent = start + count;
    int end_match = (end_agent + g_num_agents - 1) / g_num_agents;
    if (end_match > g_num_matches) end_match = g_num_matches;
    int nmatch = end_match - start_match;
    if (nmatch <= 0) return;
    bm_step_kernel<<<bm_grid(nmatch), BM_CUDA_BLOCK, 0, stream>>>(
        envs, d_matches, actions, observations, rewards, terminals,
        start, count, g_num_matches, g_num_agents);
}

static void puf_envs_close(Env* envs) {
    if (d_matches) {
        cudaFree(d_matches);
        d_matches = nullptr;
    }
    cudaFree(envs);
    g_num_matches = 0;
    g_total_agents = 0;
}

#endif
