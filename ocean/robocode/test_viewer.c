#define main robocode_viewer_main
#include "robocode.c"
#undef main

int main(void) {
    const int hidden = 32, layers = 2;
    int sizes[] = ACT_SIZES;
    int stride = 1;
    for (int h = 0; h < NUM_ATNS; h++) stride += sizes[h];
    int count = hidden * OBS_SIZE + stride * hidden + layers * 3 * hidden * hidden;
    Weights* weights = calloc(1, sizeof(Weights) + (count + 7) * sizeof(float));
    weights->data = (float*)(weights + 1);
    weights->size = count + 7;
    for (int i = 0; i < count; i++) weights->data[i] = sinf(i * 0.13f) * 0.1f;
    for (int agents = 1; agents <= 2; agents++) {
        for (int hard = 0; hard <= 1; hard++) {
            Ini ini = {0};
            puf_ini_load_env(&ini, "robocode", 0, NULL);
            Dict* kwargs = puf_ini_section(&ini, "env", 0);
            dict_set(kwargs, "num_agents", agents);
            dict_set(kwargs, "num_bots", 2 - agents);
            dict_set(kwargs, "max_ticks", 16);
            dict_set(kwargs, "dr", 0);
            Env env = {0};
            env.rng = 42;
            puf_init(&env, kwargs);
            float observations[2 * OBS_SIZE] = {0};
            float actions[2 * NUM_ATNS] = {0}, rewards[2] = {0}, terminals[2] = {0};
            for (int i = 0; i < agents; i++) {
                env.agents[i].observations = observations + i * OBS_SIZE;
                env.agents[i].actions = actions + i * NUM_ATNS;
                env.agents[i].rewards = rewards + i;
                env.agents[i].terminals = terminals + i;
            }
            weights->idx = 0;
            PufferNet* net = make_puffernet(weights, agents, OBS_SIZE, hidden, layers,
                sizes, NUM_ATNS);
            puf_reset(&env);
            for (int step = 0; step < 64; step++) {
                rb_forward(net, observations, actions, terminals, hard);
                for (int a = 0; a < agents; a++) {
                    int offset = a * stride;
                    for (int h = 0; h < NUM_ATNS; h++) {
                        assert(actions[a * NUM_ATNS + h] >= 0);
                        assert(actions[a * NUM_ATNS + h] < sizes[h]);
                        if (hard) {
                            int best = 0;
                            for (int j = 1; j < sizes[h]; j++) {
                                if (net->decoder->output[offset + j]
                                        > net->decoder->output[offset + best]) best = j;
                            }
                            assert(actions[a * NUM_ATNS + h] == best);
                        }
                        offset += sizes[h];
                    }
                }
                puf_step(&env);
            }
            assert(env.log.n > 0);
            // A terminal-marked forward must reproduce a fresh network's state.
            for (int i = 0; i < agents; i++) terminals[i] = 1;
            rb_forward(net, observations, actions, terminals, hard);
            weights->idx = 0;
            PufferNet* fresh = make_puffernet(weights, agents, OBS_SIZE, hidden, layers,
                sizes, NUM_ATNS);
            rb_forward(fresh, observations, actions, terminals, hard);
            assert(memcmp(net->mingru->state, fresh->mingru->state,
                agents * hidden * layers * sizeof(float)) == 0);
            free_puffernet(fresh);
            free_puffernet(net);
            puf_close(&env);
            puf_ini_free(&ini);
        }
    }
    free(weights);
    puts("Robocode viewer: stochastic/argmax, bot/mirror episodes, terminal carry passed");
}
