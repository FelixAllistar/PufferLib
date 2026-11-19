// Per-move tactical features (2026-11-13): a one-move lookahead the policy can weigh instead of discovering
// tactics itself. For every legal move of the side to move: play it, let the opponent resolve all captures
// (quiescence search on material), and compare material with now. Most blunders of the 12B-step 1024x2 model are
// consequences of the move (53% refuted by a quiet move, 29% leave another piece hanging, 18% move onto an unsafe
// square; 308 Stockfish-graded blunders), which the static rule planes (CHESS_RULES) cannot show.
// Features per move (CT_FEATS bits): material swing buckets (one-hot: <= -250, -250..-50, -50..50, 50..250, >= 250
// centipawns, mover's view), gives check, checkmates. Include after chess.h.
#ifndef CHESS_TACTICS_H
#define CHESS_TACTICS_H

// -DCHESS_MOVE_FEATS2 (2026-11-17) adds two bits, chosen from a Stockfish fingerprint of 612 lost/drawn games of the
// 3.4B main1 net vs Maia (reports/chess_selfplay1500_20261116, failure_drill.py): 13% of the points it lost were
// allowed mates and 14% tactics whose refutation is a check, both invisible to the captures-only lookahead.
//   f[7] allows mate in 1: after the move the opponent has a checking move that mates.
//   f[8] check-led loss: some quiet opponent check (not a capture or promotion: those are already in the
//        quiescence), answered by our best evasion (all legal evasions, no standing pat) and then the plain
//        quiescence, leaves us >= 200 worse than the plain swing says. A mate counts.
#ifdef CHESS_MOVE_FEATS2
#define CT_FEATS 9
#else
#define CT_FEATS 7
#endif
#define CT_MAX_MOVES 128
#define CT_MATE 100000

static const int CT_VAL[7] = {0, 100, 300, 300, 500, 900, 0};

// Material for the side to move, from the piece counts do_move keeps (2026-11-16; was a 64-square scan per node).
static inline int ct_material(const Position* pos) {
    int w = 0, b = 0;
    for (int pt = PAWN; pt <= QUEEN; pt++) {
        w += CT_VAL[pt] * pos->pieceCount[make_piece(CHESS_WHITE, pt)];
        b += CT_VAL[pt] * pos->pieceCount[make_piece(CHESS_BLACK, pt)];
    }
    return pos->sideToMove == CHESS_WHITE ? w - b : b - w;
}

static int ct_mvv(const Position* pos, Move m) {
    Piece v = piece_on(pos, to_sq(m)), a = piece_on(pos, from_sq(m));
    int vv = v != NO_PIECE ? CT_VAL[type_of_p(v)] : (type_of_m(m) == ENPASSANT ? 100 : 0);
    if (type_of_m(m) == PROMOTION) vv += 800;
    return vv * 16 - CT_VAL[type_of_p(a)] / 100;
}

// Does the side to move (not in check) have any legal move? Cheap tests first: an unpinned knight, slider or pawn
// with a pseudo-legal move is enough. Otherwise (king moves only, pinned pieces) fall back to the full generator.
static int ct_any_move(Position* pos, UndoInfo* us, int* up, Bitboard pinned) {
    ChessColor c = pos->sideToMove;
    Bitboard occ = pieces(pos), own = pieces_c(pos, c), free = ~pinned, bb;
    bb = pieces_cp(pos, c, KNIGHT) & free;
    while (bb) if (knight_attacks_bb(pop_lsb(&bb)) & ~own) return 1;
    bb = (pieces_cp(pos, c, BISHOP) | pieces_cp(pos, c, QUEEN)) & free;
    while (bb) if (bishop_attacks_bb(pop_lsb(&bb), occ) & ~own) return 1;
    bb = (pieces_cp(pos, c, ROOK) | pieces_cp(pos, c, QUEEN)) & free;
    while (bb) if (rook_attacks_bb(pop_lsb(&bb), occ) & ~own) return 1;
    if (shift_bb(c == CHESS_WHITE ? NORTH : SOUTH, pieces_cp(pos, c, PAWN) & free) & ~occ) return 1;
    MoveList ml;
    generate_legal(pos, &ml, us, up);
    return ml.count > 0;
}

