// Frozen copy of the first chess_tactics.h (2026-11-13, commit b5cd537), symbols renamed ct_ -> ctr_. Reference for
// test_tactics.c only: the current implementation must give the same features on every position.
// Per-move tactical features (2026-11-13): a one-move lookahead the policy can weigh instead of discovering
// tactics itself. For every legal move of the side to move: play it, let the opponent resolve all captures
// (quiescence search on material), and compare material with now. Most blunders of the 12B-step 1024x2 model are
// consequences of the move (53% refuted by a quiet move, 29% leave another piece hanging, 18% move onto an unsafe
// square; 308 Stockfish-graded blunders), which the static rule planes (CHESS_RULES) cannot show.
// Features per move (CTR_FEATS bits): material swing buckets (one-hot: <= -250, -250..-50, -50..50, 50..250, >= 250
// centipawns, mover's view), gives check, checkmates. Include after chess.h.
#ifndef CHESS_TACTICS_REF_H
#define CHESS_TACTICS_REF_H

#define CTR_FEATS 7
#define CTR_MAX_MOVES 128
#define CTR_MATE 100000

static const int CTR_VAL[7] = {0, 100, 300, 300, 500, 900, 0};

static int ctr_material(const Position* pos) {
    int s = 0;
    for (int q = 0; q < 64; q++) {
        Piece p = piece_on(pos, (Square)q);
        if (p == NO_PIECE) continue;
        int v = CTR_VAL[type_of_p(p)];
        s += color_of(p) == (int)pos->sideToMove ? v : -v;
    }
    return s;
}

static int ctr_tactical(const Position* pos, Move m) {
    return piece_on(pos, to_sq(m)) != NO_PIECE || type_of_m(m) == ENPASSANT || type_of_m(m) == PROMOTION;
}

static int ctr_mvv(const Position* pos, Move m) {
    Piece v = piece_on(pos, to_sq(m)), a = piece_on(pos, from_sq(m));
    int vv = v != NO_PIECE ? CTR_VAL[type_of_p(v)] : (type_of_m(m) == ENPASSANT ? 100 : 0);
    if (type_of_m(m) == PROMOTION) vv += 800;
    return vv * 16 - CTR_VAL[type_of_p(a)] / 100;
}

// Quiescence on material for the side to move: stand pat or capture (MVV-LVA order), mates scored.
static int ctr_q(Position* pos, UndoInfo* us, int* up, int alpha, int beta, int ply) {
    MoveList ml;
    generate_legal(pos, &ml, us, up);
    if (ml.count == 0) return is_check(pos, pos->sideToMove) ? -CTR_MATE + ply : 0;
    int stand = ctr_material(pos);
    if (stand >= beta || ply >= 16) return stand;
    if (stand > alpha) alpha = stand;
    Move mv[256]; int sc[256], n = 0;
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i].move;
        if (!ctr_tactical(pos, m) || (type_of_m(m) == PROMOTION && promotion_type(m) != QUEEN)) continue;
        mv[n] = m; sc[n] = ctr_mvv(pos, m); n++;
    }
    for (int i = 0; i < n; i++) {
        int b = i;
        for (int j = i + 1; j < n; j++) if (sc[j] > sc[b]) b = j;
        Move t = mv[i]; mv[i] = mv[b]; mv[b] = t;
        int ts = sc[i]; sc[i] = sc[b]; sc[b] = ts;
        do_move(pos, mv[i], us, up);
        int v = -ctr_q(pos, us, up, -beta, -alpha, ply + 1);
        undo_move(pos, mv[i], us, up);
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

// Fills act[i] (action index in the mover's view: from * 64 + to, rank-flipped for Black; queen promotions only)
// and feat[i * CTR_FEATS + k] (0/1) for the legal moves of the side to move. Returns the number of moves.
static int ctr_move_feats(Position* pos, UndoInfo* us, int* up, const MoveList* legal, int16_t* act, uint8_t* feat) {
    int flip = pos->sideToMove == CHESS_BLACK ? 56 : 0, n = 0;
    int now = ctr_material(pos);
    for (int i = 0; i < legal->count && n < CTR_MAX_MOVES; i++) {
        Move m = legal->moves[i].move;
        if (type_of_m(m) == PROMOTION && promotion_type(m) != QUEEN) continue;
        do_move(pos, m, us, up);
        int after = -ctr_q(pos, us, up, -CTR_MATE - 1, CTR_MATE + 1, 1);
        int check = is_check(pos, pos->sideToMove);
        int mate = after >= CTR_MATE - 64;
        undo_move(pos, m, us, up);
        int sw = after - now;
        uint8_t* f = feat + n * CTR_FEATS;
        memset(f, 0, CTR_FEATS);
        f[sw <= -250 ? 0 : sw <= -50 ? 1 : sw < 50 ? 2 : sw < 250 ? 3 : 4] = 1;
        f[5] = (uint8_t)check;
        f[6] = (uint8_t)mate;
        act[n++] = (int16_t)(((int)from_sq(m) ^ flip) * 64 + ((int)to_sq(m) ^ flip));
    }
    return n;
}

#endif
