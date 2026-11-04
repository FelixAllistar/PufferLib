#pragma once
#include "core.h"
#include "states.h"
#include "policy.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
typedef float obs_t;
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"

#ifdef POKER_ZS_CRITIC
// Zero-sum critic build (2026-11-04, -DPOKER_ZS_CRITIC; ocean/poker/poker_critic.cu): the learner's observation is
// the public POKER_PUB followed by a privileged block only the critic reads (the encoder skips it): every seat's hole
// cards, live flag, chips committed this hand, stack, button flag and occupant, the dealt board and the street.
// Frozen bank seats keep their own public buffers, so league checkpoints are unaffected.
#define POKER_PRIV (POKER_PLAYERS * POKER_SEAT_PRIV + POKER_CRITIC_XT)
#define OBS_SIZE (POKER_PUB + POKER_PRIV)
#else
#define OBS_SIZE POKER_PUB
#endif
#define NUM_ATNS 1
#define ACT_SIZES {POKER_ACTIONS}

#define POKER_PANEL_COUNT 6
struct Log {
    float perf, score, episode_return, episode_length, n;
    // Card sense: the learner's first preflop decision by hand class (premium TT+/AK/AQ; trash = unsuited,
    // unpaired, J-high or lower with a gap of 3+). A card-blind policy (gen0) folds and raises both alike.
    float premium, premium_fold, premium_raise, trash, trash_fold, trash_raise;
    // Panel tables (env.panel_fraction): BB won and hands per opponent type (POKER_PANEL order).
    float panel_score[POKER_PANEL_COUNT], panel_hands[POKER_PANEL_COUNT];
    // Hands started from a state set (env.state_file): BB won from the restore point. Kept out of score, perf and n.
    float state_score, state_hands;
    // Raise sizing (2026-10-29): learner decisions, raises among them, and all-in raises among those. gen3/gen4 collapsed
    // onto fold / call / all in (escalation.c: 0.1% of unopened decisions were sized raises), so watch it in training.
    float decisions, raises, allin_raises;
    // Style (2026-10-30, the sweep metric poker_score): re-raise wars (a street with 3+ raises) among league hands
    // (neither panel nor state-set hands), and how often the learner folds preflop when it owes chips and nobody has
    // raised. gen8 at a table of its own copies: wars 30%, those folds 0.1% (it plays 90% of hands).
    float league_hands, wars, open_spots, open_folds;
    // BB won at league tables (gen11, 2026-11-04): with [score_eval] (below) the sweep trial's final eval seats only
    // fixed frozen models there, so this is the model-league part of poker_score.
    float league_score;
    // Rake (2026-11-04, env.rake): BB the house took per learner hand (whole table's pot, not only the learner's share).
    float rake_bb;
};

// Panel opponent types (archetypes.h): one per panel table, all five opponent seats alike. Trapper joined in gen4.
static const int POKER_PANEL[POKER_PANEL_COUNT] = {1, 7, 8, 9, 10, 11};
static const char* POKER_PANEL_NAMES[POKER_PANEL_COUNT] = {"rules", "range", "maniac", "station", "tight", "trapper"};

// 0 premium, 1 medium, 2 trash (same classes as poker_game tools/policy_check.c).
static int poker_hand_class(uint8_t a, uint8_t b) {
    int r1 = a >> 2, r2 = b >> 2, hi = r1 > r2 ? r1 : r2, lo = r1 > r2 ? r2 : r1;
    if (r1 == r2) return hi >= 8 ? 0 : 1;
    if (hi == 12 && lo >= 10) return 0;
    if ((a & 3) != (b & 3) || hi > 9 || hi - lo < 3) return 1;
    return 2;
}

typedef struct Client { int unused; } Client;

// Shared PufferNet snapshots. Same fp32 checkpoint layout and MinGRU equations as src/puffercpu.c. No
// per-decision allocation or search.
// Live pool (env.league_pool_dir, GPU league only): one continuous run that adds its own checkpoints to the
// opponent pool as the trainer writes them (Robocode-style historical self-play, no stints). The manifest may then
// start with 0 banks (opponent seats are bots until the first snapshot); up to POKER_LEAGUE_MAX snapshots are kept,
// after which each new one replaces the oldest.
#define POKER_LEAGUE_MAX 128
typedef struct PokerLeague {
    float* weights;
    int count, hidden, layers, stride;
    char pool_dir[1024];     // live pool: directory of this run's checkpoints ("" = fixed bank)
    int loaded;              // live pool: snapshots loaded so far (slot = loaded % POKER_LEAGUE_MAX)
    char last[256];          // live pool: newest checkpoint name loaded
    double polled;           // live pool: time of the last directory scan
    // Live pool: opponents are drawn from an active subset of the pool (env.league_active, resampled at every
    // scan, always including the newest snapshot), so each round runs a few banks' GEMMs, not up to 128.
    int active[POKER_LEAGUE_MAX], active_count, active_max;
    // Live pool ramp (env.league_ramp snapshots, 0 = off): the bot share of opponent seats starts at 1 and falls
    // linearly to env.rules_fraction as the pool reaches that many snapshots, so early training is mostly bots.
    int ramp;
    uint64_t active_rng;
    // PFSP (2026-11-04, env.league_pfsp = scale in BB, 0 = off; live pool only): the learner's BB won at league tables
    // holding each bank (centi-BB and hands, atomics from every env) are folded into decayed per-bank sums at each
    // resample. p = sigmoid(mean BB/hand / scale) is the learner's edge over that bank; the active subset and each
    // model seat are drawn with weight (1 - p)^2 (AlphaStar), so the banks we lose to come up most. env.league_newest
    // of model seats always take the newest snapshot (self-play); the rest are PFSP draws.
    float pfsp_scale, newest_share;
    long long pfsp_centi[POKER_LEAGUE_MAX], pfsp_hands[POKER_LEAGUE_MAX];
    double pfsp_bb[POKER_LEAGUE_MAX], pfsp_n[POKER_LEAGUE_MAX];
    float active_cdf[POKER_LEAGUE_MAX];  // cumulative seat weights over active[1..active_count)
    int resamples;
    int total_envs;          // vectorized envs sharing this bank (GPU recurrent-state slots)
    float* device_weights;   // env.league_gpu: weights and per-(env, seat) MinGRU state on the GPU
    float* device_state;
} PokerLeague;

// One bank checkpoint into a slot (stride floats, POKER_PUB inputs; policy.h poker_policy_read). Returns its input count.
static int poker_read_bank(const char* path, float* dst, int stride, int h) {
    int layers = (int)((stride - (long)h * (POKER_PUB + POKER_ACTIONS + 1)) / (3L * h * h));
    return poker_policy_read(path, dst, h, layers);
}

