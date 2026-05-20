#include "incremental_maze.h"
#include "puffernet.h"

void demo() {
    Weights* weights = load_weights("resources/maze/maze_weights.bin");
    int logit_sizes[1] = {5};
    PufferNet* net = make_puffernet(weights, 1, 121, 512, 5, logit_sizes, 1);

    int horizon = 256;
    float speed = 1;
    int vision = 5;
    bool discretize = true;

    Grid* env = (Grid*)calloc(1, sizeof(Grid));
    env->num_agents = 1;
    env->rng = 1;
    env->observations = calloc(WINDOW*WINDOW, sizeof(unsigned char));
    env->actions = calloc(1, sizeof(float));
    env->rewards = calloc(1, sizeof(float));
    env->terminals = calloc(1, sizeof(float));

    int num_maps = INCREMENTAL_NUM_LEVELS * INCREMENTAL_LEVEL_POOL;
    State* levels = calloc(num_maps, sizeof(State));
    for (int level_idx = 0; level_idx < INCREMENTAL_NUM_LEVELS; level_idx++) {
        int sz = INCREMENTAL_MIN_SIZE + 2*level_idx;
        for (int pool_idx = 0; pool_idx < INCREMENTAL_LEVEL_POOL; pool_idx++) {
            int map_idx = level_idx*INCREMENTAL_LEVEL_POOL + pool_idx;
            State* level = &levels[map_idx];
            level->width = sz;
            level->height = sz;
            create_maze_level(level, 0.5f, map_idx);
        }
    }

    env->num_levels = INCREMENTAL_NUM_LEVELS;
    env->levels = levels;

    c_reset(env);
    c_render(env);
    while (!WindowShouldClose()) {
        env->actions[0] = ATN_PASS;
        env->actions[0] = ATN_SOUTH;
        State* s = &env->state;

        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)){
                env->actions[0] = ATN_NORTH;
            } else if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) {
                env->actions[0] = ATN_SOUTH;
            } else if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) {
                s->direction = PI;
                env->actions[0] = ATN_WEST;
            } else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
                s->direction = 0;
                env->actions[0] = ATN_EAST;
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

int main() {
    demo();
    return 0;
}
