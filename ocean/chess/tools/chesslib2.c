// Local chess harness library (2026-11-12): the env's own rules and observations (chess.h) for play.py.
// cl_state(fen, moves, view, obs, mask, legal, status): plays UCI `moves` from `fen`, then writes `view`'s
// observation (OBS_SIZE bytes, 0/1) and action mask (NUM_ACTIONS bytes), the side-to-move's legal moves as
// space-separated UCI into `legal` (>= 2048 bytes), and the game status:
//   0 ongoing, 1 checkmate (side to move is mated), 2 stalemate, 3 fifty-move, 4 threefold, 5 insufficient material.
// Returns the side to move (0 White, 1 Black), or -(i + 1) if move i is illegal.
#include "chess.h"

__attribute__((constructor)) static void cl_setup(void) { init_bitboards(); }

int cl_obs_size(void) { return OBS_SIZE; }
int cl_num_actions(void) { return NUM_ACTIONS; }

static void sq_name(int s, char* out) { out[0] = 'a' + (s & 7); out[1] = '1' + (s >> 3); }

int cl_state(const char* fen, const char* moves, int view, uint8_t* obs, uint8_t* mask, char* legal, int* status) {
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
    const char* p = moves ? moves : "";
    int n = 0;
    while (1) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[8] = {0};
        int len = 0;
        while (p[len] && p[len] != ' ' && len < 7) { tok[len] = p[len]; len++; }
        p += len;
        Move m = uci_to_move(tok, &env.legal_moves);
        if (m == MOVE_NONE) return -(n + 1);
        do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
        rebuild_legal_state(&env);
        n++;
    }
    populate_observations(&env);
    memcpy(obs, obs_buf[view], OBS_SIZE);
    memcpy(mask, mask_buf[view], NUM_ACTIONS);
    char* q = legal;
    for (int i = 0; i < env.legal_moves.count; i++) {
        Move m = env.legal_moves.moves[i].move;
        sq_name(from_sq(m), q); sq_name(to_sq(m), q + 2); q += 4;
        if (type_of_m(m) == PROMOTION) *q++ = "  nbrq"[promotion_type(m)];
        *q++ = ' ';
    }
    *q = 0;
    int st = 0;
    if (env.legal_moves.count == 0) st = is_check(&env.pos, env.pos.sideToMove) ? 1 : 2;
    else if (env.pos.rule50 >= 100) st = 3;
    else if (is_insufficient_material(&env.pos)) st = 5;
    else if (env.undo_stack_ptr >= 4) {
        int plies = env.undo_stack[env.undo_stack_ptr - 1].pliesFromNull, reps = 0;
        for (int i = 4; i <= plies; i += 2) {
            int idx = env.undo_stack_ptr - i;
            if (idx >= 0 && env.undo_stack[idx].key == env.pos.key) reps++;
        }
        if (reps >= 2) st = 4;
    }
    *status = st;
    return (int)env.pos.sideToMove;
}

// ---- cheap tactical search (material only) for the search test ----
static const int CL_VAL[7] = {0, 100, 300, 300, 500, 900, 0};
#define CL_MATE 100000
static __thread long cl_nodes;

static int cl_material(const Position* pos) {
    int s = 0;
    for (int q = 0; q < 64; q++) {
        Piece p = piece_on(pos, (Square)q);
        if (p == NO_PIECE) continue;
        int v = CL_VAL[type_of_p(p)];
        s += color_of(p) == (int)pos->sideToMove ? v : -v;
    }
    return s;
}

static int cl_is_tactical(const Position* pos, Move m) {
    return piece_on(pos, to_sq(m)) != NO_PIECE || type_of_m(m) == ENPASSANT || type_of_m(m) == PROMOTION;
}

static int cl_mvv(const Position* pos, Move m) {
    Piece v = piece_on(pos, to_sq(m)), a = piece_on(pos, from_sq(m));
    int vv = v != NO_PIECE ? CL_VAL[type_of_p(v)] : (type_of_m(m) == ENPASSANT ? 100 : 0);
    if (type_of_m(m) == PROMOTION) vv += 800;
    return vv * 16 - CL_VAL[type_of_p(a)] / 100;
}

