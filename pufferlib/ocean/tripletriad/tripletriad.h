#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raylib.h" // how to start game compile - LD_LIBRARY_PATH=raylib-5.0_linux_amd64/lib ./tripletriadgame

#define SELECT_CARD_1 0
#define SELECT_CARD_2 1
#define SELECT_CARD_3 2
#define SELECT_CARD_4 3
#define SELECT_CARD_5 4
#define CARD_TO_SLOT_1 5
#define CARD_TO_SLOT_2 6
#define CARD_TO_SLOT_3 7
#define CARD_TO_SLOT_4 8
#define CARD_TO_SLOT_5 9
#define CARD_TO_SLOT_6 10
#define CARD_TO_SLOT_7 11
#define CARD_TO_SLOT_8 12
#define CARD_TO_SLOT_9 13
#define ACTIONS 14

#define AGENT 0
#define BOT 1
#define PLAYERS 2

#define CARDS_PER_PLAYER 5
#define CARD_VALUES 4
#define VALUES 7
#define NORTH 0
#define SOUTH 1
#define EAST 2
#define WEST 3

#define ROWS 3
#define COLS 3
#define BOARD_SIZE 9
#define BOARD_X_OFFSET 206
#define BOARD_Y_OFFSET 10

#define MAX_EPISODE_LENGTH 30
#define OBS_SIZE (ROWS*COLS + ACTIONS + PLAYERS + PLAYERS + ROWS*COLS*CARD_VALUES + PLAYERS*CARDS_PER_PLAYER*CARD_VALUES + PLAYERS*CARDS_PER_PLAYER)

const Color PUFF_RED = (Color){187, 0, 0, 255};
const Color PUFF_CYAN = (Color){0, 187, 187, 255};
const Color PUFF_WHITE = (Color){241, 241, 241, 241};
const Color PUFF_BACKGROUND = (Color){6, 24, 24, 255};
static const Color PLAYER_COLORS[PLAYERS] = {PUFF_CYAN, PUFF_RED};

typedef struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float n;
} Log;

typedef struct Client {int width; int height;} Client;

typedef struct CTripleTriad {
    float* observations;
    int* actions;
    float* rewards;
    unsigned char* terminals;
    
    Log log;
    float perf;
    float episode_return;
    float episode_length;
    int game_over;

    float board_x[BOARD_SIZE];
    float board_y[BOARD_SIZE];
    int board_states[ROWS][COLS];
    int board_card_values[ROWS][COLS][CARD_VALUES];
    int num_cards;
    int placed_cards;
    int cards_in_hand[PLAYERS][CARDS_PER_PLAYER][CARD_VALUES];
    int card_selected[PLAYERS];
    int card_locations[PLAYERS][CARDS_PER_PLAYER];
    int action_masks[ACTIONS];
    int score[PLAYERS];

    int width;
    int height;
    int card_width;
    int card_height;
    Client* client;
} CTripleTriad;

void add_log(CTripleTriad* env) {
    env->log.perf += env->perf;
    env->log.score += env->score[0];
    env->log.episode_return += env->episode_return;
    env->log.episode_length += env->episode_length;
    env->log.n += 1.0f;
}

static inline void generate_board_positions(CTripleTriad* env) {
    for (int row=0; row<ROWS; row++) {
        for (int col=0; col<COLS; col++) {
            int idx = row * COLS + col;
            env->board_x[idx] = col * env->card_width;
            env->board_y[idx] = row * env->card_height;
        }
    }
}

static inline void reset_cards_in_hand(CTripleTriad* env) {
    for(int plr=0; plr<PLAYERS; plr++)
        for(int card=0; card<CARDS_PER_PLAYER; card++)
            for(int val=0; val<CARD_VALUES; val++)
                env->cards_in_hand[plr][card][val] = 1 + (rand() % VALUES);
}

static inline void reset_card_locations(CTripleTriad* env) {
    memset(env->card_locations, 0, sizeof(env->card_locations));
}

static inline void reset_card_selections(CTripleTriad* env) {
    memset(env->card_selected, -1, sizeof(env->card_selected));
}

static inline void reset_board_states(CTripleTriad* env) {
    memset(env->board_states, -1, sizeof(env->board_states));
}