// Legal captures, en passant and queen promotions of the side to move (not in check), unordered. The same set the
// full generator gives after the tactical filter: non-queen promotions are left out, castling is never tactical.
static int ct_gen_tactical(Position* pos, Move* mv, Bitboard pinned) {
    ChessColor c = pos->sideToMove, them = (ChessColor)!c;
    Bitboard occ = pieces(pos), enemy = pieces_c(pos, them), bb;
    Square ksq = lsb(pieces_cp(pos, c, KING));
    Move cand[128]; int nc = 0, n = 0;
    int upd = c == CHESS_WHITE ? NORTH : SOUTH;
    Bitboard rank7 = c == CHESS_WHITE ? Rank7BB : Rank2BB;
    Bitboard pawns = pieces_cp(pos, c, PAWN), on7 = pawns & rank7, not7 = pawns & ~rank7;
    bb = shift_bb(upd + WEST, not7) & enemy;
    while (bb) { Square to = pop_lsb(&bb); cand[nc++] = make_move(to - upd - WEST, to); }
    bb = shift_bb(upd + EAST, not7) & enemy;
    while (bb) { Square to = pop_lsb(&bb); cand[nc++] = make_move(to - upd - EAST, to); }
    if (on7) {
        bb = shift_bb(upd, on7) & ~occ;
        while (bb) { Square to = pop_lsb(&bb); cand[nc++] = make_promotion(to - upd, to, QUEEN); }
        bb = shift_bb(upd + WEST, on7) & enemy;
        while (bb) { Square to = pop_lsb(&bb); cand[nc++] = make_promotion(to - upd - WEST, to, QUEEN); }
        bb = shift_bb(upd + EAST, on7) & enemy;
        while (bb) { Square to = pop_lsb(&bb); cand[nc++] = make_promotion(to - upd - EAST, to, QUEEN); }
    }
    if (pos->epSquare != SQ_NONE) {
        bb = not7 & pawn_attacks_bb(them, pos->epSquare);
        while (bb) cand[nc++] = make_enpassant(pop_lsb(&bb), pos->epSquare);
    }
    bb = pieces_cp(pos, c, KNIGHT);
    while (bb) { Square f = pop_lsb(&bb); Bitboard a = knight_attacks_bb(f) & enemy;
                 while (a) cand[nc++] = make_move(f, pop_lsb(&a)); }
    bb = pieces_cp(pos, c, BISHOP) | pieces_cp(pos, c, QUEEN);
    while (bb) { Square f = pop_lsb(&bb); Bitboard a = bishop_attacks_bb(f, occ) & enemy;
                 while (a) cand[nc++] = make_move(f, pop_lsb(&a)); }
    bb = pieces_cp(pos, c, ROOK) | pieces_cp(pos, c, QUEEN);
    while (bb) { Square f = pop_lsb(&bb); Bitboard a = rook_attacks_bb(f, occ) & enemy;
                 while (a) cand[nc++] = make_move(f, pop_lsb(&a)); }
    bb = king_attacks_bb(ksq) & enemy;
    while (bb) cand[nc++] = make_move(ksq, pop_lsb(&bb));
    for (int i = 0; i < nc; i++)
        if (is_legal_move_fast(pos, cand[i], pinned, ksq, c)) mv[n++] = cand[i];
    return n;
}

// Quiescence on material for the side to move: stand pat or capture (MVV-LVA order), mates scored. The value is
// the minimax of: no legal move -> mated (-CT_MATE + ply) or stalemate (0); else max(material, -child over legal
// captures / en passant / queen promotions), cut at ply 16. Rewritten 2026-11-16 (was the full legal generator and a
// board scan at every node): same value for every position and window.
static int ct_q(Position* pos, UndoInfo* us, int* up, int alpha, int beta, int ply);

