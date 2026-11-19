// State-group teacher test (2026-11-16): drives the chess env on the CPU the way the trainer does (chess.h compiled
// as C++, my_vec_init, puf_reset / puf_step from OpenMP threads) with a capture-happy random policy, then checks
// the teacher's books against the env's own log.
// Build from the repo root:
//   gcc -O2 -c ocean/chess/tools/raylib_stubs.c -o build/chess/raylib_stubs.o
//   g++ -O2 -std=c++17 -fopenmp -DCHESS_RULES -Isrc -Iocean/chess -Iraylib-5.5_linux_amd64/include \
//       ocean/chess/tools/test_teacher.cpp build/chess/raylib_stubs.o -lm -o build/chess/test_teacher
// Run in a directory holding resources/chess/fens.txt and resources/chess/teacher_groups.txt:
//   test_teacher [--teacher 0.3] [--envs 256] [--steps 4000] [--hash-only]
// --hash-only prints a hash of every finished game (result, length, start position) and exits: with --teacher 0 it
// must equal the hash from a build of the same file against the chess.h from before the teacher (-DNO_TEACHER).
#include "chess.h"
#include <omp.h>
#include <string>
#include <vector>

static int pick_action(Env* env, unsigned* rng) {
    const MoveList& ml = env->legal_moves;
    if (ml.count == 0) return 0;
    int best = rand_r(rng) % ml.count;
    if (rand_r(rng) % 2) {  // half the time: the most valuable capture, so leads and conversions happen
        int bv = 0;
        for (int i = 0; i < ml.count; i++) {
            Piece v = piece_on(&env->pos, to_sq(ml.moves[i].move));
            int val = v == NO_PIECE ? 0 : type_of_p(v);
            if (val > bv) { bv = val; best = i; }
        }
    }
    Move m = ml.moves[best].move;
    int flip = env->pos.sideToMove == CHESS_BLACK ? 56 : 0;
    return ((int)from_sq(m) ^ flip) * 64 + ((int)to_sq(m) ^ flip);
}

int main(int argc, char** argv) {
    double teacher = 0.3; int envs_n = 256, steps = 4000, hash_only = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--teacher")) teacher = atof(argv[++a]);
        else if (!strcmp(argv[a], "--envs")) envs_n = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--steps")) steps = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--hash-only")) hash_only = 1;
    }
    Dict ek = {}, vk = {};
    const char* keys[] = {"reward_draw", "reward_invalid_piece", "reward_invalid_move", "reward_repetition", "book_reward",
                          "book_penalty", "perf_book_weight", "random_fen"};
    for (const char* k : keys) dict_set(&ek, k, 0);
    dict_set(&ek, "max_moves", 5000); dict_set(&ek, "render_fps", 30); dict_set(&ek, "mode", CHESS_MODE_SELFPLAY);
    dict_set(&ek, "enable_50_move_rule", 1); dict_set(&ek, "enable_threefold_repetition", 1);
    dict_set(&ek, "fen_curric_pct", 0.9);
#ifndef NO_TEACHER
    dict_set(&ek, "teacher_pct", teacher);