static inline void reset_board_card_values(CTripleTriad* env) {
    memset(env->board_card_values, 0, sizeof(env->board_card_values));
}

static inline void reset_scores(CTripleTriad* env) {
    env->score[0] = 0;
    env->score[1] = 0;
}

void init_ctripletriad(CTripleTriad* env) {
    generate_board_positions(env);
    reset_cards_in_hand(env);
    reset_card_locations(env);
    reset_card_selections(env);
    reset_board_states(env);
    reset_board_card_values(env);
    reset_scores(env);
}

void allocate_ctripletriad(CTripleTriad* env) {
    env->actions = (int*)calloc(1, sizeof(int));
    env->observations = (float*)calloc(OBS_SIZE, sizeof(float)); // 9 + 14 + 2 + 2 + 36 + 40 + 10 = 113
    env->terminals = (unsigned char*)calloc(1, sizeof(unsigned char));
    env->rewards = (float*)calloc(1, sizeof(float));
    env->placed_cards = 0;
    init_ctripletriad(env);
}

void c_close(CTripleTriad* env) {
    free(env->actions);
    free(env->observations);
    free(env->terminals);
    free(env->rewards);
}

void free_allocated_ctripletriad(CTripleTriad* env) {
    c_close(env);
}

void compute_observations(CTripleTriad* restrict env) {
    float* restrict obs = env->observations; // restrict so compiler will unroll + vectorise for that sweet, sweet 0.000001s time save
    int* src = &env->board_states[0][0];
    for (int i=0; i<ROWS*COLS; i++)
        *obs++ = (float)(*src++);

    src = &env->action_masks[0];
    for (int act=0; act<ACTIONS; act++)
        *obs++ = (float)(*src++);
    
    src = &env->card_selected[0];
    for (int plr=0; plr<PLAYERS; plr++)
        *obs++ = (float)(*src++);

    src = &env->score[0];
    for (int plr=0; plr<PLAYERS; plr++)
        *obs++ = (float)(*src++);

    src = &env->board_card_values[0][0][0];
    for (int i=0; i<ROWS*COLS*CARD_VALUES; i++)
        *obs++ = (float)(*src++);

    src = &env->cards_in_hand[0][0][0];
    for (int i=0; i<PLAYERS*CARDS_PER_PLAYER*CARD_VALUES; i++)
        *obs++ = (float)(*src++);
                
    src = &env->card_locations[0][0];
    for (int i=0; i<PLAYERS*CARDS_PER_PLAYER; i++)
        *obs++ = (float)(*src++);
}

void c_reset(CTripleTriad* env) {
    env->game_over = 0;
    env->terminals[0] = 0;
    env->placed_cards = 0;
    reset_cards_in_hand(env);
    reset_card_locations(env);
    reset_board_states(env);
    memset(env->action_masks, 0, sizeof(env->action_masks));
    reset_board_card_values(env);
    reset_scores(env);
    reset_card_selections(env);
    compute_observations(env);
    env->episode_length = 0;
    env->episode_return = 0;
}

void select_card(CTripleTriad* env, int card_selected, int player) {
    env->card_selected[player] = card_selected;
}

void place_card(CTripleTriad* env, int card_placement, int player) {
    int card_idx = card_placement - 1;
    int row = card_idx / ROWS;
    int col = card_idx % COLS;
    int selected_card = env->card_selected[player];
    env->card_locations[player][selected_card] = card_placement;
    env->board_states[row][col] = player;
    env->score[player]++;
    for (int val=0; val<CARD_VALUES; val++)
        env->board_card_values[row][col][val] = env->cards_in_hand[player][selected_card][val];
    
    env->placed_cards++;
}

void update_action_masks(CTripleTriad* env) {
    for (int card = 0; card < CARDS_PER_PLAYER; card++)
        env->action_masks[SELECT_CARD_1 + card] = (env->card_locations[AGENT][card] > 0);

    for (int plr=0; plr<PLAYERS; plr++) {
        for (int card=0; card<CARDS_PER_PLAYER; card++) {
        int loc = env->card_locations[plr][card];
        if (loc > 0)
            env->action_masks[SELECT_CARD_5 + loc] = 1;
        }
    }
}

