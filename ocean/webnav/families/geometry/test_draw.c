#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define PI 3.14159265358979323846
static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0x93,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void act(const WFFamily *f,uint32_t *rows,unsigned target,
                double x,double y,unsigned elapsed){
    WFAction a={.kind=WF_CLICK,.target=target,
        .arg0=target==1u?(unsigned)lround(x*256.0):0u,
        .arg1=target==1u?(unsigned)lround(y*256.0):0u,
        .elapsed_ms=elapsed};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static double angle(double ax,double ay,double bx,double by,double vx,double vy){
    double av=hypot(ax-vx,ay-vy),bv=hypot(bx-vx,by-vy),ab=hypot(ax-bx,ay-by);
    return acos((bv*bv+av*av-ab*ab)/(2.0*bv*av))*180.0/PI;
}
static double oracle(unsigned task,const uint32_t *r,double ux,double uy,int *scale){
    double ax=r[40],ay=r[41],bx=r[42],by=r[43],cx=r[44],cy=r[45];
    *scale=0;
    if(task==1u){
        double d=hypot(ux-ax-0.5,uy-ay-0.5),radius=r[46];
        if(d<radius){*scale=1;return (radius-d)/radius;}
        return -1.0;
    }
    if(task==2u){
        double d=hypot(ux-(ax+bx)/2.0,uy-(ay+by)/2.0);
        if(d<30.0){*scale=1;return (30.0-d)/30.0;}
        return -1.0;
    }
    if(task==4u){
        double user=angle(ax,ay,ux+0.5,uy+0.5,bx,by);
        double diff=fabs(90.0-user);
        if(diff<45.0){*scale=1;return (90.0-diff)/90.0;}
        return -1.0;
    }
    double total=angle(ax,ay,bx,by,cx,cy);
    double aa=angle(ax,ay,ux,uy,cx,cy),bb=angle(bx,by,ux,uy,cx,cy);
    if(aa>total||bb>total)return -1.0;
    *scale=1;return (total-fmax(aa,bb))/(total/2.0);
}
static void good_point(unsigned task,const uint32_t *r,double *x,double *y){
    double ax=r[40],ay=r[41],bx=r[42],by=r[43],cx=r[44],cy=r[45];
    if(task==1u){*x=ax+0.5;*y=ay+0.5;return;}
    if(task==2u){*x=(ax+bx)/2.0;*y=(ay+by)/2.0;return;}
    if(task==4u){
        double dx=ax-bx,dy=ay-by,length=hypot(dx,dy);
        *x=bx+dy*20.0/length-0.5;
        *y=by-dx*20.0/length-0.5;
        return;
    }
    double dx=ax-cx,dy=ay-cy,ex=bx-cx,ey=by-cy;
    double al=hypot(dx,dy),bl=hypot(ex,ey);
    double sx=dx/al+ex/bl,sy=dy/al+ey/bl,sl=hypot(sx,sy);
    *x=cx+sx*35.0/sl;*y=cy+sy*35.0/sl;
}
static void check_reward(const uint32_t *r,double raw,int scaled,unsigned elapsed){
    assert(r[WF_STATUS]==WF_TERMINAL);
    assert(fabs(real(r[WF_RAW_REWARD])-raw)<2e-4);
    double timed=scaled?raw*(1.0-(double)elapsed/10000.0):raw;
    assert(fabs(real(r[WF_TIMED_REWARD])-timed)<2e-4);
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned cases=0,variation=0;
    for(unsigned task=0;task<5u;task++)if(task!=3u){
        unsigned first=0,task_variation=0;
        for(unsigned seed=0;seed<32u;seed++){
            reset(f,rows,task,seed);assert(rows[40]<=134u);
            if(!seed)first=rows[40];
            task_variation+=rows[40]!=first;
            WFView v;assert(!f->observe(rows,&v)&&v.count>2u);
            act(f,rows,2u,0,0,250u);
            check_reward(rows,-1.0,0,250u);
            reset(f,rows,task,seed);
            double x,y;good_point(task,rows,&x,&y);
            assert(x>=2.0&&x<=150.0&&y>=2.0&&y<=130.0);
            act(f,rows,1u,x,y,100u);
            assert(rows[WF_STATUS]==WF_RUNNING&&rows[49]==1u);
            assert(rows[47]==((unsigned)lround(x*256.0)/256u)*256u&&
                   rows[48]==((unsigned)lround(y*256.0)/256u)*256u);
            double click_x=rows[47]/256.0,click_y=rows[48]/256.0;
            int scaled;double expected=oracle(task,rows,click_x,click_y,&scaled);
            act(f,rows,2u,0,0,250u);
            check_reward(rows,expected,scaled,250u);
            assert(expected>0.9);
            uint32_t frozen=rows[WF_TIMED_REWARD];
            act(f,rows,1u,0,0,500u);
            assert(rows[WF_STATUS]==WF_TERMINAL&&rows[WF_TIMED_REWARD]==frozen);
            reset(f,rows,task,seed);
            act(f,rows,1u,0.0,0.0,100u);
            expected=oracle(task,rows,0.0,0.0,&scaled);
            act(f,rows,2u,0,0,250u);
            check_reward(rows,expected,scaled,250u);
            reset(f,rows,task,seed);
            act(f,rows,1u,x,y,100u);
            act(f,rows,2u,0,0,10000u);
            assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_TIMED_REWARD])==-1.0f);
            if(task==1u||task==2u){
                reset(f,rows,task,seed);
                double edge_x=task==1u?rows[40]+rows[46]+1.0:
                    (rows[40]+rows[42])/2.0+30.0;
                double edge_y=task==1u?rows[41]:
                    (rows[41]+rows[43])/2.0;
                act(f,rows,1u,edge_x,edge_y,100u);
                act(f,rows,2u,0,0,250u);
                check_reward(rows,-1.0,0,250u);
            }
            cases++;
        }
        assert(task_variation);variation+=task_variation;
    }
    assert(variation&&cases==128u);free(rows);
    printf("PASS: %u generated drawing cases; independent F64 scores, missing/wrong/right clicks, deadline and absorption\n",cases);
}
