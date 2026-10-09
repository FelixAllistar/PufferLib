#pragma once
// Live SMB1 RAM -> Mario Lab semantic contract. Evaluation only: no ROM data
// is imported into the generated training engine. Coordinates use screen feet.
// RAM layout reference: https://6502disassembly.com/nes-smb/SuperMarioBros.html
#include "ml_sim.h"
#include "../retro/smb1_tiles.h"

typedef struct MLRomContext {
    int tick, frame_limit, previous_action, jump_frames;
    int pipe_visits, pipe_returns;
} MLRomContext;
typedef struct MLRomView {
    int origin_col, player_x, player_feet, parsed_col, screen_left;
    int known_cells, unknown_cells, unknown_enemies, big_player;
    unsigned char raw_grid[ML_GRID_H * ML_GRID_W];
    MLState state;
} MLRomView;

static inline int ml_rom_s8(unsigned char x) { return x < 128 ? x : (int)x - 256; }
static inline int ml_rom_tile(const unsigned char* ram, int col, int row) {
    if (col < 0 || row < 2 || row >= 15) return 0;
    int left = (ram[0x71a] * 256 + ram[0x71c]) / 16;
    if (col < left - 1) return -1;
    return smb1_resident_tile(ram,col,row);
}
static inline int ml_rom_collision_tile(int tile) {
    if (tile <= 0 || tile == 0x26 || tile == 0xc2 || tile == 0xc3 || tile == 0x5f || tile == 0x60
            || tile == 0x24 || tile == 0x25) return ML_AIR;
    if (tile >= 0x10 && tile <= 0x21) return ML_PIPE;
    if (tile >= 0x51 && tile <= 0x5e && tile != 0x54) return ML_BRICK;
    return ML_SOLID;
}
static inline void ml_rom_observe(const unsigned char* ram, const MLRomContext* context,
        const MLConfig* config, MLRomView* view, float* obs) {
    memset(view, 0, sizeof(*view)); MLState* s = &view->state;
    int px = ram[0x6d] * 256 + ram[0x86] + 3;
    int py = ((int)ram[0xb5] - 1) * 256 + ram[0xce] + 32;
    view->player_x = px; view->player_feet = py;
    view->origin_col = px / 16 - 32;
    view->parsed_col = ram[0x725] * 16 + ram[0x726];
    view->screen_left = ram[0x71a] * 256 + ram[0x71c];
    view->big_player = ram[0x754] == 0;
    s->widths[0] = s->widths[1] = 64;
    s->room = ram[0x74e] == 2;
    s->x = (px - view->origin_col * 16) * ML_FP + ram[0x400];
    s->y = py * ML_FP + ram[0x416];
    s->vx = ml_rom_s8(ram[0x57]) * 16; // horizontal movement uses 1/16 pixel speed
    s->vy = ml_rom_s8(ram[0x9f]) * ML_FP + ram[0x433];
    s->grounded = ram[0x1d] == 0; s->facing = ram[0x33] == 1 ? 1 : -1;
    s->previous_action = context->previous_action; s->jump_frames = context->jump_frames;
    s->tick = context->tick; s->episode_limit = context->frame_limit;
    s->pipe_visits = context->pipe_visits; s->pipe_returns = context->pipe_returns;
    s->transition_frames = (ram[0xe] != 8 || ram[0x772] != 3) ? 24 : 0;
    s->entry_x = -100; s->goal_x = 1000000; s->goal_room = 0;
    s->exit_x = -100;
    for (int col = 0; col < 64; col++) for (int row = 0; row < ML_HEIGHT; row++) {
        int raw = ml_rom_tile(ram, col + view->origin_col, row);
        s->tiles[s->room][row][col] = (unsigned char)ml_rom_collision_tile(raw);
        if (!s->room && raw == 0x10) { s->entry_x = col; s->entry_y = row; }
        if (s->room && raw == 0x1c) s->exit_x = col;
        if (!s->room && (raw == 0x24 || raw == 0x25)) s->goal_x = col * ML_TILE;
    }
    for (int j = 0; j < 5; j++) {
        int id = ram[0x16 + j], state = ram[0x1e + j];
        if (!ram[0x0f + j] || (state & 0x20) || (id == 6 && state == 4) || id >= 0x24) continue;
        MLEnemy* e = &s->enemies[j]; e->alive = 1; e->room = s->room; e->activated = 1;
        e->x = (ram[0x6e + j] * 256 + ram[0x87 + j] + 2 - view->origin_col * 16) * ML_FP + ram[0x401 + j];
        // Logical ground contact matches the synthetic entity record. The
        // real Goomba's visible/contact box ends three pixels above this.
        e->y = (((int)ram[0xb6 + j] - 1) * 256 + ram[0xcf + j] + 24) * ML_FP;
        e->vx = ml_rom_s8(ram[0x58 + j]) * 16;
        e->vy = ml_rom_s8(ram[0xa0 + j]) * ML_FP + ram[0x434 + j];
        if (id != 6 && id != 0 && id != 1) view->unknown_enemies++;
    }
    int first_col = ml_floor(s->x + ML_PLAYER_W / 2, ML_TILE) - 4;
    int first_row = ml_floor(s->y, ML_TILE) - 8;
    for (int r = 0; r < ML_GRID_H; r++) for (int c = 0; c < ML_GRID_W; c++) {
        int raw = ml_rom_tile(ram, first_col + c + view->origin_col, first_row + r);
        view->raw_grid[r * ML_GRID_W+c] = raw < 0 ? 255 : (unsigned char)raw;
        if (raw < 0) view->unknown_cells++; else view->known_cells++;
    }
    ml_observe(s, config, obs);
    // Unparsed/replaced columns are not evidence of a pit. Preserve the
    // existing input shape while suppressing unsupported-space claims when
    // the support geometry is unknown to the live block buffer.
    for (int r = 0; r < ML_GRID_H; r++) for (int col = 0; col < ML_GRID_W; col++) {
        int world_col = first_col + col + view->origin_col;
        // Residency is a property of the whole column, including cells of
        // the observation window above/below the block-buffer row range.
        int unknown = ml_rom_tile(ram, world_col, 13) < 0;
        if (unknown) obs[ML_EGO + ML_ENEMIES * ML_ENT_FEATURES
            + (r * ML_GRID_W + col) * ML_GRID_C + 3] = 0;
    }
}
