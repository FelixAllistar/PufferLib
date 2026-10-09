#pragma once
#include "fpg_core.h"

// Small-player, normal-world Goombas. Other enemy IDs are retained by the ROM
// adapter so the parity gate reports them as unsupported, never as empty air.
#define FPG_ENEMIES 5
typedef struct {
    int active, type, state, x, y, vx, vy, xsub, ysub, vyfrac;
    int direction, collision, timer;
} FpgEnemy;
typedef struct {
    FpgEnemy slots[FPG_ENEMIES];
    int frame, interval, stomp, injury, hard, timer_control;
} FpgActors;

FPG_HD void fpg_actor_timers(FpgActors* a) {
    a->frame=(a->frame+1)&255;
    if(a->timer_control && --a->timer_control)return;
    if(a->stomp)--a->stomp;
    if(--a->interval<0) {
        a->interval=20;
        if(a->injury)--a->injury;
        for(int k=0;k<FPG_ENEMIES;k++)if(a->slots[k].timer)--a->slots[k].timer;
    }
}
FPG_HD void fpg_enemy_turn(FpgEnemy* e) {e->vx=-e->vx;e->direction^=3;}
FPG_HD int fpg_boxes_overlap(const int* a,const int* b) {
    return a[0]<=b[2]&&a[2]>=b[0]&&a[1]<=b[3]&&a[3]>=b[1];
}
FPG_HD int fpg_enemy_visible(const FpgEnemy* e,const FpgCamera* c) {
    // Masked offscreen bits for a normal enemy's collision box: the left
    // eight pixels can remain visible while the collision test is disabled.
    return e->x>c->left-8 && e->x<c->left+256 && e->y>-16 && e->y<240;
}
FPG_HD void fpg_goomba_background(FpgEnemy* e,const FpgWorld* w) {
    if((e->state&32)||(((e->y&255)+62)&255)<68)return;
    int under=fpg_tile(w,e->x+8,e->y+24);
    if(under && (unsigned)((e->y&15)-8)<5) {
        if(e->state==1) {
            e->y=(e->y&~15)|8;e->vy=e->vyfrac=0;e->state=0;return;
        }
        if(e->state>=3)return;
    } else {
        const int falling[6]={1,1,2,2,2,5};
        if(e->state>=0&&e->state<6)e->state=falling[e->state];
    }
    if((e->y&255)>=32) {
        int x=e->x+(e->direction==1?16:0);
        if(fpg_tile(w,x,e->y+20))fpg_enemy_turn(e);
    }
}
FPG_HD void fpg_goomba_move(FpgEnemy* e,FpgActors* a) {
    if(e->state>=3 && e->state<=4) {
        if(e->timer==14) {memset(e,0,sizeof(*e));return;}
        if(!e->timer) {e->state=0;e->direction=(a->frame&1)+1;e->vx=e->direction==1?8:-8;}
        return;
    }
    if(e->state) {
        int fraction=e->ysub+e->vyfrac;e->ysub=fraction&255;
        e->y+=e->vy+(fraction>>8);
        fraction=e->vyfrac+61;e->vyfrac=fraction&255;e->vy+=fraction>>8;
        if(e->vy>=3&&e->vyfrac>=128){e->vy=3;e->vyfrac=0;}
    }
    int fraction=e->xsub+(e->vx&15)*16;e->xsub=fraction&255;
    e->x+=fpg_floor16(e->vx)+(fraction>>8);
}
FPG_HD void fpg_actors_step(FpgActors* a,FpgBody* b,const FpgWorld* w,
        const FpgCamera* c,const int* player_box) {
    int boxes[FPG_ENEMIES][4]={};int visible[FPG_ENEMIES]={};
    for(int k=0;k<FPG_ENEMIES;k++) {
        FpgEnemy* e=&a->slots[k];if(!e->active||e->type!=6)continue;
        int* box=boxes[k];box[0]=e->x+3;box[1]=e->y+14;box[2]=e->x+13;box[3]=e->y+20;
        visible[k]=fpg_enemy_visible(e,c);
        fpg_goomba_background(e,w);
        if((a->frame&1)&&visible[k])for(int j=k-1;j>=0;j--) {
            FpgEnemy* other=&a->slots[j];if(!other->active||other->type!=6||!visible[j])continue;
            int mask=128>>k;
            if(fpg_boxes_overlap(box,boxes[j])) {
                if(!(other->collision&mask)) {
                    other->collision|=mask;
                    if(!(e->state&32)&&!(other->state&32)){fpg_enemy_turn(e);fpg_enemy_turn(other);}
                }
            } else other->collision&=~mask;
        }
        if(!(a->frame&1)&&visible[k]&&b->y>=0&&b->y<208&&b->routine==8&&!(e->state&32)) {
            if(!fpg_boxes_overlap(player_box,box))e->collision&=~1;
            else if(!(e->collision&1)) {
                e->collision|=1;
                if(e->state<2) {
                    if(b->vy>0||a->stomp) {
                        e->state=4;e->timer=a->hard?11:16;a->stomp=(a->stomp+1)&255;b->vy=-4;
                    } else if(!a->injury) {
                        int player_relative=(player_box[0]-3-c->left)&255,enemy_relative=(e->x-c->left)&255;
                        if((player_relative<enemy_relative&&e->direction==1)
                            ||(player_relative>=enemy_relative&&e->direction!=1))fpg_enemy_turn(e);
                        b->vx=0;b->vy=-4;b->routine=11;b->motion=1;a->timer_control=255;
                    }
                }
            }
        }
        if(!a->timer_control)fpg_goomba_move(e,a);
        // The original subtraction carries a cleared carry flag for Goombas.
        if(e->active&&(e->x<c->left-73||e->x>=c->left+328))memset(e,0,sizeof(*e));
    }
}
