#pragma once
#include "policy_shape.h"
#include "logic.h"
#include <stdlib.h>

// History contains only decoded physical positions. Velocity is observed
// displacement per video frame, never a misinterpreted, type-aliased RAM byte.
struct FptEntityHistory {
    int x,y,frame,type,vx,vy,valid;
};
struct FptObservationHistory {FptEntityHistory entities[FPT_ENTITY_COUNT];};
struct FptEntity {
    int type,x,y,width,height,state,direction,timer,interval,collision,active;
    float phase_x,phase_y,spin;
};
SMB_HD int fpt_signed(int value) {return value<128?value:value-256;}
SMB_HD int fpt_x(const uint8_t* m,int obj) {return m[0x6d+obj]*256+m[0x86+obj];}
SMB_HD int fpt_y(const uint8_t* m,int obj) {return ((int)m[0xb5+obj]-1)*256+m[0xce + obj];}
SMB_HD int fpt_camera_left(const uint8_t* m) {return m[0x71a]*256+m[0x71c];}
SMB_HD bool fpt_on_screen(const FptEntity& e,int left) {
    return e.x+e.width>left&&e.x<left+256&&e.y+e.height>32&&e.y<240;
}
SMB_HD bool fpt_firebar(int id) {return id>=0x1b&&id<=0x22;}
SMB_HD void fpt_box(FptEntity* e,const uint8_t* world,int control) {
    if(control<0||control>=12){e->width=e->height=0;return;}
    const uint8_t* box=world+(0xe1fd-0x8000)+4*control;
    e->x+=box[0];e->y+=box[1];e->width=box[2]-box[0];e->height=box[3]-box[1];
}
SMB_HD void fpt_firebar_offset(const uint8_t* world,int phase,int segment,int* dx,int* dy) {
    if(segment==0){*dx=*dy=0;return;}
    int x=phase&15,y=(phase+8)&15;if(x>8)x=16-x;if(y>8)y=16-y;
    int base=world[(0xcd2e - 0x8000)+segment-1];
    int mirror=world[(0xcd2a-0x8000)+(phase>>3)];
    *dx=world[(0xccc7-0x8000)+base+x]*((mirror&1)?1:-1);
    *dy=world[(0xccc7-0x8000)+base+y]*((mirror&2)?1:-1);
}
SMB_HD FptEntity fpt_entity(const SmbLogic* s,const uint8_t* world,int index) {
    const uint8_t* m=s->ram;FptEntity e={};int left=fpt_camera_left(m);
    if(index<FPT_ENTITY_MISC_OFFSET) {
        int k=index/6,segment=index%6,flag=m[0xf+k],id=m[0x16+k];
        if(!flag)return e;
        // A duplicate long-firebar slot supplies the outer six segments.
        // Bowser's rear is a real second collision body, with its own position.
        if(flag&128) {
            int parent=flag&127;if(parent>=6)return e;
            id=m[0x16+parent];
            if(fpt_firebar(id)&&id>=0x1f){k=parent;segment+=6;}
            else if(id!=0x2d)return e;
        }
        if(id>=64||id==0x16||id==0x17||id==0x18||id==0x30||id==0x31||id==0x34||id==0x35)return e;
        e.type=id+1;e.x=fpt_x(m,k+1);e.y=fpt_y(m,k+1);e.state=m[0x1e + k];
        e.direction=m[0x46+k]==1?1:m[0x46+k]==2?-1:0;
        e.timer=k<5?m[0x78a+k]:0;e.interval=k<5?m[0x796+k]:0;
        // State bytes are type-specific: a balance platform uses 0xff for
        // its partner marker, while ordinary enemies use bit 5 for defeat.
        e.collision=m[0x491+k];e.active=1;
        if(id<=0x15||id==0x2d||id==0x33)e.active=(e.state&0x20)==0;
        if(id==6&&e.state>=2)e.active=0;
        if(id==0x2d)e.active=e.state==0;
        if(id==0x2e)e.active=e.state>=6;
        if(fpt_firebar(id)) {
            if(segment>=(id<0x1f?6:12))return FptEntity{};
            int phase=m[0xa0+k]&31,dx,dy;
            fpt_firebar_offset(world,phase,segment,&dx,&dy);
            // Match the ROM's horizontal-wrap suppression rather than letting
            // a firebar wrap around to the opposite edge of the screen.
            int screen=e.x-left,wrapped=(screen+dx)&255;
            if(screen<=0||screen>=256||(e.y&255)==0xf8||abs(wrapped-screen)>=0x59)return FptEntity{};
            e.x+=dx;e.y=(e.y+dy)&255;e.width=e.height=8;
            int ux,uy;fpt_firebar_offset(world,phase,1,&ux,&uy);
            e.phase_x=ux*(1.0f/8);e.phase_y=uy*(1.0f/8);
            e.spin=(m[0x34+k]?-1.0f:1.0f)*m[0x388+k]*(1.0f/256);
            e.timer=0;e.interval=0;e.state=0;e.direction=0;e.collision=0;e.active=1;
        } else {
            if(segment)return FptEntity{};
            if(id==0x2e)e.type=FPT_ENTITY_MUSHROOM+(m[0x39]&3);
            if(id==0x2f) {
                e.width=16;e.height=m[0x399];
                if(e.height>0x80)e.height=0x80;
                e.active=1;
            } else fpt_box(&e,world,m[0x49a+k]);
        }
    } else if(index<FPT_ENTITY_FIREBALL_OFFSET) {
        int k=index-FPT_ENTITY_MISC_OFFSET,state=m[0x2a+k];
        if(!(state&128))return e; // jumping coins are a cosmetic score effect
        e.type=FPT_ENTITY_HAMMER;e.x=fpt_x(m,13+k);e.y=fpt_y(m,13+k);e.state=state;
        e.active=(state&127)<2;e.timer=state&127;e.collision=m[0x6be + k];
        fpt_box(&e,world,m[0x4a2+k]);
    } else if(index<FPT_ENTITY_BLOCK_OFFSET) {
        int k=index-FPT_ENTITY_FIREBALL_OFFSET,state=m[0x24+k];
        if(!state||(state&128)||state==2)return e; // explosion / not positioned yet
        e.type=FPT_ENTITY_FIREBALL;e.x=fpt_x(m,7+k);e.y=fpt_y(m,7+k);e.state=state;e.active=1;
        fpt_box(&e,world,m[0x4a0+k]);
    } else {
        int k=index-FPT_ENTITY_BLOCK_OFFSET,state=m[0x26+k];
        if((state&15)!=1)return e; // bouncing blocks, not cosmetic brick debris
        e.type=FPT_ENTITY_BLOCK;e.x=fpt_x(m,9+k);e.y=fpt_y(m,9+k);e.state=state;
        e.width=e.height=16;e.active=1;
    }
    if(e.width<=0||e.height<=0||!fpt_on_screen(e,left))return FptEntity{};
    return e;
}

