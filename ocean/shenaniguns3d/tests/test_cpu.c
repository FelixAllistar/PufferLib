#include "../shenaniguns3d.h"

int main(void) {
    int sizes[] = ACT_SIZES;
    int completed = 0;
    for (int difficulty = 1; difficulty <= 3; difficulty++) {
        for (int mode = 0; mode <= 2; mode++) {
            Dict cfg = {0};
            dict_set(&cfg, "course_mode", mode);
            dict_set(&cfg, "course_difficulty", difficulty);
            dict_set(&cfg, "max_ticks", 63);
            Env env[2] = {0};
            float obs[2][OBS_SIZE] = {0}, actions[2][NUM_ATNS] = {0};
            float rewards[2] = {0}, terminals[2] = {0};
            for (int p = 0; p < 2; p++) {
                env[p].rng = 731;
                puf_init(&env[p], &cfg);
                env[p].agents[0].observations = obs[p];
                env[p].agents[0].actions = actions[p];
                env[p].agents[0].rewards = &rewards[p];
                env[p].agents[0].terminals = &terminals[p];
                puf_reset(&env[p]);
            }
            for (int t = 0; t < 512; t++) {
                for (int a = 0; a < NUM_ATNS; a++) {
                    actions[0][a] = actions[1][a] = (t*17+a*13)%sizes[a];
                }
                for (int p = 0; p < 2; p++) {
                    puf_step(&env[p]);
                    assert(isfinite(rewards[p]));
                    assert(terminals[p] == 0 || terminals[p] == 1);
                    for (int i = 0; i < OBS_SIZE; i++) assert(isfinite(obs[p][i]));
                }
                assert(!memcmp(obs[0], obs[1], sizeof(obs[0])));
                assert(rewards[0] == rewards[1] && terminals[0] == terminals[1]);
                completed += terminals[0] != 0;
            }
            assert(env[0].log.n >= 8);
            puf_close(&env[0]);
            puf_close(&env[1]);
            dict_clear(&cfg);
        }
    }
    printf("PASS Shenaniguns3D: 9216 transitions, %d paired episodes, all modes/difficulties\n", completed);
}
