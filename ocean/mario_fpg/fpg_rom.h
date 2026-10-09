#pragma once
// Evaluation adapter only. Training does not link the emulator or read ROM data.
#include "fpg_core.h"
#include "fpg_enemies.h"
#include "../retro/smb1_tiles.h"
static inline FpgActors fpg_rom_actors(const unsigned char* m) {
    FpgActors a={};a.frame=m[9];a.interval=m[0x77f];a.stomp=m[0x791];
    a.injury=m[0x79e];a.hard=m[0x76a];a.timer_control=m[0x747];
    // IDs >= 0x24 are scripted scenery (castle flag, flagpole flag, etc.).
    // The pole has its own controller; this array is the normal enemy system.
    for(int k=0;k<FPG_ENEMIES;k++)if(m[0xf+k]&&m[0x16+k]<0x24) {
        FpgEnemy* e=&a.slots[k];e->active=m[0xf+k];e->type=m[0x16+k];e->state=m[0x1e + k];
        e->x=m[0x6e + k]*256+m[0x87+k];e->y=((int)m[0xb6+k]-1)*256+m[0xcf+k];
        e->vx=(int8_t)m[0x58+k];e->vy=(int8_t)m[0xa0+k];e->xsub=m[0x401+k];
        e->ysub=m[0x417+k];e->vyfrac=m[0x434+k];e->direction=m[0x46+k];
        e->collision=m[0x491+k];e->timer=m[0x796+k];
    }
    return a;
}
static inline FpgBody fpg_rom_body(const unsigned char* m) {
    FpgBody b = {};
    b.x=m[0x6d]*256+m[0x86]; b.y=((int)m[0xb5]-1)*256+m[0xce];
    b.vx=(int8_t)m[0x57]; b.vy=(int8_t)m[0x9f];
    b.xsub=m[0x400]; b.ysub=m[0x416]; b.ax=m[0x705]; b.vyfrac=m[0x433];
    b.motion=m[0x1d]; b.facing=m[0x33]; b.moving=m[0x45];
    b.abs_vx=m[0x700]; b.running=m[0x703]; b.run_timer=m[0x783];
    b.gravity=m[0x709]; b.fall_gravity=m[0x70a]; b.jump_y=m[0x708];
    b.previous_ab=m[0xd]; b.collision=m[0x490]; b.side_timer=m[0x785];
    b.routine=m[0xe]; b.flag_y=m[0xd4]; b.flag_fraction=m[0x41c]; b.grab_y=m[0x70f];
    return b;
}
static inline FpgCamera fpg_rom_camera(const unsigned char* m) {
    FpgCamera c={1,m[0x71a]*256+m[0x71c],m[0x723],m[0x755],
        m[0x725]*16+m[0x726],m[0x71f],m[0x73d]};return c;
}
static inline int fpg_rom_last_column(const unsigned char* m) {
    return smb1_last_written_column(m);
}
static inline int fpg_rom_raw_tile(const unsigned char* m,int col,int row) {
    return smb1_resident_tile(m,col,row);
}
static inline int fpg_rom_collision_tile(int raw) {
    if(raw<=0||raw==0x26||raw==0xc2||raw==0xc3||raw==0x5f||raw==0x60)return FPG_EMPTY;
    return raw==0x24||raw==0x25?FPG_POLE:FPG_SOLID;
}
static inline void fpg_rom_world_version(const unsigned char* m, FpgWorld* w,int version) {
    memset(w,0,sizeof(*w));
    int current=(m[0x6d]*256+m[0x86])/16;
    w->origin_col=current-16; w->pole_col=-1;
    int parsed=version==1?m[0x725]*16+m[0x726]:fpg_rom_last_column(m);
    for (int row=2;row<15;row++) for(int col=parsed-31;col<=parsed;col++) {
        if(col<0) continue;
        int raw=m[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)];
        int tile=raw?FPG_SOLID:FPG_EMPTY;
        if(raw==0x24||raw==0x25) {tile=FPG_POLE; w->pole_col=col;}
        if(raw==0x26||raw==0xc2||raw==0xc3||raw==0x5f||raw==0x60) tile=FPG_EMPTY;
        fpg_put(w,col,row,tile);
        if(col>=w->origin_col&&col<w->origin_col+FPG_COLS)w->known[col-w->origin_col]=1;
    }
}
static inline void fpg_rom_world(const unsigned char* m,FpgWorld* w) {fpg_rom_world_version(m,w,2);}
static inline int fpg_rom_mask(int buttons) {
    return ((buttons&FPG_A)?1:0)|((buttons&FPG_B)?2:0)
        |((buttons&FPG_UP)?16:0)|((buttons&FPG_DOWN)?32:0)
        |((buttons&FPG_L)?64:0)|((buttons&FPG_R)?128:0);
}
static inline int fpg_from_ml(int action) {
    return ((action&1)?FPG_A:0)|((action&2)?FPG_B:0)|((action&4)?FPG_UP:0)
        |((action&8)?FPG_DOWN:0)|((action&16)?FPG_L:0)|((action&32)?FPG_R:0);
}
