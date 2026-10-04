#include "environment_binding.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static SwatWorld before;
static void cardinal_doors(void) {
    for(int yaw=0;yaw<4;yaw++) {
        SwatWorld w={0}; swat_world_init(&w);
        swat_build_framed_wall(&w,(b3Pos){3,0,4},yaw*SWAT_PI*.5f,4,2.7f,0,1.2f,0,2.1f,true);
        int index=-1;
        for(int i=0;i<w.count;i++) if(w.objects[i].door) index=i;
        assert(index>=0); SwatObject* door=&w.objects[index];
        assert(swat_environment_surface(door)==SWAT_ENV_DOOR);
        door->door_open=true;
        for(int tick=0;tick<100;tick++) {
            assert(b3Distance(swat_environment_point(door,swat_v(0,0,-.5f)),door->hinge)<1e-5f);
            assert(fabs(swat_environment_point(door,swat_v(0,-.5f,0)).y-.08)<1e-5f);
            for(int x=-1;x<=1;x+=2) for(int y=-1;y<=1;y+=2) for(int z=-1;z<=1;z+=2) {
                b3Vec3 corner=swat_v(x*door->half.x,y*door->half.y,z*door->half.z);
                b3Pos expected=b3TransformPoint(b3Body_GetTransform(door->body),corner);
                assert(b3Distance(expected,swat_environment_point(door,swat_v(.5f*x,.5f*y,.5f*z)))<1e-5f);
            }
            swat_world_step_doors(&w);
        }
        assert(swat_world_damage(&w,index,door->health+1));
        assert(swat_environment_surface(door)==SWAT_ENV_NONE);
        swat_world_close(&w);
    }
}
static void bindings_and_replication(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.hostile_fire=false;
    int bindings=0,skins=0;
    for(int seed=0;seed<32;seed++) {
        config.layout_seed=seed; config.generator=(SwatGenerator)(seed%2);
        swat_sim_init(&sim,config,42);
        memcpy(&before,&sim.world,sizeof(before));
        int removed=-1,surviving=-1;
        for(int i=0;i<sim.world.count;i++) {
            const SwatObject* o=&sim.world.objects[i];
            SwatEnvironmentSurface surface=swat_environment_surface(o);
            if(surface!=SWAT_ENV_NONE) bindings++;
            if(o->part==SWAT_PART_SKIN) {
                skins++; assert(surface==SWAT_ENV_PLASTER);
                if(removed<0) removed=i; else if(surviving<0) surviving=i;
            }
            if(o->material==SWAT_GLASS) assert(surface==SWAT_ENV_NONE);
            if(o->material==SWAT_WOOD && (o->part==SWAT_PART_FRAME || o->part==SWAT_PART_SUPPORT))
                assert(surface==SWAT_ENV_WOOD);
        }
        assert(!memcmp(&before,&sim.world,sizeof(before))); // Pure binding, no simulation mutation.
        assert(removed>=0 && surviving>=0);
        assert(swat_world_damage(&sim.world,removed,10000));
        assert(swat_environment_surface(&sim.world.objects[removed])==SWAT_ENV_NONE);
        assert(swat_environment_surface(&sim.world.objects[surviving])==SWAT_ENV_PLASTER);
        static SwatMap map; static SwatSnapshot snapshot;
        swat_capture_map(&sim,seed+1,&map); swat_apply_map(&replica,&map);
        swat_capture_snapshot(&sim,seed+1,&snapshot); assert(swat_apply_snapshot(&replica,&snapshot));
        assert(sim.world.count==replica.world.count);
        for(int i=0;i<sim.world.count;i++)
            assert(swat_environment_surface(&sim.world.objects[i])==swat_environment_surface(&replica.world.objects[i]));
        swat_sim_close(&replica); swat_sim_close(&sim);
    }
    printf("PASS environment binding: %d render pieces (%d skins), 32 generated maps, independent destruction and replica ownership\n",bindings,skins);
}
int main(void) {
    cardinal_doors(); bindings_and_replication();
    puts("PASS environment transforms: cardinal yaws, every door-swing step, collider bounds, hinge and 80 mm gap");
    return 0;
}
