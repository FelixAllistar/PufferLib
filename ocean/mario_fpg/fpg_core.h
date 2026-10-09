#pragma once
// Original, integer-only small-player controller for the precision experiment.
// Units: x/y are pixels; vx is 1/16 pixel/frame; ax is 1/256 of vx.
// The separate xsub, ysub and vyfrac accumulators must not be collapsed.
#include <stdint.h>
#include <string.h>

#ifdef __CUDACC__
#define FPG_HD __host__ __device__ static inline
#else
#define FPG_HD static inline
#endif

enum { FPG_R=1, FPG_L=2, FPG_DOWN=4, FPG_UP=8, FPG_B=64, FPG_A=128 };
enum { FPG_EMPTY, FPG_SOLID, FPG_POLE };
enum { FPG_ACTIVE, FPG_SUCCESS, FPG_NORMAL_FLAG, FPG_DEAD, FPG_TIMEOUT };
#define FPG_COLS 64
#define FPG_ROWS 16

typedef struct {
    int x, y, vx, vy, xsub, ysub, ax, vyfrac;
    int motion, facing, moving, abs_vx, running, run_timer;
    int gravity, fall_gravity, jump_y, previous_ab, collision, side_timer;
    int routine, flag_y, flag_fraction, grab_y;
} FpgBody;

typedef struct {
    unsigned char tiles[FPG_COLS * FPG_ROWS];
    unsigned char known[FPG_COLS];
    int origin_col, pole_col;
} FpgWorld;

// Camera position affects both movement at the screen edge and what is known.
// Kept outside FpgBody so the archived v1 teacher-state bank remains readable.
typedef struct {
    int enabled, left, locked, relative_x;
    int parsed_col, parser_task, scroll32;
} FpgCamera;

FPG_HD int fpg_floor16(int x) { return x >= 0 ? x / 16 : -((-x + 15) / 16); }
FPG_HD int fpg_tile(const FpgWorld* w, int x, int y) {
    int col = fpg_floor16(x) - w->origin_col, row = fpg_floor16(y);
    if (col < 0 || col >= FPG_COLS || row < 0 || row >= FPG_ROWS) return FPG_EMPTY;
    return w->tiles[row * FPG_COLS + col];
}
FPG_HD void fpg_put(FpgWorld* w, int col, int row, int tile) {
    col -= w->origin_col;
    if (col >= 0 && col < FPG_COLS && row >= 0 && row < FPG_ROWS)
        w->tiles[row * FPG_COLS + col] = (unsigned char)tile;
}
FPG_HD int fpg_column_known(const FpgWorld* w, int col) {
    col -= w->origin_col;
    return col >= 0 && col < FPG_COLS && w->known[col];
}
FPG_HD FpgCamera fpg_camera_initial(const FpgBody* b) {
    FpgCamera c = {1, b->x > 112 ? b->x - 112 : 0, 0, 0, 0, 0, 0};
    c.relative_x = b->x - c.left;
    c.parsed_col = (c.left / 32) * 2 + 24;
    c.scroll32 = c.left & 31;
    return c;
}
FPG_HD void fpg_scroll(FpgBody* b, FpgCamera* c, int displacement, int lr) {
    if (!c || !c->enabled) return;
    if (!c->locked && c->relative_x >= 80 && !b->side_timer && displacement > 0) {
        int amount = displacement;
        if (c->relative_x < 112 && amount >= 2) --amount;
        c->left += amount;
        c->scroll32 += amount;
    }
    // The left edge includes equality; the right edge leaves a 16-pixel margin.
    // Speed is retained only for input pointing back into the viewport.
    if (b->x <= c->left) {
        b->x = c->left;
        if (lr != FPG_R) b->vx = 0;
    } else if (b->x >= c->left + 240) {
        b->x = c->left + 239;
        if (lr != FPG_L) b->vx = 0;
    }
}
FPG_HD void fpg_parse_column(FpgBody* b,const FpgWorld* w,FpgCamera* c) {
    if(!c||!c->enabled)return;
    if(!c->parser_task) {
        if(c->scroll32<32)return;
        c->scroll32-=32;c->parser_task=8;
    }
    int task=--c->parser_task;
    // Each new 32-pixel strip takes eight tasks. The collision map is static,
    // but the pole's flag is instantiated by its column's parser task.
    if((task==7||task==3)&&c->parsed_col==w->pole_col)b->flag_y=48;
    if(task==4||task==0)++c->parsed_col;
}
FPG_HD void fpg_impede(FpgBody* b, int direction) {
    if ((direction == 1 && b->vx >= 0) || (direction != 1 && b->vx < 1)) {
        b->x += direction == 1 ? -1 : 1;
        b->vx = 0; b->side_timer = 16;
    }
    b->collision &= ~(direction == 1 ? 1 : 2);
}
FPG_HD void fpg_collide(FpgBody* b, const FpgWorld* w) {
    if (b->motion == 0 || b->motion == 3) b->motion = 2;
    if (b->y < 0 || b->y >= 256) return;
    b->collision = 255;
    if (b->y >= 207) return;
    if (b->y >= 16 && b->vy < 0 && (b->y & 15) >= 4
        && fpg_tile(w, b->x + 8, b->y + 18) != FPG_EMPTY) b->vy = 1;
    int foot = fpg_tile(w, b->x + 3, b->y + 32);
    if (!foot) foot = fpg_tile(w, b->x + 12, b->y + 32);
    if (foot == FPG_SOLID && b->vy >= 0) {
        if ((b->y & 15) >= 5) { fpg_impede(b, b->moving); return; }
        b->y &= ~15; b->vy = 0; b->vyfrac = 0; b->motion = 0;
    }
    if (b->y < 8 || b->y >= 208) return;
    for (int side = 2; side >= 1; --side) {
        int probe_x = b->x + (side == 2 ? 2 : 13);
        int tile = fpg_tile(w, probe_x, b->y + 24);
        if (!tile) continue;
        if (tile == FPG_POLE) {
            if ((b->x & 15) < 6 || (b->x & 15) >= 10) return;
            b->grab_y = b->y; b->motion = 3; b->routine = 4;
            b->facing = 1; b->vx = 0; b->ax = 0;
            b->x = fpg_floor16(probe_x) * 16 - 7;
        } else fpg_impede(b, side);
        return;
    }
}

