/* Raid: Great Olm boss fight from Old School RuneScape
 * A multi-agent RL environment for learning the Chambers of Xeric boss.
 * Implements head-turning mechanics, phase transitions, and prayer switching.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raylib.h"

// ============================================================================
// Constants
// ============================================================================

// Combat styles
#define STYLE_MELEE  0
#define STYLE_MAGE   1
#define STYLE_RANGE  2

// Olm head facing direction
#define FACE_LEFT   0
#define FACE_CENTER 1
#define FACE_RIGHT  2

// Action encoding (31 total discrete actions)
// 0-24:  Movement (5x5 grid, -2 to +2 in each axis)
// 25:    Attack melee claw (left claw, right side of screen)
// 26:    Attack mage claw (right claw, left side of screen)
// 27:    Attack head (when exposed)
// 28-30: Prayer toggle (protect melee=28, mage=29, range=30)
#define ACTION_MOVE_COUNT  25
#define ACTION_ATTACK_MELEE_CLAW 25
#define ACTION_ATTACK_MAGE_CLAW  26
#define ACTION_ATTACK_HEAD       27
#define ACTION_PRAYER_BASE 28
#define NUM_ACTIONS        31

// Attack targets
#define TARGET_NONE       0
#define TARGET_MELEE_CLAW 1
#define TARGET_MAGE_CLAW  2
#define TARGET_HEAD       3

// Game constants
#define CLAW_MAX_HP 600
#define HEAD_MAX_HP 600
#define CLAW_DOWN_WINDOW 30
#define OLM_ATTACK_INTERVAL 4
#define PLAYER_ATTACK_COOLDOWN 4
#define RESPAWN_DELAY 10
#define DEFAULT_ARENA_WIDTH 15
#define DEFAULT_ARENA_HEIGHT 10
#define DEFAULT_MAX_HP 99

// Olm layout - claws at top of arena (y=-1 render row)
// From player's POV facing north:
//   LEFT side (tiles 0-4): Right claw, weak to MAGIC
//   RIGHT side (tiles 10-14): Left claw, weak to MELEE
//   CENTER (tile 7): Head, weak to RANGED
#define LEFT_CLAW_START 10   // Left claw (melee) starts at tile 10
#define LEFT_CLAW_END 14     // Left claw ends at tile 14
#define LEFT_CLAW_CENTER 12  // Center of left claw
#define RIGHT_CLAW_START 0   // Right claw (magic) starts at tile 0
#define RIGHT_CLAW_END 4     // Right claw ends at tile 4
#define RIGHT_CLAW_CENTER 2  // Center of right claw
#define HEAD_CENTER 7        // Head at center tile
#define MELEE_RANGE 1        // Melee: 1 tile cardinal only (must be at y=0)
#define MAGIC_RANGE 9        // Magic: 9 tiles Chebyshev
#define RANGED_RANGE 10      // Range: 10 tiles Chebyshev

// Observation size per player
#define OBS_SIZE 26

// ============================================================================
// Structs
// ============================================================================

// Required struct for PufferLib logging. Only use floats!
typedef struct {
    float perf;            // Normalized 0-1 performance (kills / episodes)
    float score;           // Unnormalized score (total damage dealt)
    float episode_return;  // Sum of rewards over episode
    float episode_length;  // Ticks per episode
    float damage_dealt;    // Total damage dealt to Olm
    float damage_taken;    // Total damage taken by players
    float olm_kills;       // Count of successful Olm kills
    float n;               // Required as last field - episode count
} Log;

// Rendering client
typedef struct {
    Texture2D puffer;
    Texture2D olm;
    Font font;
} Client;

// Animation constants
#define TICK_FRAMES 36         // Frames per game tick at 60 FPS (0.6 seconds)
#define PROJECTILE_LIFETIME 20 // Frames for projectile to reach target
#define MAX_PROJECTILES 16     // Max simultaneous projectiles

// Projectile for attack animations with delayed damage
typedef struct {
    float start_x, start_y;  // Starting position
    float end_x, end_y;      // Target position
    int style;               // Attack style (for color)
    int tick_spawned;        // Game tick when fired
    int travel_ticks;        // How many ticks until landing
    int active;              // Is this projectile active?
    int from_olm;            // 1 if from Olm, 0 if from player
    int target_player;       // Player index (-1 if player projectile targeting Olm)
    int pending_damage;      // Damage to apply on landing
} Projectile;

// Player entity
typedef struct {
    float x, y;            // Position (0 to arena-1)
    float prev_x, prev_y;  // Previous position for lerp
    int hp;                // Current hitpoints
    int max_hp;            // Maximum hitpoints (default 99)
    int active_prayer;     // -1=none, 0=protect melee, 1=protect mage, 2=protect range
    int attack_target;     // TARGET_NONE/MELEE_CLAW/MAGE_CLAW/HEAD - persists until changed
    int attack_cooldown;   // Ticks until next attack allowed
    int respawn_tick;      // -1 if alive, else tick when respawn occurs
    float episode_return;  // Accumulated rewards this episode
    int episode_start;     // Tick when current episode started
    int attacked_this_tick; // Did player attack this tick? (for animation)
    float attack_anim_x, attack_anim_y; // Target position for attack animation
} Player;

// Olm boss entity
typedef struct {
    int left_claw_hp;          // 600 max, weak to melee
    int right_claw_hp;         // 600 max, weak to magic
    int head_hp;               // 600 max, weak to ranged (only when exposed)
    int left_claw_down_tick;   // -1 if up, else tick when destroyed
    int right_claw_down_tick;  // -1 if up, else tick when destroyed
    int facing;                // FACE_LEFT, FACE_CENTER, FACE_RIGHT
    int head_exposed;          // 1 if head is damageable, 0 otherwise
    int attack_style;          // STYLE_MAGE or STYLE_RANGE (Olm only uses these)
    int attack_tick;           // Next scheduled attack tick
    // Damage tracking for head turning (reset when head turns)
    int left_claw_damaged_since_turn;   // 1 if left claw damaged since last head turn
    int right_claw_damaged_since_turn;  // 1 if right claw damaged since last head turn
} Olm;

// Main environment struct
typedef struct {
    // Required PufferLib fields
    Log log;
    float* observations;       // Flattened observation buffer
    int* actions;              // Action buffer (1 per player)
    float* rewards;            // Reward buffer (1 per player)
    unsigned char* terminals;  // Terminal buffer (1 per player)

    // Environment configuration
    int arena_width;
    int arena_height;
    int num_players;
    int max_episode_ticks;

    // Game state
    int tick;
    Olm olm;
    Player* players;

    // Damage parameters (simplified - no formulas)
    int player_damage;         // Damage per successful hit
    int player_hit_chance;     // Hit chance percentage (0-100)
    int olm_base_damage;       // Olm damage per attack
    int prayer_reduction;      // Damage reduction % with correct prayer

    // Reward shaping
    float reward_damage_dealt; // Reward multiplier per damage dealt
    float reward_damage_taken; // Penalty multiplier per damage taken
    float reward_olm_kill;     // Bonus for killing Olm
    float reward_death;        // Penalty for player death
    float reward_claw_imbalance; // Penalty when killing one claw (scaled by other claw's HP %)

    // Animation state
    int frame;                 // Frame counter within current tick (0 to TICK_FRAMES-1)
    Projectile projectiles[MAX_PROJECTILES];
    int olm_attacked_this_tick;  // Did Olm attack this tick?

    // Pending click (accumulated during render for .c to process)
    int has_pending_click;     // 1 if click was registered during render
    int pending_click_x;       // Screen X coordinate of click
    int pending_click_y;       // Screen Y coordinate of click

    // Rendering
    Client* client;
} Raid;

// ============================================================================
// Helper Functions
// ============================================================================

static inline float fclamp(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline int iclamp(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline int iabs(int val) {
    return val < 0 ? -val : val;
}

static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Chebyshev distance (max of dx, dy) - used for hit delay calculation
static inline int chebyshev_dist(float x1, float y1, float x2, float y2) {
    int dx = iabs((int)x1 - (int)x2);
    int dy = iabs((int)y1 - (int)y2);
    return dx > dy ? dx : dy;
}

// Hit delay formulas from OSRS wiki
// Magic: 1 + floor((1 + distance) / 3) ticks
static inline int magic_hit_delay(int dist) {
    return 1 + (1 + dist) / 3;
}

// Range (thrown): 1 + floor(distance / 6) ticks
static inline int range_hit_delay(int dist) {
    return 1 + dist / 6;
}

// Spawn a projectile for attack animation with delayed damage
void spawn_projectile(Raid* env, float start_x, float start_y,
                      float end_x, float end_y, int style, int from_olm,
                      int target_player, int pending_damage) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!env->projectiles[i].active) {
            Projectile* proj = &env->projectiles[i];
            proj->start_x = start_x;
            proj->start_y = start_y;
            proj->end_x = end_x;
            proj->end_y = end_y;
            proj->style = style;
            proj->tick_spawned = env->tick;
            proj->active = 1;
            proj->from_olm = from_olm;
            proj->target_player = target_player;
            proj->pending_damage = pending_damage;

            // Calculate travel time based on distance and style
            int dist = chebyshev_dist(start_x, start_y, end_x, end_y);
            if (style == STYLE_MAGE) {
                proj->travel_ticks = magic_hit_delay(dist);
            } else {
                proj->travel_ticks = range_hit_delay(dist);
            }
            // Minimum 1 tick travel time
            if (proj->travel_ticks < 1) proj->travel_ticks = 1;

            return;
        }
    }
}

// Apply damage from projectiles that have landed
void apply_projectile_damage(Raid* env) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile* proj = &env->projectiles[i];
        if (!proj->active) continue;

        // Check if projectile has landed
        if (env->tick >= proj->tick_spawned + proj->travel_ticks) {
            // Apply damage to target
            if (proj->from_olm && proj->target_player >= 0) {
                Player* p = &env->players[proj->target_player];
                if (p->hp > 0) {
                    p->hp -= proj->pending_damage;
                    env->rewards[proj->target_player] -= proj->pending_damage * env->reward_damage_taken;
                    env->log.damage_taken += proj->pending_damage;
                    p->episode_return -= proj->pending_damage * env->reward_damage_taken;

                    // Handle death
                    if (p->hp <= 0) {
                        p->hp = 0;
                        p->respawn_tick = env->tick + RESPAWN_DELAY;
                        env->rewards[proj->target_player] -= env->reward_death;
                        p->episode_return -= env->reward_death;
                    }
                }
            }
            // Deactivate projectile after landing
            proj->active = 0;
        }
    }
}

// Check if Olm can see a player based on current facing direction
int can_olm_see_player(Raid* env, Player* p) {
    if (p->hp <= 0) return 0;  // Can't see dead players

    switch (env->olm.facing) {
        case FACE_CENTER:
            // Blind zones: cols 0-1 on left, cols 13-14 on right
            return (p->x >= 2 && p->x <= 12);
        case FACE_LEFT:
            // Can see left side up to col 7, blind at col 8+
            return (p->x <= 7);
        case FACE_RIGHT:
            // Can see right side from col 7+, blind at col 6 and below
            return (p->x >= 7);
    }
    return 0;
}

// Check if any player is visible to Olm
int any_player_visible(Raid* env) {
    for (int i = 0; i < env->num_players; i++) {
        if (can_olm_see_player(env, &env->players[i])) {
            return 1;
        }
    }
    return 0;
}

// Determine which direction Olm should turn to see players
// Returns -1 if no turn needed, otherwise FACE_LEFT/CENTER/RIGHT
int determine_turn_direction(Raid* env) {
    // If we can see any player, no turn needed
    if (any_player_visible(env)) return -1;

    // Find player position (for solo, just use first alive player)
    float player_x = env->arena_width / 2.0f;  // default to center
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];
        if (p->hp <= 0) continue;
        player_x = p->x;
        break;
    }

    // Edge tiles ALWAYS force turn in that direction (col 1 = left, col 13 = right)
    if (player_x <= 1) {
        return FACE_LEFT;
    }
    if (player_x >= 13) {
        return FACE_RIGHT;
    }

    // Player is in a blind zone - determine which side and check damage
    // Damage check is only for the claw on the PLAYER'S side
    if (player_x >= 8) {
        // Player is on RIGHT side (in blind zone when head faces left/center)
        // Check if melee claw (left_claw, on right side of screen) was damaged
        if (env->olm.left_claw_damaged_since_turn && env->olm.left_claw_hp > 0) {
            return FACE_RIGHT;  // Turn all the way to melee claw
        }
        return FACE_CENTER;  // No damage on that side - turn to center
    }
    if (player_x <= 6) {
        // Player is on LEFT side (in blind zone when head faces right/center)
        // Check if mage claw (right_claw, on left side of screen) was damaged
        if (env->olm.right_claw_damaged_since_turn && env->olm.right_claw_hp > 0) {
            return FACE_LEFT;  // Turn all the way to mage claw
        }
        return FACE_CENTER;  // No damage on that side - turn to center
    }

    // Player in true middle (col 7) - turn to center
    return FACE_CENTER;
}

// Check if player is in melee range of left claw
// Melee: 1 tile cardinal only - player at y=0 within left claw range (tiles 13-17)
int in_melee_range(Raid* env, Player* p) {
    if (p->hp <= 0 || p->y != 0) return 0;
    return (p->x >= LEFT_CLAW_START && p->x <= LEFT_CLAW_END);
}

// Magic: 7 tiles Chebyshev distance to right claw center (tile 3)
int in_mage_range(Raid* env, Player* p) {
    if (p->hp <= 0) return 0;
    int dist = chebyshev_dist(p->x, p->y, RIGHT_CLAW_CENTER, 0);
    return dist <= MAGIC_RANGE;
}

// Range: 7 tiles Chebyshev distance to head center (tile 9)
int in_range_range(Raid* env, Player* p) {
    if (p->hp <= 0 || !env->olm.head_exposed) return 0;
    int dist = chebyshev_dist(p->x, p->y, HEAD_CENTER, 0);
    return dist <= RANGED_RANGE;
}

// Get normalized claw down timer (-1 if up, 0-1 if down)
float claw_down_timer(Raid* env, int claw) {
    int down_tick = (claw == 0) ? env->olm.left_claw_down_tick
                                : env->olm.right_claw_down_tick;
    if (down_tick == -1) return -1.0f;
    int elapsed = env->tick - down_tick;
    return fclamp((float)elapsed / CLAW_DOWN_WINDOW, 0.0f, 1.0f);
}

// ============================================================================
// Initialization
// ============================================================================

void init(Raid* env) {
    env->players = calloc(env->num_players, sizeof(Player));

    // Set default values if not specified
    if (env->arena_width == 0) env->arena_width = DEFAULT_ARENA_WIDTH;
    if (env->arena_height == 0) env->arena_height = DEFAULT_ARENA_HEIGHT;
    if (env->max_episode_ticks == 0) env->max_episode_ticks = 10000;
    if (env->player_damage == 0) env->player_damage = 30;
    if (env->player_hit_chance == 0) env->player_hit_chance = 70;
    if (env->olm_base_damage == 0) env->olm_base_damage = 20;
    if (env->prayer_reduction == 0) env->prayer_reduction = 80;
    if (env->reward_damage_dealt == 0) env->reward_damage_dealt = 0.01f;
    if (env->reward_damage_taken == 0) env->reward_damage_taken = 0.02f;
    if (env->reward_olm_kill == 0) env->reward_olm_kill = 10.0f;
    if (env->reward_death == 0) env->reward_death = 1.0f;
    if (env->reward_claw_imbalance == 0) env->reward_claw_imbalance = 1.0f;

    // Initialize player max HP
    for (int i = 0; i < env->num_players; i++) {
        env->players[i].max_hp = DEFAULT_MAX_HP;
    }
}

void reset_olm(Raid* env) {
    Olm* olm = &env->olm;
    olm->left_claw_hp = CLAW_MAX_HP;
    olm->right_claw_hp = CLAW_MAX_HP;
    olm->head_hp = HEAD_MAX_HP;
    olm->left_claw_down_tick = -1;
    olm->right_claw_down_tick = -1;
    olm->facing = FACE_CENTER;
    olm->head_exposed = 0;
    olm->attack_style = STYLE_MAGE;
    olm->attack_tick = OLM_ATTACK_INTERVAL;
    olm->left_claw_damaged_since_turn = 0;
    olm->right_claw_damaged_since_turn = 0;
}

void reset_players(Raid* env) {
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];
        // Spawn players spread out at back of arena
        p->x = (env->arena_width / (env->num_players + 1)) * (i + 1);
        p->y = env->arena_height - 2;
        p->prev_x = p->x;
        p->prev_y = p->y;
        p->hp = p->max_hp;
        p->active_prayer = -1;
        p->attack_target = TARGET_NONE;
        p->attack_cooldown = 0;
        p->respawn_tick = -1;
        p->episode_return = 0.0f;
        p->episode_start = env->tick;
    }
}

// ============================================================================
// Observation Computation
// ============================================================================

void compute_observations(Raid* env) {
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];
        float* obs = &env->observations[i * OBS_SIZE];
        int idx = 0;

        // Player state (0-5)
        obs[idx++] = p->x / (float)env->arena_width;
        obs[idx++] = p->y / (float)env->arena_height;
        obs[idx++] = p->hp / (float)p->max_hp;
        obs[idx++] = (p->active_prayer + 1) / 4.0f;  // -1 to 2 -> 0 to 0.75
        obs[idx++] = p->attack_target / 4.0f;  // 0-3 -> 0 to 0.75
        obs[idx++] = p->attack_cooldown / 10.0f;

        // Olm state (6-12)
        obs[idx++] = env->olm.facing / 2.0f;
        obs[idx++] = (float)env->olm.head_exposed;
        obs[idx++] = env->olm.attack_style / 2.0f;
        int ticks_to_attack = env->olm.attack_tick - env->tick;
        obs[idx++] = fclamp(ticks_to_attack / 4.0f, 0.0f, 1.0f);
        obs[idx++] = env->olm.left_claw_hp / (float)CLAW_MAX_HP;
        obs[idx++] = env->olm.right_claw_hp / (float)CLAW_MAX_HP;
        obs[idx++] = env->olm.head_hp / (float)HEAD_MAX_HP;

        // Visibility and range (13-16)
        obs[idx++] = (float)can_olm_see_player(env, p);
        obs[idx++] = (float)in_melee_range(env, p);
        obs[idx++] = (float)in_mage_range(env, p);
        obs[idx++] = (float)in_range_range(env, p);

        // Claw timers (17-18)
        obs[idx++] = claw_down_timer(env, 0);  // Left claw
        obs[idx++] = claw_down_timer(env, 1);  // Right claw

        // Other players relative positions (19-24, up to 3 others)
        for (int j = 0; j < 3; j++) {
            int other_idx = (i + j + 1) % env->num_players;
            if (j < env->num_players - 1 && env->num_players > 1) {
                Player* other = &env->players[other_idx];
                obs[idx++] = (other->x - p->x) / (float)env->arena_width;
                obs[idx++] = (other->y - p->y) / (float)env->arena_height;
            } else {
                obs[idx++] = 0.0f;
                obs[idx++] = 0.0f;
            }
        }

        // Padding (25)
        obs[idx++] = 0.0f;
    }
}

// ============================================================================
// Action Processing
// ============================================================================

void process_actions(Raid* env) {
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];

        // Store previous position for lerp animation
        p->prev_x = p->x;
        p->prev_y = p->y;

        if (p->hp <= 0) continue;  // Dead players can't act

        int action = env->actions[i];

        if (action < ACTION_MOVE_COUNT) {
            // Movement action: decode 5x5 grid - clears attack target
            int dx = (action % 5) - 2;  // -2 to +2
            int dy = (action / 5) - 2;  // -2 to +2

            float new_x = p->x + dx;
            float new_y = p->y + dy;

            // Clamp to arena bounds
            p->x = fclamp(new_x, 0, env->arena_width - 1);
            p->y = fclamp(new_y, 0, env->arena_height - 1);

            // Manual movement clears attack target
            p->attack_target = TARGET_NONE;

        } else if (action == ACTION_ATTACK_MELEE_CLAW) {
            p->attack_target = TARGET_MELEE_CLAW;
        } else if (action == ACTION_ATTACK_MAGE_CLAW) {
            p->attack_target = TARGET_MAGE_CLAW;
        } else if (action == ACTION_ATTACK_HEAD) {
            p->attack_target = TARGET_HEAD;

        } else if (action < NUM_ACTIONS) {
            // Prayer toggle (28-30)
            int prayer = action - ACTION_PRAYER_BASE;
            if (p->active_prayer == prayer) {
                p->active_prayer = -1;  // Toggle off if same prayer
            } else {
                p->active_prayer = prayer;
            }
        }
    }
}

// ============================================================================
// Combat
// ============================================================================

// Get target position for attack target
void get_target_position(int target, float* out_x, float* out_y) {
    switch (target) {
        case TARGET_MELEE_CLAW:
            *out_x = LEFT_CLAW_CENTER;
            *out_y = 0;  // y=0 for distance calculation (front row)
            break;
        case TARGET_MAGE_CLAW:
            *out_x = RIGHT_CLAW_CENTER;
            *out_y = 0;
            break;
        case TARGET_HEAD:
            *out_x = HEAD_CENTER;
            *out_y = 0;
            break;
        default:
            *out_x = 0;
            *out_y = 0;
    }
}

// Check if player is in range of their current target
int in_attack_range(Raid* env, Player* p) {
    switch (p->attack_target) {
        case TARGET_MELEE_CLAW:
            return in_melee_range(env, p);
        case TARGET_MAGE_CLAW:
            return in_mage_range(env, p);
        case TARGET_HEAD:
            return in_range_range(env, p);
        default:
            return 0;
    }
}

// Move player toward target (2 tiles, long axis first then short)
// Returns 1 if player moved
int drag_toward_target(Raid* env, Player* p, float target_x, float target_y) {
    int dx = (int)target_x - (int)p->x;
    int dy = (int)target_y - (int)p->y;

    if (dx == 0 && dy == 0) return 0;

    int abs_dx = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;

    int move_x = 0, move_y = 0;
    int remaining = 2;  // 2 tiles per tick

    // Long axis first, then short axis
    if (abs_dx >= abs_dy) {
        // Horizontal first
        if (abs_dx > 0) {
            int step = (abs_dx < remaining) ? abs_dx : remaining;
            move_x = (dx > 0) ? step : -step;
            remaining -= step;
        }
        if (remaining > 0 && abs_dy > 0) {
            int step = (abs_dy < remaining) ? abs_dy : remaining;
            move_y = (dy > 0) ? step : -step;
        }
    } else {
        // Vertical first
        if (abs_dy > 0) {
            int step = (abs_dy < remaining) ? abs_dy : remaining;
            move_y = (dy > 0) ? step : -step;
            remaining -= step;
        }
        if (remaining > 0 && abs_dx > 0) {
            int step = (abs_dx < remaining) ? abs_dx : remaining;
            move_x = (dx > 0) ? step : -step;
        }
    }

    // Apply movement with bounds clamping
    p->x = fclamp(p->x + move_x, 0, env->arena_width - 1);
    p->y = fclamp(p->y + move_y, 0, env->arena_height - 1);

    return (move_x != 0 || move_y != 0);
}

void process_player_attacks(Raid* env) {
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];
        p->attacked_this_tick = 0;

        // Decrement cooldown
        if (p->attack_cooldown > 0) {
            p->attack_cooldown--;
        }

        if (p->hp <= 0) continue;  // Dead players can't attack
        if (p->attack_target == TARGET_NONE) continue;  // No target selected

        // Check if target is still valid (has HP)
        int target_valid = 0;
        switch (p->attack_target) {
            case TARGET_MELEE_CLAW: target_valid = (env->olm.left_claw_hp > 0); break;
            case TARGET_MAGE_CLAW: target_valid = (env->olm.right_claw_hp > 0); break;
            case TARGET_HEAD: target_valid = (env->olm.head_exposed && env->olm.head_hp > 0); break;
        }
        if (!target_valid) {
            p->attack_target = TARGET_NONE;
            continue;
        }

        // Get target position for pathfinding
        float target_x, target_y;
        get_target_position(p->attack_target, &target_x, &target_y);

        // If not in range, drag toward target
        if (!in_attack_range(env, p)) {
            drag_toward_target(env, p, target_x, target_y);
        }

        // After potential drag, check if we can attack (in range AND cooldown ready)
        if (p->attack_cooldown > 0) continue;
        if (!in_attack_range(env, p)) continue;

        // Execute attack
        int damage = 0;
        int style = STYLE_MELEE;  // default
        float anim_target_x = target_x;
        float anim_target_y = -0.5f;  // Olm row for animation

        if (p->attack_target == TARGET_MELEE_CLAW) {
            style = STYLE_MELEE;
            if (rand() % 100 < env->player_hit_chance) {
                damage = env->player_damage;
                env->olm.left_claw_hp -= damage;
                if (env->olm.left_claw_hp < 0) env->olm.left_claw_hp = 0;
                env->olm.left_claw_damaged_since_turn = 1;  // Track for head turning
            }
        } else if (p->attack_target == TARGET_MAGE_CLAW) {
            style = STYLE_MAGE;
            if (rand() % 100 < env->player_hit_chance) {
                damage = env->player_damage;
                env->olm.right_claw_hp -= damage;
                if (env->olm.right_claw_hp < 0) env->olm.right_claw_hp = 0;
                env->olm.right_claw_damaged_since_turn = 1;  // Track for head turning
            }
        } else if (p->attack_target == TARGET_HEAD) {
            style = STYLE_RANGE;
            if (rand() % 100 < env->player_hit_chance) {
                damage = env->player_damage;
                env->olm.head_hp -= damage;
                if (env->olm.head_hp < 0) env->olm.head_hp = 0;
            }
        }

        p->attack_cooldown = PLAYER_ATTACK_COOLDOWN;
        p->attacked_this_tick = 1;
        p->attack_anim_x = anim_target_x;
        p->attack_anim_y = anim_target_y;

        // Spawn projectile for mage/range attacks
        if (style != STYLE_MELEE) {
            spawn_projectile(env, p->x, p->y, anim_target_x, anim_target_y, style, 0, -1, 0);
        }

        if (damage > 0) {
            env->rewards[i] += damage * env->reward_damage_dealt;
            env->log.damage_dealt += damage;
            p->episode_return += damage * env->reward_damage_dealt;
        }
    }
}

void olm_attack_tick(Raid* env) {
    Olm* olm = &env->olm;
    env->olm_attacked_this_tick = 0;

    // Check if it's an attack tick
    if (env->tick < olm->attack_tick) return;

    // Schedule next attack
    olm->attack_tick = env->tick + OLM_ATTACK_INTERVAL;

    // Check if Olm needs to turn instead of attacking
    int turn_dir = determine_turn_direction(env);
    if (turn_dir != -1 && turn_dir != olm->facing) {
        olm->facing = turn_dir;
        // Reset damage tracking on head turn
        olm->left_claw_damaged_since_turn = 0;
        olm->right_claw_damaged_since_turn = 0;
        return;  // Turn instead of attack
    }

    // 1/5 chance to switch attack styles
    if (rand() % 5 == 0) {
        olm->attack_style = (olm->attack_style == STYLE_MAGE) ? STYLE_RANGE : STYLE_MAGE;
    }

    // Olm head position for projectile origin (in extra row above walkable area)
    float olm_x = HEAD_CENTER;
    float olm_y = -0.5f;  // Head is in render row 0, y=-0.5 for projectile math

    // Attack all visible players - damage is calculated now but applied when projectile lands
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];
        if (p->hp <= 0) continue;
        if (!can_olm_see_player(env, p)) continue;

        env->olm_attacked_this_tick = 1;

        // Calculate damage NOW (prayer check at fire time per OSRS mechanics)
        int damage = env->olm_base_damage;
        if (p->active_prayer == olm->attack_style) {
            damage = damage * (100 - env->prayer_reduction) / 100;
        }

        // Spawn projectile with pending damage - damage applied when it lands
        spawn_projectile(env, olm_x, olm_y, p->x, p->y, olm->attack_style, 1, i, damage);
    }
}

// ============================================================================
// Phase Transitions
// ============================================================================

void respawn_claws(Olm* olm) {
    olm->left_claw_hp = CLAW_MAX_HP;
    olm->right_claw_hp = CLAW_MAX_HP;
    olm->left_claw_down_tick = -1;
    olm->right_claw_down_tick = -1;
}

void check_phase_transitions(Raid* env) {
    Olm* olm = &env->olm;

    // Track when claws go down and apply imbalance penalty
    if (olm->left_claw_hp <= 0 && olm->left_claw_down_tick == -1) {
        olm->left_claw_down_tick = env->tick;
        // Penalty of -1 if other claw has > 50 HP remaining
        if (olm->right_claw_hp > 50) {
            for (int i = 0; i < env->num_players; i++) {
                env->rewards[i] -= 1.0f;
                env->players[i].episode_return -= 1.0f;
            }
        }
    }
    if (olm->right_claw_hp <= 0 && olm->right_claw_down_tick == -1) {
        olm->right_claw_down_tick = env->tick;
        // Penalty of -1 if other claw has > 50 HP remaining
        if (olm->left_claw_hp > 50) {
            for (int i = 0; i < env->num_players; i++) {
                env->rewards[i] -= 1.0f;
                env->players[i].episode_return -= 1.0f;
            }
        }
    }

    // Check if head should be exposed
    if (!olm->head_exposed &&
        olm->left_claw_down_tick != -1 &&
        olm->right_claw_down_tick != -1) {

        // Both claws down - check if within 30 tick window
        int gap = iabs(olm->left_claw_down_tick - olm->right_claw_down_tick);
        if (gap <= CLAW_DOWN_WINDOW) {
            olm->head_exposed = 1;
        }
    }

    // Check if claws should respawn (one down > 30 ticks, other still up)
    if (!olm->head_exposed) {
        if (olm->left_claw_down_tick != -1 && olm->right_claw_hp > 0 &&
            env->tick - olm->left_claw_down_tick > CLAW_DOWN_WINDOW) {
            respawn_claws(olm);
        }
        if (olm->right_claw_down_tick != -1 && olm->left_claw_hp > 0 &&
            env->tick - olm->right_claw_down_tick > CLAW_DOWN_WINDOW) {
            respawn_claws(olm);
        }
    }
}

void handle_respawns(Raid* env) {
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];

        // Check if player should respawn
        if (p->respawn_tick != -1 && env->tick >= p->respawn_tick) {
            p->hp = p->max_hp;
            p->respawn_tick = -1;
            // Spawn at safe location (back of arena) - use integer position
            p->x = env->arena_width / 2;
            p->y = env->arena_height - 2;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->active_prayer = -1;
            p->attack_cooldown = 0;
        }
    }
}

// ============================================================================
// Termination and Logging
// ============================================================================

void add_log(Raid* env, int victory) {
    env->log.n++;
    if (victory) {
        env->log.olm_kills++;
        env->log.perf = env->log.olm_kills / env->log.n;
    }

    // Aggregate episode stats
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];
        env->log.episode_return += p->episode_return;
        env->log.episode_length += env->tick - p->episode_start;
    }
    env->log.score = env->log.damage_dealt;
}

int check_termination(Raid* env) {
    // Olm killed - victory!
    if (env->olm.head_exposed && env->olm.head_hp <= 0) {
        for (int i = 0; i < env->num_players; i++) {
            env->terminals[i] = 1;
            env->rewards[i] += env->reward_olm_kill;
        }
        add_log(env, 1);
        return 1;
    }

    // Episode timeout
    if (env->tick >= env->max_episode_ticks) {
        for (int i = 0; i < env->num_players; i++) {
            env->terminals[i] = 1;
        }
        add_log(env, 0);
        return 1;
    }

    return 0;
}

// ============================================================================
// Required PufferLib API
// ============================================================================

void c_reset(Raid* env) {
    env->tick = 0;
    env->frame = 0;
    env->olm_attacked_this_tick = 0;
    reset_olm(env);
    reset_players(env);

    // Clear projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        env->projectiles[i].active = 0;
    }

    // Set prev positions to current for smooth start
    for (int i = 0; i < env->num_players; i++) {
        env->players[i].prev_x = env->players[i].x;
        env->players[i].prev_y = env->players[i].y;
    }

    // Clear buffers
    for (int i = 0; i < env->num_players; i++) {
        env->rewards[i] = 0.0f;
        env->terminals[i] = 0;
    }

    compute_observations(env);
}

void c_step(Raid* env) {
    env->tick++;

    // Clear per-tick buffers
    for (int i = 0; i < env->num_players; i++) {
        env->rewards[i] = 0.0f;
        env->terminals[i] = 0;
    }

    // 1. Apply pending projectile damage (from previous Olm attacks landing)
    apply_projectile_damage(env);

    // 2. Olm attack/turn (checks visibility BEFORE player moves this tick)
    // This means you must move BEFORE the tick Olm attacks, not on it
    olm_attack_tick(env);

    // 3. Process player actions (movement, style/prayer changes)
    process_actions(env);

    // 4. Player attacks (based on new positions)
    process_player_attacks(env);

    // 5. Phase transitions (claw down, head exposure, respawn)
    check_phase_transitions(env);

    // 6. Handle player respawns
    handle_respawns(env);

    // 7. Check termination
    if (check_termination(env)) {
        c_reset(env);
        return;
    }

    // 8. Compute observations
    compute_observations(env);
}

void c_render(Raid* env) {
    int scale = 32;
    int hp_bar_height = 40;  // Height for HP bars at top
    int width = env->arena_width * scale;
    int height = (env->arena_height + 1) * scale + hp_bar_height;  // +1 row for Olm parts + HP bars
    int hp_bar_y = 0;                           // HP bars at very top
    int olm_row_y = hp_bar_height;              // Olm render row below HP bars
    int arena_offset = hp_bar_height + scale;   // Walkable area starts below Olm row

    // Clear pending click at start - will be set if click detected during animation
    env->has_pending_click = 0;

    if (env->client == NULL) {
        InitWindow(width, height, "PufferLib Raid - Great Olm");
        SetTargetFPS(60);
        env->client = (Client*)calloc(1, sizeof(Client));
        env->client->puffer = LoadTexture("resources/shared/puffers_128.png");
        env->client->font = GetFontDefault();
    }

    // Animation loop - render TICK_FRAMES frames for smooth animation
    for (int frame = 0; frame < TICK_FRAMES; frame++) {
    if (WindowShouldClose() || IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    // Capture mouse clicks during animation (raylib clears input on EndDrawing)
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        Vector2 mouse = GetMousePosition();
        env->has_pending_click = 1;
        env->pending_click_x = (int)mouse.x;
        env->pending_click_y = (int)mouse.y;
    }

    // Calculate lerp factor for smooth animation (0 to 1 within tick)
    float t = (float)frame / TICK_FRAMES;

    BeginDrawing();
    ClearBackground((Color){20, 20, 30, 255});

    // Draw HP bar area background at top
    DrawRectangle(0, hp_bar_y, width, hp_bar_height, (Color){15, 15, 20, 255});

    // Draw HP bars with numeric values
    int bar_width = 150;
    int bar_h = 20;
    int text_y = hp_bar_y + 12;
    char hp_text[32];

    // Right claw (MAGIC weakness) - LEFT on screen
    int right_claw_x = 10;
    DrawText("Magic", right_claw_x, hp_bar_y + 2, 12, BLUE);
    DrawRectangle(right_claw_x, text_y, bar_width, bar_h, DARKGRAY);
    int right_fill = (int)(bar_width * env->olm.right_claw_hp / (float)CLAW_MAX_HP);
    DrawRectangle(right_claw_x, text_y, right_fill, bar_h, BLUE);
    DrawRectangleLines(right_claw_x, text_y, bar_width, bar_h, WHITE);
    snprintf(hp_text, sizeof(hp_text), "%d", env->olm.right_claw_hp);
    DrawText(hp_text, right_claw_x + bar_width/2 - 15, text_y + 3, 14, WHITE);

    // Head (RANGED weakness) - CENTER
    int head_x = width/2 - bar_width/2;
    DrawText("Head", head_x + bar_width/2 - 15, hp_bar_y + 2, 12, env->olm.head_exposed ? GREEN : GRAY);
    DrawRectangle(head_x, text_y, bar_width, bar_h, DARKGRAY);
    if (env->olm.head_exposed) {
        int head_fill = (int)(bar_width * env->olm.head_hp / (float)HEAD_MAX_HP);
        DrawRectangle(head_x, text_y, head_fill, bar_h, GREEN);
        snprintf(hp_text, sizeof(hp_text), "%d", env->olm.head_hp);
    } else {
        snprintf(hp_text, sizeof(hp_text), "---");
    }
    DrawRectangleLines(head_x, text_y, bar_width, bar_h, WHITE);
    DrawText(hp_text, head_x + bar_width/2 - 15, text_y + 3, 14, WHITE);

    // Left claw (MELEE weakness) - RIGHT on screen
    int left_claw_x = width - bar_width - 10;
    DrawText("Melee", left_claw_x + bar_width/2 - 18, hp_bar_y + 2, 12, RED);
    DrawRectangle(left_claw_x, text_y, bar_width, bar_h, DARKGRAY);
    int left_fill = (int)(bar_width * env->olm.left_claw_hp / (float)CLAW_MAX_HP);
    DrawRectangle(left_claw_x, text_y, left_fill, bar_h, RED);
    DrawRectangleLines(left_claw_x, text_y, bar_width, bar_h, WHITE);
    snprintf(hp_text, sizeof(hp_text), "%d", env->olm.left_claw_hp);
    DrawText(hp_text, left_claw_x + bar_width/2 - 15, text_y + 3, 14, WHITE);

    // Draw Olm row background (non-walkable)
    DrawRectangle(0, olm_row_y, width, scale, (Color){30, 30, 40, 255});

    // Draw arena grid (walkable area only, offset by 1 row)
    for (int x = 0; x <= env->arena_width; x++) {
        DrawLine(x * scale, arena_offset, x * scale, height, (Color){40, 40, 50, 255});
    }
    for (int y = 0; y <= env->arena_height; y++) {
        DrawLine(0, y * scale + arena_offset, width, y * scale + arena_offset, (Color){40, 40, 50, 255});
    }

    // Yellow outline indicators on first row (y=0) at columns 1, 6, 8, 13
    DrawRectangleLines(1 * scale, arena_offset, scale, scale, YELLOW);
    DrawRectangleLines(6 * scale, arena_offset, scale, scale, YELLOW);
    DrawRectangleLines(8 * scale, arena_offset, scale, scale, YELLOW);
    DrawRectangleLines(13 * scale, arena_offset, scale, scale, YELLOW);

    // Draw Olm claws in top row (non-walkable)
    // Right claw (MAGIC weakness) - LEFT side of screen, tiles 1-5
    Color right_claw_color = env->olm.right_claw_hp > 0 ? BLUE : DARKGRAY;
    DrawRectangle(RIGHT_CLAW_START * scale, olm_row_y, 5 * scale, scale,
        (Color){right_claw_color.r, right_claw_color.g, right_claw_color.b, 150});
    DrawRectangleLines(RIGHT_CLAW_START * scale, olm_row_y, 5 * scale, scale, right_claw_color);
    DrawText("MAGIC", RIGHT_CLAW_CENTER * scale - 20, olm_row_y + 6, 16, right_claw_color);

    // Left claw (MELEE weakness) - RIGHT side of screen, tiles 13-17
    Color left_claw_color = env->olm.left_claw_hp > 0 ? RED : DARKGRAY;
    DrawRectangle(LEFT_CLAW_START * scale, olm_row_y, 5 * scale, scale,
        (Color){left_claw_color.r, left_claw_color.g, left_claw_color.b, 150});
    DrawRectangleLines(LEFT_CLAW_START * scale, olm_row_y, 5 * scale, scale, left_claw_color);
    DrawText("MELEE", LEFT_CLAW_CENTER * scale - 20, olm_row_y + 6, 16, left_claw_color);

    // Head (RANGED weakness) - CENTER, tile 9
    Color head_color = env->olm.head_exposed ? (env->olm.head_hp > 0 ? GREEN : DARKGRAY) : (Color){50, 50, 60, 255};
    int head_screen_x = HEAD_CENTER * scale + scale / 2;
    DrawCircle(head_screen_x, olm_row_y + scale / 2, scale / 2, head_color);
    if (env->olm.head_exposed) {
        DrawText("HEAD", head_screen_x - 18, olm_row_y + 6, 14, WHITE);
    }

    // Draw facing indicator (triangle below head row)
    int facing_screen_x = HEAD_CENTER * scale + scale / 2;
    if (env->olm.facing == FACE_LEFT) facing_screen_x = RIGHT_CLAW_CENTER * scale + scale / 2;
    else if (env->olm.facing == FACE_RIGHT) facing_screen_x = LEFT_CLAW_CENTER * scale + scale / 2;
    DrawTriangle(
        (Vector2){(float)facing_screen_x, (float)arena_offset},
        (Vector2){(float)facing_screen_x - scale / 3, (float)arena_offset + scale / 3},
        (Vector2){(float)facing_screen_x + scale / 3, (float)arena_offset + scale / 3},
        YELLOW
    );

    // Draw Olm attack style indicator
    const char* attack_text = env->olm.attack_style == STYLE_MAGE ? "MAGE ATK" : "RANGE ATK";
    Color attack_color = env->olm.attack_style == STYLE_MAGE ? PURPLE : ORANGE;
    DrawText(attack_text, width / 2 - 35, olm_row_y + 6, 16, attack_color);

    // Draw projectiles (tick-based travel time, offset for render)
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile* proj = &env->projectiles[i];
        if (!proj->active) continue;

        // Calculate projectile progress based on ticks and frames
        float total_travel_frames = (float)proj->travel_ticks * TICK_FRAMES;
        float elapsed_frames = (float)(env->tick - proj->tick_spawned) * TICK_FRAMES + frame;
        float proj_t = elapsed_frames / total_travel_frames;
        proj_t = fclamp(proj_t, 0.0f, 1.0f);

        // Get target position - Olm projectiles track the target player
        float target_x = proj->end_x;
        float target_y = proj->end_y;
        if (proj->from_olm && proj->target_player >= 0 && proj->target_player < env->num_players) {
            Player* target = &env->players[proj->target_player];
            // Use lerped player position so projectile smoothly follows
            target_x = lerp(target->prev_x, target->x, t);
            target_y = lerp(target->prev_y, target->y, t);
        }

        // Projectile position (game coords to screen coords with offset)
        float game_x = lerp(proj->start_x, target_x, proj_t);
        float game_y = lerp(proj->start_y, target_y, proj_t);
        float px = game_x * scale + scale / 2;
        float py = game_y * scale + arena_offset + scale / 2;  // Use arena_offset for proper positioning

        Color proj_color;
        if (proj->from_olm) {
            // Olm projectiles are circles - blue=mage, green=range
            proj_color = (proj->style == STYLE_MAGE) ? BLUE : GREEN;
            DrawCircle((int)px, (int)py, 10, proj_color);
            DrawCircleLines((int)px, (int)py, 12, WHITE);
        } else {
            // Player projectiles are squares - blue=mage, green=range
            proj_color = (proj->style == STYLE_MAGE) ? BLUE : GREEN;
            DrawRectangle((int)px - 6, (int)py - 6, 12, 12, proj_color);
        }
    }

    // Draw players - each player fills a full tile (offset by arena_offset for Olm row)
    for (int i = 0; i < env->num_players; i++) {
        Player* p = &env->players[i];

        // Lerp tile position for smooth movement
        float lerped_x = lerp(p->prev_x, p->x, t);
        float lerped_y = lerp(p->prev_y, p->y, t);
        // Use roundf to prevent floating point errors causing off-by-one pixel issues
        int tile_x = (int)roundf(lerped_x * scale);
        int tile_y = (int)roundf(lerped_y * scale) + arena_offset;  // +offset for Olm row
        int center_x = tile_x + scale / 2;
        int center_y = tile_y + scale / 2;

        if (p->hp > 0) {
            // Draw player as full tile rectangle - color by current attack target
            Color player_color;
            switch (p->attack_target) {
                case TARGET_MELEE_CLAW: player_color = RED; break;
                case TARGET_MAGE_CLAW: player_color = BLUE; break;
                case TARGET_HEAD: player_color = GREEN; break;
                default: player_color = WHITE;  // No target
            }
            DrawRectangle(tile_x + 2, tile_y + 2, scale - 4, scale - 4, player_color);
            DrawRectangleLines(tile_x + 2, tile_y + 2, scale - 4, scale - 4, WHITE);

            // Draw melee attack indicator (slash effect in Olm row)
            if (p->attacked_this_tick && p->attack_target == TARGET_MELEE_CLAW && frame < 15) {
                int attack_tile_x = (int)(p->attack_anim_x * scale);
                int attack_tile_y = olm_row_y;  // Attack targets are in Olm row
                int slash_size = 20 - frame;
                DrawRectangle(attack_tile_x + scale/2 - slash_size/2, attack_tile_y + scale/2 - slash_size/2,
                              slash_size, slash_size, (Color){255, 100, 100, 200});
            }

            // Draw overhead prayer box
            if (p->active_prayer >= 0) {
                Color prayer_color;
                switch (p->active_prayer) {
                    case 0: prayer_color = RED; break;    // Protect melee
                    case 1: prayer_color = BLUE; break;   // Protect mage
                    case 2: prayer_color = GREEN; break;  // Protect range
                    default: prayer_color = WHITE;
                }
                // Draw box above player tile
                int box_size = 14;
                int box_x = center_x - box_size / 2;
                int box_y = tile_y - box_size - 4;
                DrawRectangle(box_x, box_y, box_size, box_size, prayer_color);
                DrawRectangleLines(box_x, box_y, box_size, box_size, WHITE);
            }

            // Draw HP bar above player tile
            float hp_pct = (float)p->hp / p->max_hp;
            int player_bar_w = scale - 4;
            int player_bar_y = tile_y - 8;
            DrawRectangle(tile_x + 2, player_bar_y, player_bar_w, 5, DARKGRAY);
            DrawRectangle(tile_x + 2, player_bar_y, (int)(player_bar_w * hp_pct), 5,
                hp_pct > 0.5f ? GREEN : (hp_pct > 0.25f ? YELLOW : RED));
        } else {
            // Dead player indicator
            DrawText("X", center_x - 8, center_y - 12, 24, GRAY);
        }
    }

    // Draw tick counter in HP bar area
    char tick_text[64];
    snprintf(tick_text, sizeof(tick_text), "Tick: %d", env->tick);
    DrawText(tick_text, width - 80, hp_bar_y + 12, 16, WHITE);

    EndDrawing();
    }  // End animation loop
}

void c_close(Raid* env) {
    free(env->players);
    if (env->client != NULL) {
        UnloadTexture(env->client->puffer);
        CloseWindow();
        free(env->client);
    }
}