PokerLeague* poker_load_league(Dict* kwargs) {
    const char* path = dict_get_str(kwargs, "native_league");
    if (!strcmp(path, "None")) {
        return NULL;
    }
    FILE* manifest = fopen(path, "r");
    assert(manifest);
    PokerLeague* bank = (PokerLeague*)calloc(1, sizeof(PokerLeague));
    int players;
    char line[4096];
    char* read = fgets(line, sizeof(line), manifest);
    assert(read);
    int fields = sscanf(line, "poker_v1 %d %d %d %d", &players, &bank->hidden,
        &bank->layers, &bank->count);
    assert(fields == 4 && players == POKER_PLAYERS);
    assert(bank->hidden > 0 && bank->hidden <= POKER_POLICY_MAX_HIDDEN && bank->hidden % 8 == 0);
    DictItem* pool = dict_find(kwargs, "league_pool_dir");
    if (pool && pool->str && *pool->str && strcmp(pool->str, "None")) {
        assert(strlen(pool->str) < sizeof(bank->pool_dir));
        // "auto": this run's own checkpoint directory (each sweep trial plays its own snapshots); the trainer publishes it.
        const char* dir = strcmp(pool->str, "auto") ? pool->str : getenv("PUF_CHECKPOINT_DIR");
        assert(dir && "env.league_pool_dir = auto needs PUF_CHECKPOINT_DIR from the trainer");
        snprintf(bank->pool_dir, sizeof(bank->pool_dir), "%s", dir);
    }
    assert(bank->layers > 0 && bank->layers <= 4 && bank->count >= (bank->pool_dir[0] ? 0 : 1)
        && bank->count <= POKER_LEAGUE_MAX);
    int h = bank->hidden;
    bank->stride = h * (POKER_PUB + POKER_ACTIONS + 1) + bank->layers * 3 * h * h;
    bank->weights = (float*)malloc((size_t)POKER_LEAGUE_MAX * bank->stride * sizeof(float));
    bank->loaded = bank->count;
    DictItem* active = dict_find(kwargs, "league_active");
    bank->active_max = active && active->value > 0 ? (int)active->value : POKER_LEAGUE_MAX;
    DictItem* ramp = dict_find(kwargs, "league_ramp");
    bank->ramp = ramp && ramp->value > 0 ? (int)ramp->value : 0;
    bank->active_rng = 0x9e3779b97f4a7c15ull;
    DictItem* pfsp = dict_find(kwargs, "league_pfsp");
    bank->pfsp_scale = pfsp && pfsp->value > 0 ? pfsp->value : 0;
    DictItem* newest = dict_find(kwargs, "league_newest");
    bank->newest_share = newest && newest->value > 0 ? newest->value : 0;
    assert(bank->newest_share <= 1);
    for (int i = 0; i < bank->count && i < bank->active_max; i++) {
        bank->active[i] = i;
        bank->active_cdf[i] = i + 1;
    }
    bank->active_count = bank->count < bank->active_max ? bank->count : bank->active_max;
    for (int i = 0; i < bank->count; i++) {
        read = fgets(line, sizeof(line), manifest);
        assert(read);
        line[strcspn(line, "\r\n")] = 0;
        int ok = poker_read_bank(line, bank->weights + (size_t)i * bank->stride, bank->stride, h);
        if (!ok) fprintf(stderr, "poker league: %s is not an H%d/L%d checkpoint\n", line, h, bank->layers);
        assert(ok);
        printf("Poker league bank %d: %s (%d inputs)\n", i, line, ok);
    }
    fclose(manifest);
    printf("Poker league: %d frozen H%d/L%d banks, %zu weight bytes%s%s\n",
        bank->count, h, bank->layers, bank->count * bank->stride * sizeof(float),
        bank->pool_dir[0] ? "; live pool from " : "", bank->pool_dir);
    return bank;
}

// PFSP weight of one bank from its decayed record (50 hands of prior at 0 BB/hand; 1 for every bank when PFSP is off).
static float poker_pfsp_weight(PokerLeague* bank, int slot) {
    if (bank->pfsp_scale <= 0) return 1;
    double mean = bank->pfsp_bb[slot] / (bank->pfsp_n[slot] + 50);
    double p = 1 / (1 + exp(-mean / bank->pfsp_scale));
    return (float)((1 - p) * (1 - p)) + 0.01f;
}

// Live pool: a new active subset: the newest snapshot plus a sample of the rest without replacement (uniform, or by
// PFSP weight), and the seat weights over it. Published by one store of active_count after the indices (readers may
// briefly mix old and new; both are valid).
static void poker_league_resample(PokerLeague* bank, int newest) {
    int pool = bank->count, k = pool < bank->active_max ? pool : bank->active_max;
    for (int i = 0; i < pool; i++) {
        long long centi = __atomic_exchange_n(&bank->pfsp_centi[i], 0, __ATOMIC_RELAXED);
        long long hands = __atomic_exchange_n(&bank->pfsp_hands[i], 0, __ATOMIC_RELAXED);
        bank->pfsp_bb[i] = bank->pfsp_bb[i] * 0.9 + centi / 100.0;  // ~200 s memory at one resample per 20 s
        bank->pfsp_n[i] = bank->pfsp_n[i] * 0.9 + hands;
    }
    int order[POKER_LEAGUE_MAX];
    float weight[POKER_LEAGUE_MAX];
    for (int i = 0; i < pool; i++) { order[i] = i; weight[i] = poker_pfsp_weight(bank, i); }
    order[newest] = 0; order[0] = newest;
    for (int i = 1; i < k; i++) {
        float total = 0;
        for (int j = i; j < pool; j++) total += weight[order[j]];
        float u = (poker_random(&bank->active_rng) >> 8) * 0x1p-24f * total;
        int j = i;
        for (; j < pool - 1; j++) { u -= weight[order[j]]; if (u < 0) break; }
        int x = order[i]; order[i] = order[j]; order[j] = x;
    }
    float cdf = 0;
    for (int i = 0; i < k; i++) {
        bank->active[i] = order[i];
        if (i + 1 < k) bank->active_cdf[i] = cdf += weight[order[i + 1]];
    }
    __atomic_store_n(&bank->active_count, k, __ATOMIC_RELEASE);
    if (bank->pfsp_scale > 0 && bank->resamples++ % 15 == 0) {
        // stderr: a sweep sends trial stdout to /dev/null but keeps stderr in logs/<env>/<run_id>.err while it runs.
        fprintf(stderr, "PFSP %ld (BB/hand over hands, weight):", (long)time(NULL));
        for (int i = 0; i < pool; i++)
            fprintf(stderr, " %d:%+.2f/%.0f,%.2f", i, bank->pfsp_bb[i] / (bank->pfsp_n[i] + 1e-9), bank->pfsp_n[i],
                weight[i]);
        fprintf(stderr, "\n");
    }
}

// One model seat's bank: the newest snapshot with probability newest_share, else a draw over the other active banks
// by seat weight (active_cdf[i] = total weight of active[1..i+1)).
static int poker_league_pick(PokerLeague* bank, int banks, uint64_t* rng) {
    if (banks <= 1 || (poker_random(rng) >> 8) * 0x1p-24f < bank->newest_share) return bank->active[0];
    float u = (poker_random(rng) >> 8) * 0x1p-24f * bank->active_cdf[banks - 2];
    for (int i = 1; i < banks - 1; i++) if (u < bank->active_cdf[i - 1]) return bank->active[i];
    return bank->active[banks - 1];
}

