// A gunshot conveys a bearing, never a shooter identity or hidden position.
static void task_fire(int variant) {
    task_fixture();sim.tick=240;
    place_actor(1,(b3Pos){-6,0,0});
    if(variant==2){sim.actors[1].role=SWAT_CIVILIAN;sim.config.hostile_fire=true;sim.actors[1].last_shot_tick=sim.tick;}
    if(variant==3){place_actor(0,(b3Pos){4,0,4});sim.actors[3].mind.order=SWAT_ORDER_FALL_IN;}
    int cover=-1;
    if(variant==1 || variant==7)cover=swat_world_box(&sim.world,(b3Pos){-3,1.5f,0},swat_v(.02f,1.5f,12.5f),SWAT_WOOD,variant==7?100:0);
    if(variant==7){sim.config.hostile_fire=true;sim.actors[1].last_shot_tick=sim.tick;}
    if(variant==5) {
        place_actor(1,(b3Pos){6,0,2});sim.actors[1].gear.surrendered=true;
        sim.evidence[1]=(SwatEvidence){.dropped=true,.actor=1,.position={5,.055f,0}};
        place_actor(3,(b3Pos){3.8f,0,0});search_aim(0,sim.evidence[1].position);
        SwatInput order=swat_neutral_input();order.squad_order=SWAT_ORDER_SEARCH;order.squad_team=1;swat_sim_step(&sim,&order);
        assert(sim.actors[3].mind.pending_order==SWAT_ORDER_SEARCH && sim.actors[3].mind.pending_actor==2);
    }
    SwatActor* officer=&sim.actors[3];b3Pos start=swat_body_feet_position(&officer->controller.body);
    float before=b3Distance(start,swat_body_feet_position(&sim.actors[0].controller.body));
    b3Pos source={-6,1.6f,0};SwatSoundKind kind=variant==4?SWAT_SOUND_STEP:SWAT_SOUND_SHOT;
    swat_sound_emit(&sim.sounds,sim.tick,-1,kind,source,1,24);
    uint32_t cue_id=sim.sounds.next_id-1;
    SwatHearingMemory probe={0};SwatHeardSound cue;
    bool received=false;
    while(swat_hearing_next(&sim.world,&sim.sounds,sim.tick+6,swat_controller_eye(&officer->controller),3,&probe,&cue))if(cue.id==cue_id){received=true;break;}
    assert(received);
    assert(cue.kind==kind && cue.gain>=.02f);
    int engaged=0;bool faced=false;
    for(int t=0;t<(variant==3?480:210);t++) {
        swat_sim_step(&sim,&(SwatInput){0});task_state(40+variant,t);
        engaged+=officer->mind.state==SWAT_ENGAGE;
        faced|=fabsf(swat_angle(officer->controller.yaw-cue.bearing))<.1f;
        if(variant==1 && t==40)place_actor(1,(b3Pos){-9,0,3});
        if(variant==7 && t<=45) {
            assert(officer->target_actor==-1 && !officer->arsenal.shots && sim.actors[1].health==100);
            if(t==45)assert(swat_world_damage(&sim.world,cover,1000));
        }
        if(variant==6 && t==5)officer->gear.stunned_ticks=130;
        if(variant==1 || variant==2 || variant==4)assert(officer->target_actor==-1 && !officer->arsenal.shots);
        if(variant==5 && t==70) {
            assert(!sim.evidence[1].collected && officer->mind.order==SWAT_ORDER_SEARCH && officer->mind.order_actor==2);
            assert(b3Distance(start,swat_body_feet_position(&officer->controller.body))<.05f);
        }
    }
    fprintf(stderr,"Fire cue variant%d faced%d engaged%d yaw%.4f remaining%d follow %.3f -> %.3f goal %.3f %.3f\n",variant,faced,engaged,officer->controller.yaw,officer->hearing_ticks,before,b3Distance(swat_body_feet_position(&officer->controller.body),swat_body_feet_position(&sim.actors[0].controller.body)),officer->mind.goal.x,officer->mind.goal.z);
    if(variant==4 || variant==6)assert(!faced && fabsf(officer->controller.yaw)<(variant==4?.05f:.4f));
    else if(variant!=5)assert(faced);
    if(variant==0 || variant==3)assert(engaged>10);
    if(variant==1 || variant==2 || variant==6)assert(!officer->hearing_ticks && !engaged);
    if(variant==1 || variant==2 || variant==4 || variant==6)assert(b3Distance(start,swat_body_feet_position(&officer->controller.body))<.05f);
    if(variant==3) {
        b3Pos end=swat_body_feet_position(&officer->controller.body);
        assert(before-b3Distance(end,swat_body_feet_position(&sim.actors[0].controller.body))>2 && b3Distance(end,officer->mind.goal)<.65f);
    }
    if(variant==5)assert(sim.evidence[1].collected && sim.debrief.evidence==1 && officer->mind.order==SWAT_ORDER_HOLD);
    if(variant==7)assert(officer->arsenal.shots && sim.actors[1].health<100 && !sim.debrief.roe_violations);
    for(int i=0;i<sim.actor_count;i++) {
        if(variant!=7)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
        else if(i!=1)assert(sim.actors[i].health==100);
    }
    swat_sim_close(&sim);
    printf("PASS squad fire cue variant%d: audible bearing, bounded attention, visual confirmation, physical orders and civilian fire inhibition\n",variant);
}
