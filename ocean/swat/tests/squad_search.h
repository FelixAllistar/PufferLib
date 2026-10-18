// Commands and retrieval use ordinary simulation inputs after fixture setup.
static void search_aim(int actor,b3Pos point) {
    b3Vec3 d=b3SubPos(point,swat_controller_eye(&sim.actors[actor].controller));
    sim.actors[actor].controller.yaw=b3Atan2(d.z,d.x);
    sim.actors[actor].controller.pitch=b3Atan2(d.y,hypotf(d.x,d.z));
}
static void task_search(int variant) {
    task_fixture();int officer=variant==8?4:3;
    place_actor(1,(b3Pos){6,0,2});sim.actors[1].gear.surrendered=true;
    sim.evidence[1]=(SwatEvidence){.dropped=true,.actor=1,.position={5,.055f,0}};
    int target=1;
    if(variant==5 || variant==6 || variant==9) {
        swat_sim_spawn_actor(&sim,6,SWAT_SUSPECT,(b3Pos){4,0,2},0);sim.actor_count=7;
        if(variant==6 || variant==9) {
            sim.actors[6].gear.surrendered=true;target=6;
            sim.evidence[6]=(SwatEvidence){.dropped=true,.actor=6,.position={5,.055f,0}};
            sim.evidence[1].position.z=.08f;
            if(variant==9){sim.evidence[1].position=(b3Pos){4.85f,.055f,0};place_actor(officer,(b3Pos){3.37f,0,0});search_aim(officer,sim.evidence[6].position);}
        }
    }
    search_aim(0,sim.evidence[target].position);
    int barrier=-1;
    if(variant==1 || variant==2)barrier=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.02f,1.5f,12.5f),variant==1?SWAT_GLASS:SWAT_CONCRETE,100);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_SEARCH;in.squad_team=variant==8?2:0;in.squad_queue=variant==3;swat_sim_step(&sim,&in);
    if(variant==2) {
        for(int i=3;i<6;i++)assert(!sim.actors[i].mind.pending_order && sim.actors[i].mind.order==SWAT_ORDER_HOLD);
        assert(!sim.evidence[1].collected);swat_sim_close(&sim);
        puts("PASS squad search: opaque cover prevents selecting hidden evidence");return;
    }
    assert(sim.actors[officer].mind.pending_order==SWAT_ORDER_SEARCH && sim.actors[officer].mind.pending_actor==target+1);
    for(int i=3;i<6;i++)if(i!=officer)assert(!sim.actors[i].mind.pending_order);
    assert(swat_squad_order_accepted(&sim,0,SWAT_ORDER_SEARCH));
    assert(swat_squad_order_accepted(&sim,2,SWAT_ORDER_SEARCH)==(variant==8));
    b3Pos start=swat_body_feet_position(&sim.actors[officer].controller.body);
    int t;
    b3Pos reported=sim.evidence[target].position;
    for(t=0;t<1200 && !sim.evidence[target].collected;t++) {
        in=swat_neutral_input();
        if(variant==3 && t==150)in.squad_execute=true;
        if(variant==4 && t==25){in.squad_order=SWAT_ORDER_HOLD;in.squad_team=1;}
        swat_sim_step(&sim,&in);task_state(20+variant,t);
        if(variant==1 && t==240) {
            assert(!sim.evidence[1].collected && swat_body_feet_position(&sim.actors[officer].controller.body).x<3);
            assert(swat_world_damage(&sim.world,barrier,1000));
        }
        if(variant==3 && t==149)assert(sim.actors[officer].mind.queued && b3Distance(start,swat_body_feet_position(&sim.actors[officer].controller.body))<.05f);
        if(variant==4 && t==120) {
            assert(!sim.evidence[1].collected && sim.actors[officer].mind.order==SWAT_ORDER_HOLD);break;
        }
        if(variant==5 && t==180) {
            assert(!sim.evidence[target].collected && sim.actors[officer].mind.order==SWAT_ORDER_SEARCH);
            assert(b3Distance(start,swat_body_feet_position(&sim.actors[officer].controller.body))<.05f);
            sim.actors[6].gear.surrendered=true;
        }
        if(variant==7 && t==50) {
            barrier=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.02f,1.5f,12.5f),SWAT_CONCRETE,100);
            sim.evidence[target].position.x=8; // Adversarial hidden movement fixture.
        }
        if(variant==7 && t==240) {
            assert(!sim.evidence[target].collected && b3Distance(sim.actors[officer].mind.task_point,reported)<1e-6f);
            assert(swat_world_damage(&sim.world,barrier,1000));
        }
    }
    fprintf(stderr,"Search variant%d tick%d collected%d position %.3f %.3f order%d pitch%.3f\n",variant,t,sim.evidence[target].collected,(float)swat_body_feet_position(&sim.actors[officer].controller.body).x,(float)swat_body_feet_position(&sim.actors[officer].controller.body).z,sim.actors[officer].mind.order,sim.actors[officer].controller.pitch);
    assert(t<1200);
    for(int n=0;n<60;n++){swat_sim_step(&sim,&(SwatInput){0});task_state(20+variant,1300+n);}
    if(variant!=4)assert(sim.evidence[target].collected && sim.debrief.evidence==1 && sim.actors[officer].mind.order==SWAT_ORDER_HOLD && !sim.actors[officer].mind.order_actor);
    else assert(!sim.evidence[1].collected && !sim.debrief.evidence);
    assert(fabsf(sim.actors[officer].controller.pitch)<.05f);
    for(int i=3;i<6;i++)if(i!=officer)assert(sim.actors[i].mind.order==SWAT_ORDER_HOLD);
    if(variant==6 || variant==9)assert(!sim.evidence[1].collected);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    swat_sim_close(&sim);
    printf("PASS squad search variant%d: directed single officer, physical retrieval, queue/cover/cancellation and completion respected\n",variant);
}
