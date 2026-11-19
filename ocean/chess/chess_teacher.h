// State-group teacher for chess self-play (2026-11-16). Included by chess.h.
//
// Why: the self-play nets throw won games (reports/chess_selfplay1500_20261116/NOTES.md: 9-14 of 60 won
// K+Q+N+2P v K+R+2P positions lost; 20 of 83 games thrown after a 5+ point lead) and no state set since v2 has had
// endgames or won positions. This adds a third pool of start states beside the standard start and the opening /
// middlegame bank (fens.txt), sampled by the bandit teacher shared with ocean/retro and ocean/mario_sim
// (ocean/retro/curriculum_teacher.h: rank-mixed failure mass x 4p(1-p), learning progress, staleness, uniform floor).
//
// Groups in the teacher pool:
//   bank groups    files listed in resources/chess/teacher_groups.txt, one "name path" per line (FENs, one per line).
//   lead classes   CHESS_LEAD_CLASSES ring buffers filled online with the policy's own thrown wins. In every natural
//                  self-play game (not started from the teacher pool, no frozen opponent) the first position where
//                  one side has led by >= CHESS_LEAD_CP material for CHESS_LEAD_PLIES plies is remembered. If that
//                  side does not win, the position goes into the ring of its class (size of the lead x how much the
//                  other side has left) and the class gets one unit of failure mass.
// Outcome of a practice game from the teacher pool: 1 if the side ahead in material at the start (by >= 300) wins,
// else 0. Both sides are the learner, so one game trains converting and defending.
// Shares: env.teacher_pct of resets come from the teacher pool; the rest follow the old rule (fen_curric_pct of them
// from fens.txt, else the standard start). teacher_pct = 0 (default) leaves the env exactly as it was, including its
// random-number stream. The lead / thrown-win counters (log lead_n, lead_win) are kept either way.
// State (statistics, rings, step count) is saved to $CHESS_TEACHER_STATE every CHESS_TEACH_SAVE_STEPS agent steps
// and on close, with a readable table beside it (.txt), and loaded from there at start.
// The teacher needs C++ (the shared header); C builds of chess.h (tools) get the counters only.
#ifndef CHESS_TEACHER_H
#define CHESS_TEACHER_H

#define CHESS_LEAD_CP 500
#define CHESS_LEAD_PLIES 4
#define CHESS_LEAD_CLASSES 9
#define CHESS_FAVORED_CP 300
#define CHESS_TEACH_RING 4096
#define CHESS_TEACH_SAVE_STEPS 50000000ULL
#define CHESS_TEACH_GROUPS_PATH "resources/chess/teacher_groups.txt"

// Drop ladder (2026-11-18, on with CHESS_TEACH_SWING=1): the in-game failure states, Mario-style "how far back".
// In every natural self-play game each side's first stable material drop is found: that side ends up CHESS_SWING_CP or
// more below a level it held for two plies in a row (any level: a level game or a lead it then bleeds), and stays there
// for CHESS_SWING_PLIES plies (a trade in progress does not count; 2 points = a knight for a pawn, the exchange). P0 = the last position before the drop with that side
// to move. If that side then does not win, the positions CHESS_DROP_OFF plies before P0 (that side to move each time)
// go into one ring per depth ("drop_2", "drop_4", ...; stm mode: practice success = the side to move does not lose),
// one unit of failure mass each. The bandit's 4p(1-p) term then favours the depth where the side to move holds about
// half the time: close to P0 the game is usually already lost (v1 used P0 alone: success .26), far back it is easy.
// CHESS_TEACH_SWING_DEPTHS=2,4,6,10,16 (even numbers, plies) overrides the depths. Unset: no groups, no tracking.
#define CHESS_SWING_CP 200
#define CHESS_SWING_PLIES 4
#define CHESS_SWING_BACK 48
#define CHESS_DROP_MAX 8
static int CHESS_SWING_ON = 0;  // set once by cteach_init before any env steps
static int CHESS_DROP_N = 0, CHESS_DROP_OFF[CHESS_DROP_MAX];

static void position_to_fen(const Position* pos, char* out);

// Material in centipawns from the piece counts do_move keeps: P 100, N 300, B 300, R 500, Q 900.
static inline int chess_material_cp(const Position* pos, int color, int with_pawns) {
    static const int val[7] = {0, 100, 300, 300, 500, 900, 0};
    int s = 0;
    for (int pt = with_pawns ? PAWN : KNIGHT; pt <= QUEEN; pt++) s += val[pt] * pos->pieceCount[make_piece(color, pt)];
    return s;
}

