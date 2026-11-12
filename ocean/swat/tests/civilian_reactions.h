// Scripted civilian fear: perceived stimuli only, deterministic reactions,
// compliance while panicked, unchanged escorts, fire lanes and no door use.
enum { SWAT_CIVILIAN_REACTION_CASES=6 };
static b3Pos civilian_feet(void) { return swat_body_feet_position(&sim.actors[2].controller.body); }
static void civilian_idle(int ticks) { for(int t=0;t<ticks;t++)swat_sim_step(&sim,&(SwatInput){0}); }
// The fixture's civilian has already seen the officer. Replace it with a
// fresh one so no earlier sight or fear is carried into the case. The caller
// finishes its own setup, then settles the bodies with civilian_idle.
static void civilian_fixture(uint32_t seed,b3Pos feet,float yaw) {
    fixture();sim.reset_seed=seed;
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,feet,yaw);
}
static void civilian_wait_panic(void) {
    for(int t=0;t<30 && sim.actors[2].mind.panic==SWAT_PANIC_NONE;t++)civilian_idle(1);
    assert(sim.actors[2].mind.panic!=SWAT_PANIC_NONE && sim.actors[2].mind.state==SWAT_FLEE);
}
static bool civilian_settled(void) {
    const SwatActor* c=&sim.actors[2];b3Vec3 d=b3SubPos(c->mind.panic_goal,civilian_feet());
    return c->mind.panic_goal_valid && hypotf(d.x,d.z)<.45f && c->controller.body.crouched;
}
// A heard shot gives only a bearing. Freeze stands, cower crouches in place,
// flight backs off the bearing without turning and then stands. Nobody moves
// toward the noise, the same seed repeats exactly and calm returns.
static void civilian_heard_gunfire(void) {
    int reactions[4]={0};
    for(uint32_t seed=1;seed<=16;seed++) {
        int panic[2];b3Pos end[2];uint32_t digest[2];
        for(int run=0;run<2;run++) {
            civilian_fixture(seed,(b3Pos){-3,0,6},SWAT_PI);civilian_idle(5);
            SwatActor* c=&sim.actors[2];b3Pos start=civilian_feet();
            assert(!swat_sim_actor_visible(&sim,2,0,24,60*SWAT_RAD) && !swat_sim_actor_visible(&sim,2,1,24,60*SWAT_RAD));
            assert(c->mind.panic==SWAT_PANIC_NONE);
            swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,(b3Pos){1,1.6f,6},1,24);
            civilian_wait_panic();
            assert(c->mind.panic_known && !c->mind.panic_seen);
            panic[run]=c->mind.panic;
            for(int t=0;t<240;t++) {
                civilian_idle(1);
                assert(civilian_feet().x<start.x+.05f);
            }
            b3Pos p=civilian_feet();float moved=hypotf((float)(p.x-start.x),(float)(p.z-start.z));
            printf("CIVILIAN_HEARD seed%u run%d panic%d moved%.3f crouched%d yaw%.4f\n",seed,run,c->mind.panic,moved,c->controller.body.crouched,c->controller.yaw);
            if(panic[run]==SWAT_PANIC_FREEZE)assert(moved<.05f && !c->controller.body.crouched);
            if(panic[run]==SWAT_PANIC_COWER)assert(moved<.1f && c->controller.body.crouched);
            if(panic[run]==SWAT_PANIC_FLEE) {
                assert(start.x-p.x>.8f && !c->controller.body.crouched);
                assert(fabsf(swat_angle(c->controller.yaw-SWAT_PI))<.01f);
            }
            int t=0;for(;t<400 && c->mind.panic!=SWAT_PANIC_NONE;t++)civilian_idle(1);
            assert(t<400 && !c->mind.panic_ticks && (c->mind.state==SWAT_IDLE || c->mind.state==SWAT_INVESTIGATE));
            end[run]=civilian_feet();digest[run]=swat_replay_digest(&sim);
            swat_sim_close(&sim);
        }
        assert(panic[0]==panic[1] && b3Distance(end[0],end[1])==0 && digest[0]==digest[1]);
        reactions[panic[0]]++;
    }
    printf("CIVILIAN_HEARD freeze%d cower%d flee%d\n",reactions[SWAT_PANIC_FREEZE],reactions[SWAT_PANIC_COWER],reactions[SWAT_PANIC_FLEE]);
    assert(reactions[SWAT_PANIC_FREEZE] && reactions[SWAT_PANIC_COWER] && reactions[SWAT_PANIC_FLEE]);
    puts("PASS civilian hearing: seeded freeze/cower/back-off from a heard bearing, never toward it, exact repeat and calm");
}
// Panic never prevents compliance (at most three orders), and a compliant civilian stays compliant.
// Without any stimulus, sighting police raises no fear and no crouch.
static void civilian_compliance(void) {
    fixture();
    assert(sim.actors[2].mind.police_tick);
    for(int t=0;t<60;t++){civilian_idle(1);assert(sim.actors[2].mind.panic==SWAT_PANIC_NONE && !sim.actors[2].controller.body.crouched);}
    swat_sim_close(&sim);
    for(uint32_t seed=1;seed<=4;seed++) {
        civilian_fixture(seed,(b3Pos){2,0,0},0);sim.actors[1].gear.surrendered=true;civilian_idle(5);
        SwatActor* c=&sim.actors[2];assert(c->mind.panic==SWAT_PANIC_NONE);
        swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,(b3Pos){2,1.6f,8},1,24);
        civilian_wait_panic();civilian_idle(30);
        search_aim(0,b3OffsetPos(swat_controller_eye(&c->controller),swat_v(0,-.2f,0)));
        SwatHit aimed=swat_context_hit(&sim,0,9);assert(aimed.kind==SWAT_HIT_ACTOR && aimed.index==2);
        // A panicked civilian may need repeated orders; the third always lands.
        for(int n=0;n<3 && !c->gear.surrendered;n++) {
            SwatInput in=swat_neutral_input();in.command=true;swat_sim_step(&sim,&in);civilian_idle(1);
        }
        assert(c->gear.surrendered);civilian_idle(30);
        assert(c->mind.state==SWAT_SURRENDER && c->mind.panic==SWAT_PANIC_NONE && c->controller.body.crouched);
        b3Pos held=civilian_feet();
        swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,(b3Pos){2,1.6f,8},1,24);civilian_idle(60);
        assert(c->mind.panic==SWAT_PANIC_NONE && b3Distance(held,civilian_feet())<.05f && c->health==100);
        swat_sim_close(&sim);
    }
    puts("PASS civilian compliance: panicked civilians obey the ordinary command and stay down; police in sight alone cause no fear");
}
// Restrained people keep their escort/detention behaviour under gunfire.
static void civilian_restrained(void) {
    for(int escorted=0;escorted<2;escorted++) {
        civilian_fixture(7,(b3Pos){-2,0,0},0);SwatActor* c=&sim.actors[2];
        c->gear.surrendered=c->gear.restrained=true;c->mind.escort_owner=escorted?0:-1;
        civilian_idle(5);
        swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,(b3Pos){-2,1.6f,6},1,24);
        for(int t=0;t<60;t++) {
            civilian_idle(1);
            assert(c->mind.panic==SWAT_PANIC_NONE && c->mind.state==(escorted?SWAT_ESCORT:SWAT_DETAINED));
        }
        swat_sim_close(&sim);
    }
    puts("PASS civilian restraint: gunfire leaves escorted and detained civilians' behaviour unchanged");
}
// The reaction roll depends only on seeds, actor and episode, so a one-tick
// probe finds a seed whose first sight of a firing suspect starts flight.
static uint32_t civilian_flee_seed(b3Pos feet,float yaw) {
    for(uint32_t seed=1;seed<128;seed++) {
        civilian_fixture(seed,feet,yaw);
        sim.actors[1].last_shot_tick=sim.tick;
        SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);
        int panic=sim.actors[2].mind.panic;bool seen=sim.actors[2].mind.panic_seen;
        swat_sim_close(&sim);
        assert(seen && panic!=SWAT_PANIC_NONE);
        if(panic==SWAT_PANIC_FLEE)return seed;
    }
    assert(false);return 0;
}
// A seen gunman (sight of recent fire) sends a fleeing civilian behind real
// cover, where the gunman's eye cannot see it, and it crouches there.
static void civilian_seen_gunman(void) {
    b3Pos start={3,0,1};uint32_t seed=civilian_flee_seed(start,0);
    civilian_fixture(seed,start,0);
    swat_world_box(&sim.world,(b3Pos){.8f,1,1.6f},swat_v(.15f,1,1.2f),SWAT_CONCRETE,0);
    civilian_idle(5);
    SwatActor* c=&sim.actors[2];const SwatActor* g=&sim.actors[1];
    assert(c->mind.panic==SWAT_PANIC_NONE && swat_sim_actor_visible(&sim,2,1,24,60*SWAT_RAD));
    float range=b3Distance(civilian_feet(),swat_body_feet_position(&g->controller.body));
    int t=0;
    for(;t<600 && !civilian_settled();t++){sim.actors[1].last_shot_tick=sim.tick;civilian_idle(1);}
    b3Pos p=civilian_feet();
    bool hidden=!swat_world_sight_clear(&sim.world,swat_controller_eye(&g->controller),swat_controller_eye(&c->controller));
    printf("CIVILIAN_SEEN seed%u ticks%d at %.3f %.3f hidden%d range %.3f -> %.3f\n",seed,t,(float)p.x,(float)p.z,hidden,range,b3Distance(p,swat_body_feet_position(&g->controller.body)));
    assert(t<600 && hidden && c->controller.body.crouched && c->mind.panic_seen && c->health==100);
    assert(b3Distance(p,swat_body_feet_position(&g->controller.body))>range+1);
    swat_sim_close(&sim);
    puts("PASS civilian sight: a visible firing suspect sends a fleeing civilian to floor that suspect cannot see");
}
// With hostile fire live, a visible suspect's active lane runs north-west
// past a fleeing civilian. The only floor that suspect cannot see lies across
// that lane. The civilian gives up that cover rather than cross the lane.
static float civilian_lane_side(b3Pos p) {
    // Signed distance from the lane centre: suspect at (6,0), aim (-.83,.55).
    return (float)((p.x-6)*.55f+p.z*.83f);
}
static void civilian_fire_lane(void) {
    b3Pos start={3,0,.5f};uint32_t seed=civilian_flee_seed(start,0);
    civilian_fixture(seed,start,0);sim.config.hostile_fire=true;
    place_actor(0,(b3Pos){-6,0,-8});
    swat_world_box(&sim.world,(b3Pos){3,1.5f,-4},swat_v(.2f,1.5f,2.5f),SWAT_CONCRETE,0); // Hides the officer from the gunman.
    swat_world_box(&sim.world,(b3Pos){2.5f,1,3.2f},swat_v(.6f,1,.15f),SWAT_CONCRETE,0); // Cover, only across the lane.
    SwatActor* c=&sim.actors[2];SwatActor* g=&sim.actors[1];
    g->mind.memory_ticks=0;g->mind.target=g->target_actor=-1;g->visible_ticks=0;
    g->controller.yaw=atan2f(.55f,-.83f);g->controller.pitch=0;
    assert(!swat_sim_actor_visible(&sim,1,0,24,60*SWAT_RAD));
    civilian_idle(5);
    assert(c->mind.panic==SWAT_PANIC_NONE && swat_sim_actor_visible(&sim,2,1,24,60*SWAT_RAD));
    float range=b3Distance(civilian_feet(),swat_body_feet_position(&g->controller.body)),side=-1e9f;
    int t=0;
    for(;t<400 && !civilian_settled();t++) {
        g->last_shot_tick=sim.tick;civilian_idle(1);
        side=fmaxf(side,civilian_lane_side(civilian_feet()));
        if(c->mind.panic_goal_valid)assert(civilian_lane_side(c->mind.panic_goal)<-.3f);
    }
    b3Pos p=civilian_feet();
    printf("CIVILIAN_LANE seed%u ticks%d end %.3f %.3f nearest side %.3f shots%d health%.1f\n",seed,t,(float)p.x,(float)p.z,side,g->arsenal.shots,c->health);
    assert(t<400 && c->mind.panic_seen && side<-.3f && c->health==100 && !g->arsenal.shots && !sim.debrief.roe_violations);
    assert(b3Distance(p,swat_body_feet_position(&g->controller.body))>range+1);
    swat_sim_close(&sim);
    puts("PASS civilian fire lane: a fleeing civilian never crosses a visible suspect's active lane, even for cover");
}
// A panicked civilian never opens a leaf or waits in its swing, even when the
// heard bearing pushes it straight at a closed door.
static void civilian_door(void) {
    for(uint32_t seed=1;seed<=12;seed++) {
        civilian_fixture(seed,(b3Pos){-3.5f,0,0},SWAT_PI);
        int owner=swat_world_box(&sim.world,(b3Pos){-5,1.095f,-.54f},swat_v(.025f,1.095f,.54f),SWAT_WOOD,120);
        SwatObject* door=&sim.world.objects[owner];door->door=true;door->door_open=false;door->door_angle=0;
        door->hinge=(b3Pos){-5,1.095f,-.54f};door->closed_yaw=0;door->yaw=0;
        door->center=swat_door_center(door,door->yaw);swat_world_place(door,door->yaw);
        civilian_idle(5);
        swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,(b3Pos){0,1.6f,0},1,24);
        civilian_wait_panic();
        for(int t=0;t<300;t++) {
            civilian_idle(1);
            assert(!door->door_open && fabsf(door->door_angle)<1e-6f && !sim.actors[2].last_interact);
        }
        b3Pos p=civilian_feet();float swing=hypotf((float)(p.x-door->hinge.x),(float)(p.z-door->hinge.z));
        printf("CIVILIAN_DOOR seed%u panic%d at %.3f %.3f hinge distance %.3f\n",seed,sim.actors[2].mind.panic,(float)p.x,(float)p.z,swing);
        assert(swing>2*door->half.z);
        swat_sim_close(&sim);
    }
    puts("PASS civilian doors: panicked civilians never use a leaf or wait in its swing");
}
static void civilian_reaction(int variant) {
    assert(variant>=0 && variant<SWAT_CIVILIAN_REACTION_CASES);
    switch(variant) {
        case 0: civilian_heard_gunfire(); break;
        case 1: civilian_compliance(); break;
        case 2: civilian_restrained(); break;
        case 3: civilian_seen_gunman(); break;
        case 4: civilian_fire_lane(); break;
        default: civilian_door(); break;
    }
}
