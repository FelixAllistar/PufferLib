#include "sim.h"
#include "door_damage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Exhaustive pre-optimization implementation is the independent oracle.
// Compare exact bands/bearings/delays, including ties and mutations, so spatial
// culling cannot silently change AI decisions or deterministic replays.
// Intersect a segment with the same oriented boxes used by physics. Actors
// do not act as acoustic walls. Destroyed cover is excluded immediately.
static float reference_thickness(const SwatObject* o, b3Pos from, b3Pos to) {
    b3Vec3 p=b3SubPos(from,o->center),d=b3SubPos(to,from);
    float c=cosf(o->yaw),s=sinf(o->yaw);
    float origin[3]={c*p.x-s*p.z,p.y,s*p.x+c*p.z};
    float delta[3]={c*d.x-s*d.z,d.y,s*d.x+c*d.z};
    if(o->pitch!=0) {
        float cp=cosf(o->pitch),sp=sinf(o->pitch),x=origin[0],dx=delta[0];
        origin[0]=cp*x+sp*origin[1]; origin[1]=-sp*x+cp*origin[1];
        delta[0]=cp*dx+sp*delta[1]; delta[1]=-sp*dx+cp*delta[1];
    }
    float half[3]={o->half.x,o->half.y,o->half.z};
    float enter=0,exit=1;
    for(int axis=0;axis<3;axis++) {
        if(fabsf(delta[axis])<1e-7f) {
            if(fabsf(origin[axis])>half[axis]) return 0;
        } else {
            float a=(-half[axis]-origin[axis])/delta[axis];
            float b=(half[axis]-origin[axis])/delta[axis];
            enter=fmaxf(enter,fminf(a,b)); exit=fminf(exit,fmaxf(a,b));
            if(exit<=enter) return 0;
        }
    }
    if(o->fractured) for(int i=0;i<4;i++) {
        int j=(i+1)%4;
        float ey=o->corners[j][0]-o->corners[i][0],ez=o->corners[j][1]-o->corners[i][1];
        float value=ey*(origin[2]-o->corners[i][1])-ez*(origin[1]-o->corners[i][0]);
        float slope=ey*delta[2]-ez*delta[1];
        if(fabsf(slope)<1e-7f) { if(value<0) return 0; }
        else if(slope>0) enter=fmaxf(enter,-value/slope);
        else exit=fminf(exit,-value/slope);
        if(exit<=enter) return 0;
    }
    return (exit-enter)*b3Length(d);
}

static void reference_transmit(const SwatWorld* world, b3Pos from, b3Pos to, float bands[3]) {
    float loss[3]={0};
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];
        if(!o->active) continue;
        float thickness=reference_thickness(o,from,to);
        if(thickness<=0) continue;
        const SwatMaterialDef* material=swat_material(o->material);
        // A thicker homogeneous slab is not many independent partitions.
        // Use a mass-law-style logarithmic thickness correction; separate
        // board faces still contribute separate losses (no cavity resonance).
        float correction=20*log10f(fmaxf(thickness/material->reference_thickness,.001f));
        for(int b=0;b<3;b++) loss[b]+=fmaxf(0,material->transmission_db[b]+correction);
    }
    for(int b=0;b<3;b++) bands[b]*=powf(10,-fminf(loss[b],180)*.05f);
}

static SwatAcousticPath reference_route(const SwatWorld* world, const SwatSoundEvent* event,
                                           b3Pos listener, const b3Pos* portal) {
    SwatAcousticPath path={0};
    float length=portal ? b3Distance(event->position,*portal)+b3Distance(*portal,listener) :
                         b3Distance(event->position,listener);
    if(length>event->range) return path;
    float attenuation=event->strength/(1.0f+length);
    for(int b=0;b<3;b++) path.bands[b]=attenuation;
    if(portal) {
        reference_transmit(world,event->position,*portal,path.bands);
        reference_transmit(world,*portal,listener,path.bands);
        // One bend loses some energy, especially in the high band.
        path.bands[0]*=0.75f; path.bands[1]*=0.55f; path.bands[2]*=0.35f;
    } else reference_transmit(world,event->position,listener,path.bands);
    path.gain=path.bands[0]*0.3f+path.bands[1]*0.5f+path.bands[2]*0.2f;
    path.direction=swat_normalize(b3SubPos(portal ? *portal : event->position,listener));
    path.delay_ticks=(int)ceilf(length/(343.0f*SWAT_DT));
    path.via_doorway=portal!=NULL;
    return path;
}