// Live pool: the next new checkpoint in pool_dir (name order = step order) that is complete (exact size, unchanged
// for 5 s), loaded into host slot *slot. Returns 1 when one was loaded. Caller holds the league lock.
static int poker_league_poll(PokerLeague* bank, int* slot) {
    double t = (double)time(NULL);
    if (!bank->pool_dir[0] || t - bank->polled < 20) return 0;
    bank->polled = t;
    DIR* dir = opendir(bank->pool_dir);
    if (!dir) return 0;
    char best[256] = "";
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        const char* name = entry->d_name;
        size_t n = strlen(name);
        if (n < 5 || n >= sizeof(best) || strcmp(name + n - 4, ".bin")) continue;
        if (strcmp(name, bank->last) <= 0 || (best[0] && strcmp(name, best) >= 0)) continue;
        snprintf(best, sizeof(best), "%s", name);
    }
    closedir(dir);
    if (!best[0]) return 0;
    char path[1400];
    snprintf(path, sizeof(path), "%s/%s", bank->pool_dir, best);
    struct stat st;
    if (stat(path, &st) || time(NULL) - st.st_mtime < 5) return 0;
    *slot = bank->loaded % POKER_LEAGUE_MAX;
    if (!poker_read_bank(path, bank->weights + (size_t)*slot * bank->stride, bank->stride, bank->hidden)) return 0;
    snprintf(bank->last, sizeof(bank->last), "%s", best);
    bank->pfsp_bb[*slot] = bank->pfsp_n[*slot] = 0;
    __atomic_store_n(&bank->pfsp_centi[*slot], 0, __ATOMIC_RELAXED);
    __atomic_store_n(&bank->pfsp_hands[*slot], 0, __ATOMIC_RELAXED);
    bank->loaded++;
    printf("Poker live pool: %s -> slot %d (%d snapshots)\n", best, *slot,
        bank->loaded < POKER_LEAGUE_MAX ? bank->loaded : POKER_LEAGUE_MAX);
    return 1;
}

void poker_linear(float* out, const float* x, const float* weights, int in, int n) {
    for (int o = 0; o < n; o++) {
        float sum = 0;
        int i = 0;
#if defined(__AVX2__)
        __m256 acc = _mm256_setzero_ps();
        for (; i + 8 <= in; i += 8) {
            acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_loadu_ps(x + i),
                _mm256_loadu_ps(weights + o * in + i)));
        }
        float lanes[8];
        _mm256_storeu_ps(lanes, acc);
        for (int k = 0; k < 8; k++) {
            sum += lanes[k];
        }
#endif
        for (; i < in; i++) {
            sum += x[i] * weights[o * in + i];
        }
        out[o] = sum;
    }
}

void poker_forward(PokerLeague* bank, int policy, float* state,
    const float* observations, float* logits) {
    int h = bank->hidden;
    float x[POKER_POLICY_MAX_HIDDEN], projected[3 * POKER_POLICY_MAX_HIDDEN];
    const float* weights = bank->weights + policy * bank->stride;
    poker_linear(x, observations, weights, POKER_PUB, h);
    const float* recurrent = weights + h * (POKER_PUB + POKER_ACTIONS + 1);
    for (int l = 0; l < bank->layers; l++) {
        poker_linear(projected, x, recurrent + l * 3 * h * h, h, 3 * h);
        for (int i = 0; i < h; i++) {
            float value = projected[i] >= 0 ? projected[i] + 0.5f
                : 1.0f / (1.0f + expf(-projected[i]));
            float gate = 1.0f / (1.0f + expf(-projected[h + i]));
            float highway = 1.0f / (1.0f + expf(-projected[2 * h + i]));
            float s = state[l * h + i];
            s += gate * (value - s);
            state[l * h + i] = s;
            x[i] = highway * s + (1 - highway) * x[i];
        }
    }
    poker_linear(logits, x, weights + h * POKER_PUB, h, POKER_ACTIONS + 1);
}

// One fixed learner seat; only acting opponents run inference. Each seat owns
// recurrent state, and each hand samples a field held fixed until settlement.
struct Env {
    Poker* game;
    PokerLeague* league;
    float* memories;
    int index;              // global env index (GPU state slot)
    uint8_t league_gpu;     // 1: frozen bank seats wait for one batched GPU forward per round
    uint8_t pending;        // waiting for a GPU decision of seat pending_seat
    uint8_t pending_seat;
    uint8_t stage;          // POKER_STAGE_*: what to do once the field reaches the learner or the hand ends
    uint8_t fresh;          // bit p: seat p's GPU recurrent state must be zeroed before its next forward
    PokerProfile* profiles;
    float rules_fraction;
    uint64_t policy_rng;
    uint32_t starting_stack;
    // Cash sessions (env.session_hands > 0): stacks carry over between hands for that many hands, then every seat sits
    // down fresh. A seat's starting depth is uniform in [stack_min_bb, stack_max_bb] big blinds, drawn at each session
    // start, after a bust (rebuy) and when a stack passes stack_cap_bb (it leaves with its winnings and a new player sits).
    // The reward stays per hand in BB. Everything the policy sees is already in big blinds (chips / 100), so blind size
    // needs no randomizing; stack depth is what varies. 0 keeps the old fresh starting_stack every hand.
    int session_hands, session_left, stack_min_bb, stack_max_bb, stack_cap_bb;
    // State sets (env.state_file, states.h): env.state_fraction of non-panel hands start from a stored mid-hand state
    // with the learner to act. The reward counts from the restore point (chips already in the pot are sunk).
    PokerStateSet* states;
    float state_fraction;
    uint32_t reward_base;   // learner stack the hand's reward is measured from
    uint8_t from_state;     // the current hand started from a state
    // env.reward_scale (2026-10-29): the training reward is BB won times this (logs stay in BB). The trainer does not
    // normalize advantages, so at 1 they are raw BB (tens to hundreds) and ent_coef 0.002 is negligible: entropy fell
    // to ~0.04 within 100M steps and the policy kept only fold / call / all in. 0.01 trains in units of 100 BB.
    float reward_scale;
    int critic_hide_cards;  // POKER_ZS_CRITIC ablation: the critic does not see opponents' hole cards
    unsigned int rng;
    int lane, num_agents, tag, boundary_reached, bot_policy, equity_samples;
    unsigned baseline_mask;
    int8_t opponents[POKER_PLAYERS];
    uint8_t baseline_styles[POKER_PLAYERS];
    uint8_t league_owner;
    uint8_t first_decision;  // the learner has not acted yet this hand
    uint8_t war;             // some street of this hand reached 3 raises
    int8_t panel;            // panel table: index into POKER_PANEL (all opponents that bot), -1 otherwise
    Agent agents[1];
    Log log;
    Client* client;
};

