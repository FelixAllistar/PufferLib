// CHESS_MOVE_FEATS2 check (2026-11-17). Two modes:
//   mf2_probe --mistakes FILE   lines "fen\tplayed_uci\tbest_uci\tcategory\tpoints": prints, per category, the points
//                               where the new bits fire on the played move, and where they fire on it but not on
//                               Stockfish's move (= would have told the two apart).
//   mf2_probe --bench FENS.txt [--cap N] [--playout P]
//                               times ct_move_feats and prints a checksum of bits 0..6 (must match between a
//                               -DCHESS_MOVE_FEATS and a -DCHESS_MOVE_FEATS2 build) and the new-bit rates.
// Build from the repo root (add -DCHESS_MOVE_FEATS2 for the new bits):
//   gcc -O2 -std=gnu2x -DCHESS_RULES -DCHESS_MOVE_FEATS -DCHESS_MOVE_FEATS2 -Isrc -Iocean/chess \
//       -Iraylib-5.5_linux_amd64/include ocean/chess/tools/mf2_probe.c ocean/chess/tools/raylib_stubs.c -lm \
//       -o build/chess/mf2_probe
#include "chess.h"
#include <time.h>
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static Chess env;
static int16_t act[CT_MAX_MOVES];
static uint8_t feat[CT_MAX_MOVES * CT_FEATS];

static int feats_now(void) {
    return ct_move_feats(&env.pos, env.undo_stack, &env.undo_stack_ptr, &env.legal_moves, act, feat);
}

static int find(int n, const char* uci) {  // slot of a UCI move in the mover's view (queen promotions only)
    int flip = env.pos.sideToMove == CHESS_BLACK ? 56 : 0;
    int f = (uci[0] - 'a') + 8 * (uci[1] - '1'), t = (uci[2] - 'a') + 8 * (uci[3] - '1');
    int a = ((f ^ flip) * 64) + (t ^ flip);
    for (int i = 0; i < n; i++) if (act[i] == a) return i;
    return -1;
}

#define NCAT 32
static char cats[NCAT][64]; static double tot[NCAT], fire[NCAT][2], disc[NCAT][2], fire_any[NCAT], disc_any[NCAT];
static int cat_id(const char* c) {
    for (int i = 0; i < NCAT; i++) {
        if (!cats[i][0]) { snprintf(cats[i], 64, "%s", c); return i; }
        if (!strcmp(cats[i], c)) return i;
    }
    return NCAT - 1;
}

int main(int argc, char** argv) {
    init_bitboards();
    if (argc >= 3 && !strcmp(argv[1], "--mistakes")) {
#if CT_FEATS < 9
        printf("build with -DCHESS_MOVE_FEATS2\n"); return 2;
#else
        FILE* f = fopen(argv[2], "r"); if (!f) return 2;
        char line[512]; int bad = 0;
        while (fgets(line, sizeof(line), f)) {
            char* fen = strtok(line, "\t"); char* pl = strtok(NULL, "\t"); char* be = strtok(NULL, "\t");
            char* cat = strtok(NULL, "\t"); char* pts = strtok(NULL, "\t\n");
            if (!pts) continue;
            memset(&env, 0, sizeof(env)); pos_set(&env.pos, fen); rebuild_legal_state(&env);
            int n = feats_now(), ip = find(n, pl), ib = find(n, be);
            if (ip < 0 || ib < 0) { bad++; continue; }
            int c = cat_id(cat); double p = atof(pts); tot[c] += p;
            const uint8_t *fp = feat + ip * CT_FEATS, *fb = feat + ib * CT_FEATS;
            for (int k = 0; k < 2; k++) {
                if (fp[7 + k]) fire[c][k] += p;
                if (fp[7 + k] && !fb[7 + k] && ip != ib) disc[c][k] += p;
            }
            int ap = fp[7] | fp[8], ab = fb[7] | fb[8];
            if (ap) fire_any[c] += p;
            if (ap && !ab && ip != ib) disc_any[c] += p;
        }
        printf("%-34s %7s  %-15s %-15s %-15s\n", "category", "points", "mate1 [disc]", "chkloss [disc]", "either [disc]");
        for (int c = 0; c < NCAT && cats[c][0]; c++)
            printf("%-34s %7.1f  %5.1f [%5.1f]   %5.1f [%5.1f]   %5.1f [%5.1f]\n", cats[c], tot[c], fire[c][0], disc[c][0],
                   fire[c][1], disc[c][1], fire_any[c], disc_any[c]);
        if (bad) printf("(%d lines skipped: move not found)\n", bad);
        return 0;
#endif
    }
    int cap = 1 << 30, playout = 0; unsigned rng = 12345;
    long positions = 0, moves = 0, nb[CT_FEATS] = {0}; unsigned long long sum = 1469598103934665603ULL; double t = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--bench")) continue;
        if (!strcmp(argv[a], "--cap")) { cap = atoi(argv[++a]); continue; }
        if (!strcmp(argv[a], "--playout")) { playout = atoi(argv[++a]); continue; }
        FILE* f = fopen(argv[a], "r"); if (!f) { printf("cannot open %s\n", argv[a]); return 2; }
        char line[256]; int k = 0;
        while (k++ < cap && fgets(line, sizeof(line), f)) {
            memset(&env, 0, sizeof(env)); pos_set(&env.pos, line); rebuild_legal_state(&env);
            for (int p = 0; p <= playout && env.legal_moves.count > 0; p++) {
                double t0 = now_s(); int n = feats_now(); t += now_s() - t0;
                positions++; moves += n;
                for (int i = 0; i < n; i++) {
                    for (int b = 0; b < 7; b++) sum = (sum ^ feat[i * CT_FEATS + b]) * 1099511628211ULL;
                    for (int b = 0; b < CT_FEATS; b++) nb[b] += feat[i * CT_FEATS + b];
                }
                Move m = env.legal_moves.moves[rand_r(&rng) % env.legal_moves.count].move;
                do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
                env.undo_stack_ptr = 0;
                rebuild_legal_state(&env);
                if (is_insufficient_material(&env.pos)) break;
            }
        }
        fclose(f);
    }
    printf("CT_FEATS %d: positions %ld, moves/pos %.1f, %.1f us/position, bits 0..6 checksum %016llx\n", CT_FEATS,
           positions, (double)moves / positions, 1e6 * t / positions, sum);
    printf("bit rates per move:");
    for (int b = 0; b < CT_FEATS; b++) printf(" %d:%.4f", b, (double)nb[b] / moves);
    printf("\n");
    return 0;
}
