#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static void step(SwatInput in,int ticks) { for(int i=0;i<ticks;i++) swat_sim_step(&sim,&in); }
static void place(int actor,b3Pos feet,float yaw) {
    SwatController* c=&sim.actors[actor].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0)); c->yaw=yaw; c->pitch=0;
}
static void fixture(SwatMaterial floor) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.max_ticks=10000; sim.config.hostile_fire=false;
    swat_world_init(&sim.world); sim.extraction=(b3Pos){-15,0,0}; sim.actor_count=3;
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),floor,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-5,0,0},0);
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){3,0,0},SWAT_PI);
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,(b3Pos){10,0,5},SWAT_PI);
    step(swat_neutral_input(),15);
}
static float bounce(SwatMaterial material) {
    fixture(material); assert(swat_throw(&sim,0,SWAT_FLASHBANG));
    SwatProjectile* p=&sim.projectiles[0];
    assert(fabsf(b3Body_GetMass(p->body)-.42f)<.005f);
    b3Body_SetTransform(p->body,(b3Pos){0,2,0},b3Quat_identity);
    b3Body_SetLinearVelocity(p->body,swat_v(0,0,0)); b3Body_SetAngularVelocity(p->body,swat_v(0,0,0));
    float peak=0; bool touched=false;
    for(int i=0;i<150;i++) {
        b3World_Step(sim.world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
        float y=(float)b3Body_GetPosition(p->body).y;
        if(y<.08f) touched=true;
        if(touched) peak=fmaxf(peak,y);
    }
    swat_sim_close(&sim); assert(touched); return peak;
}
static void physical_materials(void) {
    float hard=bounce(SWAT_TILE),soft=bounce(SWAT_CARPET);
    printf("Canister bounce: tile %.3f m / carpet %.3f m\n",hard,soft);
    assert(hard>soft+.25f && soft<.13f);
    fixture(SWAT_CONCRETE);
    swat_world_box(&sim.world,(b3Pos){1,2,0},swat_v(.00625f,2,2),SWAT_DRYWALL,50);
    assert(swat_throw(&sim,0,SWAT_FLASHBANG)); SwatProjectile* p=&sim.projectiles[0];
    b3Body_SetTransform(p->body,(b3Pos){0,1,0},b3Quat_identity);
    b3Body_SetLinearVelocity(p->body,swat_v(80,0,0));
    step(swat_neutral_input(),3);
    assert(p->position.x<1 && p->velocity.x<0);
    bool impact=false;
    for(int i=0;i<sim.sounds.count;i++) {
        const SwatSoundEvent* e=swat_sound_at(&sim.sounds,i);
        impact|=e->kind==SWAT_SOUND_IMPACT && e->material==SWAT_DRYWALL;
    }
    assert(impact); swat_sim_close(&sim);
    puts("PASS physical materials: mass, receiving-surface bounce, thin-wall collision and material impact event");
}
static void wand(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.hostile_fire=false;
    swat_sim_init(&sim,config,19); place(0,(b3Pos){3.3f,0,-2},0);
    SwatInput in=swat_neutral_input(); in.inspect=true; step(in,30);
    assert(sim.actors[0].gear.wand_mode==SWAT_WAND_UNDER);
    b3Pos inserted=swat_sim_inspection_camera(&sim,0);
    assert(inserted.x>4.1f && inserted.y<.075f);
    in.yaw_delta=.2f; in.pitch_delta=.08f; in.forward=1; in.fire=true;
    step(in,5); b3Pos turned=swat_sim_inspection_camera(&sim,0);
    assert(b3Distance(inserted,turned)<.01f && fabsf(sim.actors[0].controller.yaw)<.001f);
    assert(sim.actors[0].gear.wand_yaw>.9f && sim.actors[0].arsenal.shots==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); place(0,(b3Pos){.5f,0,0},0);
    swat_world_box(&sim.world,(b3Pos){1,1.3f,0},swat_v(.07f,1.3f,.35f),SWAT_CONCRETE,0);
    in=swat_neutral_input(); in.inspect=true; in.lean=-1; step(in,1);
    b3Pos corner=swat_sim_inspection_camera(&sim,0);
    assert(corner.x>1.1f && corner.z<-.8f);
    assert(swat_world_visible(&sim.world,corner,(b3Pos){2,1.7f,0}));
    swat_world_box(&sim.world,(b3Pos){.5f,1.3f,-.55f},swat_v(.3f,1.3f,.05f),SWAT_CONCRETE,0);
    b3Pos blocked=swat_sim_inspection_camera(&sim,0); assert(blocked.x<.6f && blocked.z>-.51f);
    swat_sim_close(&sim);
    puts("PASS optiwand: automatic closed-door insertion, independent lens rotation, body/fire lock and collision-limited corner reach");
}
static void throw_rules(void) {
    fixture(SWAT_CONCRETE); SwatInput in=swat_neutral_input(); in.throwable=1; in.fire=true; step(in,60);
    assert(sim.actors[0].gear.flashbangs==0 && sim.actors[0].arsenal.shots==1);
    int active=0; for(int i=0;i<SWAT_MAX_PROJECTILES;i++) active+=sim.projectiles[i].active;
    assert(active==1); assert(sim.actors[0].gear.used_tools);
    in=swat_neutral_input(); in.loadout=2; place(0,(b3Pos){0,0,0},0); step(in,1);
    assert(sim.actors[0].gear.kit==0 && sim.actors[0].gear.flashbangs==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE);
    swat_world_box(&sim.world,(b3Pos){-4.6f,1.3f,0},swat_v(.05f,1.3f,1),SWAT_CONCRETE,0);
    assert(!swat_throw(&sim,0,SWAT_FLASHBANG) && sim.actors[0].gear.flashbangs==1);
    swat_sim_close(&sim);
    puts("PASS throws: finite inventory, held-key edge, action recovery, no staging refill and hand clearance");
}
static void detonate_at(SwatProjectileKind kind,b3Pos p) {
    assert(swat_throw(&sim,0,kind));
    SwatProjectile* projectile=&sim.projectiles[0];
    b3Body_SetTransform(projectile->body,p,b3Quat_identity); b3Body_SetLinearVelocity(projectile->body,swat_v(0,0,0));
    projectile->remaining_ticks=1; projectile->age=89; step(swat_neutral_input(),1);
    assert(projectile->detonated);
}
static void effects(void) {
    fixture(SWAT_CONCRETE); int wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    detonate_at(SWAT_FLASHBANG,(b3Pos){1,.06f,0}); assert(sim.actors[1].gear.flash_ticks==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); detonate_at(SWAT_FLASHBANG,(b3Pos){1,.06f,0});
    assert(sim.actors[1].gear.flash_ticks>80 && sim.actors[1].health==100 && !sim.actors[1].gear.surrendered);
    SwatInput command=swat_neutral_input(); command.command=true; step(command,1);
    assert(sim.actors[1].gear.surrendered); swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    detonate_at(SWAT_CS_GAS,(b3Pos){1,.06f,0}); step(swat_neutral_input(),300);
    assert(sim.actors[1].gear.gas_ticks==0 && !sim.actors[1].gear.surrendered);
    assert(swat_world_damage(&sim.world,wall,100)); step(swat_neutral_input(),30);
    assert(sim.actors[1].gear.gas_ticks>30 && sim.actors[1].gear.surrendered && sim.actors[1].health==100);
    place(0,(b3Pos){0,0,0},0); step(swat_neutral_input(),35); assert(sim.actors[0].gear.gas_ticks>30);
    swat_equipment_kit(&sim.actors[0].gear,1); sim.actors[0].gear.gas_ticks=0; step(swat_neutral_input(),35);
    assert(sim.actors[0].gear.gas_ticks==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); place(0,(b3Pos){-2,0,0},0);
    wall=swat_world_box(&sim.world,(b3Pos){1,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    assert(swat_taser(&sim,0)); assert(sim.actors[1].gear.stunned_ticks==0);
    assert(sim.actors[0].gear.taser_charges==1 && !swat_taser(&sim,0));
    step(swat_neutral_input(),180); assert(swat_world_damage(&sim.world,wall,100));
    assert(swat_taser(&sim,0)); assert(sim.actors[1].gear.surrendered && sim.actors[1].health==100);
    assert(!swat_taser(&sim,0)); swat_sim_close(&sim);
    puts("PASS effects: flash occlusion/compliance, gas wall/destruction boundaries, masks, taser cover/range/recovery and zero damage");
}

static void doors(void) {
    fixture(SWAT_CONCRETE);
    int id=swat_world_box(&sim.world,(b3Pos){-3.7f,1.3f,0},swat_v(.04f,1.3f,.8f),SWAT_WOOD,120);
    SwatObject* door=&sim.world.objects[id]; door->door=door->locked=true;
    door->hinge=b3OffsetPos(door->center,swat_v(0,0,-.8f));
    SwatInput in=swat_neutral_input(); in.interact=true; step(in,1);
    assert(door->locked && !door->door_open);
    in=swat_neutral_input(); in.door_tool=SWAT_LOCKPICK; in.fire=true; in.forward=1;
    b3Pos start=swat_body_feet_position(&sim.actors[0].controller.body);
    step(in,60);
    assert(door->locked && sim.actors[0].gear.door_ticks==60 && sim.actors[0].arsenal.shots==0);
    assert(b3Distance(start,swat_body_feet_position(&sim.actors[0].controller.body))<.02f);
    step(swat_neutral_input(),1); assert(!sim.actors[0].gear.door_ticks);
    sim.actors[0].controller.yaw=SWAT_PI; step(in,1); assert(!sim.actors[0].gear.door_ticks);
    sim.actors[0].controller.yaw=0; step(in,179); assert(door->locked);
    step(in,1); assert(!door->locked && !door->door_open && sim.actors[0].gear.used_tools);
    step(in,8); assert(sim.actors[0].arsenal.shots==0);
    in=swat_neutral_input(); in.interact=true; step(in,50); assert(door->door_open && door->door_angle>1.5f);
    swat_equipment_kit(&sim.actors[0].gear,1); // Test-owned setup, not a staging request.
    in=swat_neutral_input(); in.door_tool=SWAT_PLACE_CHARGE; step(in,90);
    assert(door->breach_owner<0 && sim.actors[0].gear.breaching_charges==1);
    sim.actors[0].controller.yaw=atan2f(-.8f,1.5f);
    step(swat_neutral_input(),1); in=swat_neutral_input(); in.interact=true; step(in,50);
    assert(!door->door_open && door->door_angle<.01f);
    sim.actors[0].controller.yaw=0;
    in=swat_neutral_input(); in.door_tool=SWAT_PLACE_CHARGE; step(in,30);
    assert(door->breach_owner<0 && sim.actors[0].gear.breaching_charges==1 && sim.actors[0].gear.door_ticks==30);
    step(swat_neutral_input(),1); assert(!sim.actors[0].gear.door_ticks);
    place(0,(b3Pos){-8,0,0},0); step(in,90);
    assert(door->breach_owner<0 && sim.actors[0].gear.breaching_charges==1);
    place(0,(b3Pos){-5,0,0},0); step(in,89); assert(door->breach_owner<0);
    step(in,1); assert(door->breach_owner==0 && sim.actors[0].gear.breaching_charges==0);
    step(in,100); assert(door->active && door->breach_owner==0 && !sim.actors[0].gear.door_ticks);
    // A late-joining renderer receives the lock, mount, stock and interaction
    // state; it never needs a local collision body for a mounted charge.
    static SwatMap map; static SwatSnapshot snapshot,received; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(size && swat_decode_snapshot(&received,bytes,size) && swat_apply_snapshot(&replica,&received));
    assert(replica.world.objects[id].breach_owner==0 && replica.actors[0].gear.breaching_charges==0);
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){-10,0,-5},0); sim.actor_count=4;
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[3].door_tool=SWAT_DETONATE_CHARGE; swat_sim_step_inputs(&sim,inputs);
    assert(door->active && door->breach_owner==0); // Only the planter owns this remote.
    place(0,(b3Pos){-8,0,0},0); place(1,(b3Pos){-3,0,0},SWAT_PI);
    place(2,(b3Pos){-3,0,1.1f},SWAT_PI);
    swat_world_box(&sim.world,(b3Pos){-3.35f,1.3f,.55f},swat_v(.05f,1.3f,.4f),SWAT_CONCRETE,0);
    SwatCommand command={.epoch=1,.sequence=1,.input={.door_tool=SWAT_DETONATE_CHARGE}},decoded;
    size=swat_encode_command(bytes,sizeof(bytes),&command);
    assert(size && swat_decode_command(&decoded,bytes,size));
    step(decoded.input,1);
    assert(!door->active && door->door_open && !door->locked && door->breach_owner<0 && door->breach_ticks>0);
    assert(sim.actors[1].health<100 && sim.actors[1].gear.stunned_ticks>0 && sim.totals.hostile_damage>0);
    assert(sim.actors[0].health==100 && sim.actors[2].health==100 && !sim.actors[2].gear.stunned_ticks);
    // Rebuild the replica for the new wall, then verify the spent leaf/effect.
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map); swat_capture_snapshot(&sim,1,&snapshot);
    size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(size && swat_decode_snapshot(&received,bytes,size) && swat_apply_snapshot(&replica,&received));
    assert(!replica.world.objects[id].active && replica.world.objects[id].breach_ticks>0);
    step(decoded.input,12); assert(sim.totals.destroyed==1);
    in=swat_neutral_input(); in.loadout=3; place(0,(b3Pos){0,0,0},0); step(in,1);
    assert(sim.actors[0].gear.kit==1 && sim.actors[0].gear.breaching_charges==0);
    swat_sim_close(&sim); swat_sim_close(&replica);
    puts("PASS door tools: locked latch, held/aborted quiet pick, fire/movement isolation, closed-door charge mounting, finite stock, owner remote, material occlusion/injury, no refill and mounted/spent replication");
}
static void network(void) {
    fixture(SWAT_CONCRETE); assert(swat_throw(&sim,0,SWAT_CS_GAS)); step(swat_neutral_input(),25);
    static SwatMap map; static SwatSnapshot snapshot,received; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot); assert(size && swat_decode_snapshot(&received,bytes,size));
    assert(swat_apply_snapshot(&replica,&received));
    assert(replica.projectiles[0].active && replica.projectiles[0].kind==SWAT_CS_GAS);
    assert(b3Distance(sim.projectiles[0].position,replica.projectiles[0].position)<1e-5f);
    assert(replica.actors[0].gear.gas_grenades==0 && replica.actors[0].gear.used_tools);
    assert(B3_IS_NULL(replica.projectiles[0].body));
    for(size_t n=0;n<size;n++) assert(!swat_decode_snapshot(&received,bytes,n));
    SwatCommand in={.epoch=1,.sequence=1,.input={.taser=true,.throwable=2}},out;
    size=swat_encode_command(bytes,sizeof(bytes),&in); assert(size && swat_decode_command(&out,bytes,size));
    assert(out.input.taser && out.input.throwable==2);
    swat_sim_close(&sim); swat_sim_close(&replica);
    puts("PASS tactical protocol: flight, inventory, inputs, replica body isolation and every truncated packet rejected");
}