void puf_init(Env* env, Dict* kwargs) {
    if (!env->game) {
        env->game = (Poker*)calloc(1, sizeof(Poker));
        env->league = poker_load_league(kwargs);
        env->league_owner = 1;
    }
    int seed = dict_get(kwargs, "seed");
    env->game->rng[env->lane] = ((uint64_t)(unsigned)seed << 32) + env->rng + 1;
    env->policy_rng = env->game->rng[env->lane] ^ UINT64_C(0xd1b54a32d192ed03);
    env->starting_stack = dict_get(kwargs, "starting_stack");
    DictItem* session = dict_find(kwargs, "session_hands");
    env->session_hands = session ? (int)session->value : 0;
    if (env->session_hands > 0) {
        env->stack_min_bb = (int)dict_get(kwargs, "stack_min_bb");
        env->stack_max_bb = (int)dict_get(kwargs, "stack_max_bb");
        env->stack_cap_bb = (int)dict_get(kwargs, "stack_cap_bb");
        assert(env->stack_min_bb >= 2 && env->stack_min_bb <= env->stack_max_bb);
        assert(env->stack_cap_bb >= env->stack_max_bb && env->stack_cap_bb * POKER_BB <= 60000);  // PokerAmount is 16-bit
        env->session_left = 0;
    }
    // Rake (2026-11-04): env.rake of the contested pot (0.05 = 5%), at most env.rake_cap_bb (0 = no cap); no flop, no
    // drop unless env.rake_preflop. The learner's reward already pays it: its stack only gets what the pot paid out.
    DictItem* rake = dict_find(kwargs, "rake");
    if (rake) {
        assert(rake->value >= 0 && rake->value < 0.5);
        env->game->rake_permille = (uint16_t)(rake->value * 1000 + 0.5);
        env->game->rake_cap = (PokerAmount)(dict_get(kwargs, "rake_cap_bb") * POKER_BB);
        env->game->rake_preflop = dict_get(kwargs, "rake_preflop") != 0;
    }
    DictItem* hide = dict_find(kwargs, "critic_hide_cards");
    env->critic_hide_cards = hide ? hide->value != 0 : 0;
    DictItem* scale = dict_find(kwargs, "reward_scale");
    env->reward_scale = scale ? scale->value : 1;
    assert(env->reward_scale > 0);
    env->bot_policy = dict_get(kwargs, "bot_policy");
    env->rules_fraction = dict_get(kwargs, "rules_fraction");
    assert(env->starting_stack >= POKER_BB && env->starting_stack <= 65535);
    assert((int)dict_get(kwargs, "num_players") == POKER_PLAYERS);
    assert(poker_bot_supported(env->bot_policy));
    DictItem* samples = dict_find(kwargs, "equity_samples");
    env->equity_samples = samples ? (int)samples->value : 0;
    assert(env->equity_samples == 0 || env->equity_samples == 32
        || env->equity_samples == 64 || env->equity_samples == 128);
    DictItem* mix = dict_find(kwargs, "native_baseline_mask");
    env->baseline_mask = mix ? (unsigned)mix->value : 0;
    assert(!(env->baseline_mask & ~POKER_BOT_MASK));
    DictItem* history = dict_find(kwargs, "native_history");
    if (history && history->value)
        env->profiles = (PokerProfile*)calloc(POKER_PLAYERS, sizeof(PokerProfile));
    for (int p = 1; p < POKER_PLAYERS; p++) {
        env->baseline_styles[p] = env->bot_policy;
        if (env->baseline_mask) {
            unsigned mask = env->baseline_mask;
            int pick = poker_random(&env->policy_rng) % __builtin_popcount(mask);
            while (pick--) mask &= mask - 1;
            env->baseline_styles[p] = __builtin_ctz(mask);
        }
    }
    assert(env->rules_fraction >= 0 && env->rules_fraction <= 1);
    // Panel tables (gen2): env.panel_fraction of the tables seat five copies of one panel bot (cycling Rules, Range,
    // maniac, station, tight, trapper by env index) and never a league snapshot; their results log per type and as panel_worst.
    DictItem* panel = dict_find(kwargs, "panel_fraction");
    env->panel = -1;
    if (panel && panel->value > 0) {
        uint64_t mix = (uint64_t)env->rng * UINT64_C(0x9e3779b97f4a7c15);
        if ((mix >> 40) * 0x1p-24 < panel->value) {
            env->panel = (int8_t)((uint64_t)env->rng % POKER_PANEL_COUNT);
            for (int p = 1; p < POKER_PLAYERS; p++) env->baseline_styles[p] = POKER_PANEL[env->panel];
        }
    }
    DictItem* state_file = dict_find(kwargs, "state_file");
    if (state_file && state_file->str && *state_file->str && strcmp(state_file->str, "None")) {
        static PokerStateSet* loaded;  // one copy shared by every env
        static char loaded_path[1024];
        if (!loaded || strcmp(loaded_path, state_file->str)) {
            loaded = poker_state_load(state_file->str);
            snprintf(loaded_path, sizeof(loaded_path), "%s", state_file->str);
            if (env->index == 0) printf("Poker states: %zu from %s\n", loaded->count, loaded_path);
        }
        env->states = loaded;
        env->state_fraction = dict_get(kwargs, "state_fraction");
        assert(env->state_fraction >= 0 && env->state_fraction <= 1);
    }
    if (env->league) {
        env->memories = (float*)calloc((POKER_PLAYERS - 1) * env->league->hidden
            * env->league->layers, sizeof(float));
    }
    DictItem* gpu = dict_find(kwargs, "league_gpu");
#ifdef __CUDACC__
    env->league_gpu = env->league && env->league->total_envs > 0 && gpu && gpu->value != 0;
    assert((!env->league || !env->league->pool_dir[0] || env->league_gpu) && "env.league_pool_dir needs env.league_gpu=1");
#else
    env->league_gpu = 0;  // CPU-only builds always run frozen banks inline
    (void)gpu;
#endif
    env->num_agents = 1;
    env->agents[0].policy = 0;
}

// Sweep score eval (gen11, 2026-11-04): a sweep trial's final eval (mode "eval" with the trainer's result_fd, so
// manual ./puffer eval runs are untouched) applies the [score_eval] section as env overrides, e.g. a fixed manifest of
// frozen models with no live pool and no bots at league tables. Then poker_score's league_bb is BB/hand against those
// models, the same opponents for every trial.
static void poker_league_gpu_free(PokerLeague* bank);
static PokerLeague* poker_trained_leagues[8];  // leagues made by my_vec_init, for poker_configure to release
static int poker_trained_league_count;

static void poker_configure(Ini* ini, const char* mode) {
    if (strcmp(mode, "eval") || puf_ini_get(ini, "base", "result_fd") <= 0) return;
    // Training is over (the trainer closed it before this eval) but keeps its memory, and the training league's GPU
    // weights and recurrent state (4.6 GB at 32,768 agents with a 128-slot pool) left the score eval out of memory.
    for (int i = 0; i < poker_trained_league_count; i++) {
        poker_league_gpu_free(poker_trained_leagues[i]);
        poker_trained_leagues[i]->device_weights = poker_trained_leagues[i]->device_state = NULL;
    }
    poker_trained_league_count = 0;
    // The score league uploads its banks on its first batched forward, inside the eval's first rollout; with CUDA
    // graphs on, that cudaMalloc lands in the graph capture and cudaGraphInstantiate fails. The eval is short.
    puf_ini_put(ini, "base.cudagraphs", "-1");  // the trainer reads >= 0 as on
    Dict* over = puf_ini_section(ini, "score_eval", 1);  // an empty section when the config has none
    for (int i = 0; i < over->size; i++) {
        char key[PUF_DICT_MAX_KEY + 8];
        snprintf(key, sizeof(key), "env.%s", over->items[i].key);
        puf_ini_put(ini, key, over->items[i].str);
        printf("score_eval: %s = %s\n", key, over->items[i].str);
    }
}
#define PUF_CONFIGURE(ini, mode) poker_configure(ini, mode)

// OpenMP threads for the league loops below: the trainer's workers per buffer (vec.num_threads / num_buffers), like the
// trainer's own loops. Without it OpenMP uses every CPU the container reports: on a Vast box with 256 visible CPUs and
// a 24.6-CPU quota, league training ran at 11.6K steps/s instead of 321K (2026-10-29).
static int poker_omp_threads = 1;
// env.league_tf32 (2026-11-04): frozen bank GEMMs on TF32 tensor cores (GPU league). With 85% model seats at 1024x2,
// the fp32 league forwards made training 5x slower than bots-only (135K vs 722K SPS on the 5070 Ti). Frozen opponents
// do not need fp32-exact logits; 0 keeps them bit-comparable with the CPU path (test_league_gpu).
static int poker_league_tf32 = 0;