void check_win_condition(CTripleTriad* env) {
    if (env->placed_cards == BOARD_SIZE) {
        if (env->score[0] == env->score[1]) {
            env->terminals[0] = 1;
            env->rewards[0] = 0.0;
            env->game_over = 1;
        } else {
            int winner = env->score[0] > env->score[1] ? 1 : -1; // I'll allow it
            env->terminals[0] = 1;
            env->rewards[0] = winner;
            env->episode_return += winner;
            env->game_over = 1;
        }
    }
}

static inline int get_bot_card_placement(CTripleTriad* env) {
    int valid_placements[BOARD_SIZE];
    int num_valid_placements = 0;
    for (int slot=CARD_TO_SLOT_1; slot<=CARD_TO_SLOT_9; slot++)
        if (env->action_masks[slot] == 0)
            valid_placements[num_valid_placements++] = slot - SELECT_CARD_5;

    return valid_placements[rand() % num_valid_placements];
}

static inline int get_bot_card_selection(CTripleTriad* env) {
    int valid_selections[CARDS_PER_PLAYER];
    int num_valid_selections = 0;
    for (int card=0; card<CARDS_PER_PLAYER; card++)
        if (env->card_locations[BOT][card] == 0)
            valid_selections[num_valid_selections++] = card;
        return valid_selections[rand() % num_valid_selections];
}

static inline int check_legal_placement(CTripleTriad* env, int card_placement) {
    int row = (card_placement - 1) / ROWS;
    int col = (card_placement - 1) % COLS;
    return (env->board_states[row][col] == -1);
}

void check_card_conversions(CTripleTriad* env, int card_placement, int player) {
    int card_idx = card_placement - 1;
    int row = card_idx / ROWS;
    int col = card_idx % COLS;
    int enemy_player = !player;
    int values[CARD_VALUES] = {
        env->board_card_values[row][col][NORTH],
        env->board_card_values[row][col][SOUTH],
        env->board_card_values[row][col][EAST],
        env->board_card_values[row][col][WEST]
    };

    int adjacent_indices[CARD_VALUES][2] = {
        {row - 1, col},  // North
        {row + 1, col},  // South
        {row, col + 1},  // East
        {row, col - 1}   // West
    };

    int adjacent_value_indices[CARD_VALUES] = {SOUTH, NORTH, WEST, EAST};
    for (int val=0; val<CARD_VALUES; val++) {
        int adj_row = adjacent_indices[val][0];
        int adj_col = adjacent_indices[val][1];
        if (adj_row >= 0 && adj_row < ROWS && adj_col >= 0 && adj_col < COLS) {
            int adjacent_value = env->board_card_values[adj_row][adj_col][adjacent_value_indices[val]];
            if (adjacent_value < values[val] && adjacent_value != 0 && env->board_states[adj_row][adj_col] == enemy_player) {
                env->board_states[adj_row][adj_col] = player;
                env->score[player]++;
                env->score[enemy_player]--;
            }
        }
    }
}

void c_step(CTripleTriad* env) {
    env->episode_length += 1;
    env->rewards[0] = 0.0;
    int action = env->actions[0];
    if (env->episode_length >= MAX_EPISODE_LENGTH) {
        env->game_over = 1;
        env->episode_return -= 1.0;
        env->rewards[0] -= 1.0;
    }
    if (env->game_over == 1) {
        env->perf = (env->score[0] > env->score[1]) ? 1.0 : 0.0;
        add_log(env);
        c_reset(env);
        return;
    }
    if (action >= SELECT_CARD_1 && action <= SELECT_CARD_5 ) {
        env->episode_return -= 0.1; // Penalise model for swapping between selected cards to avoid playing
        env->rewards[0] -= 0.1;
        int card_selected = action;
        if(env->card_locations[0][card_selected] == 0)
            select_card(env, card_selected, AGENT);
    }
    else if (action >= CARD_TO_SLOT_1 && action <= CARD_TO_SLOT_9  ) {
        int card_placement = action - SELECT_CARD_5;
        int card_placed = 0;
        if(env->card_selected[0] >= 0) {
            if(check_legal_placement(env, card_placement)) {
                place_card(env, card_placement, AGENT);
                check_card_conversions(env, card_placement, AGENT);
                check_win_condition(env);
                update_action_masks(env);
                env->card_selected[0] = -1;
                card_placed = 1;
            } else {
                env->episode_return -= 0.1;
                env->rewards[0] -= 0.1;
            }
        } else {
            env->episode_return -= 0.1;
            env->rewards[0] -= 0.1;
        }
        if (env->terminals[0] == 0 && card_placed == 1 ) {
            int bot_card_selected = get_bot_card_selection(env);
            if(bot_card_selected >= 0) {
                select_card(env, bot_card_selected, BOT);
                int bot_card_placement = get_bot_card_placement(env);
                place_card(env,bot_card_placement, BOT);
                check_card_conversions(env, bot_card_placement, BOT);
                check_win_condition(env);
                update_action_masks(env);
                env->card_selected[1] = -1; 
            }
        }
    }
    if (env->terminals[0] == 1)
        env->game_over = 1;
    
    compute_observations(env);
}

