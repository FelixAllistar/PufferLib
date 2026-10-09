#pragma once
// Whole-game logic feasibility backend. Generated control flow is compiled as
// C/CUDA; ROM data remains a local input. This is separate from the qualified
// hand-written FPG controller and is not an approved training environment.
#include <stdint.h>
#include <string.h>
#ifdef __CUDACC__
#define SMB_HD __host__ __device__ static inline
#define SMB_PAGE __host__ __device__ __noinline__ static
#else
#define SMB_HD static inline
#define SMB_PAGE static __attribute__((noinline))
#endif

enum {SMB_RAM=2048, SMB_PRG=32768, SMB_DEBUG_OBS=2048};
typedef struct {
    int enabled,timestamp,video_frame,clock,extra,length,frame_extra,nmi;
    int vblank_cleared,frame_ended,odd_checked;
    int overflow_time,overflow_checked;
    uint16_t decay_low,decay_high;
    uint8_t control,mask,status,sprite_y,sprite_x,sprite_tile,open_bus;
    uint8_t sprites[256];
} SmbClock;
typedef struct {
    uint8_t ram[SMB_RAM];
    uint16_t pc;
    uint8_t a,x,y,p,sp;
    uint8_t joy[2],joy_shift[2],strobe;
    uint32_t instructions;
    int fault;
    SmbClock timing;
} SmbLogic;

// The reference follows the NES's diagonal ninth-sprite scan, which can read
// tile/attribute/X bytes as Y coordinates after the first eight matches.
SMB_PAGE int smb_clock_overflow(const SmbClock* c,int start) {
    uint8_t counts[240]={};int height=(c->control&32)?16:8;
    if(start<0)start=0;if(start>=240)return 0x3fffffff;
    for(int i=0;i<256;i+=4) {
        int y=c->sprites[i],end=y+height;if(end>240)end=240;
        if(y<start)y=start;for(int row=y;row<end;row++)counts[row]++;
    }
    for(int row=start;row<240;row++)if(counts[row]>=8) {
        int remaining=8;
        for(int i=0;i<256;) {
            int relative=row-c->sprites[i];i+=4;
            if((unsigned)relative<(unsigned)height&&!--remaining) {
                int offset=0;
                while(i<256) {
                    relative=row-c->sprites[i+offset];i+=4;offset=(offset+1)&3;
                    if((unsigned)relative<(unsigned)height)return (row*341+i/2)/3+2423;
                }
                break;
            }
        }
    }
    return 0x3fffffff;
}
SMB_HD void smb_clock_overflow_update(SmbClock* c,int time) {
    if(time<=2423||c->status&32||!(c->mask&24))return;
    if(!c->overflow_checked){c->overflow_time=smb_clock_overflow(c,0);c->overflow_checked=1;}
    if(time>c->overflow_time)c->status|=32;
}
SMB_HD void smb_clock_bus_decay(SmbClock* c,int time) {
    if(time>=c->decay_low)c->open_bus&=224;
    if(time>=c->decay_high)c->open_bus&=31;
}