#define MY_VEC_INIT
Env* my_vec_init(int* size, int* starts, int* counts, Dict* vk, Dict* ek) {
    int total = dict_get(vk, "total_agents"), buffers = dict_get(vk, "num_buffers");
    poker_omp_threads = (int)dict_get(vk, "num_threads") / buffers;
    DictItem* tf32 = dict_find(ek, "league_tf32");
    poker_league_tf32 = tf32 && tf32->value != 0;
    if (poker_omp_threads < 1) poker_omp_threads = 1;
    assert(total > 0 && buffers > 0 && total % buffers == 0);
    assert((int)dict_get(vk, "num_policies") == 1);
    Env* envs = (Env*)calloc(total, sizeof(Env));
    PokerLeague* league = poker_load_league(ek);
    if (league) {
        league->total_envs = total;
        if (poker_trained_league_count < 8) poker_trained_leagues[poker_trained_league_count++] = league;
    }
    for (int b = 0; b < buffers; b++) {
        starts[b] = b * (total / buffers);
        counts[b] = total / buffers;
        Poker* block = NULL;
        for (int i = 0; i < counts[b]; i++) {
            if (i % POKER_BATCH == 0) {
                block = (Poker*)calloc(1, sizeof(Poker));
            }
            Env* env = envs + starts[b] + i;
            env->game = block;
            env->league = league;
            env->league_owner = starts[b] + i == 0;
            env->lane = i % POKER_BATCH;
            env->rng = starts[b] + i;
            env->index = starts[b] + i;
            puf_init(env, ek);
        }
    }
    *size = total;
    return envs;
}

enum { POKER_STAGE_STEP, POKER_STAGE_DEAL };

// Sample a frozen bank seat's action from its logits (same masked softmax and RNG draw as before).
static int32_t poker_league_action(Env* env, float* logits) {
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(env->game, env->lane, amounts);
    float maximum = -INFINITY, total = 0;
    for (int a = 0; a < POKER_ACTIONS; a++) {
        if ((mask & (1u << a)) && logits[a] > maximum) {
            maximum = logits[a];
        }
    }
    for (int a = 0; a < POKER_ACTIONS; a++) {
        logits[a] = mask & (1u << a) ? expf(logits[a] - maximum) : 0;
        total += logits[a];
    }
    float sample = (poker_random(&env->policy_rng) >> 8) * 0x1p-24f * total;
    int selected = 31 - __builtin_clz(mask);
    for (int a = 0; a < POKER_ACTIONS; a++) {
        sample -= logits[a];
        if (sample < 0) {
            selected = a;
            break;
        }
    }
    return amounts[selected];
}

static void poker_act(Env* env, int32_t action) {
    poker_profile_update(env->profiles, env->game, env->lane, action);
    poker_step(env->game, env->lane, action);
    env->war |= env->game->raises[env->lane] >= 3;
}

// Plays opponent seats until the learner acts or the hand ends. Returns 1 when a frozen bank seat
// needs a batched GPU decision (league_gpu); CPU mode runs bank seats inline and always returns 0.
static int poker_field(Env* env) {
    Poker* g = env->game;
    int t = env->lane;
    while (g->actor[t] != 0 && g->actor[t] != POKER_DONE) {
        int p = g->actor[t], policy = env->opponents[p];
        if (policy < 0) {
            poker_act(env, poker_decide(g, t, &env->policy_rng, env->baseline_styles[p],
                env->equity_samples, env->profiles));
        } else if (env->league_gpu) {
            env->pending_seat = p;
            return 1;
        } else {
            float obs[POKER_PUB], logits[POKER_ACTIONS + 1];
            poker_observe_public(g, t, p, obs);
            poker_forward(env->league, policy, env->memories + (p - 1)
                * env->league->hidden * env->league->layers, obs, logits);
            poker_act(env, poker_league_action(env, logits));
        }
    }
    return 0;
}

static uint32_t poker_sit_down(Env* env) {
    int span = env->stack_max_bb - env->stack_min_bb + 1;
    return (uint32_t)(env->stack_min_bb + (int)(poker_random(&env->policy_rng) % (uint64_t)span)) * POKER_BB;
}

static void poker_deal_hand(Env* env) {
    uint32_t stacks[POKER_PLAYERS];
    if (env->session_hands > 0) {
        Poker* g = env->game;
        int fresh = env->session_left <= 0;
        if (fresh) env->session_left = env->session_hands;
        env->session_left--;
        for (int p = 0; p < POKER_PLAYERS; p++) {
            uint32_t s = fresh ? 0 : g->stack[p][env->lane];
            if (fresh || s < POKER_BB || s > (uint32_t)env->stack_cap_bb * POKER_BB) s = poker_sit_down(env);
            stacks[p] = s;
        }
    } else {
        for (int p = 0; p < POKER_PLAYERS; p++) {
            stacks[p] = env->starting_stack;
        }
    }
    if (env->league) {
        if (env->league_gpu) {
            env->fresh = 0x3e;  // seats 1..5
        } else {
            memset(env->memories, 0, (POKER_PLAYERS - 1) * env->league->hidden
                * env->league->layers * sizeof(float));
        }
    }
    for (int p = 1; p < POKER_PLAYERS; p++) {
        env->opponents[p] = -1;
        int banks = env->league ? __atomic_load_n(&env->league->active_count, __ATOMIC_ACQUIRE) : 0;
        float bots = env->rules_fraction;
        if (env->league && env->league->ramp) {
            float ramped = 1.0f - (float)__atomic_load_n(&env->league->count, __ATOMIC_ACQUIRE) / env->league->ramp;
            if (ramped > bots) bots = ramped;
        }
        if (env->league && env->panel < 0 && (poker_random(&env->policy_rng) >> 8) * 0x1p-24f
            >= bots && banks) {
            env->opponents[p] = poker_league_pick(env->league, banks, &env->policy_rng);
        }
    }
    Poker* g = env->game;
    env->from_state = env->states && env->panel < 0
        && (poker_random(&env->policy_rng) >> 8) * 0x1p-24f < env->state_fraction;
    if (env->from_state) {
        poker_state_restore(g, env->lane, &env->states->states[poker_random(&env->policy_rng) % env->states->count]);
        env->reward_base = g->stack[0][env->lane];
        env->first_decision = 0;
        poker_profile_begin(env->profiles);
        return;
    }
    poker_deal(g, env->lane, (g->button[env->lane] + 1) % POKER_PLAYERS, stacks);
    env->reward_base = g->initial[0][env->lane];
    env->war = 0;
    env->first_decision = 1;
    poker_profile_begin(env->profiles);
}

void poker_publish(Env* env);
float poker_log_hand(Env* env);

// Advances to the learner's next decision: after a learner action (STEP) settle a finished hand and
// deal the next; after a deal (DEAL) skip hands that end before the learner acts. Returns 1 while a
// bank seat waits for a GPU decision; otherwise publishes the learner's observation and returns 0.
static int poker_run(Env* env) {
    Poker* g = env->game;
    int t = env->lane;
    for (;;) {
        if (poker_field(env)) {
            return 1;
        }
        if (env->stage == POKER_STAGE_STEP) {
            env->agents[0].terminals[0] = g->actor[t] == POKER_DONE;
            if (!env->agents[0].terminals[0]) {
                break;
            }
            env->agents[0].rewards[0] = poker_log_hand(env) * env->reward_scale;
        } else if (g->actor[t] != POKER_DONE) {
            break;
        } else {
            // No learner decision in this hand: retain its result in raw game logs.
            poker_log_hand(env);
        }
        poker_deal_hand(env);
        env->stage = POKER_STAGE_DEAL;
    }
    poker_publish(env);
    return 0;
}