static int ct_q_search(Position* pos, UndoInfo* us, int* up, int alpha, int beta, int ply) {
    Move mv[256]; int sc[256], n = 0;
    if (is_check(pos, pos->sideToMove)) {  // in check: the full generator handles evasions
        MoveList ml;
        generate_legal(pos, &ml, us, up);
        if (ml.count == 0) return -CT_MATE + ply;
        int stand = ct_material(pos);
        if (stand >= beta || ply >= 16) return stand;
        if (stand > alpha) alpha = stand;
        for (int i = 0; i < ml.count; i++) {
            Move m = ml.moves[i].move;
            int tactical = piece_on(pos, to_sq(m)) != NO_PIECE || type_of_m(m) == ENPASSANT || type_of_m(m) == PROMOTION;
            if (!tactical || (type_of_m(m) == PROMOTION && promotion_type(m) != QUEEN)) continue;
            mv[n++] = m;
        }
    } else {
        Bitboard pinned = compute_pinned(pos, pos->sideToMove);
        int stand = ct_material(pos);
        if (stand >= beta || ply >= 16)  // stand pat cut: only "is there a legal move" is needed (stalemate = 0)
            return ct_any_move(pos, us, up, pinned) ? stand : 0;
        n = ct_gen_tactical(pos, mv, pinned);
        if (n == 0 && !ct_any_move(pos, us, up, pinned)) return 0;
        if (stand > alpha) alpha = stand;
    }
    for (int i = 0; i < n; i++) sc[i] = ct_mvv(pos, mv[i]);
    for (int i = 0; i < n; i++) {
        int b = i;
        for (int j = i + 1; j < n; j++) if (sc[j] > sc[b]) b = j;
        Move t = mv[i]; mv[i] = mv[b]; mv[b] = t;
        int ts = sc[i]; sc[i] = sc[b]; sc[b] = ts;
        do_move(pos, mv[i], us, up);
        // Futile capture: the opponent can stand pat on its material after this capture, so the move is worth at
        // most -(that material) to us. If that cannot beat alpha, the search below could only fail low; skip it.
        // Standing pat needs a legal move, so the skip is taken only when the opponent is not in check and has one.
        if (-ct_material(pos) <= alpha && !is_check(pos, pos->sideToMove)
                && ct_any_move(pos, us, up, compute_pinned(pos, pos->sideToMove))) {
            undo_move(pos, mv[i], us, up);
            continue;
        }
        int v = -ct_q(pos, us, up, -beta, -alpha, ply + 1);
        undo_move(pos, mv[i], us, up);
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

// Small per-thread table of search results. Independent trades reach the same position in every order, which is
// most of the nodes; an entry is a true bound on that node's value (position key + ply, because the depth cut and
// mate scores depend on ply), so it stays valid for any window and any later position.
#define CT_TT_SIZE 4096
enum { CT_TT_EXACT = 1, CT_TT_LOWER = 2, CT_TT_UPPER = 3 };
typedef struct { Key key; int32_t value; int32_t flag; } CtEntry;
static __thread CtEntry ct_tt[CT_TT_SIZE];

static int ct_q(Position* pos, UndoInfo* us, int* up, int alpha, int beta, int ply) {
    Key k = pos->key ^ ((Key)(ply + 1) * 0x9E3779B97F4A7C15ULL);
    CtEntry* e = &ct_tt[k & (CT_TT_SIZE - 1)];
    if (e->key == k && e->flag) {
        if (e->flag == CT_TT_EXACT || (e->flag == CT_TT_LOWER && e->value >= beta)
                || (e->flag == CT_TT_UPPER && e->value <= alpha)) return e->value;
    }
    int v = ct_q_search(pos, us, up, alpha, beta, ply);
    e->key = k; e->value = v;
    e->flag = v >= beta ? CT_TT_LOWER : v <= alpha ? CT_TT_UPPER : CT_TT_EXACT;
    return v;
}

#ifdef CHESS_MOVE_FEATS2
// After our move (opponent to move, not checking us): the best the opponent gets, in its own material view, from a
// quiet check, i.e. max over its non-capture non-promotion checking moves r of min over our legal evasions e of
// ct_q(after e) (opponent to move again, plain quiescence). Sets *mate1 when some r mates. Returns -CT_MATE when the
// opponent has no quiet check. Only moves that can give check are played: to-squares that attack our king for the
// moved piece type, plus every move of a piece that blocks a line to our king (discovered checks), castling and
// promotions; each is verified with is_check after do_move.
static int ct_quiet_check_best(Position* pos, UndoInfo* us, int* up, int* mate1) {
    ChessColor opp = pos->sideToMove, me = (ChessColor)!opp;
    Square ksq = lsb(pieces_cp(pos, me, KING));
    Bitboard occ = pieces(pos);
    Bitboard diag = bishop_attacks_bb(ksq, occ), line = rook_attacks_bb(ksq, occ);
    Bitboard to_ok[7] = {0, pawn_attacks_bb(me, ksq), knight_attacks_bb(ksq), diag, line, diag | line, 0};
    Bitboard blockers = (diag | line) & pieces_c(pos, opp);  // may uncover a slider
    MoveList ml;
    generate_legal(pos, &ml, us, up);
    int best = -CT_MATE;
    *mate1 = 0;
    for (int i = 0; i < ml.count; i++) {
        Move r = ml.moves[i].move;
        if (piece_on(pos, to_sq(r)) != NO_PIECE || type_of_m(r) == ENPASSANT) continue;  // captures: in ct_q already
        int pt = type_of_p(piece_on(pos, from_sq(r)));
        int maybe = type_of_m(r) == PROMOTION || type_of_m(r) == CASTLING || (to_ok[pt] & sq_bb(to_sq(r)))
                    || (blockers & sq_bb(from_sq(r)));
        if (!maybe || (type_of_m(r) == PROMOTION && promotion_type(r) == QUEEN)) continue;  // queen promos: in ct_q
        do_move(pos, r, us, up);
        if (is_check(pos, me)) {
            MoveList ev;
            generate_legal(pos, &ev, us, up);
            int v;  // opponent's view
            if (ev.count == 0) { v = CT_MATE - 1; *mate1 = 1; }
            else {
                v = CT_MATE;
                for (int j = 0; j < ev.count && v > best; j++) {
                    do_move(pos, ev.moves[j].move, us, up);
                    int o = ct_q(pos, us, up, -CT_MATE, CT_MATE, 3);  // opponent to move
                    undo_move(pos, ev.moves[j].move, us, up);
                    if (o < v) v = o;
                }
            }
            if (v > best) best = v;
        }
        undo_move(pos, r, us, up);
    }
    return best;
}
#endif

// Fills act[i] (action index in the mover's view: from * 64 + to, rank-flipped for Black; queen promotions only)
// and feat[i * CT_FEATS + k] (0/1) for the legal moves of the side to move. Returns the number of moves.
static int ct_move_feats(Position* pos, UndoInfo* us, int* up, const MoveList* legal, int16_t* act, uint8_t* feat) {
    int flip = pos->sideToMove == CHESS_BLACK ? 56 : 0, n = 0;
    int now = ct_material(pos);
    for (int i = 0; i < legal->count && n < CT_MAX_MOVES; i++) {
        Move m = legal->moves[i].move;
        if (type_of_m(m) == PROMOTION && promotion_type(m) != QUEEN) continue;
        do_move(pos, m, us, up);
        // Only the bucket of (after - now) is used, and its edges are within 250 of `now`: a window just wider than
        // that gives the same bucket as the full window. A mate is exactly "the opponent has no legal move and is
        // in check" (any other node can stand pat), which the first call returns as CT_MATE - 1.
        int after = -ct_q(pos, us, up, -(now + 251), -(now - 251), 1);
        int check = is_check(pos, pos->sideToMove);
        int mate = after >= CT_MATE - 64;
#ifdef CHESS_MOVE_FEATS2
        int mate1 = 0, qc = mate ? -CT_MATE : ct_quiet_check_best(pos, us, up, &mate1);
        int chk_loss = qc > -CT_MATE && -qc <= after - 200;  // `after` is exact inside now +- 251, a bound outside
#endif
        undo_move(pos, m, us, up);
        int sw = after - now;
        uint8_t* f = feat + n * CT_FEATS;
        memset(f, 0, CT_FEATS);
        f[sw <= -250 ? 0 : sw <= -50 ? 1 : sw < 50 ? 2 : sw < 250 ? 3 : 4] = 1;
        f[5] = (uint8_t)check;
        f[6] = (uint8_t)mate;
#ifdef CHESS_MOVE_FEATS2
        f[7] = (uint8_t)mate1;
        f[8] = (uint8_t)(chk_loss || mate1);
#endif
        act[n++] = (int16_t)(((int)from_sq(m) ^ flip) * 64 + ((int)to_sq(m) ^ flip));
    }
    return n;
}

#endif