static SwatAcousticPath reference_path(const SwatWorld* world, const SwatSoundEvent* event,
                                   b3Pos listener) {
    SwatAcousticPath best=reference_route(world,event,listener,NULL);
    for(int i=0;i<world->count;i++) {
        const SwatObject* door=&world->objects[i];
        if(!door->door || (door->active && door->door_angle<0.15f)) continue;
        // Authored openings retain their location when the door swings/breaks.
        // Sample across the opening: a swung leaf can obstruct the centre
        // route even when sound can pass its far edge.
        for(int sample=0;sample<3;sample++) {
            float offset=2*door->half.z*(.15f+.35f*sample);
            b3Pos portal=b3OffsetPos(door->hinge,swat_v(sinf(door->closed_yaw)*offset,0,cosf(door->closed_yaw)*offset));
            portal.y=swat_clamp((float)(event->position.y+listener.y)*.5f,
                               (float)door->center.y-door->half.y+.12f,(float)door->center.y+door->half.y-.12f);
            SwatAcousticPath candidate=reference_route(world,event,listener,&portal);
            if(candidate.gain>best.gain) best=candidate;
        }
    }
    return best;
}

static void assert_reference(const SwatWorld* world,const SwatSoundEvent* event,b3Pos listener) {
    SwatAcousticPath expected=reference_path(world,event,listener),actual=swat_acoustic_path(world,event,listener);
    for(int i=0;i<3;i++) assert(expected.bands[i]==actual.bands[i]);
    assert(expected.gain==actual.gain && expected.delay_ticks==actual.delay_ticks && expected.via_doorway==actual.via_doorway);
    assert(expected.direction.x==actual.direction.x && expected.direction.y==actual.direction.y && expected.direction.z==actual.direction.z);
}
static void test_spatial_search(void) {
    SwatWorld* world=calloc(1,sizeof(*world)); assert(world);
    uint32_t seed=42;
    for(int scene=0;scene<3;scene++) {
        swat_world_init(world);
        SwatSoundEvent event={1,0,0,SWAT_SOUND_SHOT,{0,1.6f,0},3,100,SWAT_CONCRETE,SWAT_SOUND_PROFILE_UNSPECIFIED};
        assert_reference(world,&event,(b3Pos){2,1.6f,1});
        if(scene==0) swat_world_build_range(world,&seed,false);
        else if(scene==1) swat_mission_build_house(world);
        else {
            // Arbitrary yaw, tilt, overlaps, and acoustic boxes without physics
            // bodies exercise the authored-bounds contract independently of BVH physics.
            world->count=SWAT_MAX_OBJECTS;
            for(int i=0;i<world->count;i++) {
                SwatObject* o=&world->objects[i]; o->active=true;
                o->center=(b3Pos){(i%31)*.7f-10,((i/31)%4)*.7f,(i/124)*.8f-5};
                o->half=swat_v(.03f+(i%9)*.03f,.2f,.1f+(i%7)*.05f);
                o->yaw=(i%23)*.21f; o->pitch=(i%4)*.15f;
                o->material=(SwatMaterial)(i%SWAT_MATERIAL_COUNT);
            }
        }
        for(int i=0;i<120;i++) {
            event.position=(b3Pos){(i%17)*1.7f-2,1.1f+(i%5)*.21f,(i%13)*1.1f-6};
            event.range=(i%3)==0 ? 8 : 100;
            b3Pos listener={(i%19)*1.3f-4,.5f+(i%7)*.23f,(i%11)*1.5f-5};
            if(i==30) for(int j=0;j<world->count;j++) if(world->objects[j].door) world->objects[j].door_open=true;
            if(i>=30 && scene<2) swat_world_step_doors(world);
            if(i==75 && scene<2) for(int j=0;j<world->count;j++) if(world->objects[j].door) swat_world_damage(world,j,1000);
            if(i>85) { world->objects[i].active=false; world->objects[i+1].material=SWAT_WOOD; }
            assert_reference(world,&event,listener);
            assert_reference(world,&event,event.position);
        }
        swat_world_close(world);
    }
    free(world);
    puts("PASS acoustic spatial search matches exhaustive routing exactly for 720 paths, swinging/broken doors, destruction and overlapping rotated/tilted boxes");
}

