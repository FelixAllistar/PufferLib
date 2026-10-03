#include "sim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_transmission_and_arrival(void) {
    SwatWorld world; swat_world_init(&world);
    swat_world_box(&world,(b3Pos){5,-0.5f,0},swat_v(20,0.5f,20),SWAT_CONCRETE,0);
    SwatSoundEvent event={1,10,0,SWAT_SOUND_SHOT,{0,1.6f,0},3,100};
    b3Pos listener={10,1.6f,0};
    SwatAcousticPath clear=swat_acoustic_path(&world,&event,listener);
    assert(clear.gain>0.25f && clear.delay_ticks==2 && clear.direction.x<-0.99f);
    int wall=swat_world_box(&world,(b3Pos){5,1.6f,0},swat_v(0.1f,2,3),SWAT_WOOD,100);
    SwatAcousticPath muffled=swat_acoustic_path(&world,&event,listener);
    assert(muffled.gain<clear.gain && muffled.bands[2]<muffled.bands[0]*0.2f);
    world.objects[wall].material=SWAT_CONCRETE;
    SwatAcousticPath concrete=swat_acoustic_path(&world,&event,listener);
    assert(concrete.gain<muffled.gain*0.1f);
    assert(swat_world_damage(&world,wall,200));
    assert(fabsf(swat_acoustic_path(&world,&event,listener).gain-clear.gain)<1e-6f);
    SwatSoundLog log={0}; SwatHearingMemory memory={0}; SwatHeardSound heard;
    swat_sound_append(&log,event); swat_sound_append(&log,event); assert(log.count==1);
    assert(!swat_hearing_next(&world,&log,10,listener,1,&memory,&heard));
    assert(!swat_hearing_next(&world,&log,11,listener,1,&memory,&heard));
    assert(swat_hearing_next(&world,&log,12,listener,1,&memory,&heard));
    assert(heard.id==1 && fabsf(fabsf(heard.bearing)-SWAT_PI)<0.01f);
    assert(!swat_hearing_next(&world,&log,13,listener,1,&memory,&heard));
    memset(&memory,0,sizeof(memory));
    assert(!swat_hearing_next(&world,&log,200,listener,1,&memory,&heard));
    swat_world_close(&world);
    puts("PASS acoustics: distance, propagation delay, frequency-dependent cover, destruction, duplicate suppression and expiry");
}
static void test_doorway_route(void) {
    SwatWorld world; swat_world_init(&world); uint32_t seed=42;
    swat_world_build_range(&world,&seed,false);
    SwatSoundEvent event={1,0,0,SWAT_SOUND_SHOT,{9,1.6f,3},3,100};
    b3Pos listener={5,1.6f,3};
    SwatAcousticPath closed=swat_acoustic_path(&world,&event,listener);
    int door=-1; for(int i=0;i<world.count;i++) if(world.objects[i].door) door=i;
    world.objects[door].door_open=true;
    for(int i=0;i<90;i++) swat_world_step_doors(&world);
    SwatAcousticPath open=swat_acoustic_path(&world,&event,listener);
    assert(open.via_doorway && open.gain>closed.gain*10);
    assert(open.direction.z<-.5f && open.direction.x>.4f); // heard at the doorway, not through the wall
    assert(swat_world_damage(&world,door,200));
    assert(swat_acoustic_path(&world,&event,listener).via_doorway);
    swat_world_close(&world);
    puts("PASS acoustics: open/broken doorway carries sound around the partition with arrival bearing at the opening");
}
static void test_agent_listens(void) {
    SwatSim sim; SwatConfig config=swat_default_config(); config.randomize=false;
    swat_sim_init(&sim,config,42);
    float before=sim.actors[1].controller.yaw;
    swat_sound_emit(&sim.sounds,0,0,SWAT_SOUND_SHOT,(b3Pos){18,1.6f,5},3,100);
    SwatInput neutral=swat_neutral_input();
    for(int t=0;t<24;t++) swat_sim_step(&sim,&neutral);
    assert(sim.actors[1].hearing_ticks>0);
    assert(fabsf(swat_angle(sim.actors[1].controller.yaw-before))>0.2f);
    assert(sim.actors[1].visible_ticks==0 && sim.actors[1].arsenal.shots==0);
    swat_sim_close(&sim);
    puts("PASS scripted guard orients to an audible hidden event without acquiring or firing at an unseen officer");
}
int main(void) { test_transmission_and_arrival(); test_doorway_route(); test_agent_listens(); return 0; }
