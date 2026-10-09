#define ML_HEADLESS
typedef float obs_t;
#include "../../src/pufferenv.h"
#include "ml_config.h"

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: sim_probe CONFIG.ini CASE\n"); return 2; }
    Ini ini = {0}; puf_ini_load_file(&ini, argv[1]);
    MLConfig c = ml_config(puf_ini_section(&ini, "env", 0));
    c.course = ML_FLAT; c.length = 96; c.practice_prob = 0;
    MLState s; ml_reset(&s, &c, 73); s.x = 43 * ML_FP;
    const char* name = argv[2];
    for (int t = 0; t <= 100; t++) {
        int action = 0;
        if (!strcmp(name, "run")) action = ML_RIGHT | ML_B;
        if (!strcmp(name, "walk")) action = ML_RIGHT;
        if (!strcmp(name, "brake") && t < 32) action = ML_RIGHT | ML_B;
        if (!strcmp(name, "reverse")) action = (t < 32 ? ML_RIGHT : ML_LEFT) | ML_B;
        if (!strcmp(name, "jump_hold") && t < 40) action = ML_A;
        if (!strcmp(name, "jump_tap") && t < 1) action = ML_A;
        if (!strcmp(name, "run_jump")) action = ML_RIGHT | ML_B | (t >= 48 && t < 88 ? ML_A : 0);
        if (!strcmp(name, "down_run")) action = ML_RIGHT | ML_B | (t >= 32 ? ML_DOWN : 0);
        if (!strcmp(name, "up_run")) action = ML_RIGHT | ML_B | (t >= 32 ? 4 : 0);
        if (!strcmp(name, "both_run")) action = ML_RIGHT | ML_B | (t >= 32 ? ML_LEFT : 0);
        printf("{\"tick\":%d,\"x_fp\":%d,\"feet_fp\":%d,\"vx_fp\":%d,\"vy_fp\":%d,\"grounded\":%d,\"action\":%d}\n",
            t, s.x, s.y, s.vx, s.vy, s.grounded, action);
        if (t < 100) ml_step(&s, &c, action);
    }
    puf_ini_free(&ini); return 0;
}