// Class of a lead: 3 sizes (5-7, 7-10, 10+ points) x 3 defenders (no pieces, up to a rook, more).
static inline int chess_lead_class(int lead_cp, int weak_piece_cp) {
    int size = lead_cp < 700 ? 0 : lead_cp < 1000 ? 1 : 2;
    int left = weak_piece_cp == 0 ? 0 : weak_piece_cp <= 500 ? 1 : 2;
    return size * 3 + left;
}

// Called after every move of a natural self-play game: remembers the first stable lead (see above).
static inline void chess_track_lead(Chess* env) {
    if (env->mode != CHESS_MODE_SELFPLAY || env->tag != 0 || env->teach_group >= 0 || env->snap_class >= 0) return;
    int w = chess_material_cp(&env->pos, CHESS_WHITE, 1), b = chess_material_cp(&env->pos, CHESS_BLACK, 1);
    env->lead_run[CHESS_WHITE] = w - b >= CHESS_LEAD_CP ? env->lead_run[CHESS_WHITE] + 1 : 0;
    env->lead_run[CHESS_BLACK] = b - w >= CHESS_LEAD_CP ? env->lead_run[CHESS_BLACK] + 1 : 0;
    // A position that ends the game now (or is about to, by the 50-move rule) is useless as a start state.
    if (env->legal_moves.count == 0 || is_insufficient_material(&env->pos) || env->pos.rule50 > 80) return;
    for (int c = 0; c < 2; c++) {
        if (env->lead_run[c] < CHESS_LEAD_PLIES) continue;
        env->snap_color = c;
        env->snap_class = chess_lead_class(c == CHESS_WHITE ? w - b : b - w, chess_material_cp(&env->pos, !c, 0));
        position_to_fen(&env->pos, env->snap_fen);
    }
}

// Drop ladder (see above). Called after reset and after every move.
static inline void chess_swing_push(Chess* env) {
    int k = env->swing_n % CHESS_SWING_BACK;
    env->swing_d[k] = (int16_t)(chess_material_cp(&env->pos, CHESS_WHITE, 1) - chess_material_cp(&env->pos, CHESS_BLACK, 1));
    env->swing_ok[k] = env->legal_moves.count > 1 && env->pos.rule50 <= 80 && !is_insufficient_material(&env->pos);
    position_to_fen(&env->pos, env->swing_fens[k]);
    env->swing_n++;
}
static inline void chess_track_swing(Chess* env) {
    if (!CHESS_SWING_ON || env->mode != CHESS_MODE_SELFPLAY || env->tag != 0 || env->teach_group >= 0
            || env->swing_color == 3) return;
    chess_swing_push(env);
    int n = env->swing_n, B = CHESS_SWING_BACK;
    if (n < CHESS_SWING_PLIES + 3) return;
    for (int x = 0; x < 2; x++) {
        if (env->swing_color >> x & 1) continue;  // one drop per side per game
        int sgn = x == CHESS_WHITE ? 1 : -1, now = -1000000;
        for (int j = 1; j <= CHESS_SWING_PLIES; j++) {  // the new level: x's best over the newest plies
            int v = sgn * env->swing_d[(n - j) % B];
            if (v > now) now = v;
        }
        int L = -1;  // last ply (before those) where x held now + CP for two plies in a row
        for (int j = n - CHESS_SWING_PLIES - 1; j >= 1 && j - 1 > n - B; j--) {
            int a = sgn * env->swing_d[j % B], b = sgn * env->swing_d[(j - 1) % B];
            if ((a < b ? a : b) >= now + CHESS_SWING_CP) { L = j; break; }
        }
        if (L < 0) continue;
        env->swing_color |= 1 << x;
        env->swing_mask[x] = 0;
        int p0 = -1;  // last position up to L with x to move (side to move read from the FEN)
        for (int p = L; p >= L - 1 && p >= 0; p--) {
            const char* sp = strchr(env->swing_fens[p % B], ' ');
            if (sp && (sp[1] == 'w') == (x == CHESS_WHITE)) { p0 = p; break; }
        }
        if (p0 < 0) continue;
        for (int k = 0; k < CHESS_DROP_N; k++) {
            int p = p0 - CHESS_DROP_OFF[k];
            if (p < 0 || p <= n - B || !env->swing_ok[p % B]) continue;
            snprintf(env->swing_fen[x][k], sizeof(env->swing_fen[x][k]), "%s", env->swing_fens[p % B]);
            env->swing_mask[x] |= 1 << k;
        }
    }
}

#ifdef __cplusplus
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "../retro/curriculum_teacher.h"