// One native gameplay frame through first pole contact. Enemies, powerups,
// swimming, pipe transitions, and post-flag movement need separate controllers.
FPG_HD void fpg_player_frame(FpgBody* b, const FpgWorld* w, FpgCamera* camera, int buttons, int* bounds) {
    if (b->routine != 8) return;
    int previous_x = b->x;
    if (b->run_timer) --b->run_timer;
    if (b->side_timer) --b->side_timer;
    int lr = buttons & 3, ab = buttons & 192;
    if ((buttons & FPG_DOWN) && b->motion == 0 && lr) lr = 0;
    if ((ab & FPG_A) && !(b->previous_ab & FPG_A) && b->motion == 0) {
        int fast = b->abs_vx >= 25;
        b->ysub = b->vyfrac = 0; b->jump_y = b->y;
        b->motion = 1; b->vy = fast ? -5 : -4;
        b->gravity = fast ? 40 : b->abs_vx >= 16 ? 30 : 32;
        b->fall_gravity = fast ? 144 : b->abs_vx >= 16 ? 96 : 112;
    }
    int run = 0;
    if (b->motion != 0) run = b->abs_vx >= 25;
    else if (lr == b->moving) {
        if (ab & FPG_B) b->run_timer = 10;
        run = b->run_timer != 0;
    }
    int cap = run ? 40 : 24;
    int friction = run ? 228 : (b->running || b->abs_vx >= 33) ? 208 : 152;
    if (b->facing != b->moving) friction *= 2;
    if (b->motion == 0) {
        if (b->abs_vx >= 28) b->running = b->abs_vx;
        else if (buttons & 127) {
            if ((buttons & 3) == b->moving) b->running = 0;
            else if (b->abs_vx < 11) { b->moving = b->facing; b->vx = 0; b->ax = 0; }
        }
        if (lr) b->facing = lr;
    } else if (b->motion == 2 || b->vy >= 0
        || (!((ab & b->previous_ab) & FPG_A) && ((b->jump_y - b->y) & 255) >= 1)) {
        b->gravity = b->fall_gravity;
    }
    if (b->motion == 0 || lr) {
        int direction = lr & b->collision;
        if (direction || b->vx) {
            int positive = direction ? (direction & 1) : (b->vx < 0);
            int v = b->vx * 256 + b->ax + (positive ? friction : -friction);
            b->ax = v & 255;
            b->vx = (v - b->ax) / 256;
            if (positive && b->vx >= cap) b->vx = cap;
            if (!positive && b->vx < -cap) b->vx = -cap;
        }
        b->abs_vx = b->vx < 0 ? -b->vx : b->vx;
    }
    int fractional = b->xsub + (b->vx & 15) * 16;
    b->xsub = fractional & 255;
    b->x += fpg_floor16(b->vx) + (fractional >> 8);
    if (b->motion != 0) {
        fractional = b->ysub + b->vyfrac;
        b->ysub = fractional & 255; b->y += b->vy + (fractional >> 8);
        fractional = b->vyfrac + b->gravity;
        b->vyfrac = fractional & 255; b->vy += fractional >> 8;
        if (b->vy >= 4 && b->vyfrac >= 128) { b->vy = 4; b->vyfrac = 0; }
    }
    if (b->vx) b->moving = b->vx < 0 ? 2 : 1;
    fpg_scroll(b, camera, b->x - previous_x, lr);
    // The ROM constructs the player box before background collision adjusts
    // the position. Enemy collisions later in this frame use that same box.
    if(bounds) {bounds[0]=b->x+3;bounds[1]=b->y+20;bounds[2]=b->x+13;bounds[3]=b->y+32;}
    fpg_collide(b, w);
    if (b->routine == 4 && b->motion == 3) {
        if (camera && camera->enabled) ++camera->locked;
        if (b->y >= 162 || b->flag_y >= 170) b->routine = 5;
        else {
            int fraction = b->flag_fraction + 255;
            b->flag_fraction = fraction & 255; b->flag_y += 1 + (fraction >> 8);
        }
    }
    b->previous_ab = ab;
    if (camera && camera->enabled) camera->relative_x = (b->x - camera->left) & 255;
    fpg_parse_column(b,w,camera);
}

FPG_HD void fpg_physics_camera(FpgBody* b, const FpgWorld* w, FpgCamera* camera, int buttons) {
    fpg_player_frame(b,w,camera,buttons,NULL);
}

// The archived v1 experiment deliberately remains replayable without a camera.
FPG_HD void fpg_physics(FpgBody* b, const FpgWorld* w, int buttons) {
    fpg_physics_camera(b, w, NULL, buttons);
}

// Twelve legal controller combinations, including release-A and reverse frames.
FPG_HD int fpg_buttons(int action) {
    int direction = action % 3;
    return (direction == 1 ? FPG_R : direction == 2 ? FPG_L : 0)
        | ((action / 3) & 1 ? FPG_A : 0) | (action >= 6 ? FPG_B : 0);
}
