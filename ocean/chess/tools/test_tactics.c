// Per-move feature test and benchmark (2026-11-16). Compares chess_tactics.h with the frozen first implementation
// (ct_ref.h) on every position of the given FEN files and on random playouts from them, and times both.
// Build from the repo root:
//   gcc -O2 -std=gnu2x -DCHESS_RULES -Isrc -Iocean/chess -Iocean/chess/tools -Iraylib-5.5_linux_amd64/include \
//       ocean/chess/tools/test_tactics.c ocean/chess/tools/raylib_stubs.c -lm -o build/chess/test_tactics
// Run: test_tactics FENS.txt [more.txt ...] [--playout PLIES] [--cap N]. Exit code 1 on any mismatch.
#include "chess.h"
#include "chess_tactics.h"
#include "ct_ref.h"
#define REF 1
#include <time.h>
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static Chess env;
static long positions, moves_total, mismatches, checks, bucket[CT_FEATS];
static double t_new, t_ref;

static void one(void) {
    static int16_t act[CT_MAX_MOVES], ract[CT_MAX_MOVES]; static uint8_t feat[CT_MAX_MOVES * CT_FEATS], rfeat[CT_MAX_MOVES * CT_FEATS];
    double t0 = now_s();
    int n = ct_move_feats(&env.pos, env.undo_stack, &env.undo_stack_ptr, &env.legal_moves, act, feat);
    double t1 = now_s();
    t_new += t1 - t0; positions++; moves_total += n; checks += is_check(&env.pos, env.pos.sideToMove);
    for (int i = 0; i < n * CT_FEATS; i++) bucket[i % CT_FEATS] += feat[i];
#ifdef REF
    int rn = ctr_move_feats(&env.pos, env.undo_stack, &env.undo_stack_ptr, &env.legal_moves, ract, rfeat);
    t_ref += now_s() - t1;
    if (rn != n || memcmp(act, ract, n * sizeof(int16_t)) || memcmp(feat, rfeat, n * CT_FEATS)) {
        char fen[128]; position_to_fen(&env.pos, fen);
        if (mismatches++ < 10) printf("MISMATCH %s (moves %d vs %d)\n", fen, n, rn);
    }
#endif
}

int main(int argc, char** argv) {
    init_bitboards();
    int playout = 0, cap = 1 << 30; unsigned rng = 12345;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--playout")) { playout = atoi(argv[++a]); continue; }
        if (!strcmp(argv[a], "--cap")) { cap = atoi(argv[++a]); continue; }
    }
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--playout") || !strcmp(argv[a], "--cap")) { a++; continue; }
        FILE* f = fopen(argv[a], "r"); if (!f) { printf("cannot open %s\n", argv[a]); return 2; }
        char line[256]; int k = 0;
        while (k++ < cap && fgets(line, sizeof(line), f)) {
            memset(&env, 0, sizeof(env)); pos_set(&env.pos, line); rebuild_legal_state(&env);
            one();
            for (int p = 0; p < playout && env.legal_moves.count > 0; p++) {  // random playout: checks, promotions, en passant
                Move m = env.legal_moves.moves[rand_r(&rng) % env.legal_moves.count].move;
                do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
                env.undo_stack_ptr = 0;
                rebuild_legal_state(&env);
                if (env.legal_moves.count == 0 || is_insufficient_material(&env.pos)) break;
                one();
            }
        }
        fclose(f);
    }
    printf("positions %ld (in check %ld), moves/position %.1f\n", positions, checks, (double)moves_total / positions);
    printf("feature totals: lose>=250 %ld, lose %ld, even %ld, win %ld, win>=250 %ld, check %ld, mate %ld\n",
           bucket[0], bucket[1], bucket[2], bucket[3], bucket[4], bucket[5], bucket[6]);
    printf("new: %.1f us/position\n", t_new / positions * 1e6);
#ifdef REF
    printf("ref: %.1f us/position (%.1fx), mismatches %ld\n", t_ref / positions * 1e6, t_ref / t_new, mismatches);
#endif
    return mismatches != 0;
}
