#include "maze.h"
#include "puffernet.h"

void demo() {
    Weights* weights = load_weights("resources/maze/maze_weights.bin");
    int logit_sizes[1] = {5};
    PufferNet* net = make_puffernet(weights, 1, 121, 512, 5, logit_sizes, 1);

    int num_maps = 64;
    int horizon = 256;
    float speed = 1;
    int vision = 5;
    bool discretize = true;

    Grid* env = (Grid*)calloc(1, sizeof(Grid));
    env->num_agents = 1;
    env->rng = 73;
    env->observations = calloc(WINDOW*WINDOW, sizeof(unsigned char));
    env->actions = calloc(1, sizeof(float));
    env->rewards = calloc(1, sizeof(float));
    env->terminals = calloc(1, sizeof(float));

    // Generate maps matching binding.c: random odd sizes, random difficulty
    State* levels = calloc(num_maps, sizeof(State));
    unsigned int map_rng = 42;
    for (int i = 0; i < num_maps; i++) {
        int sz = 5 + (rand_r(&map_rng) % (MAX_SIZE - 5));
        if (sz % 2 == 0) sz -= 1;
        float difficulty = (float)rand_r(&map_rng) / (float)(RAND_MAX);
        State* level = &levels[i];
        level->width = sz;
        level->height = sz;
        create_maze_level(level, difficulty, i);
    }

    env->num_levels = num_maps;
    env->levels = levels;

    c_reset(env);
    c_render(env);
    while (!WindowShouldClose()) {
        env->actions[0] = ATN_FORWARD;
        State* s = &env->state;

        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)){
                env->actions[0] = ATN_FORWARD;
            } else if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) {
                env->actions[0] = ATN_BACK;
            } else if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) {
                env->actions[0] = ATN_LEFT;
            } else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
                env->actions[0] = ATN_RIGHT;
            } else {
                env->actions[0] = ATN_PASS;
            }
        } else {
            float obs[121];
            for (int i = 0; i < 121; i++) obs[i] = env->observations[i];
            forward_puffernet(net, obs, env->actions);
        }

        c_step(env);
        c_render(env);
    }
    
    free_puffernet(net);
    free(weights);
    free(env->observations);
    free(env->actions);
    free(env->rewards);
    free(env->terminals);
    c_close(env);
    free(levels);
}

void random_uniform_test(int episodes, int num_maps, int size, unsigned int seed) {
    int timeout = 2*size*size;

    Grid* env = (Grid*)calloc(1, sizeof(Grid));
    env->num_agents = 1;
    env->rng = seed;
    env->observations = (unsigned char*)calloc(WINDOW*WINDOW, sizeof(unsigned char));
    env->actions = (float*)calloc(1, sizeof(float));
    env->rewards = (float*)calloc(1, sizeof(float));
    env->terminals = (float*)calloc(1, sizeof(float));

    State* levels = (State*)calloc(num_maps, sizeof(State));
    unsigned int map_rng = 42;
    for (int i = 0; i < num_maps; i++) {
        float difficulty = (float)rand_r(&map_rng) / (float)(RAND_MAX);
        State* level = &levels[i];
        level->width = size;
        level->height = size;
        create_maze_level(level, difficulty, i);
    }

    env->num_levels = num_maps;
    env->levels = levels;
    c_reset(env);

    unsigned int action_rng = seed ^ 0x9e3779b9u;
    int solves = 0;
    int completed = 0;
    long long steps = 0;
    while (completed < episodes) {
        env->actions[0] = (float)(rand_r(&action_rng) % 5);
        c_step(env);
        steps++;

        int done_count = (int)env->log.n;
        if (done_count > completed) {
            if (env->rewards[0] > 0.0f) {
                solves++;
            }
            completed = done_count;
        }
    }

    printf("episodes=%d maps=%d size=%d timeout=%d solves=%d solve_rate=%.9f avg_steps=%.2f\n",
        episodes, num_maps, size, timeout, solves,
        (double)solves / (double)episodes, (double)steps / (double)episodes);

    free(env->observations);
    free(env->actions);
    free(env->rewards);
    free(env->terminals);
    c_close(env);
    free(levels);
    free(env);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--random-test") == 0) {
        int episodes = argc > 2 ? atoi(argv[2]) : 10000;
        int num_maps = argc > 3 ? atoi(argv[3]) : 512;
        int size = argc > 4 ? atoi(argv[4]) : 31;
        unsigned int seed = argc > 5 ? (unsigned int)strtoul(argv[5], NULL, 10) : 123;
        random_uniform_test(episodes, num_maps, size, seed);
        return 0;
    }

    demo();
    return 0;
}
