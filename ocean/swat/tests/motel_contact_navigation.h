enum { SWAT_CONTACT_NAV_CASES=2 };
static void task_contact_navigation(int variant) {
    assert(variant>=0 && variant<SWAT_CONTACT_NAV_CASES);
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_BUSY;cfg.layout_seed=1;
    cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;cfg.squad_bots=3;swat_config_human(&cfg);swat_sim_init(&sim,cfg,82);
    // Isolate the captured contact from other hands-on tasks. Existing source
    // parking stop/car shapes stay intact; normal AI and physics run afterward.
    for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present && i!=4) {
        place_actor(i,(b3Pos){-18+(i%5)*1.5f,-.065f,17+(i/5)*1.5f});
        sim.actors[i].mind.order=SWAT_ORDER_HOLD;
        if(sim.actors[i].role!=SWAT_OFFICER){sim.actors[i].gear.surrendered=sim.actors[i].gear.restrained=true;sim.actors[i].mind.escort_owner=-1;}
    }
    place_actor(4,(b3Pos){-6.663503f,.078f,4.297612f});SwatActor* a=&sim.actors[4];a->controller.yaw=-.1979f;
    a->mind.bot=true;a->mind.replan_tick=0;a->mind.order=variant?SWAT_ORDER_FALL_IN:SWAT_ORDER_MOVE;
    a->mind.goal=variant?(b3Pos){-3.278822f,-.065f,4.824311f}:(b3Pos){-4.5f,-.065f,3.2f};
    if(variant){place_actor(0,(b3Pos){-3.5f,-.065f,2.737f});sim.actors[0].controller.yaw=-1.462f;}
    printf("CONTACT_NAV start%d goal%.6f,%.6f\n",variant,(float)a->mind.goal.x,(float)a->mind.goal.z);fflush(stdout);
    task_state(520+variant,0);int tick;
    for(tick=0;tick<2400;tick++) {
        b3Pos p=swat_body_feet_position(&a->controller.body);
        bool arrived=b3Distance(p,a->mind.goal)<.6f;
        if(variant)arrived&=b3Distance(p,swat_body_feet_position(&sim.actors[0].controller.body))<3.5f;
        if(arrived)break;
        swat_sim_step(&sim,&(SwatInput){0});task_state(520+variant,tick+1);
        for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present && sim.actors[i].role==SWAT_OFFICER)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    }
    b3Pos p=swat_body_feet_position(&a->controller.body);
    printf("CONTACT_NAV finish%d tick%d pos%.9f,%.9f,%.9f goalDistance%.6f\n",variant,tick,p.x,p.y,p.z,b3Distance(p,a->mind.goal));fflush(stdout);
    assert(tick<2400 && !sim.debrief.roe_violations);task_state(520+variant,0);swat_sim_close(&sim);
    printf("PASS source-contact navigation%d: ordinary %s clears the captured bumper contact and reaches its real goal within the original 2400-tick bound, with fresh replicas\n",variant,variant?"Fall In":"Move");
}