float poker_log_hand(Env* env) {
    Poker* g = env->game;
    int t = env->lane;
    float reward = ((int32_t)g->stack[0][t] - (int32_t)env->reward_base) / 100.0f;
    if (env->from_state) {
        env->log.state_score += reward;
        env->log.state_hands += 1;
        return reward;
    }
    env->log.perf += reward;
    env->log.score += reward;
    env->log.episode_return += reward;
    env->log.episode_length += g->decisions[t];
    env->log.rake_bb += g->raked[t] / 100.0f;
    if (env->panel >= 0) {
        env->log.panel_score[env->panel] += reward;
        env->log.panel_hands[env->panel] += 1;
    } else {
        env->log.league_hands += 1;
        env->log.league_score += reward;
        env->log.wars += env->war;
        PokerLeague* bank = env->league;
        if (bank && bank->pfsp_scale > 0) {  // PFSP record: this hand counts once for each bank at the table
            long long centi = llroundf(reward * 100);
            for (int p = 1; p < POKER_PLAYERS; p++) {
                int k = env->opponents[p], seen = 0;
                for (int q = 1; q < p; q++) seen |= env->opponents[q] == k;
                if (k < 0 || seen) continue;
                __atomic_fetch_add(&bank->pfsp_centi[k], centi, __ATOMIC_RELAXED);
                __atomic_fetch_add(&bank->pfsp_hands[k], 1, __ATOMIC_RELAXED);
            }
        }
    }
    env->log.n++;
    return reward;
}

#ifdef POKER_ZS_CRITIC
static inline void poker_card_onehot(float* f, uint8_t c) {
    if (c >= 52) return;
    f[c >> 2] = 1;
    f[13 + (c & 3)] = 1;
}
// The critic's privileged block (seat 0 is the learner). Chips are in reward units (BB x env.reward_scale), so the
// critic's exact terms (a folded seat's value is minus what it put in) need no scale of their own. The last value is
// the learner's reward offset: rewards count from the restore point in state-set hands (chips already in are sunk).
static_assert(42 + POKER_PANEL_COUNT == POKER_SEAT_PRIV, "seat block layout");
static void poker_privileged(Env* env, float* f) {
    Poker* g = env->game;
    int t = env->lane;
    float chip = env->reward_scale / 100.0f;
    memset(f, 0, POKER_PRIV * sizeof(float));
    for (int p = 0; p < POKER_PLAYERS; p++) {
        float* s = f + p * POKER_SEAT_PRIV;
        if (p == 0 || !env->critic_hide_cards) {  // env.critic_hide_cards: ablation, opponents' cards hidden
            poker_card_onehot(s, g->hole[p][0][t]);
            poker_card_onehot(s + 17, g->hole[p][1][t]);
        }
        s[34] = (g->live[t] >> p) & 1;
        s[35] = g->total[p][t] * chip;
        s[36] = g->stack[p][t] / 10000.0f;
        s[37] = p == g->button[t];
        s[38] = g->total[p][t] > 0 || ((g->live[t] >> p) & 1);
        // Who sits here (2026-11-04): the learner, a frozen model, or a bot and its panel type.
        int bot = p > 0 && env->opponents[p] < 0;
        s[39] = p == 0;
        s[40] = p > 0 && !bot;
        s[41] = bot;
        for (int k = 0; bot && k < POKER_PANEL_COUNT; k++) s[42 + k] = env->baseline_styles[p] == POKER_PANEL[k];
    }
    float* b = f + POKER_PLAYERS * POKER_SEAT_PRIV;
    int dealt = g->street[t] ? g->street[t] + 2 : 0;
    for (int i = 0; i < dealt; i++) poker_card_onehot(b + 17 * i, g->board[i][t]);
    b[85 + (g->street[t] < 4 ? g->street[t] : 3)] = 1;
    b[89] = ((int32_t)g->initial[0][t] - (int32_t)env->reward_base) * chip;
}
#endif

void poker_publish(Env* env) {
    poker_observe_public(env->game, env->lane, 0, env->agents[0].observations);
#ifdef POKER_ZS_CRITIC
    poker_privileged(env, env->agents[0].observations + POKER_PUB);
#endif
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(env->game, env->lane, amounts);
    for (int a = 0; a < POKER_ACTIONS; a++) {
        env->agents[0].action_mask[a] = (mask >> a) & 1;
    }
}

void puf_reset(Env* env) {
    poker_deal_hand(env);
    env->stage = POKER_STAGE_DEAL;
    env->pending = poker_run(env);
}

void puf_step(Env* env) {
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(env->game, env->lane, amounts);
    int action = env->agents[0].actions[0];
    assert(!env->pending);
    assert(action >= 0 && action < POKER_ACTIONS && (mask & (1u << action)));
    Poker* g = env->game;
    int t = env->lane;
    if (env->first_decision && g->street[t] == 0) {
        int c = poker_hand_class(g->hole[0][0][t], g->hole[0][1][t]);
        if (c != 1) {
            float* row = c ? &env->log.trash : &env->log.premium;
            row[0] += 1; row[1] += amounts[action] < 0; row[2] += amounts[action] > 0;
        }
    }
    env->first_decision = 0;
    if (!env->from_state && g->street[t] == 0 && g->raises[t] == 0 && g->bet[0][t] < g->high[t]) {
        env->log.open_spots += 1;
        env->log.open_folds += amounts[action] < 0;
    }
    env->log.decisions += 1;
    if (amounts[action] > 0) {
        env->log.raises += 1;
        env->log.allin_raises += (uint32_t)amounts[action] >= g->bet[0][t] + g->stack[0][t];
    }
    poker_act(env, amounts[action]);
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->stage = POKER_STAGE_STEP;
    env->pending = poker_run(env);
}

#ifdef __CUDACC__
// env.league_gpu: frozen bank seats run as one batched GPU forward per round instead of one CPU forward
// per decision. After the trainer steps a buffer's envs (PUF_CPU_POST_STEP, on that buffer's thread),
// every env waiting on a bank seat contributes one row; rows are grouped by bank and run through cuBLAS
// with the MinGRU state kept on the GPU per (env, seat). Logits come back and each env samples and
// continues on the CPU exactly as the inline path does (same masked softmax, same RNG draws), possibly
// waiting again. Rounds repeat until no env waits.
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <mutex>

#define POKER_CUDA(call) do { cudaError_t poker_err_ = (call); if (poker_err_ != cudaSuccess) { \
    fprintf(stderr, "poker league GPU: %s at %s:%d\n", cudaGetErrorString(poker_err_), __FILE__, __LINE__); \
    abort(); } } while (0)
#define POKER_CUBLAS(call) do { cublasStatus_t poker_st_ = (call); if (poker_st_ != CUBLAS_STATUS_SUCCESS) { \
    fprintf(stderr, "poker league cuBLAS error %d at %s:%d\n", (int)poker_st_, __FILE__, __LINE__); \
    abort(); } } while (0)

__global__ void poker_zero_fresh(float* state, const int* slot, const uint8_t* fresh, int n, int width) {
    long idx = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (long)n * width) return;
    int r = idx / width;
    if (fresh[r]) state[(long)slot[r] * width + idx % width] = 0;
}

// Same MinGRU update as poker_forward, for rows [0, n) of one bank's segment.
__global__ void poker_mingru(const float* proj, float* x, float* state, const int* slot, int n, int h,
    int layer, int layers) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n * h) return;
    int r = idx / h, i = idx % h;
    const float* p = proj + (long)r * 3 * h;
    float value = p[i] >= 0 ? p[i] + 0.5f : 1.0f / (1.0f + expf(-p[i]));
    float gate = 1.0f / (1.0f + expf(-p[h + i]));
    float highway = 1.0f / (1.0f + expf(-p[2 * h + i]));
    float* sp = state + ((long)slot[r] * layers + layer) * h + i;
    float sv = *sp;
    sv += gate * (value - sv);
    *sp = sv;
    x[(long)r * h + i] = highway * sv + (1 - highway) * x[(long)r * h + i];
}

