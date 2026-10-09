#include "../fpg_task.h"
#include "../fpg_rom.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    unsigned char ram[2048]={0};ram[0x725]=12;ram[0x726]=8; // cursor 200
    ram[0x500+(13-2)*16+8]=0x54; // column 168, aliased with 200
    for(int phase=0;phase<8;phase++) {
        ram[0x71f]=(unsigned char)phase;
        int written=(phase&3)!=0;
        assert(fpg_rom_last_column(ram)==199+written);
        assert(fpg_rom_raw_tile(ram,200,13)==(written?0x54:-1));
        assert(fpg_rom_raw_tile(ram,168,13)==(written?-1:0x54));
        assert(fpg_rom_raw_tile(ram,167,13)==-1);
        assert(fpg_rom_raw_tile(ram,201,13)==-1);
    }
    FpgState s={0};FpgConfig cfg={0};cfg.contract_version=2;cfg.max_frames=240;
    s.camera.enabled=1;s.camera.left=100;s.body.x=200;s.body.y=176;s.world.pole_col=30;
    memset(s.world.known,1,sizeof(s.world.known));
    assert(fpg_visible_column(&s,6)&&fpg_visible_column(&s,22));
    assert(!fpg_visible_column(&s,5)&&!fpg_visible_column(&s,23));
    assert(fpg_observation(&s,&cfg,23)==0&&fpg_observation(&s,&cfg,27)==0);
    s.world.pole_col=20;assert(fpg_observation(&s,&cfg,27)==1);
    // Grid starts at col 8, row 5. Rows below the block buffer have no hidden floor.
    fpg_put(&s.world,8,15,FPG_SOLID);
    assert(fpg_observation(&s,&cfg,FPG_EGO+10*FPG_GRID_W*2)==0);
    s.world.known[8]=0;
    assert(fpg_observation(&s,&cfg,FPG_EGO)==-1&&fpg_observation(&s,&cfg,FPG_EGO+1)==-1);
    s.world.known[8]=1;assert(fpg_observation(&s,&cfg,FPG_EGO)==0);
    // Negative controls: legacy padding and unknown-space behavior differ.
    cfg.contract_version=1;
    assert(fpg_observation(&s,&cfg,FPG_EGO+10*FPG_GRID_W*2)==1);
    s.world.known[8]=0;assert(fpg_observation(&s,&cfg,FPG_EGO)==0);
    puts("FPG contract: all parser phases, 32-column aliasing, viewport, unknown space and row padding PASS");
    return 0;
}
