// The Hollow (SWAT_HOLLOW): map structure, connectivity and a physical
// rescue run. The officer walks the authoritative navigation route from the
// lane to every room and the hostage, escorts the hostage back, and the
// ordinary tactical rules end the mission in success.
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim;
static b3Pos feet(int actor) { return swat_body_feet_position(&sim.actors[actor].controller.body); }
static b3Pos room_floor(int room) {
    const SwatRoom* r=&sim.world.rooms[room];
    return b3OffsetPos(r->center,swat_v(0,-r->half.y+.02f,0));
}
// Steer the officer along swat_navigation_next until within `radius` of goal.
static bool trace,escorting;
static bool walk(b3Pos goal,float radius,int limit) {
    for(int t=0;t<limit;t++) {
        b3Pos at=feet(0);
        if(trace && t%240==0) printf("  t%d officer (%.1f %.1f %.1f) hostage (%.1f %.1f %.1f) owner %d\n",t,at.x,at.y,at.z,feet(2).x,feet(2).y,feet(2).z,sim.actors[2].mind.escort_owner);
        if(b3Distance(at,goal)<radius) return true;
        b3Pos next; SwatInput in=swat_neutral_input();
        bool wait=escorting && b3Distance(at,feet(2))>4.0f; // let the hostage keep up
        if(!wait && swat_navigation_next_for_actor(&sim,0,goal,&next)) {
            b3Vec3 d=b3SubPos(next,at); sim.actors[0].controller.yaw=atan2f(d.z,d.x); in.forward=1;
        }
        swat_sim_step(&sim,&in);
    }
    return false;
}
static void structure(void) {
    printf("Hollow: %d objects, %d rooms\n",sim.world.count,sim.world.room_count);
    assert(sim.world.count<SWAT_MAX_OBJECTS && sim.world.room_count==8);
    int doors=0,locked=0,boards=0,lights=0;
    for(int i=0;i<sim.world.count;i++) {
        const SwatObject* o=&sim.world.objects[i];
        doors+=o->door; locked+=o->door && o->locked; lights+=o->part==SWAT_PART_LIGHT;
        boards+=o->part==SWAT_PART_SKIN && o->material==SWAT_WOOD && o->wall_group;
    }
    printf("Hollow: %d doors (%d locked), %d wall boards, %d bulbs\n",doors,locked,boards,lights);
    assert(doors==10 && locked==3 && boards>300 && lights==8);
    assert(swat_sim_hostiles(&sim)==2 && swat_sim_unsecured(&sim)==1);
    assert(swat_world_room(&sim.world,b3OffsetPos(feet(1),swat_v(0,1,0)))==4);   // owner upstairs
    assert(swat_world_room(&sim.world,b3OffsetPos(feet(2),swat_v(0,1,0)))==7);   // hostage in the cellar
    assert(swat_world_room(&sim.world,b3OffsetPos(feet(8),swat_v(0,1,0)))==5);   // accomplice in the barn
    assert(fabsf((float)feet(2).y-SWAT_HOLLOW_HOSTAGE.y)<.05f && fabsf((float)feet(1).y-SWAT_HOLLOW_OWNER.y)<.05f);
    // Staging at the lane is hidden from every window of the house.
    b3Pos eye=b3OffsetPos(sim.mission.staging,swat_v(0,1.6f,0));
    for(int i=0;i<sim.world.count;i++) {
        const SwatObject* o=&sim.world.objects[i];
        if(o->material!=SWAT_GLASS || o->part!=SWAT_PART_SOLID || o->center.x<13.9f || o->center.x>22.1f || o->center.z>.1f) continue;
        b3Pos pane=b3OffsetPos(o->center,swat_v(0,0,o->center.z<-5.9f ? -.2f : 0));
        if(swat_world_sight_clear(&sim.world,eye,pane)) printf("staging visible from window at (%.2f %.2f %.2f)\n",o->center.x,o->center.y,o->center.z);
        assert(!swat_world_sight_clear(&sim.world,eye,pane));
    }
    puts("PASS hollow map: boxed-house board walls, doors, eight rooms with bulbs, occupants placed, staging screened from the house");
}
static void connectivity(void) {
    // Unlock and swing open every leaf so the planner and the walker test the
    // building itself (breaching is covered by the door tests).
    for(int i=0;i<sim.world.count;i++) if(sim.world.objects[i].door) { sim.world.objects[i].locked=false; sim.world.objects[i].door_open=true; }
    for(int t=0;t<90;t++) swat_sim_step(&sim,&(SwatInput){0});
    b3Pos next;
    for(int room=0;room<sim.world.room_count;room++) {
        bool routed=swat_navigation_next(&sim,sim.mission.staging,room_floor(room),&next);
        if(!routed) printf("room %d (%.1f %.1f %.1f) has no route, nearest node %d\n",room,room_floor(room).x,room_floor(room).y,room_floor(room).z,swat_navigation_nearest(sim.navigation,room_floor(room)));
        assert(routed);
    }
    assert(swat_navigation_next(&sim,sim.mission.staging,SWAT_HOLLOW_HOSTAGE,&next));
    // Walk it: lane -> barn loft -> house upstairs -> smokehouse -> cellar.
    const b3Pos stops[]={{38.5f,HOLLOW_LOFT,1.5f},{15.6f,HOLLOW_UPPER,-2.2f},{20.0f,HOLLOW_UPPER,-4.5f},{25.5f,0,4.4f},{16.4f,HOLLOW_FLOOR,1.0f}};
    for(int i=0;i<5;i++) {
        bool ok=walk(stops[i],.7f,60*90);
        printf("walk %d -> (%.1f %.1f %.1f): %s at (%.2f %.2f %.2f) tick %d\n",i,stops[i].x,stops[i].y,stops[i].z,ok?"reached":"FAILED",
               feet(0).x,feet(0).y,feet(0).z,sim.tick);
        assert(ok);
    }
    // Keep the owner and the accomplice out of the run: both are already in custody.
    sim.actors[1].gear.surrendered=sim.actors[1].gear.restrained=true;
    sim.actors[8].gear.surrendered=sim.actors[8].gear.restrained=true;
    sim.evidence[1].collected=sim.evidence[8].collected=true;
    bool ok=walk(SWAT_HOLLOW_HOSTAGE,1.2f,60*90);
    printf("walk cellar: %s at (%.2f %.2f %.2f)\n",ok?"reached":"FAILED",feet(0).x,feet(0).y,feet(0).z); assert(ok);
    puts("PASS hollow connectivity: every room routes from staging; the officer walked to the loft, both storeys, the smokehouse, the kitchen and the cellar");
}
static void rescue(void) {
    // Restrain the hostage and lead her out to the lane.
    sim.actors[2].gear.surrendered=sim.actors[2].gear.restrained=true; sim.actors[2].mind.escort_owner=0;
    trace=escorting=true; bool ok=walk(sim.extraction,.8f,60*180); trace=escorting=false;
    // The two prisoners' weapons were bagged where they were cuffed.
    for(int i=0;i<sim.actor_count;i++) if(sim.evidence[i].dropped) { sim.evidence[i].collected=true; sim.debrief.evidence++; }
    for(int t=0;t<60*30 && sim.end==SWAT_RUNNING;t++) swat_sim_step(&sim,&(SwatInput){0});
    SwatScenarioProgress p=swat_sim_progress(&sim);
    printf("rescue: officer %s, hostage at (%.2f %.2f %.2f) rescued %d, end %d, secured %d (threats %d unsecured %d evacuees %d evidence %d away %d)\n",ok?"home":"lost",
           feet(2).x,feet(2).y,feet(2).z,sim.actors[2].rescued,sim.end,p.secured,p.threats,p.unsecured,p.evacuees,p.evidence,p.officers_away);
    assert(ok && sim.actors[2].rescued && p.secured && sim.end==SWAT_SUCCESS && sim.debrief.rescued==1 && sim.debrief.arrests==2);
    puts("PASS hollow completion: hostage escorted from the cellar to the lane, both men in custody, mission success");
}
static void replication(void) {
    static SwatSim replica; static SwatMap map;
    static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,3,&map); size_t n=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(n && swat_decode_map(&map,bytes,n)); swat_apply_map(&replica,&map);
    assert(replica.config.mission==SWAT_HOLLOW && replica.world.count==sim.world.count && replica.world.room_count==8);
    swat_sim_close(&replica);
    printf("PASS hollow replication: the full board map encodes (%zu bytes), decodes and rebuilds on a replica\n",n);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOLLOW; swat_config_human(&config);
    config.squad_bots=0; config.hostile_fire=false; config.randomize=false; config.max_ticks=60*60*20;
    swat_sim_init(&sim,config,7);
    structure(); replication(); connectivity(); rescue();
    swat_sim_close(&sim); return 0;
}