typedef struct {
    cudaStream_t stream;
    cublasHandle_t blas;
    int capacity;
    int* order;
    float *host_obs, *host_logits, *obs, *x, *proj, *logits;
    int *host_slot, *slot;
    uint8_t *host_fresh, *fresh;
} PokerLeagueGPU;

static std::mutex poker_league_gpu_mutex;

// Upload the bank once and allocate the recurrent state of every (env, opponent seat).
static void poker_league_gpu_bank(PokerLeague* bank) {
    std::lock_guard<std::mutex> lock(poker_league_gpu_mutex);
    if (bank->device_weights) return;
    // A live pool grows to POKER_LEAGUE_MAX slots; a fixed manifest never changes, so it reserves only its own banks
    // (2026-11-04: a sweep trial's [score_eval] league ran out of memory reserving 3.3 GB beside the training league's).
    size_t weights = (size_t)(bank->pool_dir[0] ? POKER_LEAGUE_MAX : bank->count) * bank->stride;
    size_t state = (size_t)bank->total_envs * (POKER_PLAYERS - 1) * bank->layers * bank->hidden;
    POKER_CUDA(cudaMalloc(&bank->device_weights, weights * sizeof(float)));
    POKER_CUDA(cudaMemcpy(bank->device_weights, bank->weights, (size_t)bank->count * bank->stride * sizeof(float),
        cudaMemcpyHostToDevice));
    POKER_CUDA(cudaMalloc(&bank->device_state, state * sizeof(float)));
    POKER_CUDA(cudaMemset(bank->device_state, 0, state * sizeof(float)));
    printf("Poker league GPU: %d banks, %zu MB recurrent state\n", bank->count, state * sizeof(float) >> 20);
}

// One stream, cuBLAS handle and staging buffers per calling thread (each trainer buffer has its own).
static PokerLeagueGPU* poker_league_gpu_context(int capacity, int hidden) {
    static thread_local PokerLeagueGPU c;
    if (!c.stream) {
        POKER_CUDA(cudaStreamCreateWithFlags(&c.stream, cudaStreamNonBlocking));
        POKER_CUBLAS(cublasCreate(&c.blas));
        POKER_CUBLAS(cublasSetStream(c.blas, c.stream));
        POKER_CUBLAS(cublasSetMathMode(c.blas, poker_league_tf32 ? CUBLAS_TF32_TENSOR_OP_MATH : CUBLAS_DEFAULT_MATH));
    }
    if (capacity > c.capacity) {
        if (c.capacity) {
            free(c.order);
            cudaFreeHost(c.host_obs); cudaFreeHost(c.host_logits); cudaFreeHost(c.host_slot); cudaFreeHost(c.host_fresh);
            cudaFree(c.obs); cudaFree(c.x); cudaFree(c.proj); cudaFree(c.logits); cudaFree(c.slot); cudaFree(c.fresh);
        }
        c.capacity = capacity;
        c.order = (int*)malloc(capacity * sizeof(int));
        POKER_CUDA(cudaMallocHost(&c.host_obs, (size_t)capacity * POKER_PUB * sizeof(float)));
        POKER_CUDA(cudaMallocHost(&c.host_logits, (size_t)capacity * (POKER_ACTIONS + 1) * sizeof(float)));
        POKER_CUDA(cudaMallocHost(&c.host_slot, capacity * sizeof(int)));
        POKER_CUDA(cudaMallocHost(&c.host_fresh, capacity));
        POKER_CUDA(cudaMalloc(&c.obs, (size_t)capacity * POKER_PUB * sizeof(float)));
        POKER_CUDA(cudaMalloc(&c.x, (size_t)capacity * hidden * sizeof(float)));
        POKER_CUDA(cudaMalloc(&c.proj, (size_t)capacity * 3 * hidden * sizeof(float)));
        POKER_CUDA(cudaMalloc(&c.logits, (size_t)capacity * (POKER_ACTIONS + 1) * sizeof(float)));
        POKER_CUDA(cudaMalloc(&c.slot, capacity * sizeof(int)));
        POKER_CUDA(cudaMalloc(&c.fresh, capacity));
    }
    return &c;
}

// Batched forward of n rows grouped by bank (rows [start[k], start[k] + count[k]) use bank k). Leaves
// logits in c->host_logits.
static void poker_league_gpu_forward(PokerLeague* bank, PokerLeagueGPU* c, int n, const int* start,
    const int* count, int banks) {
    int h = bank->hidden, layers = bank->layers, outputs = POKER_ACTIONS + 1;
    cudaStream_t st = c->stream;
    POKER_CUDA(cudaMemcpyAsync(c->obs, c->host_obs, (size_t)n * POKER_PUB * sizeof(float), cudaMemcpyHostToDevice, st));
    POKER_CUDA(cudaMemcpyAsync(c->slot, c->host_slot, n * sizeof(int), cudaMemcpyHostToDevice, st));
    POKER_CUDA(cudaMemcpyAsync(c->fresh, c->host_fresh, n, cudaMemcpyHostToDevice, st));
    int width = layers * h;
    poker_zero_fresh<<<(int)(((long)n * width + 255) / 256), 256, 0, st>>>(bank->device_state, c->slot, c->fresh, n, width);
    const float one = 1, zero = 0;
    for (int k = 0; k < banks; k++) {
        int m = count[k], s = start[k];
        if (!m) continue;
        const float* w = bank->device_weights + (size_t)k * bank->stride;
        const float* recurrent = w + h * (POKER_PUB + POKER_ACTIONS + 1);
        float *x = c->x + (size_t)s * h, *proj = c->proj + (size_t)s * 3 * h;
        // Row-major Y[m, out] = X[m, in] * W[out, in]^T, i.e. column-major Y^T = W^T(op T) * X^T.
        POKER_CUBLAS(cublasSgemm(c->blas, CUBLAS_OP_T, CUBLAS_OP_N, h, m, POKER_PUB, &one, w, POKER_PUB,
            c->obs + (size_t)s * POKER_PUB, POKER_PUB, &zero, x, h));
        for (int l = 0; l < layers; l++) {
            POKER_CUBLAS(cublasSgemm(c->blas, CUBLAS_OP_T, CUBLAS_OP_N, 3 * h, m, h, &one,
                recurrent + (size_t)l * 3 * h * h, h, x, h, &zero, proj, 3 * h));
            poker_mingru<<<(m * h + 255) / 256, 256, 0, st>>>(proj, x, bank->device_state, c->slot + s, m, h, l, layers);
        }
        POKER_CUBLAS(cublasSgemm(c->blas, CUBLAS_OP_T, CUBLAS_OP_N, outputs, m, h, &one, w + h * POKER_PUB, h,
            x, h, &zero, c->logits + (size_t)s * outputs, outputs));
    }
    POKER_CUDA(cudaGetLastError());
    POKER_CUDA(cudaMemcpyAsync(c->host_logits, c->logits, (size_t)n * outputs * sizeof(float), cudaMemcpyDeviceToHost, st));
    POKER_CUDA(cudaStreamSynchronize(st));
}

