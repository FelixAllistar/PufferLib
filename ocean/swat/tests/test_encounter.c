#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static void fixture(void) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.tactical_rules=true; sim.config.hostile_fire=false; sim.config.max_ticks=10000;
    sim.extraction=(b3Pos){-7,0,0}; sim.mission.staging=(b3Pos){0,0,0}; swat_world_init(&sim.world);
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(12,.5f,12),SWAT_CONCRETE,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){6,0,0},SWAT_PI);
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,(b3Pos){1.2f,0,0},SWAT_PI);
    sim.actor_count=3;
    for(int t=0;t<10;t++) swat_sim_step(&sim,&(SwatInput){0});
}
static void navigation(void) {
    fixture(); int wall=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.1f,1.5f,12.5f),SWAT_DRYWALL,30);
    b3Pos next; assert(!swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){6,0,0},&next));
    assert(sim.navigation && sim.navigation->built);
    assert(swat_world_damage(&sim.world,wall,100));
    assert(swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){6,0,0},&next));
    swat_sim_close(&sim);
    assert(!sim.navigation);
    puts("PASS navigation: actual static clearance blocks a route; authoritative destruction invalidates the cache and opens it");
}
static void navigation_lifecycle(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_RANGE; config.randomize=false;
    swat_sim_init(&sim,config,73);
    static SwatMap map;
    for(int i=0;i<3;i++) {
        assert(!sim.navigation);
        assert(!swat_navigation_crouch(&sim,(b3Pos){0,0,0}));
        b3Pos next; (void)swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){1,0,0},&next);
        assert(sim.navigation && sim.navigation->built);
        swat_capture_map(&sim,1,&map); swat_apply_map(&sim,&map);
        assert(!sim.navigation);
        (void)swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){1,0,0},&next);
        assert(sim.navigation);
        swat_sim_reset(&sim);
    }
    swat_sim_close(&sim); swat_sim_close(&sim);
    puts("PASS navigation lifecycle: lazy allocation, map replacement, reset and repeated close");
}
static void orders_and_escort(void) {
    fixture(); assert(swat_sim_set_player(&sim,1,true)); sim.actors[3].mind.bot=true; sim.actors[3].mind.team=1;
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[3].squad_order=SWAT_ORDER_HOLD; swat_encounter_orders(&sim,inputs); assert(!sim.actors[3].mind.pending_order);
    inputs[0].squad_order=SWAT_ORDER_HOLD; inputs[0].squad_queue=true;
    swat_encounter_orders(&sim,inputs); assert(sim.actors[3].mind.queued);
    sim.tick+=30; inputs[0].squad_order=0; swat_encounter_orders(&sim,inputs); assert(!sim.actors[3].mind.order);
    inputs[0].squad_execute=true; swat_encounter_orders(&sim,inputs); assert(sim.actors[3].mind.order==SWAT_ORDER_HOLD);
    assert(swat_sim_set_player(&sim,1,true)); assert(!sim.actors[3].mind.bot);
    inputs[0].squad_order=SWAT_ORDER_FALL_IN; swat_encounter_orders(&sim,inputs); assert(!sim.actors[3].mind.pending_order);
    SwatActor* civilian=&sim.actors[2]; civilian->gear.surrendered=civilian->gear.restrained=true;
    civilian->mind.escort_owner=0;
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){-4,.91f,0},b3Quat_identity);
    b3Pos before=swat_body_feet_position(&civilian->controller.body);
    for(int t=0;t<240;t++) swat_sim_step(&sim,&(SwatInput){0});
    assert(swat_body_feet_position(&civilian->controller.body).x<before.x-1);
    swat_sim_close(&sim);
    puts("PASS squad/escort: leader-only orders, delayed queue/execute, human replacement immune to bot orders, collision-based restrained civilian movement");
}
static void escort_open_leaf(void) {
    for(int role=0;role<2;role++)for(int side=-1;side<=1;side+=2) {
        fixture();
        sim.actors[1].gear.surrendered=true;
        SwatActor* civilian=&sim.actors[2];
        civilian->role=role?SWAT_SUSPECT:SWAT_CIVILIAN;
        civilian->gear.surrendered=civilian->gear.restrained=true;civilian->mind.escort_owner=0;
        b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){side*4,.9144f,0},b3Quat_identity);
        b3Body_SetTransform(civilian->controller.body.body,(b3Pos){-side*1.2f,.9144f,0},b3Quat_identity);
        int owner=swat_world_box(&sim.world,(b3Pos){0,1.095f,0},swat_v(.025f,1.095f,.54f),SWAT_WOOD,120);
        SwatObject* door=&sim.world.objects[owner];door->door=true;door->door_open=true;
        door->closed_yaw=-SWAT_PI*.5f;door->door_angle=SWAT_PI*.5f;door->hinge=(b3Pos){0,1.095f,-.54f};
        for(int t=0;t<1200 && side*swat_body_feet_position(&civilian->controller.body).x<1;t++)swat_sim_step(&sim,&(SwatInput){0});
        assert(side*swat_body_feet_position(&civilian->controller.body).x>1);
        assert(door->door_open && fabsf(door->door_angle-SWAT_PI*.5f)<1e-5f);
        if(role)assert(!civilian->rescued && sim.debrief.rescued==0);
        swat_sim_close(&sim);
    }
    puts("PASS escort: civilian and cuffed suspect detour around an open leaf from both sides, without closing it or crediting suspect rescue");
}
static void perception_evidence_roe(void) {
    fixture(); sim.actors[2].gear.surrendered=true;
    int wall=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.1f,1.5f,12.5f),SWAT_CONCRETE,0);
    SwatInput inputs[SWAT_MAX_ACTORS]; swat_sim_bot_inputs(&sim,inputs); assert(sim.actors[1].mind.target==-1);
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){0,.9f,4},b3Quat_identity);
    swat_sim_bot_inputs(&sim,inputs); assert(sim.actors[1].mind.target==-1 && sim.actors[1].mind.memory_ticks==0);
    (void)wall;
    sim.actors[1].gear.surrendered=true; swat_encounter_step(&sim); assert(sim.evidence[1].dropped);
    swat_sim_damage_actor(&sim,1,0,10); assert(sim.debrief.roe_violations==1 && sim.debrief.unlawful_damage==10);
    sim.evidence[1].position=(b3Pos){.5f,.1f,0};
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){0,.9f,0},b3Quat_identity);
    sim.actors[0].controller.pitch=atan2f(.1f-swat_controller_eye(&sim.actors[0].controller).y,.5f);
    assert(swat_collect_evidence(&sim,0) && !swat_collect_evidence(&sim,0)); assert(sim.debrief.evidence==1);
    static SwatMap map; static SwatSnapshot state,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map); swat_capture_snapshot(&sim,1,&state);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&state); assert(size && swat_decode_snapshot(&decoded,bytes,size));
    assert(swat_apply_snapshot(&replica,&decoded));
    assert(replica.debrief.roe_violations==1 && replica.evidence[1].collected && replica.actors[1].mind.state==sim.actors[1].mind.state);
    for(size_t n=0;n<size;n++) assert(!swat_decode_snapshot(&decoded,bytes,n));
    swat_sim_close(&replica); swat_sim_close(&sim);
    puts("PASS encounter: no hidden-position pursuit, weapon evidence exactly once, unlawful force record, public state/debrief replication and truncation rejection");
}
static void place_actor(int id,b3Pos feet) {
    SwatController* c=&sim.actors[id].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
}
static void light_perception(void) {
    fixture();place_actor(1,(b3Pos){18,0,0});place_actor(2,(b3Pos){0,0,6});
    sim.actors[0].mind.bot=true;sim.actors[0].mind.order=SWAT_ORDER_HOLD;
    sim.world.rooms[0]=(SwatRoom){{8,1.5f,0},{20,1.5f,10},SWAT_DRYWALL,SWAT_CONCRETE};sim.world.room_count=1;
    int roof=swat_world_box(&sim.world,(b3Pos){8,3.3f,0},swat_v(20,.2f,10),SWAT_CONCRETE,200);
    int lamp=swat_world_box(&sim.world,(b3Pos){18,2.9f,0},swat_v(.2f,.05f,.2f),SWAT_STEEL,0);
    sim.world.objects[lamp].part=SWAT_PART_LIGHT;
    SwatRoomLight light=swat_world_room_light(&sim.world,0);
    assert(light.power==1 && fabsf(light.origin.y-2.84f)<1e-5f && !b3Length(light.direction));
    assert(!swat_world_room_light(&sim.world,-1).power && !swat_world_room_light(&sim.world,1).power);
    SwatInput inputs[SWAT_MAX_ACTORS];float obs[SWAT_OBS_SIZE];
    int center=SWAT_PROPRIO_SIZE+((SWAT_SENSOR_ROWS/2)*SWAT_SENSOR_COLS+SWAT_SENSOR_COLS/2)*SWAT_SENSOR_CHANNELS;
    assert(swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    swat_sim_bot_inputs(&sim,inputs);assert(sim.actors[0].mind.target==1);
    swat_sim_observe(&sim,0,obs);assert(obs[center+1]==.8f && obs[center+2]==1);
    sim.world.room_light_off_mask=1;
    assert(!swat_world_room_light(&sim.world,0).power && !swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    swat_sim_bot_inputs(&sim,inputs);assert(sim.actors[0].mind.target==-1);
    swat_sim_observe(&sim,0,obs);assert(obs[center]==1 && obs[center+1]==0 && obs[center+2]==0);
    // The same switches and source produce the same perception in a late replica.
    static SwatMap map;static SwatSnapshot snapshot;
    swat_capture_map(&sim,4,&map);swat_apply_map(&replica,&map);swat_capture_snapshot(&sim,4,&snapshot);
    assert(swat_apply_snapshot(&replica,&snapshot) && !swat_sim_actor_visible(&replica,0,1,24,60*SWAT_RAD));
    assert(swat_world_visual_range(&replica.world,swat_controller_eye(&replica.actors[1].controller),24)==12);
    swat_sim_close(&replica);
    // Darkness never hides a nearby target or suppresses physical hearing.
    place_actor(1,(b3Pos){8,0,0});assert(swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    place_actor(1,(b3Pos){18,0,0});sim.actors[1].mind.memory_ticks=0;
    swat_sound_emit(&sim.sounds,sim.tick,0,SWAT_SOUND_SHOT,swat_controller_eye(&sim.actors[0].controller),1,32);
    sim.tick+=10; // Allow the existing finite-speed acoustic path to arrive.
    swat_sim_bot_inputs(&sim,inputs);assert(sim.actors[1].mind.target==-1 && sim.actors[1].mind.memory_ticks>0);
    // Both ordinary non-tactical NPCs and tactical NPCs use the same boundary.
    sim.config.tactical_rules=false;sim.config.hostile_fire=true;
    swat_sim_bot_inputs(&sim,inputs);assert(sim.actors[1].target_actor==-1 && !inputs[1].fire);
    sim.config.tactical_rules=true;sim.config.hostile_fire=false;
    sim.world.room_light_off_mask=0;
    int shade=swat_world_box(&sim.world,(b3Pos){18,2.2f,0},swat_v(.4f,.05f,.4f),SWAT_WOOD,20);
    assert(!swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    assert(swat_world_damage(&sim.world,shade,100) && swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    int pane=swat_world_box(&sim.world,(b3Pos){18,2.2f,0},swat_v(.4f,.05f,.4f),SWAT_GLASS,20);
    assert(swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));(void)pane;
    sim.world.room_light_off_mask=1;
    assert(swat_world_damage(&sim.world,roof,1000) && swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    int wall=swat_world_box(&sim.world,(b3Pos){9,1.5f,0},swat_v(.1f,1.5f,3),SWAT_CONCRETE,0);
    assert(!swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));(void)wall;
    place_actor(1,(b3Pos){0,0,8});assert(!swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
    assert(swat_world_visual_range(&sim.world,(b3Pos){-25,1,0},24)==24);
    swat_sim_close(&sim);
    puts("PASS light perception: shared source/switches, occlusion, glass transmission, roof loss, close detection, hearing, tactical/ordinary NPCs, policy visibility and late replica");
}
static void scenario_completion(void) {
    SwatConfig config=swat_default_config();assert(!config.tactical_rules && !config.squad_bots);
    for(int mission=0;mission<SWAT_MISSION_COUNT;mission++) {
        config.mission=mission;swat_config_human(&config);
        bool tactical=mission!=SWAT_ANNEX && mission!=SWAT_RANGE;
        assert(config.tactical_rules==tactical && config.squad_bots==(tactical?3:0));
    }
    config.mission=SWAT_MOTEL;swat_config_human(&config);config.hostile_fire=false;
    swat_sim_init(&sim,config,42);
    for(int slot=1;slot<SWAT_MAX_PLAYERS;slot++)assert(sim.actors[swat_player_actor(slot)].mind.bot);
    SwatScenarioProgress p=swat_sim_progress(&sim);
    assert(p.threats==2 && p.unsecured==3 && p.evacuees==3 && !p.secured);
    swat_sim_close(&sim);

    // Outcome rules are isolated from pathfinding here; controller-driven
    // escort/navigation tests exercise the physical trip separately.
    fixture();sim.config.mission=SWAT_HOUSE;
    sim.actors[1].gear.surrendered=sim.actors[1].gear.restrained=true;
    sim.actors[2].gear.surrendered=sim.actors[2].gear.restrained=true;
    swat_sim_damage_actor(&sim,2,0,10);
    sim.totals.civilian_damage=10; // Historical accepted injury, before the next tick clears events.
    swat_sim_step(&sim,&(SwatInput){0});
    assert(sim.end==SWAT_RUNNING && sim.actors[2].alive && sim.debrief.roe_violations==1);
    p=swat_sim_progress(&sim);assert(!p.threats && !p.unsecured && p.evacuees==1 && p.evidence==1);
    place_actor(0,sim.extraction);swat_sim_step(&sim,&(SwatInput){0});assert(sim.end==SWAT_RUNNING);
    assert(swat_sim_set_player(&sim,1,true));sim.actors[3].mind.bot=true;sim.actors[3].mind.order=SWAT_ORDER_HOLD;
    place_actor(3,b3OffsetPos(sim.extraction,swat_v(4,0,0)));
    sim.evidence[1].collected=true;sim.debrief.evidence=1;
    swat_sim_step(&sim,&(SwatInput){0});assert(sim.end==SWAT_RUNNING);
    sim.actors[2].mind.escort_owner=0;
    place_actor(2,b3OffsetPos(sim.extraction,swat_v(0,0,2)));
    swat_sim_step(&sim,&(SwatInput){0});
    assert(sim.actors[2].rescued && sim.actors[2].mind.escort_owner==-1 && sim.end==SWAT_RUNNING);
    p=swat_sim_progress(&sim);assert(p.secured && p.officers_away==1);
    place_actor(3,b3OffsetPos(sim.extraction,swat_v(1.8f,0,0)));
    swat_sim_step(&sim,&(SwatInput){0});assert(sim.end==SWAT_SUCCESS);
    assert(sim.debrief.roe_violations==1 && sim.debrief.unlawful_damage==10 && sim.debrief.rescued==1);
    static SwatMap map;static SwatSnapshot state,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,7,&map);swat_apply_map(&replica,&map);swat_capture_snapshot(&sim,7,&state);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);assert(n && swat_decode_snapshot(&decoded,bytes,n));
    assert(swat_apply_snapshot(&replica,&decoded));
    p=swat_sim_progress(&replica);assert(p.secured && !p.officers_away && replica.end==SWAT_SUCCESS && replica.debrief.roe_violations==1);
    swat_sim_close(&replica);swat_sim_close(&sim);

    fixture();sim.config.mission=SWAT_HOUSE;
    sim.actors[2].gear.restrained=true;place_actor(2,sim.extraction);
    swat_sim_damage_actor(&sim,2,0,1000);sim.totals.civilian_damage=100;
    swat_sim_step(&sim,&(SwatInput){0});p=swat_sim_progress(&sim);
    assert(sim.end==SWAT_RUNNING && p.civilian_casualties==1 && !p.evacuees && !sim.actors[2].rescued);
    swat_sim_damage_actor(&sim,0,1,1000);swat_sim_step(&sim,&(SwatInput){0});
    assert(sim.end==SWAT_OFFICER_DOWN && swat_sim_progress(&sim).officer_casualties==1);
    swat_sim_close(&sim);
    puts("PASS tactical completion: shared human defaults, motel squad, persistent civilian harm, evidence/evacuation/regroup gates, formation-sized staging, casualties and exact replicated debrief");
}
static void rotated_entry_planning(void) {
    for(int orientation=0;orientation<4;orientation++)for(int mirror=-1;mirror<=1;mirror+=2) {
        fixture();place_actor(1,(b3Pos){10,0,10});place_actor(2,(b3Pos){10,0,8});
        float yaw=orientation*SWAT_PI*.5f;b3Vec3 normal=swat_v(cosf(yaw),0,-sinf(yaw)),side=swat_v(sinf(yaw),0,cosf(yaw));
        place_actor(0,b3OffsetPos((b3Pos){0},swat_mul(normal,-3)));
        sim.actors[0].controller.yaw=atan2f(normal.z,normal.x);sim.actors[0].controller.pitch=0;
        int id=swat_world_box(&sim.world,(b3Pos){0,1.1f,0},swat_v(.04f,1.1f,.55f),SWAT_WOOD,120);
        SwatObject* door=&sim.world.objects[id];door->door=true;door->closed_yaw=yaw;
        door->hinge=b3OffsetPos(door->center,swat_mul(side,-.55f));swat_world_place(door,yaw);
        // Mirror the available interior flank and rotate the complete doorway.
        for(int wall=0;wall<2;wall++) {
            b3Pos center=b3OffsetPos((b3Pos){0,1.5f,0},swat_add(swat_mul(normal,2),swat_mul(side,mirror*(wall?3.2f:-.8f))));
            int w=swat_world_box(&sim.world,center,swat_v(2,1.5f,.1f),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[w],yaw);
        }
        for(int slot=1;slot<4;slot++) {
            assert(swat_sim_set_player(&sim,slot,true));int actor=swat_player_actor(slot);
            place_actor(actor,b3OffsetPos((b3Pos){0},swat_add(swat_mul(normal,-5),swat_mul(side,(slot-2)*1.1f))));
            sim.actors[actor].mind.bot=true;
        }
        SwatInput inputs[SWAT_MAX_ACTORS]={0};inputs[0].squad_order=SWAT_ORDER_CLEAR;inputs[0].squad_queue=true;
        swat_encounter_orders(&sim,inputs);
        for(int slot=1;slot<4;slot++) {
            SwatMind* mind=&sim.actors[swat_player_actor(slot)].mind;
            assert(mind->pending_order==SWAT_ORDER_CLEAR && mind->pending_door==id && mind->queued);
            assert(b3Dot(normal,b3SubPos(mind->pending_goal,(b3Pos){0}))>.8f);
            assert(fabsf(swat_angle(mind->pending_yaw-atan2f(normal.z,normal.x)))<1e-5f);
            for(int other=1;other<slot;other++)assert(b3Distance(mind->pending_goal,sim.actors[swat_player_actor(other)].mind.pending_goal)>=.9f);
        }
        assert(mirror*b3Dot(side,b3SubPos(sim.actors[3].mind.pending_goal,(b3Pos){0}))>1.6f);
        // Pointing into empty sky cannot turn a Move command into a trip to (0,0,0).
        for(int slot=1;slot<4;slot++)sim.actors[swat_player_actor(slot)].mind.pending_order=0;
        inputs[0].squad_order=0;swat_encounter_orders(&sim,inputs);sim.actors[0].controller.pitch=SWAT_PI*.5f;
        inputs[0].squad_order=SWAT_ORDER_MOVE;swat_encounter_orders(&sim,inputs);
        for(int slot=1;slot<4;slot++)assert(!sim.actors[swat_player_actor(slot)].mind.pending_order);
        swat_sim_close(&sim);
    }
    puts("PASS entry planning: four doorway rotations and both interior flanks, separate supported sectors, inward facing, queued ownership and empty-target rejection");
}
static void local_body_avoidance(void) {
    fixture();sim.config.mission=SWAT_HOUSE;
    sim.actors[1].gear.surrendered=true;sim.actors[2].gear.surrendered=true;
    place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){10,0,5});place_actor(2,(b3Pos){0,0,0});
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){-3,0,0},0);sim.actor_count=4;
    sim.actors[3].mind.bot=true;sim.actors[3].mind.order=SWAT_ORDER_MOVE;sim.actors[3].mind.goal=(b3Pos){3,0,0};
    int t=0;float clearance=100,side=0;
    for(;t<900;t++) {
        swat_sim_step(&sim,&(SwatInput){0});b3Pos p=swat_body_feet_position(&sim.actors[3].controller.body);
        b3Pos civilian=swat_body_feet_position(&sim.actors[2].controller.body);
        clearance=fminf(clearance,hypotf((float)(p.x-civilian.x),(float)(p.z-civilian.z)));side=fmaxf(side,fabsf((float)p.z));
        if(b3Distance(p,sim.actors[3].mind.goal)<.65f)break;
    }
    printf("Body avoidance: %d ticks, %.3f m closest centers, %.3f m sidestep\n",t,clearance,side);fflush(stdout);
    assert(t<900 && clearance>.48f && side>.55f && sim.actors[2].health==100);
    swat_sim_close(&sim);
    puts("PASS local avoidance: officer passes a visible stationary civilian using supported, collision-checked motion without pushing through or damage");
}
int main(void) { navigation(); navigation_lifecycle(); orders_and_escort(); escort_open_leaf(); perception_evidence_roe(); light_perception(); scenario_completion(); rotated_entry_planning(); local_body_avoidance(); return 0; }
