#pragma once
#include "logic.h"

// A scene is a complete initial-condition template plus explicit overrides.
// ROM clip import and procedural interventions go through this same reset.
// Keeping residue is intentional: scratch/OAM/parser history is part of exact
// reconstruction. This is not yet a generator of arbitrary complete levels.
typedef struct {
    SmbLogic initial;
    uint32_t mask;
    int player_x,player_y,player_xsub,player_ysub,player_vx,player_vy;
    int player_form,player_size,frame,interval;
    uint8_t random[7];
    int actor_slot,actor_x,actor_y,actor_frame_timer,actor_interval_timer;
    int tile_column,tile_row,tile_value;
} SmbScene;
enum {SMB_SCENE_PLAYER=1,SMB_SCENE_FORM=2,SMB_SCENE_CLOCK=4,
      SMB_SCENE_ACTOR=8,SMB_SCENE_TILE=16};

SMB_HD int smb_scene_reset(SmbLogic* s,const SmbScene* scene) {
    *s=scene->initial;s->fault=0;s->instructions=0;
    if(scene->mask&SMB_SCENE_PLAYER) {
        smb_set_player_xy(s,scene->player_x,scene->player_y);
        s->ram[0x400]=(uint8_t)scene->player_xsub;s->ram[0x416]=(uint8_t)scene->player_ysub;
        s->ram[0x57]=(uint8_t)scene->player_vx;s->ram[0x9f]=(uint8_t)scene->player_vy;
    }
    if(scene->mask&SMB_SCENE_FORM) {
        s->ram[0x756]=(uint8_t)scene->player_form;s->ram[0x754]=(uint8_t)scene->player_size;
    }
    if(scene->mask&SMB_SCENE_CLOCK)smb_set_clock(s,scene->frame,scene->interval,scene->random);
    if(scene->mask&SMB_SCENE_ACTOR) {
        int k=scene->actor_slot;
        if(k<0||k>=6)return s->fault=-2;
        smb_set_actor_xy(s,k,scene->actor_x,scene->actor_y);
        if(k<5){s->ram[0x78a+k]=(uint8_t)scene->actor_frame_timer;s->ram[0x796+k]=(uint8_t)scene->actor_interval_timer;}
    }
    if(scene->mask&SMB_SCENE_TILE) {
        int cursor=s->ram[0x725]*16+s->ram[0x726];
        int last=cursor-((s->ram[0x71f]&3)==0),col=scene->tile_column,row=scene->tile_row;
        if(col<last-31||col>last||row<2||row>14)return s->fault=-3;
        s->ram[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)]=(uint8_t)scene->tile_value;
    }
    return s->fault;
}
