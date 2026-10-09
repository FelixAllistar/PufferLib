// Complete one episode at a time: no vector-batch censoring of slow failures.
#define _POSIX_C_SOURCE 200809L
#define ML_HEADLESS
#include "../../src/puffercpu.c"
#include "mario_lab.h"

int main(int argc, char** argv) {
    if (argc < 2 || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "usage: %s MODEL.bin [--section.key=value ...]\n"
            "Loads config/default.ini + config/mario_lab.ini from cwd.\n"
            "Set --base.eval_episodes=N and --env.split=1 for validation.\n"
            "Writes episode outcomes and complete action tapes as JSONL.\n", argv[0]);
        return argc < 2 ? 2 : 0;
    }
    Ini ini = {0}; puf_ini_load_env(&ini, "mario_lab", argc - 2, argv + 2);
    MLConfig c = ml_config(puf_ini_section(&ini, "env", 0));
    int hidden = (int)puf_ini_get(&ini, "policy", "hidden_size");
    int layers = (int)puf_ini_get(&ini, "policy", "num_layers");
    double requested = puf_ini_get(&ini, "base", "eval_episodes");
    if (hidden < 32 || hidden > 512 || hidden % 32 || layers < 1 || layers > 8
            || requested != floor(requested) || requested < 1 || requested > 100000) {
        fprintf(stderr, "Unsupported architecture or episode count (set --base.eval_episodes).\n");
        return 2;
    }
    Weights* weights = load_weights(argv[1]);
    int need = ML_OBS_SIZE * hidden + 65 * hidden + layers * 3 * hidden * hidden;
    if (!weights || weights->size - 7 != need) {
        fprintf(stderr, "Missing checkpoint or architecture mismatch; expected %d float32 weights.\n", need);
        free(weights); return 2;
    }
    for (int i = 0; i < need; i++) if (!isfinite(weights->data[i])) {
        fprintf(stderr, "Non-finite checkpoint weight at %d.\n", i); free(weights); return 2;
    }
    int action_sizes[] = {64};
    PufferNet* net = make_puffernet(weights, 1, ML_OBS_SIZE, hidden, layers, action_sizes, 1);
    float observations[ML_OBS_SIZE], action, terminal;
    int* tape = malloc((size_t)ml_max(c.max_frames, c.practice_frames) * sizeof(int));
    if (!tape) abort();
    uint32_t stream = ml_initial_seed(&c, 0);
    int clears = 0, deaths = 0, timeouts = 0;
    long total_frames = 0, clear_frames = 0;
    printf("{\"type\":\"header\",\"format\":\"mario_lab_policy_eval_v2\",\"sim_version\":%d,"
        "\"seed\":%u,\"split\":%u,\"course\":%d,\"length\":%d,\"difficulty\":%d,"
        "\"max_frames\":%d,\"require_pipe\":%d,\"episodes\":%d,\"hidden\":%d,\"layers\":%d,"
        "\"practice_prob\":%.9g,\"sampling\":\"stochastic; libc rand reseeded per episode\"}\n",
        ML_VERSION, c.seed, c.split, c.course, c.length, c.difficulty, c.max_frames,
        c.require_pipe, (int)requested, hidden, layers, c.practice_prob);
    for (int episode = 0; episode < (int)requested; episode++) {
        MLState s; ml_reset(&s, &c, ml_rand(&stream));
        int start_x = s.x, start_y = s.y, start_vx = s.vx, start_room = s.room;
        uint32_t action_seed = ml_hash(s.level_seed ^ 0xc53e29a1u);
        srand(action_seed);
        terminal = 1; // Reset recurrent memory before the first observation.
        while (s.status == ML_RUNNING) {
            ml_observe(&s, &c, observations);
            forward_puffernet(net, observations, &action, NULL, &terminal);
            terminal = 0;
            tape[s.tick] = (int)action;
            ml_step(&s, &c, (int)action);
        }
        MLState replay; ml_reset(&replay, &c, s.level_seed);
        for (int t = 0; t < s.tick; t++) ml_step(&replay, &c, tape[t]);
        assert(!memcmp(&replay, &s, sizeof(s)) && "saved action tape must reproduce the complete final state");
        clears += s.status == ML_CLEAR; deaths += s.status == ML_DEATH;
        timeouts += s.status == ML_TIMEOUT; total_frames += s.tick;
        if (s.status == ML_CLEAR) clear_frames += s.tick;
        printf("{\"type\":\"episode\",\"episode\":%d,\"level_seed\":%u,\"action_seed\":%u,"
            "\"course\":%d,\"status\":%d,\"frames\":%d,\"return\":%.9g,"
            "\"pipe_visits\":%d,\"pipe_returns\":%d,\"kills\":%d,\"task\":%d,\"goal_task\":%d,"
            "\"start_x\":%d,\"start_y\":%d,\"start_vx\":%d,\"start_room\":%d,"
            "\"replay_verified\":true,\"actions\":[",
            episode, s.level_seed, action_seed, s.course, s.status, s.tick,
            s.episode_return, s.pipe_visits, s.pipe_returns, s.kills, s.task, s.goal_task,
            start_x, start_y, start_vx, start_room);
        for (int t = 0; t < s.tick; t++) printf("%s%d", t ? "," : "", tape[t]);
        puts("]}");
    }
    printf("{\"type\":\"summary\",\"episodes\":%d,\"clears\":%d,\"deaths\":%d,\"timeouts\":%d,"
        "\"clear_rate\":%.9g,\"mean_clear_frames\":%.9g,\"total_frames\":%ld}\n",
        (int)requested, clears, deaths, timeouts, clears / requested,
        clears ? (double)clear_frames / clears : 0, total_frames);
    free(tape); free_puffernet(net); free(weights); puf_ini_free(&ini);
    return 0;
}
