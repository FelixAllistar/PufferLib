#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
#define LANES 4u
enum {RUNNING,WIN,DRAW,LOSS,TIMEOUT};
typedef struct {unsigned x,o,rng;int outcome;} Ref;
static unsigned random_moves,smart_moves,opening_moves;
static unsigned next(unsigned rng){return (rng*73u+19u)%10000u;}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static int won(unsigned mask){
    static const unsigned lines[8][3]={{0,1,2},{3,4,5},{6,7,8},
      {0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for(unsigned i=0;i<8;i++)if((mask&(1u<<lines[i][0]))&&
      (mask&(1u<<lines[i][1]))&&(mask&(1u<<lines[i][2])))return 1;
    return 0;
}
static int triple(unsigned occupied,unsigned mark,unsigned target,
                  unsigned a,unsigned b){
    return !(occupied&(1u<<target))&&(mark&(1u<<a))&&(mark&(1u<<b));
}
/* Independent source-order interpretation: each helper takes its first hit;
   the outer filter/pop retains the last helper with a hit. */
static int directional(unsigned occupied,unsigned mark){
    int groups[7];
    for(unsigned col=0;col<3;col++){
        groups[col]=-1;
        for(unsigned row=0;row<3;row++){
            unsigned base=row*3,target=base+col;
            if(triple(occupied,mark,target,base+(col+1)%3,
                      base+(col+2)%3)){groups[col]=(int)target;break;}
        }
    }
    static const unsigned diagonals[6][3]={{0,4,8},{4,0,8},{8,0,4},
      {2,4,6},{4,2,6},{6,2,4}};
    groups[3]=-1;
    for(unsigned i=0;i<6;i++)if(triple(occupied,mark,
      diagonals[i][0],diagonals[i][1],diagonals[i][2])){
        groups[3]=(int)diagonals[i][0];break;
    }
    for(unsigned col=0;col<3;col++){
        groups[col+4]=-1;
        for(unsigned row=0;row<3;row++){
            unsigned target=col+row*3;
            unsigned a=col+((row+1)%3)*3,b=col+((row+2)%3)*3;
            if(triple(occupied,mark,target,a,b)){
                groups[col+4]=(int)target;break;
            }
        }
    }
    int chosen=-1;for(unsigned i=0;i<7;i++)if(groups[i]>=0)chosen=groups[i];
    return chosen;
}
static int smart(unsigned x,unsigned o){
    unsigned occupied=x|o;
    int winning=directional(occupied,o),blocking=directional(occupied,x);
    return winning>=0?winning:blocking;
}
static void opponent(Ref *s){
    unsigned occupied=s->x|s->o,chance=next(s->rng);s->rng=chance;
    unsigned count=9u-(unsigned)__builtin_popcount(occupied);
    int selected=-1;
    if(chance/100u>=55u&&count!=9u){
        selected=smart(s->x,s->o);
        if(selected>=0)smart_moves++;
    }
    if(selected<0){
        unsigned sample=next(s->rng);s->rng=sample;
        unsigned rank=sample*count/10000u;
        for(unsigned i=0;i<9;i++)if(!(occupied&(1u<<i))){
            if(rank==0){selected=(int)i;break;}rank--;
        }
        random_moves++;
    }
    assert(selected>=0&&selected<9);
    s->o|=1u<<(unsigned)selected;
    if(won(s->o))s->outcome=LOSS;
    else if((s->x|s->o)==511u)s->outcome=DRAW;
}
static Ref opening(unsigned seed){
    Ref s={0,0,next(seed%10000u),RUNNING};
    if(s.rng/100u>50u){opponent(&s);opening_moves++;}
    return s;
}
static void ref_click(Ref *s,unsigned cell,unsigned elapsed){
    if(s->outcome!=RUNNING)return;
    if(elapsed>=10000){s->outcome=TIMEOUT;return;}
    if((s->x|s->o)&(1u<<cell))return;
    s->x|=1u<<cell;
    if(won(s->x))s->outcome=WIN;
    else if((s->x|s->o)==511u)s->outcome=DRAW;
    else opponent(s);
}
static void ref_wait(Ref *s,unsigned elapsed){
    if(s->outcome==RUNNING&&elapsed>=10000)s->outcome=TIMEOUT;
}
static float raw(int outcome){
    switch(outcome){
        case WIN:return 1.0f;case DRAW:return -0.5f;
        case LOSS:return -0.75f;case TIMEOUT:return -1.0f;
        default:return 0.0f;
    }
}
static void compare(const uint32_t *r,Ref s,unsigned elapsed){
    assert(r[33]==s.x&&r[34]==s.o&&r[32]==s.rng);
    assert(r[WF_STATUS]==(s.outcome==RUNNING?WF_RUNNING:
                          s.outcome==TIMEOUT?WF_TIMEOUT:WF_TERMINAL));
    assert(fabsf(real(r[WF_RAW_REWARD])-raw(s.outcome))<1e-6f);
    float expected=s.outcome==WIN?1.0f-(float)elapsed/10000.0f:raw(s.outcome);
    assert(fabsf(real(r[WF_TIMED_REWARD])-expected)<1e-5f);
}
static void reset(const WFFamily *f,uint32_t *rows,unsigned seed){
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;
        r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;
        r[WF_SEED]=seed+lane;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}
static void apply(const WFFamily *f,uint32_t *rows,unsigned kind,
                  unsigned cell,unsigned elapsed){
    WFAction a={.kind=kind,.target=kind==WF_CLICK?cell+1:0,
                .elapsed_ms=elapsed};
    assert(!f->action(rows,&a));f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==1);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    unsigned starts_empty=0,starts_o=0,games=0;
    for(unsigned seed=1;seed<=256;seed++){
        reset(f,rows,seed);Ref s=opening(seed);
        compare(rows,s,0);starts_empty+=s.o==0;starts_o+=s.o!=0;
        WFView a,b;assert(!f->observe(rows,&a));
        uint32_t changed[ROW];memcpy(changed,rows,sizeof changed);
        changed[32]=(changed[32]+1)%10000u;
        assert(!f->observe(changed,&b)&&!memcmp(&a,&b,sizeof a));
        for(unsigned move=0;move<5&&s.outcome==RUNNING;move++){
            unsigned cell=(seed+move*3)%9;
            while((s.x|s.o)&(1u<<cell))cell=(cell+1)%9;
            unsigned elapsed=100+move*400;
            ref_click(&s,cell,elapsed);
            apply(f,rows,WF_CLICK,cell,elapsed);compare(rows,s,elapsed);
            if(s.outcome==RUNNING){
                unsigned marked=cell;
                ref_click(&s,marked,elapsed+1);
                apply(f,rows,WF_CLICK,marked,elapsed+1);
                compare(rows,s,elapsed+1);
            }
        }
        games++;
    }
    assert(starts_empty>0&&starts_o>0&&random_moves>0&&smart_moves>0);
    /* Force smart win and smart block, a last-cell draw, and deadline. */
    struct {unsigned x,o,rng,cell,elapsed;} fixtures[]={
      {3,24,100,6,100},{3,264,100,4,100},{141,114,100,8,100}};
    for(unsigned i=0;i<3;i++){
        reset(f,rows,2);rows[33]=fixtures[i].x;rows[34]=fixtures[i].o;
        rows[32]=fixtures[i].rng;
        Ref s={fixtures[i].x,fixtures[i].o,fixtures[i].rng,RUNNING};
        ref_click(&s,fixtures[i].cell,fixtures[i].elapsed);
        apply(f,rows,WF_CLICK,fixtures[i].cell,fixtures[i].elapsed);
        compare(rows,s,fixtures[i].elapsed);
    }
    reset(f,rows,2);Ref timeout=opening(2);ref_wait(&timeout,10000);
    apply(f,rows,WF_WAIT,0,10000);compare(rows,timeout,10000);
    uint32_t *fresh=calloc(LANES*ROW,sizeof *fresh);assert(fresh);
    memset(rows,0xa5,LANES*ROW*sizeof *rows);
    reset(f,rows,70);reset(f,fresh,70);
    assert(!memcmp(rows,fresh,LANES*ROW*sizeof *rows));
    free(fresh);free(rows);
    printf("board independent oracle: %u games, %u empty starts, %u O starts, %u random, %u smart\n",
           games,starts_empty,starts_o,random_moves,smart_moves);
    return 0;
}
