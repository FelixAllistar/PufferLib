#pragma once
#include "scene.h"

// Named initial-condition setters shared by clip construction and future
// procedural scene generation. These modify the reset template only; they
// must never inject reference state during replay.
SMB_HD int smb_scene_powerup(SmbScene* scene,int type,int emerged) {
    if(type<0||type>3)return -1;
    auto* m=scene->initial.ram;
    if(!m[0x14]||m[0x1b]!=46)return -2;
    m[0x39]=(uint8_t)type;
    if(emerged){m[0x23]=0x80;m[0x5d]=16;m[0x4b]=1;}
    return 0;
}
SMB_HD void smb_scene_player_effects(SmbScene* scene,int star_timer,int injury_timer) {
    scene->initial.ram[0x79f]=(uint8_t)star_timer;
    scene->initial.ram[0x79e]=(uint8_t)injury_timer;
}
SMB_HD void smb_scene_difficulty(SmbScene* scene,int world,int primary,int secondary) {
    scene->initial.ram[0x75f]=(uint8_t)world;
    scene->initial.ram[0x76a]=(uint8_t)primary;
    scene->initial.ram[0x6cc]=(uint8_t)secondary;
}