static bool reference_hearing_next(const SwatWorld* world, const SwatSoundLog* log, int tick,
                       b3Pos listener, int actor, SwatHearingMemory* memory,
                       SwatHeardSound* heard) {
    for(int i=0;i<log->count;i++) {
        const SwatSoundEvent* event=swat_sound_at(log,i);
        if(event->source_actor==actor || swat_hearing_consumed(memory,event->id)) continue;
        if(tick-event->tick>SWAT_SOUND_LIFETIME) { swat_hearing_consume(memory,event->id); continue; }
        SwatAcousticPath path=reference_path(world,event,listener);
        if(tick<event->tick+path.delay_ticks || path.gain<0.008f) continue;
        swat_hearing_consume(memory,event->id);
        float angle=atan2f(path.direction.z,path.direction.x);
        // Sixteen bearing sectors represent a hearing cue, not exact world truth.
        float sector=SWAT_PI/8;
        *heard=(SwatHeardSound){event->id,event->kind,path.gain,roundf(angle/sector)*sector};
        return true;
    }
    return false;
}
static void test_hearing_sequence(void) {
    SwatWorld* world=calloc(1,sizeof(*world)); assert(world);
    swat_world_init(world); swat_mission_build_house(world);
    SwatHearingMemory actual={0},expected={0}; SwatSoundLog log={0};
    for(int tick=0;tick<180;tick++) {
        b3Pos source={10+(tick%7)*.3f,1.6f,-4+(tick%11)*.4f};
        swat_sound_emit(&log,tick,tick%3,SWAT_SOUND_IMPACT,source,.3f,30);
        swat_sound_emit(&log,tick,tick%2,SWAT_SOUND_SHOT,source,3,100);
        b3Pos listener={12+(tick%5)*.2f,1.6f,-3+(tick%9)*.2f};
        if(tick==30) for(int j=0;j<world->count;j++) if(world->objects[j].door) world->objects[j].door_open=true;
        if(tick==90) for(int j=0;j<world->count;j++) if(world->objects[j].door) swat_world_damage(world,j,1000);
        if(tick==120) { swat_world_damage(world,50,1000); world->objects[51].material=SWAT_GLASS; }
        swat_world_step_doors(world);
        for(int n=0;n<4;n++) {
            SwatHeardSound a={0},e={0};
            bool heard=swat_hearing_next(world,&log,tick,listener,1,&actual,&a);
            assert(heard==reference_hearing_next(world,&log,tick,listener,1,&expected,&e));
            if(heard) assert(a.id==e.id && a.kind==e.kind && a.gain==e.gain && a.bearing==e.bearing);
            assert(memcmp(&actual,&expected,sizeof(actual))==0);
            if(!heard) break;
        }
    }
    swat_world_close(world); free(world);
    puts("PASS exact hearing sequence through moving listener/doors, destruction, material changes, arrival delays, expiry and sound-log wrap");
}