SMB_HD void smb_clock_length(SmbClock* c,int time) {
    if(!c->odd_checked&&time*3+c->extra>7148) {
        c->odd_checked=1;
        if(!(c->mask&8)||(c->video_frame&1)) {
            if(--c->frame_extra<0){c->frame_extra=2;c->length++;}
        }
    }
}
SMB_HD void smb_clock_vblank_end(SmbClock* c) {
    if(!c->vblank_cleared){c->status&=31;c->vblank_cleared=1;}
}
SMB_HD void smb_clock_frame_end(SmbClock* c,int time) {
    smb_clock_length(c,time);
    if(!c->frame_ended&&time>=c->length) {
        c->frame_ended=1;c->status|=128;
        if(c->control&128)c->nmi=c->length+2-(c->frame_extra>>1);
    }
}
SMB_HD void smb_clock_begin(SmbClock* c) {
    c->clock=c->timestamp/3;c->extra=c->timestamp%3;
    int end=89341-c->extra;c->length=(end+2)/3;c->frame_extra=c->length*3-end;
    c->nmi=(c->control&c->status&128)?2-(c->extra>>1):0x3fffffff;
    c->frame_ended=c->vblank_cleared=c->odd_checked=0;
    c->overflow_checked=0;c->overflow_time=0x3fffffff;
    c->decay_low=(uint16_t)(c->decay_low+c->clock);c->decay_high=(uint16_t)(c->decay_high+c->clock);
}
SMB_HD int smb_clock_status(SmbLogic* s) {
    SmbClock* c=&s->timing;int time=c->clock-1;smb_clock_length(c,time);
    if(time>2272) {
        smb_clock_vblank_end(c);
        smb_clock_overflow_update(c,time);
        // SMB's fixed status-bar marker: tile $ff has its first opaque pixel
        // at (2,6). Geometry/sprite edits outside this marker require a fuller
        // PPU contract; fail explicitly if its tile changes while visible.
        if(c->sprite_y<240&&(c->mask&24)==24) {
            if(c->sprite_tile!=255){s->fault=-4;return 0;}
            int hit=21*341+339+(c->sprite_y+6)*341+c->sprite_x+2-341;
            if(time*3+c->extra>=hit)c->status|=64;
        }
        smb_clock_frame_end(c,time);
        if(c->extra!=1&&time==c->length)c->nmi=0x3fffffff;
        if(c->extra==1&&time==c->length-1){c->status&=127;c->frame_ended=1;c->nmi=0x3fffffff;}
    }
    int value=c->status;c->status&=127;
    c->open_bus=(c->open_bus&31)|(value&224);c->decay_high=(uint16_t)(time+11366);
    smb_clock_bus_decay(c,time);return (value&224)|(c->open_bus&31);
}
SMB_HD void smb_clock_write(SmbLogic* s,int address,int value) {
    SmbClock* c=&s->timing;int time=c->clock-1;smb_clock_length(c,time);
    if(address==0x2000) {
        if((c->control^value)&128) {
            if(time>2272+((c->extra-1)>>2&1))smb_clock_vblank_end(c);
            if(value&c->status&128)c->nmi=time+2;
            if(time>=29770)smb_clock_frame_end(c,time-1+(c->extra&1));
        }
        c->control=(uint8_t)value;
    } else if(address==0x2001) {
        if(time>2272)smb_clock_vblank_end(c);
        smb_clock_overflow_update(c,time);c->mask=(uint8_t)value;c->overflow_checked=0;
    }
    else if(address==0x4014) {
        if(time>2272)smb_clock_vblank_end(c);
        smb_clock_overflow_update(c,time);c->overflow_checked=0;
        int base=(value*256)&2047;c->sprite_y=s->ram[base];c->sprite_tile=s->ram[base+1];c->sprite_x=s->ram[base+3];
        for(int i=0;i<256;i++)c->sprites[i]=s->ram[(base+i)&2047];
        c->clock+=513;
    }
    if(address>=0x2000&&address<=0x2007) {
        c->open_bus=(uint8_t)value;c->decay_low=c->decay_high=(uint16_t)(time+11366);
    }
}
SMB_HD void smb_clock_finish(SmbClock* c) {
    smb_clock_overflow_update(c,c->clock);smb_clock_bus_decay(c,c->clock);
    c->decay_low=(uint16_t)(c->decay_low-c->clock);c->decay_high=(uint16_t)(c->decay_high-c->clock);
    c->timestamp=(c->clock-c->length)*3+c->frame_extra;c->video_frame++;
}

