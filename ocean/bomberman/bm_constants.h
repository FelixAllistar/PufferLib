#pragma once

// Classic Bomberman-style match for multiagent self-play.
// Bounds intentionally match the shipped 13x11 training arena. Keeping the
// state compact matters because every CPU worker touches one BMMatch per step.
#define BM_MAX_W 13
#define BM_MAX_H 11
#define BM_MAX_CELLS (BM_MAX_W * BM_MAX_H)
#define BM_MAX_AGENTS 4
#define BM_MAX_BOMBS_PER_AGENT 8
#define BM_MAX_BOMBS (BM_MAX_AGENTS * BM_MAX_BOMBS_PER_AGENT)
#define BM_MAX_FLAME_RANGE 8
#define BM_MAX_SPEED_LEVEL 3
#define BM_SPAWN_INVULN 12
#define BM_DANGER_SAFE 255
#define BM_FLAME_OWNER_NONE (-1)
#define BM_FLAME_OWNER_MIXED (-2)

// Discrete action head. Directions are expressed in each agent's canonical
// frame; the simulator mirrors them back to world coordinates per spawn slot.
#define BM_ACT_STAY 0
#define BM_ACT_UP 1
#define BM_ACT_DOWN 2
#define BM_ACT_LEFT 3
#define BM_ACT_RIGHT 4
#define BM_ACT_BOMB 5
#define BM_NUM_ACTIONS 6

// Reverse combat curriculum: adjacent stationary finish, farther stationary
// target, moving target, plausible midgame, then untouched opening. Reaching
// level 5 exits the curriculum into ordinary self-play.
#define BM_CURRICULUM_STAGES 5

// Tile kinds stored in the static map layer.
#define BM_TILE_EMPTY 0
#define BM_TILE_HARD 1
#define BM_TILE_SOFT 2

// Powerups hidden under soft blocks and revealed after destruction.
#define BM_ITEM_NONE 0
#define BM_ITEM_BOMB 1
#define BM_ITEM_FLAME 2
#define BM_ITEM_SPEED 3

// Canonical full-board float observation in [0, 1]. This uses PufferLib's
// standard float observation path and requires no shared-framework changes.
//
// Every agent sees its original spawn corner mirrored into the top-left. This
// makes shared self-play policies equivariant without requiring the MLP to learn
// four copies of every wall, escape, and attack pattern.
//
// Per cell (8 channels):
//   0 hard wall
//   1 soft block
//   2 item kind: 0 / 1/3 / 2/3 / 1
//   3 own-bomb fuse urgency (0 absent, 1 imminent)
//   4 enemy-bomb fuse urgency
//   5 earliest blast danger urgency (current flame = 1)
//   6 self position
//   7 any foe position
//
// Globals (16): progress, current danger, nearest-foe proximity, bombs-left,
// bomb-hits-foe, bomb-soft-count, bomb-escape, escape-margin, six legal-action
// bits, alive-foe fraction, legal-move fraction.
//
// Agent block (10 x 4), viewer first: alive, canonical x/y, bombs-left,
// max-bombs, range, speed, bombs-out, move cooldown, spawn invulnerability.
#define BM_CELL_CH 8
#define BM_GLOBAL_FEAT 16
#define BM_AGENT_FEAT 10
#define BM_OBS_SIZE (BM_MAX_CELLS * BM_CELL_CH \
    + BM_GLOBAL_FEAT \
    + BM_MAX_AGENTS * BM_AGENT_FEAT)

#define BM_NUM_ATNS 1