struct ChessTeachGroup {
    std::string name;
    bool ring = false;                 // lead class (online ring) or bank (fixed file)
    std::vector<std::string> fens;     // bank states, or the ring's contents
    size_t head = 0;                   // ring: next slot to overwrite once full
    uint64_t nat_n = 0, nat_win = 0;   // lead classes: natural arrivals and how many the leader won
    bool stm = false;                  // bank flagged "stm": the side to move is the favored side, success = not losing
};
struct ChessTeacher {
    std::mutex mu;
    TeacherParams params;
    std::vector<TeacherGroup> groups;  // statistics, parallel to info
    std::vector<ChessTeachGroup> info;
    std::vector<float> cdf;
    std::atomic<uint64_t> steps{0}, next_save{CHESS_TEACH_SAVE_STEPS};
    std::string state_path;
    bool on = false;
    int users = 0;                     // env sets using the teacher (training and in-run evals share one process)
};
static ChessTeacher CHESS_TEACHER;

static void cteach_add_group(ChessTeacher& t, const std::string& name, bool ring) {
    TeacherGroup g;
    g.key = g.area = (uint32_t)t.groups.size();  // one area per group: a failure credits only its own group
    t.groups.push_back(g);
    ChessTeachGroup i; i.name = name; i.ring = ring;
    t.info.push_back(i);
}

static void cteach_table(ChessTeacher& t, FILE* f) {
    uint64_t now = t.steps.load();
    teacher_priorities(t.groups, now, t.params);
    fprintf(f, "chess teacher at %llu agent steps\n%-22s %7s %9s %7s %7s %7s %9s %8s %9s %6s\n", (unsigned long long)now,
            "group", "states", "practice", "p", "fast", "slow", "natural", "nat_conv", "failmass", "prob");
    for (size_t k = 0; k < t.groups.size(); k++) {
        const TeacherGroup& g = t.groups[k]; const ChessTeachGroup& i = t.info[k];
        double fast, slow; teacher_rates(g, t.params, &fast, &slow);
        fprintf(f, "%-22s %7zu %9llu %7.3f %7.3f %7.3f %9llu %8.3f %9.1f %6.3f\n", i.name.c_str(), i.fens.size(),
                (unsigned long long)g.attempts, ((double)g.wins + 1) / ((double)g.attempts + 2), fast, slow,
                (unsigned long long)i.nat_n, i.nat_n ? (double)i.nat_win / (double)i.nat_n : 0.0,
                teacher_decayed(g, now, t.params), g.prob);
    }
}

// Text state: "chess_teacher 1 <steps>", then per group one line of statistics followed by its ring's FENs.
static void cteach_save(ChessTeacher& t) {
    if (t.state_path.empty()) return;
    std::string tmp = t.state_path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;
    fprintf(f, "chess_teacher 1 %llu\n", (unsigned long long)t.steps.load());
    for (size_t k = 0; k < t.groups.size(); k++) {
        const TeacherGroup& g = t.groups[k]; const ChessTeachGroup& i = t.info[k];
        fprintf(f, "group %s %d %llu %llu %.17g %.17g %.17g %llu %llu %llu %llu %zu %zu\n", i.name.c_str(), (int)i.ring,
                (unsigned long long)g.attempts, (unsigned long long)g.wins, g.fast, g.slow, g.deaths,
                (unsigned long long)g.death_stamp, (unsigned long long)g.last_selected, (unsigned long long)i.nat_n,
                (unsigned long long)i.nat_win, i.ring ? i.fens.size() : (size_t)0, i.head);
        if (i.ring) for (const std::string& s : i.fens) fprintf(f, "%s\n", s.c_str());
    }
    fclose(f);
    rename(tmp.c_str(), t.state_path.c_str());
    FILE* h = fopen((t.state_path + ".txt").c_str(), "w");
    if (h) { cteach_table(t, h); fclose(h); }
}