SMB_HD void smb_nz(SmbLogic* s,int v) {
    s->p=(s->p&~130)|((v&255)?0:2)|(v&128);
}
SMB_HD void smb_push(SmbLogic* s,int v) {s->ram[256+s->sp]=(uint8_t)v;--s->sp;}
SMB_HD int smb_pop(SmbLogic* s) {++s->sp;return s->ram[256+s->sp];}
SMB_HD int smb_read(SmbLogic* s,const uint8_t* data,int address) {
    address&=65535;
    if(address<8192)return s->ram[address&2047];
    if(address>=32768)return data[address-32768];
    if(address==0x4016||address==0x4017) {
        int k=address&1,value=s->joy_shift[k]&1;
        if(!s->strobe)s->joy_shift[k]=(s->joy_shift[k]>>1)|128;
        return value;
    }
    // PPU status polling is replaced at its generated call sites. Other reads
    // are audited by the generator and unsupported reads fail visibly.
    if(address==0x2002)return s->timing.enabled?smb_clock_status(s):0;
    if(address==0x4015)return 0;
    s->fault=address|0x10000;return 0;
}
SMB_HD void smb_write(SmbLogic* s,int address,int value) {
    address&=65535;
    if(address<8192){s->ram[address&2047]=(uint8_t)value;return;}
    if(s->timing.enabled)smb_clock_write(s,address,value);
    if(address==0x4016) {
        if(s->strobe&&!(value&1)){s->joy_shift[0]=s->joy[0];s->joy_shift[1]=s->joy[1];}
        s->strobe=value&1;
        return;
    }
    // Rendering/audio writes have no game-state effect in mapper 0. OAM in
    // RAM is retained: firebar collision actually reads a player sprite byte.
    if((address>=0x2000&&address<=0x2007)||(address>=0x4000&&address<=0x4017))return;
    s->fault=address|0x20000;
}
SMB_HD void smb_adc(SmbLogic* s,int v) {
    int a=s->a,t=a+v+(s->p&1);
    s->p=(s->p&~65)|(t>255)|((~(a^v)&(a^t)&128)>>1);
    s->a=(uint8_t)t;smb_nz(s,s->a);
}
SMB_HD void smb_cmp(SmbLogic* s,int a,int b) {
    s->p=(s->p&~1)|(a>=b);smb_nz(s,(a-b)&255);
}
SMB_HD int smb_shift(SmbLogic* s,int v,int kind) {
    int c=s->p&1,result;
    if(kind==0||kind==1){s->p=(s->p&~1)|(v>>7);result=((v<<1)|(kind==1?c:0))&255;}
    else {s->p=(s->p&~1)|(v&1);result=(v>>1)|(kind==3?c*128:0);}
    smb_nz(s,result);return result;
}
// A lossless debugging observation, deliberately not a proposed policy encoder.
// Byte-to-float and a power-of-two scale are bitwise identical on CPU and CUDA.
SMB_HD void smb_debug_observe(const SmbLogic* s,float* out) {
    for(int i=0;i<SMB_DEBUG_OBS;i++)out[i]=(float)s->ram[i]*(1.0f/256.0f);
}

// Explicit scene knobs. Import and generated interventions use these same
// setters; none may be applied again during a persistent clip replay.
SMB_HD void smb_set_player_xy(SmbLogic* s,int x,int y) {
    s->ram[0x6d]=(uint8_t)(x>>8);s->ram[0x86]=(uint8_t)x;
    s->ram[0xb5]=(uint8_t)((y+256)>>8);s->ram[0xce]=(uint8_t)y;
}
SMB_HD void smb_set_actor_xy(SmbLogic* s,int slot,int x,int y) {
    if(slot<0||slot>=6){s->fault=-2;return;}
    s->ram[0x6e + slot]=(uint8_t)(x>>8);s->ram[0x87+slot]=(uint8_t)x;
    s->ram[0xb6+slot]=(uint8_t)((y+256)>>8);s->ram[0xcf+slot]=(uint8_t)y;
}
SMB_HD void smb_set_clock(SmbLogic* s,int frame,int interval,const uint8_t* rng) {
    s->ram[9]=(uint8_t)frame;s->ram[0x77f]=(uint8_t)interval;
    for(int i=0;i<7;i++)s->ram[0x7a7+i]=rng[i];
}