Client* make_client(int width, int height) {
    Client* client = (Client*)calloc(1, sizeof(Client));
    client->width = width;
    client->height = height;
    InitWindow(width, height, "PufferLib Ray TripleTriad");
    SetTargetFPS(60);
    return client;
}

void c_render(CTripleTriad* env) {
    if (IsKeyDown(KEY_ESCAPE))
        exit(0);
    if (env->client == NULL)
        env->client = make_client(env->width, env->height);

    BeginDrawing();
    ClearBackground(PUFF_BACKGROUND);
    for (int row=0; row<ROWS; row++) {
        for (int col=0; col<COLS; col++) {
            int board_idx = row * COLS + col;
            int x = env->board_x[board_idx];
            int y = env->board_y[board_idx];
            DrawRectangleLines(x + BOARD_X_OFFSET, y + BOARD_Y_OFFSET, env->card_width, env->card_height, PUFF_WHITE);
        }
    }
    for(int plr=0; plr<PLAYERS; plr++) {
        for(int card=0; card<CARDS_PER_PLAYER; card++) {
            int card_x = (plr == 0) ? 10 : (env->width - env->card_width - 10);
            int card_y = 10 + env->card_height / 2 * card;
            int board_idx = env->card_locations[plr][card] - 1;
            int board_row = board_idx / ROWS;
            int board_col = board_idx % COLS;
            if (env->card_locations[plr][card] != 0) {
                card_x = env->board_x[board_idx] + BOARD_X_OFFSET;
                card_y = env->board_y[board_idx] + BOARD_Y_OFFSET;
            }
            Color card_color = PLAYER_COLORS[plr];
            if (env->card_locations[plr][card] != 0) {
                card_color = PLAYER_COLORS[env->board_states[board_row][board_col]];
            }
            DrawRectangle(card_x, card_y, env->card_width, env->card_height, card_color);
            Rectangle rect = (Rectangle){card_x, card_y, env->card_width, env->card_height};
            if (env->card_selected[plr] == card)
                DrawRectangleLinesEx(rect, 3, PUFF_RED);
            else
                DrawRectangleLinesEx(rect, 2, PUFF_WHITE);

            for(int val=0; val<CARD_VALUES; val++) {
                int x_offset, y_offset;
                switch(val) {
                    case NORTH:
                        x_offset = card_x + 25;
                        y_offset = card_y + 5;
                        break;
                    case SOUTH:
                        x_offset = card_x + 25;
                        y_offset = card_y + 45;
                        break;
                    case EAST:
                        x_offset = card_x + 45;
                        y_offset = card_y + 25;
                        break;
                    case WEST:
                        x_offset = card_x + 5;
                        y_offset = card_y + 25;
                        break;
                }
                DrawText(TextFormat("%d", env->cards_in_hand[plr][card][val]), x_offset, y_offset, 20, PUFF_WHITE);
            }
            // add a little text on the top right that says Card 1, Card 2, Card 3, Card 4, Card 5
            DrawText(TextFormat("Card %d", card+1), card_x + env->card_width -50, card_y + 5, 10, PUFF_WHITE);
        }
        if (plr == 0)
            DrawText(TextFormat("%d", env->score[plr]), env->card_width * 0.4, env->height - 100, 100, PUFF_WHITE);
        else
            DrawText(TextFormat("%d", env->score[plr]), env->width - env->card_width * 0.6, env->height - 100, 100, PUFF_WHITE);
    }
    EndDrawing();
}

void close_client(Client* client) {
    CloseWindow();
    free(client);
}
