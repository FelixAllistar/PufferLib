#include "sim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static int exhaustive(const SwatNavigation* nav,b3Pos point) {
    int best=-1;float distance=2;
    for(int i=0;i<nav->nodes;i++)if(nav->walkable[i]) {
        b3Pos p={nav->x[i],nav->height[i],nav->z[i]};
        if(fabs(p.y-point.y)>.65)continue;
        float d=b3Distance(point,p);if(d<distance){distance=d;best=i;}
    }
    return best;
}
static uint32_t rng=19;
static float random_float(float lo,float hi) {
    rng=rng*1664525u+1013904223u;return lo+(hi-lo)*(float)(rng>>8)/16777216;
}
static int queries;
static void check(const SwatNavigation* nav,b3Pos p) {
    assert(swat_navigation_nearest(nav,p)==exhaustive(nav,p));queries++;
}
int main(void) {
    static SwatSim sim;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,42);b3Pos next;
    swat_navigation_next(&sim,(b3Pos){0,-.08f,20},(b3Pos){0,-.08f,30},&next);
    SwatNavigation* nav=sim.navigation;assert(nav && nav->built);
    for(int i=0;i<2000;i++)check(nav,(b3Pos){random_float(-70,70),random_float(-1,4),random_float(-54,58)});
    for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].door) {
        b3Pos p=sim.world.objects[i].center;p.y-=sim.world.objects[i].half.y;
        for(int x=-2;x<=2;x++)for(int z=-2;z<=2;z++)check(nav,b3OffsetPos(p,swat_v(x*.3f,0,z*.3f)));
    }
    // Adversarial bounded offsets, four levels, ties and points beyond all
    // borders exercise the search window independently of this map's samples.
    for(int i=0;i<nav->nodes;i++) {
        int cell=i%nav->cells,layer=i/nav->cells;
        nav->walkable[i]=i%5 ? (i%3?3:1) : 0;
        nav->x[i]=nav->min_x+(cell%nav->width+.5f)*.6f+random_float(-.59f,.59f);
        nav->z[i]=nav->min_z+(cell/nav->width+.5f)*.6f+random_float(-.59f,.59f);
        nav->height[i]=layer*1.2f;
    }
    for(int i=0;i<2000;i++)check(nav,(b3Pos){random_float(-70,70),random_float(-1,5),random_float(-54,58)});
    b3Pos p={nav->x[0],nav->height[0],nav->z[0]};nav->walkable[0]=nav->walkable[1]=3;
    nav->x[1]=p.x;nav->z[1]=p.z;nav->height[1]=p.y;check(nav,p);
    const int n=1000;double start=(double)clock()/CLOCKS_PER_SEC;volatile int sink=0;
    for(int i=0;i<n;i++)sink+=exhaustive(nav,(b3Pos){i*.001f,1.2f,0});
    double full=(double)clock()/CLOCKS_PER_SEC-start;start=(double)clock()/CLOCKS_PER_SEC;
    for(int i=0;i<n;i++)sink+=swat_navigation_nearest(nav,(b3Pos){i*.001f,1.2f,0});
    double local=(double)clock()/CLOCKS_PER_SEC-start;
    printf("PASS navigation search: %d exact nearest/tie comparisons; exhaustive %.3f ms, local %.3f ms (%d)\n",queries,full*1000,local*1000,sink);
    swat_sim_close(&sim);return 0;
}