SMB_HD int fpt_terrain_tile(const SmbLogic* s,int cell) {
    const uint8_t* m=s->ram;int left=fpt_camera_left(m),col=(left>>4)+cell%FPT_GRID_W;
    if(col*16>=left+256)return -2;
    // At parser phases 0/4 the cursor denotes the NEXT unwritten column.
    int last=m[0x725]*16+m[0x726]-((m[0x71f]&3)==0);
    if(col<last-31||col>last)return -1;
    return m[0x500+((col&16)?0xd0:0)+(cell/FPT_GRID_W)*16+(col&15)];
}

SMB_HD void fpt_observe_player(const SmbLogic* s,const uint8_t* world,float* o) {
    const uint8_t* m=s->ram;for(int i=0;i<FPT_PLAYER_FEATURES;i++)o[i]=0;
    int x=fpt_x(m,0),y=fpt_y(m,0),left=fpt_camera_left(m);
    o[0]=fpt_signed(m[0x57])*(1.0f/64);
    o[1]=(fpt_signed(m[0x9f])+m[0x433]*(1.0f/256))*(1.0f/8);
    o[2]=m[0x400]*(1.0f/256);o[3]=m[0x416]*(1.0f/256);o[4]=m[0x705]*(1.0f/256);
    o[5]=(x&15)*(1.0f/16);o[6]=(y&15)*(1.0f/16);
    o[7]=(x-left)*(1.0f/256);o[FPT_P_ANCHOR_Y]=y*(1.0f/256);
    o[FPT_P_GRID_DX]=((left&~15)-x)*(1.0f/256);
    o[10]=(m[0x754]?16:m[0x714]?24:32)*(1.0f/32);o[11]=1;
    for(int j=0;j<4;j++)o[12+j]=m[0x1d]==j;
    o[16]=m[0x33]==1;o[17]=m[0x33]==2;o[18]=m[0x45]==1;o[19]=m[0x45]==2;
    for(int j=0;j<3;j++)o[20+j]=m[0x756]==j;
    o[23]=m[0x714]!=0;o[24]=m[0x704]!=0;o[25]=m[0x716]!=0;o[26]=m[0xe]==8;
    o[27]=m[0x783]*(1.0f/32);o[28]=m[0x782]*(1.0f/64);o[29]=m[0x785]*(1.0f/32);
    o[30]=m[0x709]*(1.0f/256);o[31]=m[0x70a]*(1.0f/256);
    o[32]=(m[0xd]&128)!=0;o[33]=(m[0xd]&64)!=0;
    o[34]=(m[0xc]&1)!=0;o[35]=(m[0xc]&2)!=0;o[36]=(m[0xb]&8)!=0;o[37]=(m[0xb]&4)!=0;
    o[38]=(m[0x490]&1)!=0;o[39]=(m[0x490]&2)!=0;
    o[40]=m[0x79e]*(1.0f/256);o[41]=m[0x79f]*(1.0f/256);
    o[42]=(((int)m[0x707]-1)*256+m[0x708]-y)*(1.0f/256);o[43]=m[0x791]*(1.0f/64);
    // Collision/frame-rule phases, not the HUD's game timer or score.
    o[44]=(m[9]&1)!=0;o[45]=m[0x77f]*(1.0f/32);o[46]=m[0x723]!=0;
    o[47]=(left+256-x)*(1.0f/256);
    for(int col=0;col<FPT_GRID_W;col++)for(int row=0;row<FPT_GRID_H;row++) {
        int tile=fpt_terrain_tile(s,row*FPT_GRID_W+col);
        int shaft=(left&~15)+col*16+6;
        if((tile==0x24||tile==0x25)&&shaft>=left&&shaft<left+256) {
            o[48]=1;o[49]=(shaft-x)*(1.0f/256);
            o[50]=(32+row*16+16-y)*(1.0f/256);
        }
    }
    if(o[48])for(int k=0;k<6;k++)if(m[0xf+k]&&m[0x16+k]==0x30)o[51]=(fpt_y(m,k+1)-y)*(1.0f/256);
    o[FPT_P_SCROLL_FRACTION]=(left&15)*(1.0f/16);o[53]=m[0x70b]!=0;
    o[54]=m[0x703]*(1.0f/64);o[55]=m[0x700]*(1.0f/64);
    o[56]=(240-y-32)*(1.0f/256);
    int control=m[0x499];if(control<12) {
        const uint8_t* b=world+(0xe1fd-0x8000)+4*control;
        for(int j=0;j<4;j++)o[57+j]=b[j]*(1.0f/32);
    }
    o[61]=m[0x74e]==0;o[62]=m[0x74e]==3;o[63]=m[0x747]!=0;
}

