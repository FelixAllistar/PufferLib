// Drop ladder check (2026-11-18). Plays fixed UCI lines and random games through chess_track_swing and prints what
// would be harvested at each ladder depth. Build from the repo root:
//   gcc -O2 -std=gnu2x -DCHESS_RULES -DCHESS_MOVE_FEATS -DCHESS_MOVE_FEATS2 -Isrc -Iocean/chess
//       -Iraylib-5.5_linux_amd64/include ocean/chess/tools/swing_test.c ocean/chess/tools/raylib_stubs.c -lm
//       -o build/chess/swing_test
#include "chess.h"
static Chess env;

static void start(void) {
    memset(&env, 0, sizeof(env));
    env.mode = CHESS_MODE_SELFPLAY; env.tag = 0; env.teach_group = -1;
    pos_set(&env.pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    rebuild_legal_state(&env);
    env.swing_n = 0; env.swing_color = 0; env.swing_mask[0] = env.swing_mask[1] = 0;
    chess_track_swing(&env);
}
static int play(const char* uci) {
    Move m = uci_to_move(uci, &env.legal_moves);
    if (!m) { printf("  illegal %s\n", uci); return 0; }
    do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
    rebuild_legal_state(&env);
    chess_track_swing(&env);
    return 1;
}
static void line(const char* name, const char* moves) {
    start();
    char buf[1024]; snprintf(buf, sizeof(buf), "%s", moves);
    for (char* t = strtok(buf, " "); t; t = strtok(NULL, " ")) if (!play(t)) break;
    printf("%-28s drops: %s%s\n", name, env.swing_color & 1 ? "white " : "", env.swing_color & 2 ? "black" : env.swing_color ? "" : "none");
    for (int x = 0; x < 2; x++) if (env.swing_color >> x & 1)
        for (int k = 0; k < CHESS_DROP_N; k++)
            printf("  %c -%-2d %s\n", "wb"[x], CHESS_DROP_OFF[k], env.swing_mask[x] >> k & 1 ? env.swing_fen[x][k] : "(none: too early in the game)");
}

int main(void) {
    init_bitboards();
    CHESS_SWING_ON = 1;
    int offs[] = {0, 2, 4, 6, 10, 16};
    CHESS_DROP_N = 6; for (int k = 0; k < 6; k++) CHESS_DROP_OFF[k] = offs[k];
    // White gives the queen for a pawn: expect color 0 and the position after 2...Nc6 (white to move).
    line("queen blunder (white)", "e2e4 e7e5 d1h5 b8c6 h5e5 c6e5 d2d3 g8f6 b1c3 f8c5");
    // Black ignores e5 hitting the knight (...a6) and loses it: expect color 1, black to move after 2.e5.
    line("hanging knight (black)", "e2e4 g8f6 e4e5 a7a6 e5f6 b7b6 d2d4 c8b7 g1f3 e7e6");
    // Plain exchanges only: expect -1.
    line("trades only (expect none)", "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c8f5 f1c4 e7e6");
    // White takes a defended pawn with the knight (3.Nxe5? Nxe5): knight for pawn, expect color 0 after 2...Nc6.
    line("knight for pawn (white)", "e2e4 e7e5 g1f3 b8c6 f3e5 c6e5 d2d4 e5g6 f1d3 g8f6");
    // Black hangs the queen (2...Qh4?? 3.Nxh4): white is +9. Later white gives the queen back
    // for a knight (5.Qxf6?? gxf6). Expect both drops: black before 2...Qh4, white from the +9 level before 5.Qxf6.
    line("lead then bled", "e2e4 e7e5 g1f3 d8h4 f3h4 g8f6 b1c3 b8c6 d1f3 f8c5 f3f6 g7f6 d2d3 d7d6 f1e2 c8e6 a2a3 a7a6");
    // Random games: harvest rate and that every harvested FEN loads back.
    unsigned rng = 7; int games = 2000, got = 0, bad = 0, plies = 0, depth_got[8] = {0};
    for (int g = 0; g < games; g++) {
        start();
        for (int p = 0; p < 200 && env.legal_moves.count > 0 && !is_insufficient_material(&env.pos); p++) {
            Move m = env.legal_moves.moves[rand_r(&rng) % env.legal_moves.count].move;
            do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
            if (env.undo_stack_ptr > MAX_GAME_PLIES - 8) env.undo_stack_ptr = 0;
            rebuild_legal_state(&env);
            chess_track_swing(&env);
            plies++;
        }
        for (int x = 0; x < 2; x++) if (env.swing_color >> x & 1) {
            got++;
            for (int d = 0; d < CHESS_DROP_N; d++) {
                if (!(env.swing_mask[x] >> d & 1)) continue;
                depth_got[d]++;
                const char* f = env.swing_fen[x][d];
                Position q; pos_set(&q, f);
                char back[128]; position_to_fen(&q, back);
                // pos_set does not read the move counters: compare up to them
                int fields = 0, k = 0; while (f[k] && fields < 4) fields += f[k++] == ' ';
                if (strncmp(back, f, k)) { bad++; if (bad < 3) printf("  roundtrip %s -> %s\n", f, back); }
                char stm = strchr(f, ' ')[1];
                if ((stm == 'w') != (x == 0)) { bad++; printf("  wrong side %s\n", f); }
            }
        }
    }
    printf("random games %d: drops %d, bad %d, %.0f plies/game; per depth:", games, got, bad, (double)plies / games);
    for (int d = 0; d < CHESS_DROP_N; d++) printf(" -%d:%d", CHESS_DROP_OFF[d], depth_got[d]);
    printf("\n");
    return 0;
}
