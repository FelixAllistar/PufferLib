// Focused command and same-tick combat regressions; included by test_encounter.c.
static FILE* interruption_trace;
static void interruption_state(void) {
    static SwatMap map;
    static SwatSnapshot snapshot,decoded;
    static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    uint32_t digest=swat_replay_digest(&sim);assert(digest==swat_replay_digest(&replica));
    if(interruption_trace)assert(fprintf(interruption_trace,"%08x\n",digest)>0);
    swat_sim_close(&replica);
}
static void interruption_squad(void) {
    fixture();place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){9,0,9});place_actor(2,(b3Pos){9,0,7});
    for(int i=3;i<6;i++) {
        swat_sim_spawn_actor(&sim,i,SWAT_OFFICER,(b3Pos){-3,0,(i-4)*1.1f},0);
        sim.actors[i].mind.bot=true;sim.actors[i].mind.team=i==5?2:1;
        sim.actors[i].mind.order=SWAT_ORDER_FALL_IN;
    }
    sim.actor_count=6;
}
static void interrupt_orders(void) {
    interruption_squad();
    SwatInput inputs[SWAT_MAX_ACTORS]={0};inputs[0].squad_order=SWAT_ORDER_HOLD;inputs[0].squad_queue=true;
    swat_encounter_orders(&sim,inputs);
    for(int i=3;i<6;i++)assert(sim.actors[i].mind.queued && sim.actors[i].mind.pending_order==SWAT_ORDER_HOLD);
    // One Execute pulse before the radio delay must arm every recipient. The
    // delay still applies, but releasing the key cannot leave commands stuck.
    inputs[0]=swat_neutral_input();inputs[0].squad_execute=true;swat_encounter_orders(&sim,inputs);
    for(int i=3;i<6;i++)assert(!sim.actors[i].mind.queued && sim.actors[i].mind.pending_order==SWAT_ORDER_HOLD);
    interruption_state();
    inputs[0]=swat_neutral_input();sim.tick+=18;swat_encounter_orders(&sim,inputs);
    for(int i=3;i<6;i++)assert(sim.actors[i].mind.order==SWAT_ORDER_HOLD && !sim.actors[i].mind.pending_order);
    interruption_state();swat_sim_close(&sim);

    interruption_squad();inputs[0]=swat_neutral_input();inputs[0].squad_order=SWAT_ORDER_HOLD;inputs[0].squad_queue=true;
    swat_encounter_orders(&sim,inputs);
    sim.actors[3].mind.pending_actor=sim.actors[4].mind.pending_actor=2;
    // Human takeover and death before execution cancel only their pending work.
    assert(swat_sim_set_player(&sim,1,true));
    assert(!sim.actors[3].mind.bot && !sim.actors[3].mind.pending_order && !sim.actors[3].mind.queued);
    assert(!sim.actors[3].mind.pending_actor);
    swat_sim_damage_actor(&sim,4,1,1000);assert(!sim.actors[4].alive);
    inputs[0]=swat_neutral_input();inputs[0].squad_execute=true;sim.tick+=30;swat_encounter_orders(&sim,inputs);
    assert(sim.actors[3].mind.order==SWAT_ORDER_FALL_IN);
    assert(!sim.actors[4].mind.pending_order && !sim.actors[4].mind.queued);
    assert(!sim.actors[4].mind.pending_actor);
    assert(sim.actors[5].mind.order==SWAT_ORDER_HOLD && !sim.actors[5].mind.pending_order);
    interruption_state();swat_sim_close(&sim);
    puts("PASS interrupted orders: one early Execute pulse survives the delay; human/dead recipients cancel pending work while the remaining bot executes");
}
static void fire_fixture(bool bot) {
    fixture();sim.config.mission=SWAT_HOUSE;sim.config.hostile_fire=true;
    place_actor(0,(b3Pos){2,0,-2});place_actor(1,(b3Pos){6,0,0});place_actor(2,(b3Pos){0,0,8});
    sim.actors[1].mind.resolve=0;
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){0,0,0},0);sim.actor_count=4;
    SwatActor* officer=&sim.actors[3];officer->mind.bot=bot;officer->mind.order=SWAT_ORDER_HOLD;
    officer->mind.target=officer->target_actor=1;officer->visible_ticks=40;
    officer->arsenal.slots[0].cooldown=officer->arsenal.equip_remaining=0;
    officer->arsenal.last_fire=false;officer->controller.ads=1;
    // The scripted bot has perceived a recent armed threat before this tick.
    sim.actors[1].last_shot_tick=sim.tick;sim.tick=36;
    b3Vec3 d=b3SubPos(swat_controller_eye(&sim.actors[1].controller),swat_controller_eye(&sim.actors[0].controller));
    sim.actors[0].controller.yaw=b3Atan2(d.z,d.x);sim.actors[0].controller.pitch=b3Atan2(d.y,hypotf(d.x,d.z));
}
static void interrupt_fire(void) {
    for(int buffered=0;buffered<2;buffered++) {
        fire_fixture(true);SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);
        assert(inputs[3].fire && !sim.actors[1].gear.surrendered);
        if(buffered){inputs[3].fire=false;sim.actors[3].arsenal.fire_buffer_ticks=4;}
        inputs[0].command=true;
        float health=sim.actors[1].health;swat_sim_step_inputs(&sim,inputs);
        assert(sim.actors[1].gear.surrendered && sim.actors[1].health==health);
        assert(!sim.actors[3].arsenal.shots && !sim.actors[3].arsenal.fire_buffer_ticks && !sim.debrief.roe_violations);
        interruption_state();swat_sim_close(&sim);
    }
    // A continuing threat still receives the ordinary scripted shot.
    fire_fixture(true);SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);
    assert(inputs[3].fire);inputs[0]=swat_neutral_input();swat_sim_step_inputs(&sim,inputs);
    assert(sim.actors[3].arsenal.shots==1 && !sim.debrief.roe_violations);
    interruption_state();swat_sim_close(&sim);
    // Human inputs retain agency and the existing truthful ROE consequences.
    fire_fixture(false);for(int i=0;i<SWAT_MAX_ACTORS;i++)inputs[i]=swat_neutral_input();
    sim.actors[3].controller.pitch=-b3Atan2(.7f,6);
    inputs[0].command=true;inputs[3].fire=true;inputs[3].aim=true;swat_sim_step_inputs(&sim,inputs);
    assert(sim.actors[1].gear.surrendered && sim.actors[3].arsenal.shots==1 && sim.debrief.roe_violations>0);
    interruption_state();swat_sim_close(&sim);
    puts("PASS same-tick surrender: scripted direct/buffered fire cancels after compliance, continuing threats remain actionable, human fire retains normal ROE consequences and replicas agree");
}
static void interrupt_queue(void) {
    interruption_squad();sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;
    place_actor(3,(b3Pos){-2,0,4});place_actor(4,(b3Pos){-3,0,0});place_actor(5,(b3Pos){-3,0,-2});
    int owner=swat_world_box(&sim.world,(b3Pos){0,1.095f,-.54f},swat_v(.025f,1.095f,.54f),SWAT_WOOD,120);
    SwatObject* door=&sim.world.objects[owner];door->door=true;door->door_open=true;door->door_angle=SWAT_PI*.5f;
    door->hinge=(b3Pos){0,1.095f,-.54f};door->closed_yaw=0;door->yaw=door->door_angle;
    door->center=swat_door_center(door,door->yaw);swat_world_place(door,door->yaw);
    SwatMind* waiting=&sim.actors[3].mind;waiting->order=SWAT_ORDER_HOLD;
    waiting->pending_order=SWAT_ORDER_CLEAR;waiting->pending_door=owner;waiting->queued=true;waiting->command_tick=sim.tick;
    waiting->pending_goal=(b3Pos){2,0,4};
    for(int i=4;i<6;i++){sim.actors[i].mind.order=SWAT_ORDER_CLEAR;sim.actors[i].mind.order_door=owner;sim.actors[i].mind.goal=(b3Pos){3,0,(i-4)*-1.3f};}
    SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);
    assert(hypotf(inputs[4].forward,inputs[4].strafe)>.1f && !inputs[5].forward && !inputs[5].strafe);
    int ticks=0;
    for(;ticks<900 && (!sim.actors[4].mind.entry_settled || !sim.actors[5].mind.entry_settled);ticks++) {
        swat_sim_step(&sim,&(SwatInput){0});
        if(!(ticks%30))interruption_state();
    }
    assert(ticks<900 && waiting->queued && waiting->pending_order==SWAT_ORDER_CLEAR && waiting->order==SWAT_ORDER_HOLD);
    interruption_state();swat_sim_close(&sim);
    puts("PASS independent entry queues: an unexecuted queued Clear cannot block the active doorway queue; active officers still enter in order and reach separate supported sectors");
}