static bool cteach_load(ChessTeacher& t) {
    FILE* f = fopen(t.state_path.c_str(), "r");
    if (!f) return false;
    char line[512]; unsigned long long steps = 0; int version = 0;
    if (!fgets(line, sizeof(line), f) || sscanf(line, "chess_teacher %d %llu", &version, &steps) != 2 || version != 1) {
        fclose(f); return false;
    }
    t.steps.store(steps);
    t.next_save.store(steps + CHESS_TEACH_SAVE_STEPS);
    while (fgets(line, sizeof(line), f)) {
        char name[64]; int ring; unsigned long long a, w, ds, ls, nn, nw; double fast, slow, deaths; size_t count, head;
        if (sscanf(line, "group %63s %d %llu %llu %lg %lg %lg %llu %llu %llu %llu %zu %zu", name, &ring, &a, &w, &fast,
                   &slow, &deaths, &ds, &ls, &nn, &nw, &count, &head) != 13) continue;
        int k = -1;
        for (size_t j = 0; j < t.info.size(); j++) if (t.info[j].name == name && (int)t.info[j].ring == ring) k = (int)j;
        std::vector<std::string> fens;
        for (size_t j = 0; j < count && fgets(line, sizeof(line), f); j++) {
            line[strcspn(line, "\r\n")] = 0;
            fens.push_back(line);
        }
        if (k < 0) continue;  // a group that no longer exists: its lines are skipped
        TeacherGroup& g = t.groups[k];
        g.attempts = a; g.wins = w; g.fast = fast; g.slow = slow; g.deaths = deaths; g.death_stamp = ds; g.last_selected = ls;
        t.info[k].nat_n = nn; t.info[k].nat_win = nw;
        if (ring) { t.info[k].fens = fens; t.info[k].head = head < fens.size() ? head : 0; }
        g.eligible = !t.info[k].fens.empty();
    }
    fclose(f);
    return true;
}

static void cteach_init(void) {
    ChessTeacher& t = CHESS_TEACHER;
    std::lock_guard<std::mutex> lock(t.mu);
    t.users++;
    if (t.on) return;
    static const char* size[3] = {"lead5", "lead7", "lead10"};
    static const char* left[3] = {"bare", "light", "heavy"};
    for (int c = 0; c < CHESS_LEAD_CLASSES; c++) cteach_add_group(t, std::string(size[c / 3]) + "_" + left[c % 3], true);
    const char* sw = getenv("CHESS_TEACH_SWING");
    if (sw && atoi(sw)) {
        const char* dl = getenv("CHESS_TEACH_SWING_DEPTHS");
        char buf[128]; snprintf(buf, sizeof(buf), "%s", dl && dl[0] ? dl : "2,4,6,10,16");
        CHESS_DROP_N = 0;
        for (char* tok = strtok(buf, ","); tok && CHESS_DROP_N < CHESS_DROP_MAX; tok = strtok(NULL, ",")) {
            int off = atoi(tok);
            if (off < 0 || off >= CHESS_SWING_BACK - CHESS_SWING_PLIES - 4 || off % 2) {
                printf("chess teacher: drop depth %d skipped (even, 0..%d)\n", off, CHESS_SWING_BACK - CHESS_SWING_PLIES - 5);
                continue;
            }
            CHESS_DROP_OFF[CHESS_DROP_N++] = off;
            cteach_add_group(t, "drop_" + std::to_string(off), true);
            t.info.back().stm = true;
        }
        CHESS_SWING_ON = CHESS_DROP_N > 0;
    }
    FILE* f = fopen(CHESS_TEACH_GROUPS_PATH, "r");
    char line[512];
    while (f && fgets(line, sizeof(line), f)) {
        char name[64], path[400], mode[16] = "";
        if (line[0] == '#' || sscanf(line, "%63s %399s %15s", name, path, mode) < 2) continue;
        FILE* b = fopen(path, "r");
        if (!b) { printf("chess teacher: cannot open %s (group %s skipped)\n", path, name); continue; }
        cteach_add_group(t, name, false);
        t.info.back().stm = !strcmp(mode, "stm");
        char fen[256];
        while (fgets(fen, sizeof(fen), b)) {
            fen[strcspn(fen, "\r\n")] = 0;
            if (fen[0]) t.info.back().fens.push_back(fen);
        }
        fclose(b);
        t.groups.back().eligible = !t.info.back().fens.empty();
    }
    if (f) fclose(f);
    const char* path = getenv("CHESS_TEACHER_STATE");
    t.state_path = path ? path : "";
    bool loaded = !t.state_path.empty() && cteach_load(t);
    t.on = true;
    printf("chess teacher: %zu groups (%d lead classes%s + %zu banks), state %s%s\n", t.groups.size(), CHESS_LEAD_CLASSES,
           CHESS_SWING_ON ? " + drop ladder" : "", t.groups.size() - CHESS_LEAD_CLASSES - CHESS_DROP_N, t.state_path.empty() ? "not saved (set CHESS_TEACHER_STATE)" : t.state_path.c_str(),
           loaded ? " (loaded)" : "");
    cteach_table(t, stdout);
}