static void poker_league_flush(Env* envs, int count) {
    if (count <= 0 || !envs[0].league_gpu) return;
    PokerLeague* bank = envs[0].league;
    poker_league_gpu_bank(bank);
    if (bank->pool_dir[0] && poker_league_gpu_mutex.try_lock()) {
        int slot;
        if (poker_league_poll(bank, &slot)) {
            POKER_CUDA(cudaMemcpy(bank->device_weights + (size_t)slot * bank->stride, bank->weights
                + (size_t)slot * bank->stride, (size_t)bank->stride * sizeof(float), cudaMemcpyHostToDevice));
            if (slot == bank->count) __atomic_store_n(&bank->count, bank->count + 1, __ATOMIC_RELEASE);
            poker_league_resample(bank, slot);
        } else if (bank->count && bank->polled == (double)time(NULL)) {
            poker_league_resample(bank, (bank->loaded - 1) % POKER_LEAGUE_MAX);
        }
        poker_league_gpu_mutex.unlock();
    }
    PokerLeagueGPU* c = poker_league_gpu_context(count, bank->hidden);
    for (;;) {
        int per_bank[POKER_LEAGUE_MAX] = {0}, start[POKER_LEAGUE_MAX], cursor[POKER_LEAGUE_MAX], n = 0;
        for (int i = 0; i < count; i++) {
            if (envs[i].pending) {
                per_bank[envs[i].opponents[envs[i].pending_seat]]++;
                n++;
            }
        }
        if (!n) return;
        int banks = __atomic_load_n(&bank->count, __ATOMIC_ACQUIRE);
        for (int k = 0, sum = 0; k < banks; k++) {
            start[k] = cursor[k] = sum;
            sum += per_bank[k];
        }
        for (int i = 0; i < count; i++) {
            if (envs[i].pending) c->order[cursor[envs[i].opponents[envs[i].pending_seat]]++] = i;
        }
        #pragma omp parallel for schedule(static) num_threads(poker_omp_threads) if (n > 512)
        for (int r = 0; r < n; r++) {
            Env* e = &envs[c->order[r]];
            int p = e->pending_seat;
            poker_observe_public(e->game, e->lane, p, c->host_obs + (size_t)r * POKER_PUB);
            c->host_slot[r] = e->index * (POKER_PLAYERS - 1) + (p - 1);
            c->host_fresh[r] = (e->fresh >> p) & 1;
            e->fresh &= ~(1u << p);
        }
        poker_league_gpu_forward(bank, c, n, start, per_bank, banks);
        #pragma omp parallel for schedule(static) num_threads(poker_omp_threads) if (n > 512)
        for (int r = 0; r < n; r++) {
            Env* e = &envs[c->order[r]];
            poker_act(e, poker_league_action(e, c->host_logits + (size_t)r * (POKER_ACTIONS + 1)));
            e->pending = poker_run(e);
        }
    }
}
#define PUF_CPU_POST_STEP(envs, count) poker_league_flush(envs, count)

static void poker_league_gpu_free(PokerLeague* bank) {
    cudaError_t w = bank->device_weights ? cudaFree(bank->device_weights) : cudaSuccess;
    cudaError_t st = bank->device_state ? cudaFree(bank->device_state) : cudaSuccess;
    size_t free_b = 0, total_b = 0;
    cudaMemGetInfo(&free_b, &total_b);
    printf("Poker league GPU freed: weights %s, state %s, %zu MB free\n", cudaGetErrorString(w), cudaGetErrorString(st),
        free_b >> 20);
    cudaGetLastError();  // a failed free must not surface as the next league kernel's error
}
#else
static void poker_league_gpu_free(PokerLeague* bank) { (void)bank; }
#endif

void puf_set_bot_policy(Env* env, int policy) {
    assert(poker_bot_supported(policy));
    env->bot_policy = policy;
    env->baseline_mask = 0;
    for (int p = 1; p < POKER_PLAYERS; p++) env->baseline_styles[p] = policy;
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "n", log->n);
    float premium_fold = log->premium ? log->premium_fold / log->premium : 0;
    float premium_raise = log->premium ? log->premium_raise / log->premium : 0;
    float trash_fold = log->trash ? log->trash_fold / log->trash : 0;
    float trash_raise = log->trash ? log->trash_raise / log->trash : 0;
    dict_set(out, "premium_fold", premium_fold);
    dict_set(out, "premium_raise", premium_raise);
    dict_set(out, "trash_fold", trash_fold);
    dict_set(out, "trash_raise", trash_raise);
    // In [-2, 2]: folds trash more than premium plus raises premium more than trash. ~0 for gen0.
    float card_sense = (trash_fold - premium_fold) + (premium_raise - trash_raise);
    dict_set(out, "card_sense", card_sense);
    // Sweep metric (gen1): BB/hand vs the bots plus 2 BB per unit of card sense. Blind aggression beats these
    // bots (gen0: +2.5 BB/hand, card_sense ~0), so score alone selects card-blind exploiters.
    dict_set(out, "skill", log->score + 2 * card_sense);
    // Panel tables: BB/hand vs each panel bot, and the worst of them (the gen2 sweep metric; 0 until all have hands).
    float worst = 1e9f;
    int seen = 0;
    for (int k = 0; k < POKER_PANEL_COUNT; k++) {
        if (!log->panel_hands[k]) continue;
        float bb = log->panel_score[k] / log->panel_hands[k];
        char key[32];
        snprintf(key, sizeof(key), "panel_%s", POKER_PANEL_NAMES[k]);
        dict_set(out, key, bb);
        worst = bb < worst ? bb : worst;
        seen++;
    }
    dict_set(out, "panel_worst", seen == POKER_PANEL_COUNT ? worst : 0);
    if (log->state_hands) dict_set(out, "state_bb", log->state_score / log->state_hands);
    dict_set(out, "state_share", log->state_hands / (log->state_hands + log->n));
    dict_set(out, "raise_rate", log->decisions ? log->raises / log->decisions : 0);
    dict_set(out, "allin_of_raises", log->raises ? log->allin_raises / log->raises : 0);
    float war_rate = log->league_hands ? log->wars / log->league_hands : 0;
    float open_fold = log->open_spots ? log->open_folds / log->open_spots : 0;
    dict_set(out, "war_rate", war_rate);
    dict_set(out, "open_fold", open_fold);
    dict_set(out, "rake_bb", log->rake_bb);
    // Sweep metric (2026-10-30): panel_worst rewards beating the bots, which loose aggression does (gen8 plays 90% of
    // hands, +117 BB/hand vs the station), so penalize re-raise wars and credit preflop folds. The panel term is capped
    // at +1 BB/hand: overnight trials that learned to fold preflop lost to the bots while loose ones beat them, so an
    // uncapped panel term paid for exactly the looseness the other terms penalize. Beating every bot is a floor, not a goal.
    float panel = seen == POKER_PANEL_COUNT ? worst : 0;
    // gen11 (2026-11-04): plus BB/hand at league tables, which the final eval of a sweep trial fills with fixed
    // frozen models only ([score_eval]). The bot term stays as a floor so the score cannot pay for a model-only exploit.
    float league_bb = log->league_hands ? log->league_score / log->league_hands : 0;
    dict_set(out, "league_bb", league_bb);
    dict_set(out, "poker_score", league_bb + (panel < 1 ? panel : 1) - 10 * war_rate + 2 * open_fold);
}

void puf_close(Env* env) {
    free(env->memories);
    free(env->profiles);
    if (env->league_owner && env->league) {
        poker_league_gpu_free(env->league);
        free(env->league->weights);
        free(env->league);
    }
    if (env->lane == 0) {
        free(env->game);
    }
}

void puf_render(Env* env) {
}
