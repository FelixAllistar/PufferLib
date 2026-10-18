// Browser transport around the unchanged native game and policies.
#include "core.h"

static Poker game;
static uint64_t policies[POKER_PLAYERS];
static int hand;
static int32_t view[24 + 9 * POKER_PLAYERS];

void web_next(void) {
    uint32_t stacks[POKER_PLAYERS];
    for (int p = 0; p < POKER_PLAYERS; p++) stacks[p] = 20000;
    poker_deal(&game, 0, hand++ % POKER_PLAYERS, stacks);
}

void web_init(uint32_t seed) {
    memset(&game, 0, sizeof(game));
    game.rng[0] = (uint64_t)seed + 1;
    for (int p = 0; p < POKER_PLAYERS; p++)
        policies[p] = (uint64_t)seed + 0x9e3779b9u * (uint64_t)(p + 1);
    hand = 0;
    web_next();
}

int web_act(int32_t action) {
    if (game.actor[0] == POKER_DONE) return 0;
    int p = game.actor[0];
    if (action < -1) return 0;
    if (action == -1 && game.bet[p][0] >= game.high[0]) return 0;
    if (action > 0) {
        uint32_t maximum = game.bet[p][0] + game.stack[p][0];
        if (!poker_can_raise(&game, 0) || action <= game.high[0]
            || (uint32_t)action > maximum
            || ((uint32_t)action < poker_min_raise(&game, 0)
                && (uint32_t)action != maximum)) return 0;
    }
    poker_step(&game, 0, action);
    return 1;
}

int32_t web_bot(int policy) {
    if (game.actor[0] == POKER_DONE || !poker_bot_supported(policy)) return -2;
    return poker_decide(&game, 0, policies + game.actor[0], policy, 0, NULL);
}

// Export only the hero's private cards and public showdown holdings.
// The predealt runout and folded opponents' cards never cross this boundary.
int32_t* web_state(void) {
    memset(view, 0, sizeof(view));
    int actor = game.actor[0], done = actor == POKER_DONE;
    int showdown = done && __builtin_popcount(game.live[0]) > 1;
    int board_n = game.street[0] ? game.street[0] + 2 : 0;
    view[0] = POKER_PLAYERS; view[1] = actor; view[2] = game.street[0];
    view[3] = game.pot[0]; view[4] = game.high[0]; view[5] = game.increment[0];
    view[6] = game.live[0]; view[7] = game.button[0]; view[8] = game.decisions[0];
    if (!done) {
        int32_t amounts[POKER_ACTIONS];
        view[9] = poker_actions(&game, 0, amounts);
        view[10] = poker_min_raise(&game, 0);
        view[11] = game.bet[actor][0] + game.stack[actor][0];
        uint32_t call = game.high[0] - game.bet[actor][0];
        view[12] = call < game.stack[actor][0] ? call : game.stack[actor][0];
    }
    view[13] = board_n; view[14] = showdown; view[15] = hand;
    for (int i = 0; i < 5; i++) view[16+i] = i < board_n ? game.board[i][0] : 52;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        int32_t* row = view + 24 + 9*p;
        int live = !!(game.live[0] & (1u << p));
        int visible = p == 0 || (showdown && live);
        row[0] = game.stack[p][0]; row[1] = game.bet[p][0];
        row[2] = game.total[p][0];
        row[3] = visible ? game.hole[p][0][0] : 52;
        row[4] = visible ? game.hole[p][1][0] : 52;
        row[5] = live; row[6] = -1;
        if (visible && board_n >= 3) {
            uint8_t cards[7] = {game.hole[p][0][0], game.hole[p][1][0]};
            for (int i = 0; i < board_n; i++) cards[i+2] = game.board[i][0];
            row[6] = poker_rank(cards, board_n+2) >> 24;
        }
        row[7] = done ? (int32_t)game.stack[p][0] - 20000 : 0;
        row[8] = 20000;
    }
    return view;
}