static void cteach_close(void) {
    ChessTeacher& t = CHESS_TEACHER;
    std::lock_guard<std::mutex> lock(t.mu);
    if (!t.on || --t.users > 0) return;
    cteach_save(t);
    t.groups.clear(); t.info.clear(); t.cdf.clear();
    t.steps.store(0); t.next_save.store(CHESS_TEACH_SAVE_STEPS);
    t.on = false;
    CHESS_SWING_ON = 0;
    CHESS_DROP_N = 0;
}

// Once per env step: counts agent steps (the teacher's clock) and saves the state every CHESS_TEACH_SAVE_STEPS.
static inline void cteach_tick(int agents) {
    ChessTeacher& t = CHESS_TEACHER;
    if (!t.on) return;
    uint64_t now = t.steps.fetch_add((uint64_t)agents) + (uint64_t)agents;
    if (now < t.next_save.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(t.mu);
    if (now < t.next_save.load()) return;
    t.next_save.store(now + CHESS_TEACH_SAVE_STEPS);
    cteach_save(t);
}

// A start state from the teacher pool. Returns 0 when no group has a state yet.
static int cteach_pick(unsigned int* rng, char* fen_out, int fen_cap, int* group_out) {
    ChessTeacher& t = CHESS_TEACHER;
    if (!t.on) return 0;
    std::lock_guard<std::mutex> lock(t.mu);
    uint64_t now = t.steps.load();
    if (!teacher_priorities(t.groups, now, t.params)) return 0;
    teacher_cdf(t.groups, &t.cdf);
    float u = (float)rand_r(rng) / ((float)RAND_MAX + 1.0f);
    int g = teacher_sample(t.cdf.data(), (int)t.cdf.size(), u);
    while (g > 0 && !t.groups[g].eligible) g--;  // the forced final 1.0 can land on a trailing empty group
    if (!t.groups[g].eligible) return 0;
    t.groups[g].last_selected = now;
    const std::vector<std::string>& fens = t.info[g].fens;
    snprintf(fen_out, (size_t)fen_cap, "%s", fens[(size_t)rand_r(rng) % fens.size()].c_str());
    *group_out = g;
    return 1;
}

// Bank flagged "stm" in the groups file (failure states, 2026-11-17): positions just before a decisive mistake of
// our net, mostly level material. Their practice game is scored for the side to move: 1 unless it loses. Fixed after
// init, so no lock.
static int cteach_group_stm(int g) {
    ChessTeacher& t = CHESS_TEACHER;
    return t.on && g >= 0 && g < (int)t.info.size() && t.info[g].stm;
}

// Result of a practice game started from group g.
static void cteach_practice(int g, int favored_won) {
    ChessTeacher& t = CHESS_TEACHER;
    if (!t.on) return;
    std::lock_guard<std::mutex> lock(t.mu);
    if (g < 0 || g >= (int)t.groups.size()) return;
    teacher_record(t.groups[g], 1, favored_won ? 1 : 0, t.params);
}

// Result of a natural game in which one side reached a stable lead of class c at position fen.
static void cteach_natural(int c, int leader_won, const char* fen) {
    ChessTeacher& t = CHESS_TEACHER;
    if (!t.on) return;
    std::lock_guard<std::mutex> lock(t.mu);
    if (c < 0 || c >= CHESS_LEAD_CLASSES) return;
    ChessTeachGroup& i = t.info[c];
    i.nat_n++;
    if (leader_won) { i.nat_win++; return; }
    TeacherGroup& g = t.groups[c];
    teacher_settle(g, t.steps.load(), t.params);
    g.deaths += 1;
    if (i.fens.size() < CHESS_TEACH_RING) i.fens.push_back(fen);
    else { i.fens[i.head] = fen; i.head = (i.head + 1) % CHESS_TEACH_RING; }
    g.eligible = 1;
}
// A natural game's drop position at ladder depth k, whose losing side then did not win (see the drop ladder above).
static void cteach_drop(int k, const char* fen) {
    ChessTeacher& t = CHESS_TEACHER;
    if (!t.on || !CHESS_SWING_ON || k < 0 || k >= CHESS_DROP_N) return;
    std::lock_guard<std::mutex> lock(t.mu);
    int c = CHESS_LEAD_CLASSES + k;  // the ladder rings are added right after the lead classes, in depth order
    ChessTeachGroup& i = t.info[c];
    TeacherGroup& g = t.groups[c];
    teacher_settle(g, t.steps.load(), t.params);
    g.deaths += 1;
    if (i.fens.size() < CHESS_TEACH_RING) i.fens.push_back(fen);
    else { i.fens[i.head] = fen; i.head = (i.head + 1) % CHESS_TEACH_RING; }
    g.eligible = 1;
}
#endif  // __cplusplus

#endif
