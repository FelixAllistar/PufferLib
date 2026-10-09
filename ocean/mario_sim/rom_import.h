#pragma once
#include "logic.h"
#include "../retro/nes_emu/Nes_Emu.h"

static inline SmbLogic smb_import_logic(Nes_Emu& e) {
    SmbLogic s={};memcpy(s.ram,e.low_mem(),SMB_RAM);const auto& r=e.cpu_debug().r;
    s.pc=r.pc;s.a=r.a;s.x=r.x;s.y=r.y;s.sp=r.sp;s.p=r.status;
    uint8_t input[3];e.controller_shift_debug(input);
    s.strobe=input[0]&1;s.joy_shift[0]=input[1];s.joy_shift[1]=input[2];
    int q[12];e.frame_clock_debug(q);SmbClock* c=&s.timing;
    c->enabled=1;c->timestamp=q[0];c->video_frame=q[1];c->control=q[2];c->mask=q[3];c->status=q[4];
    c->sprite_y=q[6];c->sprite_x=q[7];c->sprite_tile=q[8];
    c->open_bus=q[9];c->decay_low=q[10];c->decay_high=q[11];e.sprite_ram_debug(c->sprites);
    return s;
}
