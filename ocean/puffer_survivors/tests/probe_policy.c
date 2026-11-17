// Headless policy probe: plays a saved checkpoint on the CPU sim and prints episode
// lengths and, per player level, how much of the time it dashes and is invulnerable and
// how much damage it takes.
//   make -C ocean/puffer_survivors probe
//   ./ocean/puffer_survivors/build/probe CHECKPOINT.bin [EPISODES=16] [GREEDY=0] [--env.KEY=VALUE ...]
// --env.KEY=VALUE plays under a changed game rule without editing the ini.
// CHECKPOINT may be circle or circle:T: a scripted pilot that turns 45 degrees every T ticks
// (default 40: a circle of radius ~9, kiting; 12: radius ~2.7, holding ground)
// and always takes the first card, for policy-free measurements (e.g. weapon kill rates
// with --env.free_upgrade=W --env.xp_threshold_base=1e9 --env.player_health=100000).
// Run from the repo root. The network size comes from [policy] in the env's ini.
#define main ps_viewer_main
#include "../puffer_survivors.c"
#undef main

int main(int argc, char** argv) {
    // Positional arguments in order; anything starting with -- is an ini override.
    char* pos[3] = {NULL, NULL, NULL};
    char* overrides[64];
    int npos = 0, nover = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (nover < 64) overrides[nover++] = argv[i];
        } else if (npos < 3) {
            pos[npos++] = argv[i];
        }
    }
    if (!pos[0]) {
        fprintf(stderr, "usage: %s CHECKPOINT.bin [EPISODES=16] [GREEDY=0] [--env.KEY=VALUE ...]\n",
            argv[0]);
        return 1;
    }
    int episodes = pos[1] ? atoi(pos[1]) : 16;
    int greedy = pos[2] ? atoi(pos[2]) : 0;
    int circle = strncmp(pos[0], "circle", 6) == 0;
    int circle_ticks = circle && pos[0][6] == ':' ? atoi(pos[0] + 7) : 40;
    if (circle_ticks < 1) circle_ticks = 40;
    PufferNet* net = circle ? NULL : ps_load_policy(pos[0]);
    if (!circle && !net) return 1;
    // Right, down-right, down, down-left, left, up-left, up, up-right.
    static const int circle_dirs[8] = {4, 8, 2, 7, 3, 5, 1, 6};

    float observations[PS_OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    Env env = {0};
    env.num_agents = 1;
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    Ini ini = {0};
    puf_ini_load_env(&ini, PS_ENV_NAME, nover, overrides);
    env.cfg = ps_config_from_kwargs(puf_ini_section(&ini, "env", 0));
    puf_ini_free(&ini);

    // Buckets by player level.
    enum { LB = 64 };
    double ticks[LB] = {0}, dashing[LB] = {0}, invuln[LB] = {0}, dmg[LB] = {0}, dashes[LB] = {0};
    double tot_len = 0, survived = 0, tot_level = 0, tot_dashes = 0, tot_dmg = 0;
    double tot_kills = 0, tot_straight = 0;
    printf("%s, %d episodes, %s", pos[0], episodes, greedy ? "greedy" : "sampled");
    for (int i = 0; i < nover; i++) printf(" %s", overrides[i]);
    printf("\n");
    printf("max_steps %d  dash cooldown %.0f %+.0f/level, floor %.0f  dash %d ticks at %.2f  walk %.2f\n",
        env.cfg.max_steps, env.cfg.dash_cooldown, env.cfg.dash_cooldown_per_level,
        env.cfg.dash_cooldown_min, env.cfg.dash_duration, env.cfg.dash_speed, env.cfg.player_speed);
    for (int ep = 0; ep < episodes; ep++) {
        env.rng = 1000u + (unsigned)ep * 7919u;
        srand(1234 + ep);
        c_reset(&env);
        if (net) ps_reset_policy_state(net);
        int len = 0, level = 1, alive = 0, ep_dashes = 0;
        float ep_dmg = 0, kills = 0, path = 0;
        // Net displacement over a window against the distance walked in it: near 1 means
        // the player walks one way (kiting), near 0 means it stays around one area.
        enum { WINDOW = 600 };
        float wx = env.px, wy = env.py, wpath = 0, straight_sum = 0;
        int windows = 0;
        // Upgrades taken (by PS_UPGRADE_* id), the first picks in order, and the level at
        // a few times, to see what an episode that never builds up did differently.
        int taken[PS_UPGRADE_COUNT] = {0}, first[10], nfirst = 0;
        static const int marks[4] = {1200, 2400, 4800, 9600};
        int level_at[4] = {0, 0, 0, 0};
        for (;;) {
            if (circle) {
                actions[0] = (float)circle_dirs[(len / circle_ticks) % 8];
                actions[1] = 0.0f;
            } else if (greedy) {
                ps_forward_policy_argmax(net, observations, actions);
            } else {
                forward_puffernet(net, observations, actions, NULL, terminals);
            }
            if (!env.pending_upgrade) actions[1] = 0.0f;
            float hp_before = env.hp, x_before = env.px, y_before = env.py;
            int timer_before = env.dash_timer;
            int lv = env.level < LB ? env.level : LB - 1;
            if (env.pending_upgrade) {
                int pick = env.offered[(int)((unsigned)actions[1] % PS_UPGRADE_SLOTS)];
                taken[pick]++;
                if (nfirst < 10) first[nfirst++] = pick;
            }
            c_step(&env);
            len++;
            if (terminals[0] > 0.0f) {
                // The env has already reset; only the length says how it ended.
                alive = len >= env.cfg.max_steps;
                break;
            }
            level = env.level;
            kills = env.episode_kills;
            for (int m = 0; m < 4; m++) if (len == marks[m]) level_at[m] = level;
            float step = sqrtf((env.px - x_before) * (env.px - x_before)
                + (env.py - y_before) * (env.py - y_before));
            path += step;
            wpath += step;
            if (len % WINDOW == 0) {
                float net = sqrtf((env.px - wx) * (env.px - wx) + (env.py - wy) * (env.py - wy));
                if (wpath > 1.0f) {
                    straight_sum += net / wpath;
                    windows++;
                }
                wx = env.px;
                wy = env.py;
                wpath = 0;
            }
            int started = timer_before <= 0 && env.dash_timer > 0;
            ticks[lv]++;
            dashing[lv] += env.dash_timer > 0 || timer_before > 0;
            invuln[lv] += env.invuln_timer > 0;
            dashes[lv] += started;
            ep_dashes += started;
            if (env.hp < hp_before) {
                dmg[lv] += hp_before - env.hp;
                ep_dmg += hp_before - env.hp;
            }
        }
        tot_len += len;
        survived += alive;
        tot_level += level;
        tot_dashes += ep_dashes;
        tot_dmg += ep_dmg;
        float straight = windows ? straight_sum / windows : 0;
        tot_kills += kills;
        tot_straight += straight;
        printf("ep %2d  len %5d  %-8s  level %2d  kills %5.0f  dashes %3d  damage %3.0f  "
            "moving %3.0f%%  straight %.2f\n",
            ep, len, alive ? "survived" : "died", level, kills, ep_dashes, ep_dmg,
            100 * path / (len * env.cfg.player_speed), straight);
        printf("       level at 1200/2400/4800/9600: %d %d %d %d  first picks:",
            level_at[0], level_at[1], level_at[2], level_at[3]);
        for (int i = 0; i < nfirst; i++) printf(" %d", first[i]);
        printf("  taken:");
        for (int i = 0; i < PS_UPGRADE_COUNT; i++) printf(" %d", taken[i]);
        printf("\n");
    }
    printf("\nmean length %.0f of %d (%.0f%%), survived %.0f of %d, mean level %.1f, "
        "mean kills %.0f, mean dashes %.1f, mean damage %.1f, mean straightness %.2f\n",
        tot_len / episodes, env.cfg.max_steps, 100 * tot_len / episodes / env.cfg.max_steps,
        survived, episodes, tot_level / episodes, tot_kills / episodes, tot_dashes / episodes,
        tot_dmg / episodes, tot_straight / episodes);
    printf("\nlevel   ticks  dashing%%  invuln%%  dashes/1000t  damage/1000t\n");
    for (int l = 1; l < LB; l++) {
        if (ticks[l] < 1) continue;
        printf("%5d %7.0f  %7.1f  %6.1f  %12.1f  %12.2f\n", l, ticks[l],
            100 * dashing[l] / ticks[l], 100 * invuln[l] / ticks[l],
            1000 * dashes[l] / ticks[l], 1000 * dmg[l] / ticks[l]);
    }

    // Fun scorecard: per-episode means from the sim's own log.
    float n = env.log.n > 0 ? env.log.n : 1;
    const char* terms[PS_FUN_TERMS] = {"tension", "damage", "survival", "coverage",
        "combo", "burst", "growth", "straight", "crowd"};
    float raw[PS_FUN_TERMS] = {env.log.raw_tension, env.log.raw_damage, env.log.raw_survival,
        env.log.raw_coverage, env.log.raw_combo, env.log.raw_burst, env.log.raw_growth,
        env.log.raw_straight, env.log.raw_crowd};
    float score[PS_FUN_TERMS] = {env.log.fun_tension, env.log.fun_damage, env.log.fun_survival,
        env.log.fun_coverage, env.log.fun_combo, env.log.fun_burst, env.log.fun_growth,
        env.log.fun_straight, env.log.fun_crowd};
    printf("\nscorecard (mean of %.0f episodes)\nterm          raw    band            score\n", env.log.n);
    for (int t = 0; t < PS_FUN_TERMS; t++) {
        printf("%-9s %8.3f    [%.2f, %.2f]  %6.3f\n", terms[t], raw[t] / n,
            env.cfg.fun_lo[t], env.cfg.fun_hi[t], score[t] / n);
    }
    printf("fun (mean of the per-episode product of scores) %.3f\n", env.log.fun / n);
    printf("kills per HP lost (mean kills / (1 + mean damage)) %.1f\n",
        env.log.kills / (n + env.log.damage_taken));
    if (net) free_puffernet(net);
    free(g_policy_weights);
    return 0;
}
