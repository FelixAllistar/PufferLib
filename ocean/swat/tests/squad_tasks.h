// Ordinary squad commands and controller motion; fixture edits only set up cases.
static FILE* task_trace;
static void task_state(int test,int tick) {
    static SwatMap map;
    static SwatSnapshot snapshot,decoded;
    static unsigned char bytes[SWAT_NET_PACKET_MAX];
    if(task_trace)assert(fprintf(task_trace,"%d %d %08x\n",test,tick,swat_replay_digest(&sim))>0);
    if(tick%30)return;
    swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));
    swat_sim_close(&replica);
}
static void task_fixture(void) {
    fixture();sim.config.mission=SWAT_HOUSE;
    place_actor(0,(b3Pos){0,0,-4});place_actor(1,(b3Pos){6,0,0});place_actor(2,(b3Pos){9,0,9});
    sim.actors[1].mind.resolve=0;
    for(int i=3;i<6;i++) {
        swat_sim_spawn_actor(&sim,i,SWAT_OFFICER,(b3Pos){i==3?0:-3,0,(i-3)*2},0);
        sim.actors[i].mind.bot=true;sim.actors[i].mind.team=i==3?1:2;
        sim.actors[i].mind.order=SWAT_ORDER_HOLD;
    }
    sim.actor_count=6;
    b3Vec3 d=b3SubPos(swat_controller_eye(&sim.actors[1].controller),swat_controller_eye(&sim.actors[0].controller));
    sim.actors[0].controller.yaw=b3Atan2(d.z,d.x);sim.actors[0].controller.pitch=b3Atan2(d.y,hypotf(d.x,d.z));
}
static void task_restrain(int variant) {
    task_fixture();
    if(variant==1)sim.actors[1].gear.surrendered=true;
    if(variant==2){sim.actors[1].role=SWAT_CIVILIAN;sim.config.hostile_fire=true;sim.actors[1].last_shot_tick=sim.tick;}
    int pane=-1;
    if(variant==3)pane=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.003f,1.5f,2),SWAT_GLASS,8);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_CUFF;in.squad_team=variant==1?0:1;swat_sim_step(&sim,&in);
    assert(sim.actors[3].mind.pending_order==SWAT_ORDER_CUFF && sim.actors[3].mind.pending_actor==2);
    assert(!sim.actors[4].mind.pending_order && !sim.actors[5].mind.pending_order);
    int tick;bool interrupted=false;
    for(tick=0;tick<1000;tick++) {
        in=swat_neutral_input();
        if(variant==4 && !interrupted && sim.actors[3].gear.cuff_ticks>12) {
            in.squad_order=SWAT_ORDER_HOLD;in.squad_team=1;interrupted=true;
        }
        swat_sim_step(&sim,&in);task_state(variant,tick);
        if(tick==180)assert(sim.actors[1].gear.surrendered);
        if(variant==3 && tick==300) {
            assert(!sim.actors[1].gear.restrained && !sim.actors[3].gear.cuff_ticks);
            assert(sim.world.objects[pane].active && swat_body_feet_position(&sim.actors[3].controller.body).x<3);
            assert(swat_world_damage(&sim.world,pane,100));
        }
        if(variant==4 && interrupted && !sim.actors[3].mind.pending_order && sim.actors[3].mind.order==SWAT_ORDER_HOLD) {
            for(int n=0;n<100;n++){swat_sim_step(&sim,&(SwatInput){0});task_state(variant,1000+n);}
            assert(!sim.actors[1].gear.restrained && !sim.actors[3].gear.cuff_ticks);break;
        }
        if(variant!=4 && sim.actors[1].gear.restrained)break;
    }
    fprintf(stderr,"Restrain variant%d tick%d surrender%d cuff%d officer %.3f %.3f order%d\n",variant,tick,
        sim.actors[1].gear.surrendered,sim.actors[1].gear.restrained,
        (float)swat_body_feet_position(&sim.actors[3].controller.body).x,(float)swat_body_feet_position(&sim.actors[3].controller.body).z,sim.actors[3].mind.order);
    assert(tick<1000 && sim.actors[1].health==100 && !sim.debrief.roe_violations);
    if(variant!=4) {
        // Finishing a cuff does not turn the next held input into an escort.
        for(int n=0;n<100;n++){swat_sim_step(&sim,&(SwatInput){0});task_state(variant,1100+n);}
        assert(sim.actors[3].mind.order==SWAT_ORDER_HOLD && !sim.actors[3].mind.order_actor);
        assert(sim.actors[1].mind.escort_owner==-1);
    }
    assert(sim.actors[4].mind.order==SWAT_ORDER_HOLD && sim.actors[5].mind.order==SWAT_ORDER_HOLD);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].arsenal.shots==0 && sim.actors[i].health==100);
    swat_sim_close(&sim);
    printf("PASS squad restraint variant%d: directed compliance, physical approach and held cuffs; cover/interruptions respected\n",variant);
}
static void task_recall(void) {
    task_fixture();place_actor(0,(b3Pos){-5,0,-3});
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;in.squad_team=1;swat_sim_step(&sim,&in);
    b3Pos goal={-6.2f,0,-4};float before=b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),goal);
    int tracking=0;
    for(int t=0;t<300;t++) {
        swat_sim_step(&sim,&(SwatInput){0});tracking+=sim.actors[3].mind.state==SWAT_ENGAGE;task_state(5,t);
    }
    float after=b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),goal);
    fprintf(stderr,"Recall distance %.3f -> %.3f tracking%d\n",before,after,tracking);
    assert(tracking>100 && before-after>2);
    in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;in.squad_team=1;swat_sim_step(&sim,&in);
    for(int t=0;t<35;t++)swat_sim_step(&sim,&(SwatInput){0});
    b3Pos stopped=swat_body_feet_position(&sim.actors[3].controller.body);
    for(int t=0;t<90;t++){swat_sim_step(&sim,&(SwatInput){0});task_state(6,t);}
    assert(b3Distance(stopped,swat_body_feet_position(&sim.actors[3].controller.body))<.05f);
    assert(sim.actors[1].health==100 && !sim.actors[3].arsenal.shots);
    swat_sim_close(&sim);
    puts("PASS squad recall: Fall In keeps moving while tracking a visible threat, Hold then stops the officer");
}
static void task_cover(void) {
    task_fixture();
    int wall=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.1f,1.5f,12),SWAT_CONCRETE,100);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_CUFF;swat_sim_step(&sim,&in);
    for(int i=3;i<6;i++)assert(!sim.actors[i].mind.pending_order && sim.actors[i].mind.order==SWAT_ORDER_HOLD);
    assert(!sim.actors[1].gear.surrendered);
    assert(swat_world_damage(&sim.world,wall,1000));
    swat_sim_step(&sim,&(SwatInput){0});swat_sim_step(&sim,&in);
    for(int t=0;t<35;t++)swat_sim_step(&sim,&(SwatInput){0});
    assert(sim.actors[3].mind.order==SWAT_ORDER_CUFF);
    b3Pos known=sim.actors[3].mind.goal;
    swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.1f,1.5f,12),SWAT_CONCRETE,0);
    place_actor(1,(b3Pos){9,0,2});
    for(int t=0;t<180;t++) {
        swat_sim_step(&sim,&(SwatInput){0});task_state(7,t);
        assert(b3Distance(known,sim.actors[3].mind.goal)<1e-6f);
        assert(!sim.actors[1].gear.surrendered && !sim.actors[1].gear.restrained);
        assert(swat_body_feet_position(&sim.actors[3].controller.body).x<3);
    }
    swat_sim_close(&sim);
    puts("PASS directed task perception: opaque aim cannot select a person; new cover prevents compliance, cuffing and hidden-position goal updates");
}
static void task_follow_wall(void) {
    task_fixture();place_actor(0,(b3Pos){-1,0,-1});sim.actors[0].controller.yaw=-SWAT_PI*.5f;
    place_actor(3,(b3Pos){-1,0,3});
    swat_world_box(&sim.world,(b3Pos){-2.2f,1.5f,0},swat_v(.1f,1.5f,12),SWAT_CONCRETE,0);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;in.squad_team=1;swat_sim_step(&sim,&in);
    for(int t=0;t<300;t++) {
        swat_sim_step(&sim,&(SwatInput){0});task_state(8,t);
        assert(swat_body_feet_position(&sim.actors[3].controller.body).x>-2);
        if(t>20)assert(sim.actors[3].mind.goal.x>-2);
    }
    assert(b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),sim.actors[3].mind.goal)<.7f);
    swat_sim_close(&sim);
    puts("PASS formation walls: a turned commander keeps the follower's goal on the connected side of solid cover");
}
static void task_regroup(int edge) {
    task_fixture();sim.extraction=(b3Pos){0,0,0};
    place_actor(0,sim.extraction);sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=0;
    if(edge==1){place_actor(0,(b3Pos){2.1f,0,0});sim.actors[0].controller.yaw=SWAT_PI;}
    place_actor(1,(b3Pos){9,0,9});sim.actors[1].gear.surrendered=true;
    for(int i=3;i<6;i++)place_actor(i,(b3Pos){-4-(i-3),0,(i-4)*1.5f});
    if(edge==2) {
        // The actual crowd from a failed ordinary-input motel extraction.
        // Reproduce it on open supported floor; no task receives a teleport.
        const b3Pos positions[]={{.100f,0,9.672f},{-.745f,0,9.838f},{1.897f,0,8.389f},
            {1.341f,0,7.045f},{.106f,0,8.479f},{-.498f,0,8.696f},{2.344f,0,9.187f},
            {-1.100f,0,7.773f},{.897f,0,9.898f}};
        sim.extraction=(b3Pos){0,0,10};sim.actors[0].controller.yaw=SWAT_PI*.5f;
        for(int i=0;i<9;i++) {
            if(i>=6)swat_sim_spawn_actor(&sim,i,i==8?SWAT_SUSPECT:SWAT_CIVILIAN,positions[i],0);
            else place_actor(i,positions[i]);
            if(i==1 || i==2 || i>=6) {
                sim.actors[i].gear.surrendered=sim.actors[i].gear.restrained=true;
                sim.actors[i].rescued=sim.actors[i].role==SWAT_CIVILIAN;
            }
        }
        sim.actor_count=9;
    }
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;swat_sim_step(&sim,&in);
    for(int t=0;t<900;t++){swat_sim_step(&sim,&(SwatInput){0});task_state(17+edge,t);}
    for(int i=3;i<6;i++) {
        float distance=b3Distance(swat_body_feet_position(&sim.actors[i].controller.body),sim.extraction);
        fprintf(stderr,"Regroup actor%d distance %.3f\n",i,distance);
        assert(distance<SWAT_STAGING_RADIUS && sim.actors[i].health==100);
    }
    assert(!swat_sim_progress(&sim).officers_away);
    swat_sim_close(&sim);
    puts("PASS squad regroup: all three recalled officers physically enter extraction, including the rear formation slot");
}
static void task_crowded_start(void) {
    task_fixture();sim.config.mission=SWAT_RANGE;
    const b3Pos positions[]={{.100f,0,9.644f},{.932f,0,9.839f},{-1.101f,0,7.764f},
        {1.906f,0,9.639f},{-.380f,0,10.737f},{-.033f,0,8.700f},{-1.798f,0,8.286f},
        {1.300f,0,7.878f},{-.722f,0,9.851f}};
    for(int i=0;i<9;i++) {
        if(i>=6)swat_sim_spawn_actor(&sim,i,i==8?SWAT_SUSPECT:SWAT_CIVILIAN,positions[i],0);
        else place_actor(i,positions[i]);
        if(i==1 || i==2 || i>=6)sim.actors[i].gear.surrendered=sim.actors[i].gear.restrained=true;
    }
    sim.actor_count=9;sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=0;
    b3Pos goal={4.8f,0,6},next;
    assert(swat_navigation_next_for_actor(&sim,0,goal,&next));
    for(int t=0;t<600;t++) {
        b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);
        if(feet.z<7)break;
        if(t%30==0)assert(swat_navigation_next_for_actor(&sim,0,goal,&next));
        b3Vec3 delta=b3SubPos(next,feet);delta.y=0;delta=swat_normalize(delta);
        SwatInput in=swat_neutral_input();in.forward=.7f*delta.x;in.strafe=.7f*delta.z;in.gait=SWAT_WALK;
        swat_sim_step(&sim,&in);task_state(20,t);
    }
    assert(swat_body_feet_position(&sim.actors[0].controller.body).z<7);
    for(int i=0;i<9;i++)assert(sim.actors[i].health==100);
    swat_sim_close(&sim);
    puts("PASS crowded route start: a physically clear oblique step escapes occupied grid neighbours using ordinary player movement");
}
static void task_yield(int mirror,bool escort) {
    task_fixture();sim.actors[1].gear.surrendered=true;
    int officer=mirror<0?4:3;
    sim.actors[3].mind.team=sim.actors[4].mind.team=2;sim.actors[officer].mind.team=1;
    place_actor(0,(b3Pos){mirror<0?1.6f:1,0,mirror*.9f});sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=0;
    place_actor(1,(b3Pos){9,0,9});place_actor(2,(b3Pos){9,0,7});place_actor(officer,(b3Pos){.2f,0,0});
    if(escort) {
        sim.actors[officer].role=SWAT_CIVILIAN;sim.actors[officer].mind.bot=false;
        sim.actors[officer].gear.surrendered=sim.actors[officer].gear.restrained=true;
        sim.actors[officer].mind.escort_owner=0;
    }
    int first=sim.world.count;swat_build_framed_wall(&sim.world,(b3Pos){0,0,0},mirror<0?SWAT_PI:0,6,2.7f,0,1.4f,0,2.19f,false);
    int owner=-1;for(int i=first;i<sim.world.count;i++)if(sim.world.objects[i].door)owner=i;
    assert(owner>=0);SwatObject* door=&sim.world.objects[owner];
    door->door_open=true;door->door_angle=SWAT_PI*.5f;door->yaw=door->closed_yaw+door->door_angle;
    door->center=swat_door_center(door,door->yaw);swat_world_place(door,door->yaw);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;in.squad_team=1;swat_sim_step(&sim,&in);
    int test=escort?13:9;
    for(int t=0;t<350;t++){swat_sim_step(&sim,&(SwatInput){0});task_state(test+(mirror>0),t);}
    b3Pos position=swat_body_feet_position(&sim.actors[officer].controller.body);
    fprintf(stderr,"Door yield escort%d mirror%d feet %.3f %.3f goal %.3f %.3f\n",escort,mirror,(float)position.x,(float)position.z,
        (float)sim.actors[officer].mind.goal.x,(float)sim.actors[officer].mind.goal.z);
    b3Pos leader=swat_body_feet_position(&sim.actors[0].controller.body),align={.7f,leader.y,0};
    SwatTraceResult passage=swat_body_trace_body(&sim.actors[0].controller.body,leader,align,1,1);
    SwatTag* obstruction=passage.hit?b3Body_GetUserData(b3Shape_GetBody(passage.shapeId)):NULL;
    assert(!obstruction || obstruction->kind!=SWAT_HIT_ACTOR || obstruction->index!=officer);
    in=swat_neutral_input();if(!escort){in.squad_order=SWAT_ORDER_HOLD;in.squad_team=1;}swat_sim_step(&sim,&in);
    for(int t=0;t<30;t++)swat_sim_step(&sim,&(SwatInput){0});
    // The commander actually passes the held officer or active escort.
    const b3Pos route[]={{.7f,0,0},{-1.8f,0,0}};
    for(int leg=0;leg<2;leg++)for(int t=0;t<300;t++) {
        b3Vec3 delta=b3SubPos(route[leg],swat_body_feet_position(&sim.actors[0].controller.body));delta.y=0;
        if(b3Length(delta)<.15f)break;
        delta=swat_normalize(delta);in=swat_neutral_input();in.forward=.7f*delta.x;in.strafe=.7f*delta.z;in.gait=SWAT_WALK;
        swat_sim_step(&sim,&in);task_state(test+2+(mirror>0),leg*300+t);
    }
    assert(swat_body_feet_position(&sim.actors[0].controller.body).x<-1.4f);
    assert(sim.actors[0].health==100 && sim.actors[officer].health==100 && door->door_open);
    swat_sim_close(&sim);
    printf("PASS doorway yield escort%d mirror%d: follower physically clears the commander's narrow portal and the commander passes through\n",escort,mirror);
}