#endif
    dict_set(&vk, "total_agents", 2 * envs_n); dict_set(&vk, "num_buffers", 1);
    int num_envs = 0, starts[4], counts[4];
    Env* envs = my_vec_init(&num_envs, starts, counts, &vk, &ek);
    std::vector<uint8_t> obs((size_t)num_envs * 2 * OBS_SIZE), mask((size_t)num_envs * 2 * NUM_ACTIONS);
    std::vector<float> act(num_envs * 2), rew(num_envs * 2), term(num_envs * 2);
    std::vector<unsigned> rng(num_envs);
    std::vector<uint64_t> hash(num_envs, 1469598103934665603ULL);
    std::vector<uint64_t> obs_hash(num_envs, 1469598103934665603ULL);  // base observation + mask of both seats, every step
    for (int i = 0; i < num_envs; i++) {
        for (int s = 0; s < 2; s++) {
            Agent& ag = envs[i].agents[s];
            ag.observations = &obs[((size_t)i * 2 + s) * OBS_SIZE]; ag.action_mask = &mask[((size_t)i * 2 + s) * NUM_ACTIONS];
            ag.actions = &act[i * 2 + s]; ag.rewards = &rew[i * 2 + s]; ag.terminals = &term[i * 2 + s];
        }
        rng[i] = 1000 + i;
        puf_reset(&envs[i]);
    }
    #pragma omp parallel for schedule(dynamic, 8)
    for (int i = 0; i < num_envs; i++) {
        Env* env = &envs[i];
        for (int t = 0; t < steps; t++) {
            float games = env->log.n;
            int plies = env->chess_moves;
            char start[128]; memcpy(start, env->game_start_fen, sizeof(start));
            int mover = (int)env->pos.sideToMove;
            act[i * 2 + env->slot_for_color[mover]] = (float)pick_action(env, &rng[i]);
            act[i * 2 + env->slot_for_color[!mover]] = (float)PASS_ACTION;
            rew[i * 2] = rew[i * 2 + 1] = 0; term[i * 2] = term[i * 2 + 1] = 0;
            puf_step(env);
            for (int sdx = 0; sdx < 2; sdx++) {  // the base part must not depend on -DCHESS_MOVE_FEATS
                const uint8_t* o = &obs[((size_t)i * 2 + sdx) * OBS_SIZE]; const uint8_t* mk = &mask[((size_t)i * 2 + sdx) * NUM_ACTIONS];
                uint64_t h = obs_hash[i];
                for (int k = 0; k < CHESS_OBS_END; k++) h = (h ^ o[k]) * 1099511628211ULL;
                for (int k = 0; k < NUM_ACTIONS; k += 7) h = (h ^ mk[k]) * 1099511628211ULL;
                obs_hash[i] = h;
            }
            if (env->log.n != games) {  // a game ended (the env has already reset itself)
                uint64_t h = hash[i];
                for (const char* c = start; *c; c++) h = (h ^ (uint8_t)*c) * 1099511628211ULL;
                h = (h ^ (uint64_t)(plies + 1)) * 1099511628211ULL;
                h = (h ^ (uint64_t)(rew[i * 2] * 2 + 3)) * 1099511628211ULL;
                hash[i] = h;
            }
        }
    }
    uint64_t total_hash = 0; double n = 0, draws = 0, plies = 0;
    for (int i = 0; i < num_envs; i++) { total_hash ^= hash[i] * (uint64_t)(i + 1); n += envs[i].log.n; draws += envs[i].log.draw_rate; plies += envs[i].log.chess_moves; }
    uint64_t total_obs = 0;
    for (int i = 0; i < num_envs; i++) total_obs ^= obs_hash[i] * (uint64_t)(i + 1);
    printf("games %.0f, draws %.3f, plies per game %.1f, hash %016llx, observation hash %016llx\n", n, draws / n, plies / n,
           (unsigned long long)total_hash, (unsigned long long)total_obs);
    if (hash_only) return 0;
