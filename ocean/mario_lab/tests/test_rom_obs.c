#include "../ml_rom_obs.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void set_tile(unsigned char* m, int col, int row, int value) {
    m[0x500 + ((col & 16) ? 0xd0 : 0) + (row - 2) * 16 + (col & 15)] = value;
}
int main(void) {
    unsigned char m[2048] = {0};
    m[0x725] = 2; m[0x726] = 6; // next column 38; resident ring 6..37
    m[0x71a] = 1; m[0x71c] = 0; // scroll column 16
    set_tile(m, 15, 13, 0x54); set_tile(m, 16, 13, 0x51); set_tile(m, 32, 13, 0x10);
    assert(ml_rom_tile(m, 15, 13) == 0x54);
    assert(ml_rom_tile(m, 16, 13) == 0x51);
    assert(ml_rom_tile(m, 32, 13) == 0x10);
    assert(ml_rom_tile(m, 14, 13) == -1 && ml_rom_tile(m, 39, 13) == -1);
    // The cursor must not expose the stale tile from one ring period ago.
    set_tile(m,6,13,0x51);
    assert(ml_rom_tile(m,38,13)==-1);
    m[0x71f]=7;set_tile(m,38,13,0x54);
    assert(ml_rom_tile(m,38,13)==0x54);
    assert(smb1_resident_tile(m,6,13)==-1);
    m[0x71f]=4;assert(ml_rom_tile(m,38,13)==-1);
    assert(ml_rom_collision_tile(0x54) == ML_SOLID);
    assert(ml_rom_collision_tile(0x51) == ML_BRICK);
    assert(ml_rom_collision_tile(0xc2) == ML_AIR);
    memset(m, 0, sizeof(m));
    m[0x725] = 2; m[0x726] = 6; m[0x71a] = 1;
    m[0x6d] = 1; m[0x86] = 64; m[0x400] = 73;
    m[0xb5] = 1; m[0xce] = 176; m[0x416] = 91;
    m[0x57] = 0xfe; m[0x9f] = 0xfd; m[0x433] = 17;
    m[0x33] = 1; m[0x754] = 1; m[0x74e] = 1; m[0xe] = 8; m[0x772] = 3;
    for (int col = 15; col <= 38; col++) { set_tile(m, col, 13, 0x54); set_tile(m, col, 14, 0x54); }
    m[0x0f] = 1; m[0x16] = 6; m[0x6e] = 1; m[0x87] = 100;
    m[0xb6] = 1; m[0xcf] = 184; m[0x58] = 0xf8;
    MLRomContext context = {0}; context.frame_limit = 4000;
    MLConfig c = ml_default_config(); MLRomView a, b; float oa[ML_OBS_SIZE], ob[ML_OBS_SIZE];
    ml_rom_observe(m, &context, &c, &a, oa);
    assert(a.player_x == 323 && a.player_feet == 208);
    assert(a.state.vx == -32 && a.state.vy == -3 * ML_FP + 17);
    assert(!a.unknown_cells && a.known_cells == ML_GRID_W * ML_GRID_H);
    assert(a.state.enemies[0].y == 208 * ML_FP && a.state.enemies[0].vx == -128);
    assert(oa[4] == 73 / 255.0f && oa[5] == 91 / 255.0f);
    m[0x725] = 1; m[0x726] = 8; // camera extends ahead of the parser
    ml_rom_observe(m, &context, &c, &b, ob); assert(b.unknown_cells > 0);
    for (int row = 0; row < ML_GRID_H; row++)
        assert(ob[ML_EGO + ML_ENEMIES * ML_ENT_FEATURES + (row * ML_GRID_W + 15) * ML_GRID_C + 3] == 0);
    m[0x725] = 2; m[0x726] = 6;
    // Whole ring-period translation must preserve the complete semantic observation.
    m[0x6d] += 2; m[0x6e] += 2; m[0x725] += 2; m[0x71a] += 2;
    ml_rom_observe(m, &context, &c, &b, ob);
    assert(!memcmp(oa, ob, sizeof(oa)));
    // Defeated Goombas must vanish rather than turn into stationary hazards.
    m[0x1e] = 4; ml_rom_observe(m, &context, &c, &b, ob); assert(!b.state.enemies[0].alive);
    for (int i = 0; i < ML_OBS_SIZE; i++) assert(isfinite(ob[i]));
    puts("ROM adapter: ring addressing, resident bounds, terrain tags, fractional state, enemies and translation PASS");
}
