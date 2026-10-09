#define _POSIX_C_SOURCE 200809L
#include "mario_lab.h"

static int integer_arg(const char* text, int lo, int hi) {
    char* end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !*text || *end || value < lo || value > hi) {
        fprintf(stderr, "Expected integer in [%d, %d], got %s\n", lo, hi, text);
        exit(2);
    }
    return (int)value;
}

static void trace_header(const MLConfig* c, unsigned seed) {
    printf("{\"type\":\"header\",\"format\":\"mario_lab_diagnostic_v2\","
        "\"sim_version\":%d,\"seed\":%u,\"observation_size\":%d,"
        "\"state_units_per_pixel\":256,\"expert\":false,\"config\":{", ML_VERSION, seed, ML_OBS_SIZE);
#define INT_FIELD(name) printf("\"" #name "\":%d,", (int)c->name)
    INT_FIELD(course); INT_FIELD(difficulty); INT_FIELD(length); INT_FIELD(max_frames);
    INT_FIELD(require_pipe); INT_FIELD(seed); INT_FIELD(split); INT_FIELD(accel);
    INT_FIELD(friction); INT_FIELD(walk_speed); INT_FIELD(run_speed); INT_FIELD(jump_speed);
    INT_FIELD(gravity_hold); INT_FIELD(gravity_release); INT_FIELD(max_fall); INT_FIELD(hold_frames);
    INT_FIELD(practice_gap); INT_FIELD(practice_entry); INT_FIELD(practice_exit);
    INT_FIELD(practice_route); INT_FIELD(practice_frames);
    INT_FIELD(practice_short_goals);
    INT_FIELD(mix_flat); INT_FIELD(mix_gaps); INT_FIELD(mix_pipes); INT_FIELD(mix_stairs);
    INT_FIELD(mix_walkers); INT_FIELD(mix_underground); INT_FIELD(mix_composite);
    INT_FIELD(physics_mode); INT_FIELD(walk_accel); INT_FIELD(brake_accel);
    INT_FIELD(player_width); INT_FIELD(player_height); INT_FIELD(jump_fast_speed);
    INT_FIELD(jump_fast_threshold); INT_FIELD(gravity_fast_hold); INT_FIELD(gravity_fast_release);
    INT_FIELD(pipe_min_height); INT_FIELD(pipe_max_height); INT_FIELD(stair_height);
    INT_FIELD(generator_mode); INT_FIELD(length_min); INT_FIELD(section_min); INT_FIELD(section_max);
    INT_FIELD(spacing_min); INT_FIELD(spacing_max);
#undef INT_FIELD
    printf("\"completion_reward\":%.9g,\"death_penalty\":%.9g,\"speed_bonus\":%.9g,"
        "\"progress_reward\":%.9g,\"practice_prob\":%.9g}}\n", c->completion_reward, c->death_penalty,
        c->speed_bonus, c->progress_reward, c->practice_prob);
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5 || (strcmp(argv[1], "play") && strcmp(argv[1], "script") && strcmp(argv[1], "trace"))) {
        fprintf(stderr, "usage: %s play|script|trace [SEED [COURSE [CONFIG.ini]]]\n", argv[0]);
        return 2;
    }
    Ini ini = {0};
    if (argc == 5) puf_ini_load_file(&ini, argv[4]);
    else puf_ini_load_env(&ini, "mario_lab", 0, NULL);
    MLConfig c = ml_config(puf_ini_section(&ini, "env", 0));
    unsigned seed = argc > 2 ? (unsigned)integer_arg(argv[2], 1, 2147483647) : c.seed;
    if (argc > 3) c.course = integer_arg(argv[3], 0, ML_COMPOSITE);
    MLState s;
    ml_reset(&s, &c, seed);
    int play = !strcmp(argv[1], "play"), trace = !strcmp(argv[1], "trace");
    if (trace) trace_header(&c, seed);
    if (play) ml_render_state(&s, &c);
    while (play ? !WindowShouldClose() : s.status == ML_RUNNING) {
        int action = 0;
        if (play) {
            if (IsKeyPressed(KEY_R)) ml_reset(&s, &c, seed);
            if (IsKeyPressed(KEY_T)) s.theme = (s.theme + 1) % 3;
            if (IsKeyDown(KEY_RIGHT)) action |= ML_RIGHT;
            if (IsKeyDown(KEY_LEFT)) action |= ML_LEFT;
            if (IsKeyDown(KEY_DOWN)) action |= ML_DOWN;
            if (IsKeyDown(KEY_X) || IsKeyDown(KEY_SPACE)) action |= ML_A;
            if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_LEFT_SHIFT)) action |= ML_B;
        } else action = ml_script_action(&s);
        if (trace) {
            printf("{\"type\":\"step\",\"frame\":%d,\"room\":%d,\"x\":%d,\"y\":%d,"
                "\"vx\":%d,\"vy\":%d,\"task\":%d,\"action\":%d,\"observation\":[",
                s.tick, s.room, s.x, s.y, s.vx, s.vy, s.task, action);
            for (int i = 0; i < ML_OBS_SIZE; i++)
                printf("%s%.9g", i ? "," : "", ml_observation_at(&s, &c, i));
        }
        float reward = ml_step(&s, &c, action);
        if (trace) printf("],\"reward\":%.9g,\"terminal\":%s}\n", reward, s.status ? "true" : "false");
        if (play) ml_render_state(&s, &c);
    }
    printf("{\"type\":\"outcome\",\"seed\":%u,\"course\":%d,\"status\":%d,"
        "\"clear\":%s,\"frames\":%d,\"return\":%.9g,\"pipe_visits\":%d,\"pipe_returns\":%d}\n",
        seed, s.course, s.status, s.status == ML_CLEAR ? "true" : "false", s.tick,
        s.episode_return, s.pipe_visits, s.pipe_returns);
    if (play) CloseWindow();
    puf_ini_free(&ini);
    return 0;
}
