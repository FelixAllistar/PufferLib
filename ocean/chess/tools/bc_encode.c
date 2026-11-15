// Behavior-cloning encoder (2026-11-10): turns a game (start FEN + UCI moves) into the exact observations, action
// masks and actions the chess env produces in self-play, using the env's own code (chess.h populate_observations),
// so a cloned policy sees what it will see in training. Self-play gives BOTH players an observation every ply (the
// side not to move must pass, action 4096), and the recurrent state advances on every ply, so each game yields two
// sequences (White's view and Black's view) of one row per ply.
// Rows are bit-packed: observation bit i is byte i / 8, bit i % 8 (every input is 0 or 1); the action mask likewise.
// Build (a shared library for bc_data.py, with the training build's flags):
//   cc -O2 -shared -fPIC -std=gnu2x -DCHESS_RULES -Isrc -Iraylib-5.5_linux_amd64/include ocean/chess/tools/bc_encode.c
//     ocean/chess/tools/raylib_stubs.c -lm
//     -o build/chess/libbc_encode.so
#include "../chess.h"

#define BC_OBS_BYTES ((OBS_SIZE + 7) / 8)
#define BC_MASK_BYTES ((NUM_ACTIONS + 7) / 8)

// Attack tables, built once when the library loads (before any thread can call bc_encode).
__attribute__((constructor)) static void bc_setup(void) { init_bitboards(); }

int bc_obs_size(void) { return OBS_SIZE; }
int bc_obs_bytes(void) { return BC_OBS_BYTES; }
int bc_mask_bytes(void) { return BC_MASK_BYTES; }

static void pack(const uint8_t* src, int n, uint8_t* dst) {
    memset(dst, 0, (n + 7) / 8);
    for (int i = 0; i < n; i++) if (src[i]) dst[i >> 3] |= (uint8_t)(1u << (i & 7));
}

// Encodes up to max_plies plies of one game. fen: NULL or "" for the start position. moves: UCI moves separated by
// spaces ("e2e4 e7e5 g1f3"). Output arrays, indexed [ply][color] (color 0 White's view, 1 Black's view):
//   obs  [max_plies][2][BC_OBS_BYTES]  packed observation
//   mask [max_plies][2][BC_MASK_BYTES] packed legal-action mask (only the pass action for the side not to move)
//   act  [max_plies][2]                the action taken: from * 64 + to in the mover's view, 4096 for the other side
// An underpromotion is played as in the game but recorded as its from-to action (which the env plays as a queen
// promotion; the mask has only the queen promotions). Returns the number of
// plies encoded, or -(i + 1) if move i is illegal or unparsable (the plies before it are still written).
int bc_encode(const char* fen, const char* moves, int max_plies, uint8_t* obs, uint8_t* mask, int16_t* act) {
    static __thread Chess env;
    static __thread uint8_t obs_buf[2][OBS_SIZE];
    static __thread unsigned char mask_buf[2][NUM_ACTIONS];
    memset(&env, 0, sizeof(env));
    env.mode = CHESS_MODE_SELFPLAY;
    env.slot_for_color[0] = 0;
    env.slot_for_color[1] = 1;
    for (int c = 0; c < 2; c++) {
        env.obs_ptr[c] = obs_buf[c];
        env.action_mask_ptr[c] = mask_buf[c];
        env.agents[c].action_mask = mask_buf[c];
    }
    pos_set(&env.pos, fen && fen[0] ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    env.undo_stack_ptr = 0;
    rebuild_legal_state(&env);
    const char* p = moves;
    int n = 0;
    while (n < max_plies) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[8] = {0};
        int len = 0;
        while (p[len] && p[len] != ' ' && len < 7) { tok[len] = p[len]; len++; }
        p += len;
        Move m = uci_to_move(tok, &env.legal_moves);
        if (m == MOVE_NONE) return -(n + 1);
        populate_observations(&env);
        int mover = (int)env.pos.sideToMove, flip = mover * 56;
        for (int c = 0; c < 2; c++) {
            pack(obs_buf[c], OBS_SIZE, obs + ((long)n * 2 + c) * BC_OBS_BYTES);
            pack(mask_buf[c], NUM_ACTIONS, mask + ((long)n * 2 + c) * BC_MASK_BYTES);
            act[n * 2 + c] = c == mover ? (int16_t)(((int)from_sq(m) ^ flip) * 64 + ((int)to_sq(m) ^ flip)) : PASS_ACTION;
        }
        do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
        rebuild_legal_state(&env);
        n++;
    }
    return n;
}

// Per-move tactical features (chess_tactics.h) along a game, for bc_train.py --move-feats: for ply i, the mover's
// legal moves act[i][j] (j < cnt[i]) and their CT_FEATS bits feat[i][j][k]. Same game format and return as bc_encode.
#include "../chess_tactics.h"
int bc_ct_feats(void) { return CT_FEATS; }
int bc_ct_max_moves(void) { return CT_MAX_MOVES; }
int bc_move_feats(const char* fen, const char* moves, int max_plies, int16_t* act, uint8_t* feat, int16_t* cnt) {
    static __thread Chess env;
    memset(&env, 0, sizeof(env));
    pos_set(&env.pos, fen && fen[0] ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    env.undo_stack_ptr = 0;
    rebuild_legal_state(&env);
    const char* p = moves;
    int n = 0;
    while (n < max_plies) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[8] = {0};
        int len = 0;
        while (p[len] && p[len] != ' ' && len < 7) { tok[len] = p[len]; len++; }
        p += len;
        Move m = uci_to_move(tok, &env.legal_moves);
        if (m == MOVE_NONE) return -(n + 1);
        cnt[n] = (int16_t)ct_move_feats(&env.pos, env.undo_stack, &env.undo_stack_ptr, &env.legal_moves,
                                         act + (long)n * CT_MAX_MOVES, feat + (long)n * CT_MAX_MOVES * CT_FEATS);
        do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
        rebuild_legal_state(&env);
        n++;
    }
    return n;
}