SMB_HD void fpt_observe_entity(const SmbLogic* s,const uint8_t* world,
        FptObservationHistory* history,int index,float* o) {
    for(int j=0;j<FPT_ENTITY_FEATURES;j++)o[j]=0;
    FptEntity e=fpt_entity(s,world,index);FptEntityHistory* h=&history->entities[index];
    if(!e.type){h->type=0;h->valid=0;return;}
    int frame=s->timing.video_frame;
    if(h->type==e.type&&h->frame==frame-1) {
        h->vx=e.x-h->x;h->vy=e.y-h->y;
        h->valid=abs(h->vx)<=32&&abs(h->vy)<=32;
    } else if(h->type!=e.type||h->frame!=frame)h->valid=0;
    h->x=e.x;h->y=e.y;h->frame=frame;h->type=e.type;
    o[FPT_E_TYPE]=(float)e.type;
    o[FPT_E_DX]=(e.x-fpt_x(s->ram,0))*(1.0f/256);
    o[FPT_E_DY]=(e.y-fpt_y(s->ram,0))*(1.0f/256);
    o[FPT_E_WIDTH]=e.width*(1.0f/64);o[FPT_E_HEIGHT]=e.height*(1.0f/64);
    o[FPT_E_VX]=h->valid?h->vx*(1.0f/8):0;o[FPT_E_VY]=h->valid?h->vy*(1.0f/8):0;
    o[FPT_E_MOTION_KNOWN]=(float)h->valid;
    for(int bit=0;bit<8;bit++)o[FPT_E_STATE_BITS+bit]=(e.state>>bit)&1;
    o[FPT_E_DIRECTION]=(float)e.direction;o[FPT_E_TIMER]=e.timer*(1.0f/256);o[FPT_E_INTERVAL]=e.interval*(1.0f/256);
    o[FPT_E_PHASE_X]=e.phase_x;o[FPT_E_PHASE_Y]=e.phase_y;o[FPT_E_SPIN]=e.spin;
    // Contact latch and state-based activity are separate from presence:
    // attached hammers and emerging objects still supply geometry and state.
    o[FPT_E_COLLISION]=e.collision!=0;o[FPT_E_ACTIVE]=(float)e.active;
}
SMB_HD void fpt_observe(const SmbLogic* s,const uint8_t* world,FptObservationHistory* history,float* o) {
    fpt_observe_player(s,world,o);
    for(int cell=0;cell<FPT_GRID_CELLS;cell++)o[FPT_TERRAIN_OFFSET+cell]=(float)fpt_terrain_tile(s,cell);
    for(int k=0;k<FPT_ENTITY_COUNT;k++)fpt_observe_entity(s,world,history,k,o+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES);
}
