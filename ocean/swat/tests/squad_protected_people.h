enum { SWAT_PROTECTED_PEOPLE_CASES=8 };
static void task_protected_people(int variant) {
    assert(variant>=0 && variant<SWAT_PROTECTED_PEOPLE_CASES);
    task_fixture();sim.tick=270;sim.config.hostile_fire=true;
    place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){8,0,0});place_actor(3,(b3Pos){-7,0,-5});place_actor(4,(b3Pos){0,0,1.5f});place_actor(5,(b3Pos){-7,0,5});
    swat_sim_spawn_actor(&sim,2,SWAT_SUSPECT,(b3Pos){2,0,0},0);
    SwatActor* a=&sim.actors[4],*g=&sim.actors[1],*victim=&sim.actors[2];
    a->controller.yaw=b3Atan2(-1.5f,8);a->controller.pitch=0;a->controller.ads=1;a->visible_ticks=40;a->mind.target=a->target_actor=1;
    g->controller.yaw=SWAT_PI;g->controller.pitch=0;g->controller.ads=1;
    victim->gear.surrendered=variant!=2;
    if(variant==1)victim->gear.restrained=true;
    // Normal compliant controls crouch the victim before weapon execution.
    // Establish that actual pose now, and keep the attacker aimed at its eye.
    swat_body_set_crouch(&victim->controller.body,true);
    victim->controller.eye_height=victim->controller.body.totalHeight-.2032f;
    search_aim(1,swat_controller_eye(&victim->controller));
    if(variant==3 || variant==4)swat_world_box(&sim.world,(b3Pos){1,1.5f,.75f},swat_v(.15f,1.5f,.3f),variant==3?SWAT_CONCRETE:SWAT_GLASS,variant==3?0:8);
    if(variant==5){victim->alive=false;victim->health=0;b3Body_Disable(victim->controller.body.body);}
    bool want=variant!=2 && variant!=3 && variant!=5;
    SwatInput in[SWAT_MAX_ACTORS];bool observed=swat_sim_observed_weapon_threat(&sim,4,1,24);swat_sim_bot_inputs(&sim,in);
    printf("PROTECTED_PERSON case%d observed%d want%d fire%d victimRole%d surrendered%d restrained%d\n",variant,observed,want,in[4].fire,victim->role,victim->gear.surrendered,victim->gear.restrained);fflush(stdout);
    assert(observed==want && in[4].fire==want);
    // Preserve selection, then execute normal controller/weapon handling. The
    // attacker keeps the observed pose but has no commanded shot this tick.
    in[0]=swat_neutral_input();in[1]=swat_neutral_input();in[1].aim=true;
    in[2]=swat_neutral_input();
    if(variant==6){in[1].aim=false;in[1].ready=SWAT_LOW_READY;}
    if(variant==7){a->arsenal.fire_buffer_ticks=4;in[4].fire=false;sim.config.hostile_fire=false;}
    swat_sim_step_inputs(&sim,in);
    int shots=want && variant<6 ? 1 : 0;
    printf("PROTECTED_EXEC case%d shots%d want%d attackerHealth%.3f victimHealth%.3f\n",variant,a->arsenal.shots,shots,g->health,victim->health);
    assert(a->arsenal.shots==shots && g->health==(shots?66:100));
    assert(victim->health==(variant==5?0:100) && !sim.debrief.roe_violations);
    if(variant==7)assert(!a->arsenal.fire_buffer_ticks);
    task_state(530+variant,0);swat_sim_close(&sim);
    printf("PASS protected person%d: visible compliance establishes protection, opaque/active/dead controls and fresh execution gates hold, encoded replicas agree\n",variant);
}