#ifndef NO_TEACHER
    double teach_n = 0, teach_win = 0, lead_n = 0, lead_win = 0;
    for (int i = 0; i < num_envs; i++) { teach_n += envs[i].log.teach_n; teach_win += envs[i].log.teach_win; lead_n += envs[i].log.lead_n; lead_win += envs[i].log.lead_win; }
    printf("teacher games %.0f (%.3f of all, set %.2f), favored side won %.3f | natural stable leads %.0f, leader won %.3f\n",
           teach_n, teach_n / n, teacher, teach_n ? teach_win / teach_n : 0, lead_n, lead_n ? lead_win / lead_n : 0);
    ChessTeacher& T = CHESS_TEACHER;
    int fail = 0;
    #define CHECK(cond, ...) do { if (!(cond)) { fail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
    if (teacher > 0) {
        uint64_t attempts = 0, wins = 0, nat_n = 0, nat_win = 0; double prob = 0; int eligible = 0;
        cteach_table(T, stdout);
        for (size_t k = 0; k < T.groups.size(); k++) {
            const TeacherGroup& g = T.groups[k]; const ChessTeachGroup& in = T.info[k];
            attempts += g.attempts; wins += g.wins; nat_n += in.nat_n; nat_win += in.nat_win; prob += g.prob; eligible += g.eligible;
            if (in.ring) {
                uint64_t failures = in.nat_n - in.nat_win;
                CHECK(in.fens.size() == (failures < CHESS_TEACH_RING ? failures : CHESS_TEACH_RING), "%s ring %zu vs failures %llu", in.name.c_str(), in.fens.size(), (unsigned long long)failures);
                for (const std::string& fen : in.fens) {  // every stored state is a lead of this class
                    Position p; pos_set(&p, fen.c_str());
                    int w = chess_material_cp(&p, CHESS_WHITE, 1), b = chess_material_cp(&p, CHESS_BLACK, 1);
                    int lead = w > b ? w - b : b - w, weak = chess_material_cp(&p, w > b ? CHESS_BLACK : CHESS_WHITE, 0);
                    CHECK(lead >= CHESS_LEAD_CP && chess_lead_class(lead, weak) == (int)k, "%s holds %s (lead %d)", in.name.c_str(), fen.c_str(), lead);
                }
            }
            CHECK(g.eligible == !in.fens.empty(), "%s eligible flag", in.name.c_str());
            if (g.eligible) CHECK(g.prob >= T.params.floor / 64, "%s prob %.4f", in.name.c_str(), g.prob);
        }
        // games still running hold results the teacher has not seen; finished games must all be booked
        CHECK(attempts == (uint64_t)teach_n && wins == (uint64_t)teach_win, "practice books %llu/%llu vs log %.0f/%.0f", (unsigned long long)wins, (unsigned long long)attempts, teach_win, teach_n);
        CHECK(nat_n == (uint64_t)lead_n && nat_win == (uint64_t)lead_win, "natural books %llu/%llu vs log %.0f/%.0f", (unsigned long long)nat_win, (unsigned long long)nat_n, lead_win, lead_n);
        CHECK(fabs(prob - 1) < 1e-9, "probabilities sum to %.6f", prob);
        CHECK(fabs(teach_n / n - teacher) < 0.03, "teacher share %.3f vs %.2f", teach_n / n, teacher);
        CHECK(T.steps.load() == (uint64_t)num_envs * 2 * steps, "step clock %llu", (unsigned long long)T.steps.load());
        // save -> close -> init (load): same statistics and rings
        std::vector<TeacherGroup> g0 = T.groups; std::vector<ChessTeachGroup> i0 = T.info; uint64_t s0 = T.steps.load();
        setenv("CHESS_TEACHER_STATE", "teacher_state_test.txt", 1);
        T.state_path = "teacher_state_test.txt";
        cteach_close(); cteach_init();
        CHECK(T.groups.size() == g0.size() && T.steps.load() == s0, "reload: %zu groups, %llu steps", T.groups.size(), (unsigned long long)T.steps.load());
        for (size_t k = 0; k < g0.size() && k < T.groups.size(); k++) {
            const TeacherGroup& a = g0[k]; const TeacherGroup& b = T.groups[k];
            CHECK(a.attempts == b.attempts && a.wins == b.wins && a.fast == b.fast && a.slow == b.slow && a.deaths == b.deaths
                  && a.death_stamp == b.death_stamp && a.last_selected == b.last_selected && a.eligible == b.eligible, "reload stats of %s", i0[k].name.c_str());
            CHECK(i0[k].fens == T.info[k].fens && i0[k].nat_n == T.info[k].nat_n && i0[k].nat_win == T.info[k].nat_win && i0[k].head == T.info[k].head, "reload states of %s", i0[k].name.c_str());
        }
    } else {
        CHECK(teach_n == 0 && !T.on, "teacher off but %.0f teacher games", teach_n);
    }
    printf(fail ? "%d checks FAILED\n" : "all checks passed\n", fail);
    my_vec_close(envs);
    return fail != 0;
#else
    return 0;
#endif
}
