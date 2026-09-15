#pragma once

/* Called before CUDA allocation. Does not choose a different controller or
 * silently override rewards. It also backs the read-only `check` command. */
static void kag_training_preflight(Ini* ini, const char* load_path) {
    KagObservationContract c = kag_observation_contract(ini);
    if (!c.enabled) return;
    if (load_path) kag_executor_check_load(load_path, c, 0);
#ifdef KAG_REWARD_V2_AVAILABLE
    Dict* env = puf_ini_section(ini, "env", 0);
    kag_reward_bind_train_config(env, puf_ini_get(ini, "train", "gamma"),
        puf_ini_get(ini, "train", "reward_clip"));
    Env* probe = (Env*)calloc(1, sizeof(Env));
    puf_init(probe, env);
#ifndef PUFFER_GPU_ENV
    if (probe->reset_state_prob > 0) {
        fprintf(stderr, "Replay-bank starts require the GPU environment build. Set env.reset_state_prob=0 or rebuild with --gpu.\n");
        exit(1);
    }
#endif
    KagRewardConfig r = probe->reward;
    printf("Kaggriculture effective setup: mode=%d executor=%d obs=%d H=%d L=%d interval=%d score_features=%d\n",
        c.mode, c.executor, c.learner, c.hidden, c.layers, c.interval, c.score_features);
    printf("  weights=%s; frozen checkpoints use their own controller metadata\n", load_path ? load_path : "fresh (None)");
    printf("  rewards: cash=%g (%s), quality=%g (%s; idle_cost=%g), PBRS=%g\n",
        r.money_scale, r.money_timing ? "dense" : "terminal", r.quality_scale,
        r.quality_timing ? "dense" : "terminal", r.quality_idle_cost, r.pbrs_scale);
    printf("  growth: land=%g crop=%g animal=%g alive/day=%g; targets=%d plots/%d crops/%d animals\n",
        r.growth_land, r.growth_crop, r.growth_animal, r.alive_daily,
        r.target_plots, kag_reward_crop_target(probe), r.target_animals);
    printf("  replay_reset_prob=%g; land gate=%d days after filling a plot; LR=%g entropy=%g\n",
        probe->reset_state_prob, probe->land_buy_min_days,
        puf_ini_get(ini, "train", "learning_rate"), puf_ini_get(ini, "train", "ent_coef"));
    if (c.score_features && c.mode != 1 && c.mode != 2)
        printf("  NOTE: score_features supplies macro value estimates only in modes 1/2; use 0 for a fresh mode-%d run.\n", c.mode);
    if (load_path) printf("  NOTE: loading a .bin is a weights-only warm start, not optimizer/step-counter restoration.\n");
    free(probe);
#endif
}

static void kag_check_output_directory(const char* directory) {
    DIR* stream = opendir(directory);
    if (!stream) return;
    struct dirent* entry;
    while ((entry = readdir(stream))) {
        size_t n = strlen(entry->d_name);
        if (n >= 4 && !strcmp(entry->d_name + n - 4, ".bin")) {
            fprintf(stderr, "Refusing to overwrite checkpoints in %s. Set base.run_id=None (automatic new ID) or a new run ID. Keep base.load_model_path explicit to warm-start those weights.\n", directory);
            closedir(stream); exit(1);
        }
    }
    closedir(stream);
}
