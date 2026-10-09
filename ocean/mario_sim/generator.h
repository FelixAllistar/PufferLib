#pragma once
#include "parameters.h"
#include "world.h"

enum {SMB_GEN_CLOCK=1,SMB_GEN_FRACTIONS=2,SMB_GEN_ACTORS=4,SMB_GEN_FORM=8,
      SMB_GEN_ALL=15};
SMB_HD uint32_t smb_random(uint32_t* state) {
    uint32_t x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x;
}
SMB_HD uint32_t smb_seed(uint32_t seed,uint32_t stream) {
    uint32_t x=seed^(stream*0x9e3779b9u+0x85ebca6bu);
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x?x:1;
}

// Every mutation occurs at reset. Spawn trajectories, collisions and future
// parser updates are computed by the persistent game logic afterwards.
SMB_HD int smb_generate(SmbLogic* out,const SmbScene* source,uint32_t* rng,unsigned knobs) {
    int fault=smb_scene_reset(out,source);if(fault)return fault;
    auto* m=out->ram;
    if(knobs&SMB_GEN_CLOCK) {
        uint8_t random[7];for(int i=0;i<7;i++)random[i]=(uint8_t)smb_random(rng);
        int frame=smb_random(rng)&255,interval=smb_random(rng)%21;
        smb_set_clock(out,frame,interval,random);
    }
    if(knobs&SMB_GEN_FRACTIONS) {
        m[0x400]=(uint8_t)smb_random(rng);m[0x416]=(uint8_t)smb_random(rng);
    }
    if(knobs&SMB_GEN_ACTORS)for(int k=0;k<5;k++)if(m[15+k]&&m[15+k]<128&&m[22+k]<36) {
        int x=m[0x6e + k]*256+m[0x87+k]+(int)(smb_random(rng)%17)-8;
        int y=((int)m[0xb6+k]-1)*256+m[0xcf+k];
        smb_set_actor_xy(out,k,x,y);
        m[0x78a+k]=(uint8_t)(smb_random(rng)%65);
        m[0x796+k]=(uint8_t)(smb_random(rng)%33);
    }
    if(knobs&SMB_GEN_FORM) {
        m[0x756]=(uint8_t)(smb_random(rng)%3);m[0x754]=m[0x756]?0:1;
    }
    return out->fault;
}

// Produce a fixed per-world data stream. Edits preserve entry lengths and
// area/pipe/maze links. Loaded state remains the explicit reset template;
// these settings control data read by subsequent native parser/spawn updates.
SMB_HD int smb_generate_world(uint8_t* out,const uint8_t* base,uint32_t seed) {
    for(int i=0;i<SMB_PRG;i++)out[i]=base[i];
    uint32_t rng=smb_seed(seed,0);int changes=0;
    const int counts[]={3,22,3,6};
    for(int type=0;type<4;type++)for(int index=0;index<counts[type];index++) {
        int p=smb_world_area_pointer(out,type,index,1),entry=0;
        if(p<0x8000)return -1;
        for(int off=0;off<256;) {
            int a=smb_world_byte(out,p+off),b=smb_world_byte(out,p+off+1),row=a&15;
            if(a<0||b<0)return -1;
            if(a==255)break;
            if(row==14){off+=3;continue;}
            if(row!=15) {
                int id=b&63,replacement=id;uint32_t choice=smb_random(&rng);
                if(id==0||id==2||id==3||id==6) {
                    const int walkers[]={0,2,3,6};replacement=walkers[choice%4];
                } else if(id==10||id==11)replacement=10+(choice&1);
                else if(id>=27&&id<=30)replacement=27+(choice&3);
                else if(id==38||id==39)replacement=38+(choice&1);
                else if(id==43||id==44)replacement=43+(choice&1);
                if(replacement!=id) {
                    if(smb_world_enemy_type(out,type,index,entry,replacement))return -1;
                    changes++;
                }
                entry++;
            }
            off+=2;
        }
        p=smb_world_area_pointer(out,type,index,0);entry=0;
        if(p<0x8000)return -1;
        for(int off=2;off<258;off+=2) {
            int a=smb_world_byte(out,p+off),b=smb_world_byte(out,p+off+1);
            if(a<0||b<0)return -1;
            if(a==253)break;
            if((a&15)<12&&!(b&112)&&(b&15)<=8) {
                int id=b&15,replacement=id;uint32_t choice=smb_random(&rng);
                // Keep the geometry class (question/hidden/brick) and vines.
                if(id<=1)replacement=choice%2;
                else if(id==2||id==3)replacement=2+(choice&1);
                else if(id!=5){const int bricks[]={4,6,7,8};replacement=bricks[choice%4];}
                if(replacement!=id) {
                    if(smb_world_block_contents(out,type,index,entry,replacement))return -1;
                    changes++;
                }
                entry++;
            }
        }
    }
    return changes;
}
