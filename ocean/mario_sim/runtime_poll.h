#pragma once
#include "logic.h"

// Collapse complete iterations of SMB's LDA $2002 / AND #$40 / branch
// polling loops. The first and final reads retain PPU latch/decay effects.
// Leave interrupt/frame boundaries and the first changed result to the normal
// instruction path, including its partial-instruction boundary state.
SMB_HD void smb_runtime_poll_skip(SmbLogic* s,int wait_for_set) {
    SmbClock* c=&s->timing;
    if(!c->enabled)return;
    int begin=c->clock,event;
    if(wait_for_set) {
        if(c->status&64||begin+3<=2272)return;
        event=c->length;
        if(c->sprite_y<240&&(c->mask&24)==24) {
            if(c->sprite_tile!=255)return;
            int hit=21*341+339+(c->sprite_y+6)*341+c->sprite_x+2-341;
            event=(hit-c->extra+2)/3-3;
        }
    } else {
        if(!(c->status&64)||c->vblank_cleared)return;
        event=2270;
    }
    if(event<=begin)return;
    int stop=c->length<c->nmi?c->length:c->nmi;
    int count=(event-begin+8)/9;
    // A status read can adjust odd-frame length by one cycle. Keep enough
    // margin that neither that adjustment nor a partial loop is skipped.
    int maximum=(stop-begin-12)/9;
    if(count>maximum)count=maximum;
    if(count<2)return;
    c->clock=begin+4;smb_clock_status(s);
    c->clock=begin+(count-1)*9+4;
    s->a=(uint8_t)(smb_clock_status(s)&64);smb_nz(s,s->a);
    c->clock+=5;s->instructions+=(unsigned)count*3;
}
