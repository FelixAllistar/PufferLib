enum { SWAT_PARTIAL_NAV_CASES=2 };
static void test_motel_partial_navigation(int variant) {
    assert(variant>=0 && variant<SWAT_PARTIAL_NAV_CASES);
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_MIXED;cfg.layout_seed=7;
    cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);swat_sim_init(&sim,cfg,2026);
    sim.world.objects[16].locked=false;sim.world.objects[16].door_open=true;
    for(int n=0;n<120;n++)swat_world_step_doors(&sim.world);
    for(int i=1;i<sim.actor_count;i++)if(sim.actors[i].present && sim.actors[i].role!=SWAT_OFFICER) {
        sim.actors[i].gear.surrendered=sim.actors[i].gear.restrained=true;sim.actors[i].mind.escort_owner=-1;
    }
    // Outstanding Move, body positions and two phases of the spin are captured
    // from the failed blast-staging journal. Setup ends before normal AI steps.
    sim.actors[1].gear.surrendered=sim.actors[1].gear.restrained=false;
    sim.actors[1].alive=false;sim.actors[1].health=0;b3Body_Disable(sim.actors[1].controller.body.body);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-7.4905f,-.065f,2.1916f},-.767f);
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,variant?(b3Pos){-7.29499f,.015f,.59643f}:(b3Pos){-7.14525f,.015f,.52872f},-2.669f);
    swat_sim_spawn_actor(&sim,4,SWAT_OFFICER,(b3Pos){-9.4804f,-.065f,3.8981f},2.375f);
    swat_sim_spawn_actor(&sim,5,SWAT_OFFICER,(b3Pos){-8.6807f,-.065f,2.1935f},2.375f);
    place_actor(7,(b3Pos){.6956f,-.065f,7.6179f});sim.actors[7].rescued=true;
    const b3Pos goals[]={{-8.9311f,-.065f,3.5789f},{-9.889f,-.065f,3.738f},{-8.736f,-.065f,2.628f}};
    for(int i=3;i<6;i++){sim.actors[i].mind.bot=true;sim.actors[i].mind.order=SWAT_ORDER_MOVE;sim.actors[i].mind.order_yaw=2.375f;sim.actors[i].mind.goal=goals[i-3];}
    bool head=swat_world_sight_clear(&sim.world,swat_controller_eye(&sim.actors[3].controller),swat_controller_eye(&sim.actors[4].controller));
    printf("PARTIAL_NAV start%d head%d\n",variant,head);assert(head==(variant!=0));
    // Relocating a person fully behind the room wall must not change the
    // observed route. This control does not expose hidden coordinates to AI.
    b3Pos hidden=swat_body_feet_position(&sim.actors[2].controller.body),before,after;
    assert(!swat_world_sight_clear(&sim.world,swat_controller_eye(&sim.actors[3].controller),swat_controller_eye(&sim.actors[2].controller)));
    assert(swat_navigation_next_for_actor(&sim,3,goals[0],&before));place_actor(2,(b3Pos){-2.2f,.015f,-3.2f});
    assert(!swat_world_sight_clear(&sim.world,swat_controller_eye(&sim.actors[3].controller),swat_controller_eye(&sim.actors[2].controller)));
    assert(swat_navigation_next_for_actor(&sim,3,goals[0],&after));assert(b3Distance(before,after)<1e-6f);place_actor(2,hidden);
    int tick=0;
    for(;tick<2400;tick++) {
        b3Pos p=swat_body_feet_position(&sim.actors[3].controller.body);if(b3Distance(p,goals[0])<.6f)break;
        swat_sim_step(&sim,&(SwatInput){0});task_state(510+variant,tick);
        for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present && sim.actors[i].role==SWAT_OFFICER)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    }
    b3Pos p=swat_body_feet_position(&sim.actors[3].controller.body);
    printf("PARTIAL_NAV finish%d tick%d pos%.9f,%.9f,%.9f distance%.6f\n",variant,tick,p.x,p.y,p.z,b3Distance(p,goals[0]));
    assert(tick<2400 && !sim.debrief.roe_violations);swat_sim_close(&sim);
    printf("PASS partial-body navigation%d: captured Move clears the porch post with ordinary steering, original 2400-tick limit and fresh replicas\n",variant);
}