static int cl_q(Position* pos, UndoInfo* us, int* up, int alpha, int beta, int ply) {
    cl_nodes++;
    MoveList ml; generate_legal(pos, &ml, us, up);
    if (ml.count == 0) return is_check(pos, pos->sideToMove) ? -CL_MATE + ply : 0;
    int stand = cl_material(pos);
    if (stand >= beta || ply >= 24) return stand;
    if (stand > alpha) alpha = stand;
    Move mv[256]; int sc[256], n = 0;
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i].move;
        if (!cl_is_tactical(pos, m)) continue;
        if (type_of_m(m) == PROMOTION && promotion_type(m) != QUEEN) continue;
        mv[n] = m; sc[n] = cl_mvv(pos, m); n++;
    }
    for (int i = 0; i < n; i++) {
        int b = i;
        for (int j = i + 1; j < n; j++) if (sc[j] > sc[b]) b = j;
        Move t = mv[i]; mv[i] = mv[b]; mv[b] = t; int ts = sc[i]; sc[i] = sc[b]; sc[b] = ts;
        do_move(pos, mv[i], us, up);
        int v = -cl_q(pos, us, up, -beta, -alpha, ply + 1);
        undo_move(pos, mv[i], us, up);
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

static int cl_ab(Position* pos, UndoInfo* us, int* up, int depth, int alpha, int beta, int ply) {
    if (depth <= 0) return cl_q(pos, us, up, alpha, beta, ply);
    cl_nodes++;
    MoveList ml; generate_legal(pos, &ml, us, up);
    if (ml.count == 0) return is_check(pos, pos->sideToMove) ? -CL_MATE + ply : 0;
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i].move;
        if (type_of_m(m) == PROMOTION && promotion_type(m) != QUEEN) continue;
        do_move(pos, m, us, up);
        int v = -cl_ab(pos, us, up, depth - 1, -beta, -alpha, ply + 1);
        undo_move(pos, m, us, up);
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

// Scores each candidate (space-separated UCI) for the side to move after fen+moves: material (centipawns, mover's
// view) after `depth` more plies of full-width alpha-beta and a capture quiescence search; mates are +-100000.
// Returns the number of nodes searched.
long cl_search(const char* fen, const char* moves, const char* cands, int depth, int* scores) {
    static __thread Chess env;
    memset(&env, 0, sizeof(env));
    pos_set(&env.pos, fen && fen[0] ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    rebuild_legal_state(&env);
    const char* p = moves ? moves : "";
    while (1) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[8] = {0}; int len = 0;
        while (p[len] && p[len] != ' ' && len < 7) { tok[len] = p[len]; len++; }
        p += len;
        Move m = uci_to_move(tok, &env.legal_moves);
        if (m == MOVE_NONE) return -1;
        do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
        rebuild_legal_state(&env);
    }
    cl_nodes = 0;
    p = cands; int k = 0;
    while (1) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[8] = {0}; int len = 0;
        while (p[len] && p[len] != ' ' && len < 7) { tok[len] = p[len]; len++; }
        p += len;
        Move m = uci_to_move(tok, &env.legal_moves);
        if (m == MOVE_NONE) { scores[k++] = -2 * CL_MATE; continue; }
        do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
        scores[k++] = -cl_ab(&env.pos, env.undo_stack, &env.undo_stack_ptr, depth, -CL_MATE - 1, CL_MATE + 1, 1);
        undo_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
    }
    return cl_nodes;
}

// Per-move tactical features (chess_tactics.h) for the side to move after fen+moves; returns the move count.
#include "chess_tactics.h"
int cl_move_feats(const char* fen, const char* moves, int16_t* act, uint8_t* feat) {
    static __thread Chess env;
    memset(&env, 0, sizeof(env));
    pos_set(&env.pos, fen && fen[0] ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    rebuild_legal_state(&env);
    const char* p = moves ? moves : "";
    while (1) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[8] = {0}; int len = 0;
        while (p[len] && p[len] != ' ' && len < 7) { tok[len] = p[len]; len++; }
        p += len;
        Move m = uci_to_move(tok, &env.legal_moves);
        if (m == MOVE_NONE) return -1;
        do_move(&env.pos, m, env.undo_stack, &env.undo_stack_ptr);
        rebuild_legal_state(&env);
    }
    return ct_move_feats(&env.pos, env.undo_stack, &env.undo_stack_ptr, &env.legal_moves, act, feat);
}
