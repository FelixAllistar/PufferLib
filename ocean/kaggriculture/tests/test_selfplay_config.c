#include "../kaggriculture.h"

// Configuration and assignment preflight only: no GPU, PPO, or saved artifacts.
int main(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    kag_configure_potential(&ini, "train");
    Dict* vk = puf_ini_section(&ini, "vec", 0);
    Dict* ek = puf_ini_section(&ini, "env", 0);
    assert(puf_ini_get(&ini, "selfplay", "enabled") == 1);
    assert(puf_ini_get(&ini, "selfplay", "max_size") == 100);
    assert(puf_ini_get(&ini, "selfplay", "opp_timeout_steps") > 0);
    assert(dict_get(vk, "num_policies") == 7);
    assert(dict_get(vk, "hist_policy_percent") == .5);
    assert(kag_critic_mode == 2 && dict_get(ek, "reward_win_loss_draw") == 1);
    // Avoid touching replay-bank data while testing seat assignment.
    dict_set(ek, "reset_state_prob", 0);
    int buffers = dict_get(vk, "num_buffers");
    int total = dict_get(vk, "total_agents");
    int* starts = (int*)calloc(buffers, sizeof(int));
    int* counts = (int*)calloc(buffers, sizeof(int));
    int games = 0;
    Env* envs = my_vec_init(&games, starts, counts, vk, ek);
    assert(games == total / 2);
    int reference[7] = {0};
    for (int b = 0; b < buffers; b++) {
        int policies[7] = {0}, historical = 0;
        for (int i = starts[b]; i < starts[b] + counts[b]; i++) {
            Env* env = envs + i;
            int p0 = env->agents[0].policy, p1 = env->agents[1].policy;
            assert(p0 >= 0 && p0 < 7 && p1 >= 0 && p1 < 7);
            assert(p0 == 0 || p1 == 0);
            policies[p0]++;
            policies[p1]++;
            historical += p0 != 0 || p1 != 0;
            assert(kag_opponent_noise_probability(&env->opponent_noise)
                == env->opponent_noise.initial);
            unsigned int rng = env->opponent_noise.rng;
            assert(!kag_opponent_noise_replace(&env->opponent_noise, 0, 1));
            assert(env->opponent_noise.rng == rng);
            env->opponent_noise.ticks = 250000000 / total;
            float midpoint = (env->opponent_noise.initial + env->opponent_noise.final) / 2;
            assert(fabsf(kag_opponent_noise_probability(&env->opponent_noise) - midpoint) < .00001f);
            env->opponent_noise.ticks = 500000000 / total + 1;
            assert(kag_opponent_noise_probability(&env->opponent_noise)
                == env->opponent_noise.final);
        }
        assert(historical == counts[b] / 2);
        assert(policies[0] == 3 * counts[b] / 2);
        for (int p = 1; p < 7; p++) assert(policies[p] > 0);
        if (b == 0) memcpy(reference, policies, sizeof(reference));
        else assert(!memcmp(reference, policies, sizeof(reference)));
    }
    assert(reference[0] * buffers == 768);
    assert((reference[0] * buffers) % 16 == 0);
    kag_configure_potential(&ini, "eval");
    assert(dict_get(ek, "opponent_noise_initial") == 0);
    assert(dict_get(ek, "opponent_noise_final") == 0);
    free(envs);
    free(starts);
    free(counts);
    puf_ini_free(&ini);
    puts("Six-opponent config, identical buffer layouts, learner protection and noise decay PASS");
}