static void context(void) {
    fixture(SWAT_CONCRETE);
    sim.config.mission=SWAT_HOUSE;
    sim.actors[0].controller.yaw=4*SWAT_RAD;
    SwatHit direct=swat_world_ray(&sim.world,swat_controller_eye(&sim.actors[0].controller),
        swat_controller_aim(&sim.actors[0].controller),9,sim.actors[0].controller.body.body);
    assert(direct.kind!=SWAT_HIT_ACTOR);
    SwatContext focused=swat_context(&sim,0);
    assert(focused.hit.kind==SWAT_HIT_ACTOR && focused.hit.index==1 && focused.action==SWAT_CONTEXT_COMPLY);
    sim.actors[0].controller.yaw=12*SWAT_RAD;
    assert(swat_context(&sim,0).action==SWAT_CONTEXT_NONE);
    sim.actors[0].controller.yaw=0;
    int wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    assert(swat_context(&sim,0).action==SWAT_CONTEXT_NONE);
    SwatInput command=swat_neutral_input(); command.command=true;
    sim.actors[1].gear.stunned_ticks=200;
    step(command,1); assert(!sim.actors[1].gear.surrendered);
    assert(swat_world_damage(&sim.world,wall,100));
    step(swat_neutral_input(),1);
    place(2,(b3Pos){-8,0,0},0); // Visible behind the officer, outside the aimed shout.
    step(command,1);
    assert(sim.actors[1].gear.surrendered && !sim.actors[2].gear.surrendered);
    place(0,(b3Pos){1.8f,0,0},0);
    sim.actors[0].controller.pitch=atan2f(.6f-swat_controller_eye(&sim.actors[0].controller).y,1.2f);
    focused=swat_context(&sim,0);
    assert(focused.action==SWAT_CONTEXT_CUFF && focused.ready && focused.hit.index==1);
    command=swat_neutral_input(); command.interact=true; step(command,12);
    assert(sim.actors[0].gear.cuff_ticks==12);
    sim.actors[0].controller.yaw=SWAT_PI; step(command,1);
    assert(!sim.actors[0].gear.cuff_ticks && !sim.actors[1].gear.restrained);
    sim.actors[0].controller.yaw=0; step(command,72);
    assert(!sim.actors[0].gear.cuff_ticks && !sim.actors[1].gear.restrained);
    step(swat_neutral_input(),1); step(command,12);
    assert(sim.actors[0].gear.cuff_ticks==12);
    // Raw remote input must not move a held cuff to a different person, even
    // without the local frontend's target latch.
    sim.actors[2].gear.surrendered=true; swat_body_set_crouch(&sim.actors[2].controller.body,true);
    place(2,(b3Pos){1.8f,0,1.2f},0);
    sim.actors[0].controller.yaw=SWAT_PI*.5f;
    step(command,80);
    assert(!sim.actors[0].gear.cuff_ticks && !sim.actors[2].gear.restrained);
    sim.actors[0].controller.yaw=0; step(swat_neutral_input(),1); step(command,72);
    assert(sim.actors[1].gear.restrained && swat_context(&sim,0).action==SWAT_CONTEXT_SECURED);
    swat_sim_close(&sim);
    puts("PASS contextual use: narrow aim tolerance, larger misses rejected, intact-wall blocking, forward-only compliance, close cuff reach, look-away interruption and authority held-target isolation");
}
static void wedges_spray_traps(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.hostile_fire=false;
    swat_sim_init(&sim,config,71); place(0,(b3Pos){3.3f,0,-2},0);
    SwatContext c=swat_context(&sim,0); assert(c.hit.kind==SWAT_HIT_WORLD);
    SwatObject* door=&sim.world.objects[c.hit.index]; door->locked=false;
    SwatInput in=swat_neutral_input(); in.door_tool=SWAT_WEDGE; step(in,100);
    assert(door->wedge_owner==0 && sim.actors[0].gear.wedges==1);
    step(swat_neutral_input(),1); in=swat_neutral_input(); in.interact=true; step(in,40);
    assert(!door->door_open && door->door_angle==0);
    in=swat_neutral_input(); in.door_tool=SWAT_REMOVE_WEDGE; step(in,80);
    assert(door->wedge_owner==-1 && sim.actors[0].gear.wedges==2);
    step(swat_neutral_input(),1); in=swat_neutral_input(); in.interact=true; in.peek=true; step(in,30);
    assert(door->peek && fabsf(door->door_angle-12*SWAT_RAD)<.001f);
    step(swat_neutral_input(),1); in.peek=false; step(in,60);
    assert(!door->peek && door->door_angle>1.5f);
    door->door_open=false; step(swat_neutral_input(),90); door->trapped=true;
    in=swat_neutral_input(); in.door_tool=SWAT_DISARM; step(in,130); assert(door->trapped);
    in=swat_neutral_input(); in.inspect=true; step(in,1);
    assert(door->trap_known&1); in=swat_neutral_input(); in.door_tool=SWAT_DISARM; step(in,120);
    assert(!door->trapped && !door->trap_known);
    step(swat_neutral_input(),1); door->trapped=true; in=swat_neutral_input(); in.interact=true; step(in,5);
    assert(!door->trapped && sim.actors[0].gear.flash_ticks>0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); place(0,(b3Pos){1,0,0},0);
    int wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.05f,1.5f,2),SWAT_DRYWALL,50);
    in=swat_neutral_input(); in.pepper_spray=true; in.fire=true; step(in,20);
    assert(!sim.actors[1].gear.gas_ticks && sim.actors[0].gear.spray_ticks==340 && !sim.actors[0].arsenal.shots);
    assert(swat_world_damage(&sim.world,wall,100)); step(in,20);
    assert(sim.actors[1].gear.surrendered && sim.actors[0].gear.spray_ticks==320);
    step(in,400); assert(sim.actors[0].gear.spray_ticks==0 && !sim.actors[0].arsenal.shots);
    swat_sim_close(&sim);
    puts("PASS tactical tools: held-action isolation, persistent/recoverable wedge, physical peek, inspection-before-disarm, trap exposure, finite and occluded spray");
}
static void aim_at(b3Pos point) {
    SwatController* c=&sim.actors[0].controller;
    b3Vec3 d=b3SubPos(point,swat_controller_eye(c));
    c->yaw=atan2f(d.z,d.x);c->pitch=atan2f(d.y,sqrtf(d.x*d.x+d.z*d.z));
}
static void light_switches(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=5000;
    swat_sim_init(&sim,cfg,87);
    SwatInput use=swat_neutral_input();use.interact=true;
    for(int room=1;room<=4;room++) {
        b3Pos button;assert(swat_motel_lamp_switch(&sim.world,room,&button));
        place(0,(b3Pos){button.x,.06f,button.z+1},0);aim_at(button);
        SwatContext c=swat_context(&sim,0);assert(c.action==SWAT_CONTEXT_LIGHT && c.ready && c.hit.index==room);
        unsigned before=sim.world.room_light_off_mask;
        step(use,1);assert(sim.world.room_light_off_mask==(before^(1u<<room)));
        step(use,12);assert(sim.world.room_light_off_mask==(before^(1u<<room))); // Held F never flickers.
        step(swat_neutral_input(),1);aim_at(button);step(use,1);
        assert(sim.world.room_light_off_mask==before);
        step(swat_neutral_input(),1);
    }
    b3Pos button;assert(swat_motel_lamp_switch(&sim.world,1,&button));
    place(0,(b3Pos){button.x,.06f,button.z+1},0);aim_at(button);step(use,1);
    assert(sim.world.room_light_off_mask==2);
    static SwatMap map;static SwatSnapshot snapshot,received;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(n && swat_decode_snapshot(&received,bytes,n) && swat_apply_snapshot(&replica,&received));
    assert(replica.world.room_light_off_mask==2);
    received.room_light_off_mask=1u<<SWAT_MAX_ROOMS;
    assert(!swat_apply_snapshot(&replica,&received) && replica.world.room_light_off_mask==2);
    step(swat_neutral_input(),1);
    place(0,(b3Pos){button.x,.06f,button.z+2},0);aim_at(button);
    SwatContext c=swat_context(&sim,0);assert(c.action==SWAT_CONTEXT_LIGHT && !c.ready);
    step(use,1);assert(sim.world.room_light_off_mask==2);
    step(swat_neutral_input(),1);
    place(0,(b3Pos){button.x,.06f,button.z+1},0);aim_at(button);
    sim.actors[0].controller.yaw+=.2f;
    assert(swat_context(&sim,0).action!=SWAT_CONTEXT_LIGHT);
    place(0,(b3Pos){button.x,.06f,button.z-1},0);aim_at(button);
    assert(swat_context(&sim,0).action!=SWAT_CONTEXT_LIGHT); // Cannot use through partition.
    step(use,1);assert(sim.world.room_light_off_mask==2);
    step(swat_neutral_input(),1);
    place(0,(b3Pos){button.x,.06f,button.z+1},0);aim_at(button);
    b3Pos eye=swat_controller_eye(&sim.actors[0].controller);
    swat_world_box(&sim.world,b3OffsetPos(eye,swat_mul(b3SubPos(button,eye),.5f)),swat_v(.12f,.12f,.12f),SWAT_WOOD,0);
    assert(swat_context(&sim,0).action!=SWAT_CONTEXT_LIGHT); // Intervening object blocks use too.
    SwatMotelInstance mount;int owner=swat_motel_dressing(&sim.world,7,&mount);assert(owner>0);
    sim.world.objects[owner].active=false;
    assert(!swat_motel_lamp_switch(&sim.world,1,&button));
    swat_sim_close(&sim);swat_sim_close(&replica);
    swat_sim_init(&sim,cfg,87);assert(!sim.world.room_light_off_mask);swat_sim_close(&sim);
    puts("PASS light switches: all four mounts, press/release, narrow targeting, reach, real cover, destroyed support, late-join snapshot, invalid state rejection and reset");
}
int main(void) { physical_materials(); wand(); throw_rules(); effects(); doors(); network(); context(); wedges_spray_traps(); light_switches(); return 0; }
