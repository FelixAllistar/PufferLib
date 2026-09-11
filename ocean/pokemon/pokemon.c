#define _POSIX_C_SOURCE 200809L
#include "pokemon.h"
#include "policy_view.h"
#include "profile.h"
static int profile_enabled;

static void pk_teams(const PKGame* game) {
    for (int p = 0; p < 2; p++) {
        printf("P%d: ", p + 1);
        for (int i = 0; i < 6; i++) {
            int set = game->teams[p][i];
            printf("\n  %s%s: ", pk_set_name(set), i ? "" : " [lead]");
            for (int m = 0; m < 4; m++) printf("%s%s", m ? "/" : "", pk_move_name(pk_set_move(set, m)));
        }
        putchar('\n');
    }
    fflush(stdout);
}
static void pk_same_rules(PKPolicy* a, PKPolicy* b) {
    if (!b->net) return;
    const char* fields[] = {"generation", "format", "team_selection", "max_updates"};
    for (int i = 0; i < 4; i++) if (puf_ini_get(&a->ini, "env", fields[i]) != puf_ini_get(&b->ini, "env", fields[i])) {
        fprintf(stderr, "Different env.%s: supply env.%s=value to evaluate under one ruleset\n", fields[i], fields[i]); exit(1);
    }
}
static double pk_match(PKPolicy* a, PKPolicy* b, int games, uint64_t seed, int deterministic, int watch) {
    pk_same_rules(a, b);
    Env env = {0};
    puf_init(&env, puf_ini_section(&a->ini, "env", 0));
    int wins = 0, losses = 0, draws = 0, timeouts = 0;
    PKProfileSummary summary = {0};
    double next_step = 0, delay = 0.25;
    int paused = 0;
    if (watch) { InitWindow(1000, 620, "Pokemon policy watch"); SetTargetFPS(30); }
    for (int game = 0; watch || game < games; game++) {
        int seat_a = game & 1;
        PKPolicy* players[2] = {seat_a ? b : a, seat_a ? a : b};
        for (int p = 0; p < 2; p++) pk_parse_fixed_team(&env.game, p, players[p]->team);
        env.game.rng = seed + (uint64_t)game * UINT64_C(0x9e3779b97f4a7c15);
        pk_reset_policy(a, seed + 2 * (uint64_t)game);
        pk_reset_policy(b, seed + 2 * (uint64_t)game + 1);
        pk_game_reset(&env.game);
        PKProfile profile = {0};
        PKProfile profile_b = {0};
        int printed = 0;
        if (watch) printf("Game %d: model A in P%d, model B in P%d\n", game + 1, seat_a + 1, 2 - seat_a);
        while (!env.game.result) {
            int step = 1;
            if (watch) {
                if (WindowShouldClose()) goto done;
                if (IsKeyPressed(KEY_SPACE)) paused = !paused;
                if (IsKeyPressed(KEY_UP)) delay = fmax(0.03, delay / 1.5);
                if (IsKeyPressed(KEY_DOWN)) delay = fmin(2, delay * 1.5);
                step = IsKeyPressed(KEY_N) || (!paused && GetTime() >= next_step);
            }
            if (step) {
                if (profile_enabled) pk_profile_step(&profile, &env.game, seat_a);
                if (profile_enabled == 3) pk_profile_step(&profile_b, &env.game, 1-seat_a);
                int actions[2];
                for (int p = 0; p < 2; p++) actions[p] = pk_policy_action(players[p], env.game.obs[p], env.game.masks[p], deterministic);
                if (pk_game_step(&env.game, actions[0], actions[1]) == 4) { fprintf(stderr, "Engine error\n"); exit(1); }
                if (watch) next_step = GetTime() + delay;
            }
            if (watch) {
                if (!printed && env.game.picks == 6) { pk_teams(&env.game); printed = 1; }
                puf_render(&env);
            }
        }
        int result = env.game.result;
        int winner = result == 1 ? 0 : result == 2 ? 1 : -1;
        wins += winner == seat_a; losses += winner >= 0 && winner != seat_a;
        draws += winner < 0; timeouts += result == 5;
        if (profile_enabled) pk_profile_accumulate(&summary, &profile, &env.game, seat_a);
        if (profile_enabled == 2) pk_profile_emit(&profile, &env.game, seat_a,
            winner<0?0.5:winner==seat_a?1.0:0.0);
        if (profile_enabled == 3) {
            printf("PK_SIDE A\n");
            pk_profile_emit(&profile, &env.game, seat_a, winner<0?0.5:winner==seat_a?1.0:0.0);
            printf("PK_SIDE B\n");
            pk_profile_emit(&profile_b, &env.game, 1-seat_a, winner<0?0.5:winner==seat_a?0.0:1.0);
        }
        if (watch) {
            printf("Result=%s; model A W/D/L=%d/%d/%d\n", winner < 0 ? "draw" : winner == seat_a ? "A wins" : "B wins", wins, draws, losses);
            double until = GetTime() + 2;
            while (!WindowShouldClose() && GetTime() < until && !IsKeyPressed(KEY_ENTER)) puf_render(&env);
            if (WindowShouldClose()) goto done;
        }
    }
done:
    if (IsWindowReady()) CloseWindow();
    int n = wins + losses + draws;
    double score = n ? (wins + 0.5 * draws) / n : 0;
    double bound = n ? sqrt(log(40.0) / (2 * n)) : 1;
    printf("A=%s B=%s games=%d W=%d D=%d L=%d score=%.4f conservative_95%%=[%.4f,%.4f] timeouts=%d sampling=%s\n",
        a->path, b->path, n, wins, draws, losses, score, fmax(0, score - bound), fmin(1, score + bound), timeouts, deterministic ? "argmax" : "stochastic");
    if (profile_enabled == 1) pk_profile_summary(&summary);
    return score;
}
int main(int argc, char** argv) {
    if (argc < 2 || !strcmp(argv[1], "--help")) {
        printf("Eval profiles: --profile compact summary; --profile-json per-game raw JSON for tools.\n");
        printf("--profile-both-json: both policies' realized profiles from the same games (PK_SIDE A/B tags).\n");
        printf("Fixed teams (eval/watch): --team-a=id,id,id,id,id,id --team-b=...; None means unrestricted.\n");
        printf("Usage:\n  %s watch [latest|A.bin|random] [B.bin|random] [--emag]\n  %s eval [latest|A.bin] [B.bin|random] [--games=256]\n  %s matrix A.bin B.bin [C.bin ...] [--games=256]\nOptions: --seed=N --deterministic --emag env.KEY=VALUE\nWatch: Space pause, N single step, Up/Down speed, Enter skip result.\n", argv[0], argv[0], argv[0]);
        return 0;
    }
    int watch = !strcmp(argv[1], "watch"), matrix = !strcmp(argv[1], "matrix");
    if (!watch && !matrix && strcmp(argv[1], "eval")) { fprintf(stderr, "Use watch, eval or matrix; see --help\n"); return 1; }
    char* paths[64], *overrides[64];
    const char* team_a = NULL;
    const char* team_b = NULL;
    int count = 0, override_count = 0, games = 256, deterministic = 0, emag = 0;
    uint64_t seed = 42;
    for (int i = 2; i < argc; i++) {
        if (!strncmp(argv[i], "--team-a=", 9)) team_a = argv[i] + 9;
        else if (!strncmp(argv[i], "--team-b=", 9)) team_b = argv[i] + 9;
        else if (!strcmp(argv[i], "--profile-both-json")) profile_enabled = 3;
        else if (!strcmp(argv[i], "--profile")) profile_enabled = 1;
        else if (!strcmp(argv[i], "--profile-json")) profile_enabled = 2;
        else if (!strcmp(argv[i], "--deterministic")) deterministic = 1;
        else if (!strcmp(argv[i], "--emag")) emag = 1;
        else if (!strncmp(argv[i], "--games=", 8)) games = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--seed=", 7)) seed = strtoull(argv[i] + 7, NULL, 10);
        else if (!strncmp(argv[i], "env.", 4) && strchr(argv[i], '=')) {
            if (override_count == 64) return 1;
            overrides[override_count++] = argv[i];
        } else if (!strncmp(argv[i], "--", 2)) { fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1; }
        else { if (count == 64) return 1; paths[count++] = argv[i]; }
    }
    if (games < 2 || games % 2) { fprintf(stderr, "--games must be positive and even for equal seats\n"); return 1; }
    if (profile_enabled && (watch || matrix)) { fprintf(stderr,"--profile requires eval\n"); return 1; }
    if (matrix && count < 2) { fprintf(stderr, "matrix needs at least two checkpoints\n"); return 1; }
    if (!count) paths[count++] = "latest";
    if (!matrix && count == 1) paths[count++] = watch ? paths[0] : "random";
    if (!matrix && count != 2) return 1;
    PKPolicy* policies = calloc(count, sizeof(PKPolicy));
    for (int i = 0; i < count; i++) pk_load_policy(policies + i, paths[i], emag, override_count, overrides);
    if (matrix && (team_a || team_b)) { fprintf(stderr, "Team overrides require eval/watch\n"); return 1; }
    if (team_a) snprintf(policies[0].team, sizeof(policies[0].team), "%s", team_a);
    if (team_b) snprintf(policies[1].team, sizeof(policies[1].team), "%s", team_b);
    if (matrix) {
        for (int i = 0; i < count; i++) for (int j = i + 1; j < count; j++) {
            printf("PAIR %d %d\n", i, j);
            pk_match(policies + i, policies + j, games, seed, deterministic, 0);
        }
    } else pk_match(policies, policies + 1, games, seed, deterministic, watch);
    for (int i = 0; i < count; i++) pk_free_policy(policies + i);
    free(policies);
    return 0;
}
