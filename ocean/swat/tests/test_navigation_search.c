#include "sim.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
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
static void low_fixture(void) {
    static SwatSim sim;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_RANGE;cfg.randomize=false;cfg.hostile_fire=false;cfg.squad_bots=0;
    swat_sim_init(&sim,cfg,19);
    int owner=swat_world_box(&sim.world,(b3Pos){2,.2f,2},swat_v(.7f,.2f,.7f),SWAT_WOOD,100);
    sim.world.objects[owner].part=SWAT_PART_FIXTURE;b3Pos next;
    assert(swat_navigation_next(&sim,(b3Pos){0,.015f,2},(b3Pos){4,.015f,2},&next));
    int cell=swat_navigation_nearest(sim.navigation,(b3Pos){2,.015f,2});assert(cell>=0);
    b3Pos point={sim.navigation->x[cell],sim.navigation->height[cell],sim.navigation->z[cell]};
    assert(b3Distance(point,(b3Pos){2,.015f,2})>.7f); // Neither seat nor floor underneath is a route node.
    assert(swat_world_damage(&sim.world,owner,1000));
    assert(swat_navigation_next(&sim,(b3Pos){0,.015f,2},(b3Pos){4,.015f,2},&next));
    cell=swat_navigation_nearest(sim.navigation,(b3Pos){2,.015f,2});assert(cell>=0);
    point=(b3Pos){sim.navigation->x[cell],sim.navigation->height[cell],sim.navigation->z[cell]};
    assert(b3Distance(point,(b3Pos){2,.015f,2})<.43f);
    swat_sim_close(&sim);puts("PASS low fixture navigation: routes avoid the seat and ground beneath it; authoritative removal restores floor access");
}
static void low_support_escort(int variant) {
    static SwatSim sim;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;
    cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=2400;cfg.tactical_rules=true;cfg.squad_bots=variant?3:0;
    swat_sim_init(&sim,cfg,81);
    int stops=0;
    for(int owner=1;owner<=SWAT_MOTEL_INSTANCES;owner++)if(swat_motel_instance(owner-1)->asset==SWAT_MOTEL_PARKING_STOP_ASSET) {
        const SwatObject* stop=&sim.world.objects[owner];
        assert(stop->part==SWAT_PART_OBSTACLE && b3Shape_GetType(stop->shape)==b3_meshShape);
        assert(sim.world.motel_meshes[SWAT_MOTEL_PARKING_STOP_ASSET]->triangleCount==swat_motel_asset(SWAT_MOTEL_PARKING_STOP_ASSET)->triangle_count);
        SwatHit hit=swat_world_ray(&sim.world,b3OffsetPos(stop->center,swat_v(0,.5f,0)),swat_v(0,-1,0),1,b3_nullBodyId);
        assert(hit.hit && hit.index==owner);stops++;
    }
    assert(stops==5);
    const b3Pos original[]={{-2.9,-.065,6.3},{-2.385337114,.064999998,4.499994278}};
    // Captured full-route failure: retain grounded state and approach velocity.
    // Dropping fresh stationary bodies at rounded coordinates misses the snag.
    const b3Pos crowded[]={{.100002378,-.065000057,9.672427177},{.800554872,-.064999968,10.225691795},{-2.374074221,-.064999998,9.242354393},{-.525001526,-.064999998,10.288771629},{.099925950,-.064999998,8.482886314},{-2.855974436,.064999998,4.499983311},{1.144464493,.064999998,4.499990463},{1.300004363,-.064999908,7.879238605},{6,.014999986,-1.399999976}};
    const b3Pos crowded_other[]={{.1,-.065,9.630},{-2,.015,-2},{-1.616,.065,4.5},{.234,-.065,8.738},{-1.696,-.065,11.102},{-.539,-.065,11.234},{2,.015,-2},{-1.055,-.065,7.749},{6,.015,-1.4}};
    const b3Pos* positions=variant==2?crowded_other:variant?crowded:original;int count=variant?9:2,target=variant==2?2:variant?6:1;
    for(int i=0;i<count;i++) {
        SwatController* c=&sim.actors[i].controller;
        if((variant==1 && (i==1 || i==2)) || (variant && i==7))swat_body_set_crouch(&c->body,true);
        b3Body_SetTransform(c->body.body,b3OffsetPos(positions[i],swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);
        b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);c->yaw=variant?(i==0?SWAT_PI*.5f:0):SWAT_PI;
        if(variant)c->body.onGround=true;
        if(variant==1 && i==5)b3Body_SetLinearVelocity(c->body.body,swat_v(-.202664942f,-.494241685f,.000013716f));
        if(variant && i==target)b3Body_SetLinearVelocity(c->body.body,swat_v(-.161651954f,-.250439614f,.000012911f));
        if(variant && i>=3 && i<=5)sim.actors[i].mind.order=SWAT_ORDER_FALL_IN;
        if((variant==1 && (i==1 || i==2)) || (variant && i==7)) {
            sim.actors[i].gear.surrendered=sim.actors[i].gear.restrained=true;
            sim.actors[i].rescued=i!=1;sim.actors[i].mind.escort_owner=-1;
        }
    }
    sim.actors[target].gear.surrendered=sim.actors[target].gear.restrained=true;sim.actors[target].mind.escort_owner=0;
    if(variant){b3Pos next;assert(swat_navigation_next_for_actor(&sim,target,positions[0],&next));fprintf(stderr,"Crowded first waypoint %.9f %.9f %.9f\n",next.x,next.y,next.z);}
    SwatInput neutral=swat_neutral_input();for(int i=0;i<(variant?1200:360);i++)swat_sim_step(&sim,&neutral);
    b3Pos end=swat_body_feet_position(&sim.actors[target].controller.body);
    fprintf(stderr,"Low support variant%d end %.3f %.3f %.3f rescued%d distance%.3f\n",variant,(float)end.x,(float)end.y,(float)end.z,sim.actors[target].rescued,b3Distance(end,positions[0]));
    // The exact side of the officer is not prescribed. Leave the stop's raised
    // surface and reach the real follow distance / civilian extraction gate.
    assert(end.z>5.2 && end.y<.02 && (variant?sim.actors[target].rescued:b3Distance(end,positions[0])<1.4f));
    if(variant)for(int i=3;i<6;i++) {
        b3Pos officer=swat_body_feet_position(&sim.actors[i].controller.body);
        assert(officer.z>5.2 && officer.y<.02 && b3Distance(officer,sim.extraction)<SWAT_STAGING_RADIUS);
    }
    swat_sim_close(&sim);printf("PASS escort over low support variant%d: original parking-stop collision, ordinary NPC movement leaves the rim and reaches its officer/extraction\n",variant);
}

int main(int argc,char** argv) {
    if(argc==2 && !strcmp(argv[1],"escort")){for(int i=0;i<3;i++)low_support_escort(i);return 0;}
    assert(argc==1);
    low_fixture();
    for(int i=0;i<3;i++)low_support_escort(i);
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