static void test_transmission_and_arrival(void) {
    SwatWorld world; swat_world_init(&world);
    swat_world_box(&world,(b3Pos){5,-0.5f,0},swat_v(20,0.5f,20),SWAT_CONCRETE,0);
    SwatSoundEvent event={1,10,0,SWAT_SOUND_SHOT,{0,1.6f,0},3,100,SWAT_CONCRETE,SWAT_SOUND_PROFILE_UNSPECIFIED};
    b3Pos listener={10,1.6f,0};
    SwatAcousticPath clear=swat_acoustic_path(&world,&event,listener);
    assert(clear.gain>0.25f && clear.delay_ticks==2 && clear.direction.x<-0.99f);
    int wall=swat_world_box(&world,(b3Pos){5,1.6f,0},swat_v(0.1f,2,3),SWAT_WOOD,100);
    SwatAcousticPath muffled=swat_acoustic_path(&world,&event,listener);
    assert(muffled.gain<clear.gain && muffled.bands[2]<muffled.bands[0]*0.2f);
    world.objects[wall].material=SWAT_CONCRETE;
    SwatAcousticPath concrete=swat_acoustic_path(&world,&event,listener);
    assert(concrete.gain<muffled.gain*0.2f);
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

static void test_hearing_bounds(void) {
    SwatWorld* world=calloc(1,sizeof(*world));assert(world);swat_world_init(world);
    float step=343.0f*SWAT_DT;
    const float distances[]={0,.00001f,nextafterf(step,0),step,nextafterf(step,INFINITY),
        nextafterf(35,0),35,nextafterf(35,INFINITY),100};
    const float ranges[]={0,.5f,35,100};const int ages[]={-1,0,1,2,6,7,120,121};int checked=0;
    for(unsigned d=0;d<sizeof(distances)/sizeof(*distances);d++)
    for(unsigned r=0;r<sizeof(ranges)/sizeof(*ranges);r++)
    for(unsigned t=0;t<sizeof(ages)/sizeof(*ages);t++) {
        SwatSoundLog log={0};SwatHearingMemory actual={0},expected={0};SwatHeardSound a={0},e={0};
        swat_sound_emit(&log,10,0,SWAT_SOUND_DOOR_CLOSE,(b3Pos){0,1.6f,0},3,ranges[r]);
        b3Pos listener={distances[d],1.6f,0};int tick=10+ages[t];
        bool heard=swat_hearing_next(world,&log,tick,listener,1,&actual,&a);
        assert(heard==reference_hearing_next(world,&log,tick,listener,1,&expected,&e));
        if(heard)assert(a.id==e.id && a.kind==e.kind && a.gain==e.gain && a.bearing==e.bearing);
        assert(!memcmp(&actual,&expected,sizeof(actual)));checked++;
    }
    SwatSoundLog log={0};SwatHearingMemory actual={0},expected={0};int heard_tick=-1;
    swat_sound_emit(&log,0,0,SWAT_SOUND_BREACH,(b3Pos){0,1.6f,0},3,5);
    for(int tick=0;tick<125;tick++) {
        b3Pos listener={101-tick,1.6f,0};SwatHeardSound a={0},e={0};
        bool heard=swat_hearing_next(world,&log,tick,listener,1,&actual,&a);
        assert(heard==reference_hearing_next(world,&log,tick,listener,1,&expected,&e));
        if(heard){assert(a.id==e.id && a.gain==e.gain && a.bearing==e.bearing);heard_tick=tick;}
        assert(!memcmp(&actual,&expected,sizeof(actual)));
    }
    assert(heard_tick==96);swat_world_close(world);free(world);
    printf("PASS hearing bounds: %d exhaustive next-float range/arrival/future/expiry comparisons and approaching listener hears the retained event at tick 96\n",checked);
}
static void test_doorway_route(void) {
    SwatWorld world; swat_world_init(&world); uint32_t seed=42;
    swat_world_build_range(&world,&seed,false);
    SwatSoundEvent event={1,0,0,SWAT_SOUND_SHOT,{9,1.6f,3},3,100,SWAT_CONCRETE,SWAT_SOUND_PROFILE_UNSPECIFIED};
    b3Pos listener={5,1.6f,3};
    SwatAcousticPath closed=swat_acoustic_path(&world,&event,listener);
    world.room_count=1;
    world.rooms[0]=(SwatRoom){.center={11,1.8f,0},.half={4,1.8f,6.8f},.surface=SWAT_CONCRETE,.floor=SWAT_CONCRETE};
    float closed_wet=swat_acoustic_room(&world,event.position).wet;
    int door=-1; for(int i=0;i<world.count;i++) if(world.objects[i].door) door=i;
    world.objects[door].door_open=true;
    for(int i=0;i<90;i++) swat_world_step_doors(&world);
    SwatAcousticPath open=swat_acoustic_path(&world,&event,listener);
    assert(open.via_doorway && open.gain>closed.gain*10);
    assert(open.direction.z<-.5f && open.direction.x>.4f); // heard at the doorway, not through the wall
    SwatObject* leaf=&world.objects[door];
    // Changing only portal identity must refresh the cached door list too.
    leaf->door=false;assert_reference(&world,&event,listener);
    assert(!swat_acoustic_path(&world,&event,listener).via_doorway);
    leaf->door=true;assert_reference(&world,&event,listener);
    SwatAcousticPath restored=swat_acoustic_path(&world,&event,listener);
    assert(restored.via_doorway && restored.gain==open.gain && restored.delay_ticks==open.delay_ticks);
    leaf->door_open=false;for(int t=0;t<90;t++)swat_world_step_doors(&world);
    assert(swat_world_break_latch(&world,door));
    for(int h=0;h<SWAT_DOOR_HINGES;h++) {
        b3Vec3 anchor=swat_door_hinge(leaf,h);
        assert(swat_world_cut(&world,door,(SwatCut){.y=anchor.y,.z=anchor.z,.height=.18f,.width=.10f,.seed=(unsigned)h,.kind=SWAT_CUT_IMPACT})==1);
    }
    assert(swat_door_loose(leaf));
    for(int t=0;t<600;t++) {
        swat_world_step_doors(&world);b3World_Step(world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);swat_world_sync_doors(&world);
    }
    if(b3Body_IsAwake(leaf->body) || leaf->center.y>=.2f)
        fprintf(stderr,"Acoustic fallen fixture: centre %.6f %.6f %.6f awake%d\n",(float)leaf->center.x,(float)leaf->center.y,(float)leaf->center.z,b3Body_IsAwake(leaf->body));
    assert(!b3Body_IsAwake(leaf->body) && leaf->center.y<.2f);
    // Legacy yaw/open controls do not define a fallen leaf's full pose.
    leaf->door_open=false;leaf->door_angle=0;
    SwatAcousticPath fallen=swat_acoustic_path(&world,&event,listener);
    assert(fallen.via_doorway && fallen.gain>closed.gain*10 && fabsf(fallen.direction.y)<1e-6f);
    assert(swat_acoustic_room(&world,event.position).wet<closed_wet);
    assert(swat_world_damage(&world,door,200));
    assert(swat_acoustic_path(&world,&event,listener).via_doorway);
    swat_world_close(&world);
    puts("PASS acoustics: open/fallen/broken doorway carries sound around the partition at the original opening height; full leaf tilt releases room reverberation");
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
static void test_surface_room(void) {
    SwatWorld world; swat_world_init(&world); swat_mission_build_house(&world);
    b3Pos listener={14,1.4f,-2};
    SwatRoomAcoustics carpet=swat_acoustic_room(&world,listener);
    world.rooms[1].floor=SWAT_TILE;
    SwatRoomAcoustics tile=swat_acoustic_room(&world,listener);
    assert(tile.rt60[1]>carpet.rt60[1]*1.4f && tile.rt60[2]>carpet.rt60[2]*1.5f);
    int roof=-1;
    for(int i=0;i<world.count;i++) if(world.objects[i].center.y>2.7f && world.objects[i].half.x>3 && world.objects[i].half.z>3) roof=i;
    assert(roof>=0); world.objects[roof].material=SWAT_INSULATION;
    SwatRoomAcoustics absorbent=swat_acoustic_room(&world,listener);
    assert(absorbent.rt60[1]<tile.rt60[1]*.6f);
    for(int i=0;i<world.count;i++) if(world.objects[i].door) swat_world_damage(&world,i,1000);
    SwatRoomAcoustics open=swat_acoustic_room(&world,listener);
    assert(open.rt60[1]<absorbent.rt60[1] && open.wet<absorbent.wet);
    swat_world_close(&world);
    puts("PASS room surfaces: floor finish, actual roof absorption, furnishings and destroyed door openings affect decay");
}
static void test_fragment_path(void) {
    SwatWorld* world=calloc(1,sizeof(*world)); assert(world); swat_world_init(world);
    int id=swat_world_box(world,(b3Pos){0,1,0},swat_v(.02f,1,1),SWAT_DRYWALL,50);
    SwatObject* skin=&world->objects[id]; skin->part=SWAT_PART_SKIN;
    const float corners[4][2]={{-1,-1},{1,-1},{.5f,1},{-.5f,1}};
    assert(swat_world_fragment(skin,corners));
    SwatSoundEvent e={1,0,0,SWAT_SOUND_SHOT,{-1,1.9f,.9f},3,100,SWAT_CONCRETE,SWAT_SOUND_PROFILE_UNSPECIFIED};
    b3Pos listener={1,1.9f,.9f};
    assert(!swat_world_ray(world,e.position,swat_v(1,0,0),2,b3_nullBodyId).hit);
    SwatAcousticPath outside=swat_acoustic_path(world,&e,listener);
    assert(fabsf(outside.gain-1)<1e-6f);
    e.position=(b3Pos){-1,1,0}; listener=(b3Pos){1,1,0};
    assert(swat_world_ray(world,e.position,swat_v(1,0,0),2,b3_nullBodyId).hit);
    SwatAcousticPath inside=swat_acoustic_path(world,&e,listener); assert(inside.gain<outside.gain*.7f);
    swat_world_damage(world,id,1000); assert(fabsf(swat_acoustic_path(world,&e,listener).gain-1)<1e-6f);
    swat_world_close(world); free(world);
    puts("PASS fragment acoustics: empty convex corners agree with physical rays, board attenuates, destruction refreshes immediately");
}

int main(void) {
    test_fragment_path(); test_spatial_search(); test_hearing_sequence(); test_hearing_bounds(); test_transmission_and_arrival(); test_doorway_route(); test_agent_listens(); test_surface_room(); return 0; }
