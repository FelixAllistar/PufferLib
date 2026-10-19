enum { SWAT_VISIBLE_AIM_CASES=8 };
static void task_visible_aim(int variant) {
    assert(variant>=0 && variant<SWAT_VISIBLE_AIM_CASES);task_fixture();sim.tick=270;sim.config.hostile_fire=true;
    float heading=variant==6?SWAT_PI*.5f:variant==7?.37f:0;
    place_actor(0,fire_lane_point(heading,-5,0,5));place_actor(2,fire_lane_point(heading,-9,0,8));place_actor(3,fire_lane_point(heading,-7,0,-5));place_actor(5,fire_lane_point(heading,-7,0,5));
    int observer=variant==1?1:4,target=variant==1?4:1;
    place_actor(observer,fire_lane_point(heading,0,0,0));place_actor(target,fire_lane_point(heading,8,0,0));
    SwatActor* a=&sim.actors[observer],*b=&sim.actors[target];a->controller.ads=b->controller.ads=1;
    if(variant==2){swat_body_set_crouch(&b->controller.body,true);b->controller.eye_height=b->controller.body.totalHeight-.2032f;}
    search_aim(observer,swat_controller_eye(&b->controller));search_aim(target,swat_controller_eye(&a->controller));
    a->visible_ticks=40;a->mind.target=a->target_actor=target;
    if(variant==3 || variant==4) {
        int pane=swat_world_box(&sim.world,fire_lane_point(heading,4,1.3f,0),swat_v(.05f,.18f,1),variant==3?SWAT_CONCRETE:SWAT_GLASS,variant==3?0:8);swat_world_place(&sim.world.objects[pane],-heading);
    }
    if(variant==5) {
        int support=swat_world_box(&sim.world,fire_lane_point(heading,4,.21f,0),swat_v(.4f,.21f,.4f),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[support],-heading);
        place_actor(2,fire_lane_point(heading,4,.42f,0));swat_body_set_crouch(&sim.actors[2].controller.body,true);sim.actors[2].controller.eye_height=sim.actors[2].controller.body.totalHeight-.2032f;
    }
    if(variant==7){b->arsenal.slots[0].magazine=0;b->arsenal.slots[0].reserve=0;b->target_actor=5;b->mind.target=5;}
    assert(swat_sim_actor_visible(&sim,observer,target,24,60*SWAT_RAD));
    b3Pos point=swat_controller_eye(&b->controller);
    if(variant!=3 && variant!=5)point=b3OffsetPos(swat_body_feet_position(&b->controller.body),swat_v(b->controller.body.upperOffset.x,b->controller.body.totalHeight*.65f,b->controller.body.upperOffset.z));
    b3Vec3 d=b3SubPos(point,swat_controller_eye(&a->controller));float desired=atan2f(d.y,hypotf(d.x,d.z));
    float expected=swat_clamp(desired-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
    SwatInput input[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,input);
    printf("VISIBLE_AIM case%d role%d actualPitch%.9f expected%.9f fire%d\n",variant,a->role,input[observer].pitch_delta,expected,input[observer].fire);fflush(stdout);
    assert(fabsf(input[observer].pitch_delta-expected)<1e-5f && input[observer].aim && input[observer].fire);
    if(variant==3 || variant==5)assert(fabsf(input[observer].pitch_delta)<1e-6f);else assert(input[observer].pitch_delta<-.001f);
    if(variant==5) {
        // The lower person's body blocks the torso sample, while both the
        // head sight line and the actual rifle/spread path pass above them.
        input[0]=swat_neutral_input();input[1]=swat_neutral_input();input[1].aim=true;
        input[2]=swat_neutral_input();input[2].crouch=true;
        swat_sim_step_inputs(&sim,input);
        printf("VISIBLE_AIM friendly-shot targetHealth%.3f friendlyHealth%.3f shots%d\n",b->health,sim.actors[2].health,a->arsenal.shots);
        assert(a->arsenal.shots==1 && b->health==49 && sim.actors[2].health==100 && !sim.debrief.roe_violations);
    }
    task_state(550+variant,0);swat_sim_close(&sim);
    printf("PASS visible aim%d: exposed torso preference, head-only cover/friendly fallback, current crouch/rotation/optical geometry and encoded replicas\n",variant);
}

enum { SWAT_FOLLOW_SECURITY_CASES=6 };
static void task_follow_security(int variant) {
    assert(variant>=0 && variant<SWAT_FOLLOW_SECURITY_CASES);task_fixture();sim.tick=270;
    sim.config.hostile_fire=variant!=2;sim.extraction=(b3Pos){10,0,10};
    float heading=variant==1?SWAT_PI*.5f:0;
    place_actor(0,(b3Pos){0,0,0});sim.actors[0].controller.yaw=heading;
    place_actor(1,(b3Pos){-8,0,0});sim.actors[1].controller.yaw=0;
    swat_world_box(&sim.world,(b3Pos){-4,2,0},swat_v(.1f,2,12),SWAT_CONCRETE,0);
    for(int i=3;i<6;i++) {
        place_actor(i,(b3Pos){i==3?-2:-3,0,i==4?3:-3});sim.actors[i].controller.yaw=0;
        sim.actors[i].mind.order=variant==3?SWAT_ORDER_HOLD:SWAT_ORDER_FALL_IN;
    }
    b3Pos starts[3];for(int i=3;i<6;i++)starts[i-3]=swat_body_feet_position(&sim.actors[i].controller.body);
    if(variant==4) {
        for(int i=3;i<6;i++){sim.actors[i].heard_yaw=SWAT_PI*.25f;sim.actors[i].hearing_ticks=60;}
        SwatInput input[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,input);
        for(int i=3;i<6;i++)assert(input[i].yaw_delta==3*SWAT_RAD && !input[i].fire && sim.actors[i].mind.state==SWAT_INVESTIGATE);
    } else if(variant==5) {
        SwatInput before[SWAT_MAX_ACTORS],after[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,before);
        place_actor(1,(b3Pos){-8,0,3});sim.actors[1].controller.yaw=1;sim.actors[1].arsenal.slots[0].magazine=0;sim.actors[1].target_actor=5;
        swat_sim_bot_inputs(&sim,after);
        for(int i=3;i<6;i++) {
            assert(!swat_sim_actor_visible(&sim,i,1,24,SWAT_PI));
            assert(before[i].yaw_delta==after[i].yaw_delta && before[i].pitch_delta==after[i].pitch_delta);
            assert(before[i].forward==after[i].forward && before[i].strafe==after[i].strafe && before[i].aim==after[i].aim && !before[i].fire && !after[i].fire);
        }
    }
    task_state(540+variant,0);
    for(int tick=0;tick<180;tick++) {
        swat_sim_step(&sim,&(SwatInput){0});task_state(540+variant,tick+1);
        for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    }
    if(variant==0 || variant==1 || variant==5)for(int i=3;i<6;i++) {
        float sector=i==3?SWAT_PI*.5f:i==4?SWAT_PI:-SWAT_PI*.5f;
        float error=fabsf(swat_angle(sim.actors[i].controller.yaw-heading-sector));
        printf("FOLLOW_SECURITY case%d actor%d sectorError%.6f goalDistance%.6f\n",variant,i,error,b3Distance(swat_body_feet_position(&sim.actors[i].controller.body),sim.actors[i].mind.goal));fflush(stdout);
        assert(error<.01f && b3Distance(starts[i-3],swat_body_feet_position(&sim.actors[i].controller.body))>.2f);
    }
    if(variant==3)for(int i=3;i<6;i++)assert(sim.actors[i].controller.yaw==0 && sim.actors[i].mind.order==SWAT_ORDER_HOLD);
    assert(!sim.debrief.roe_violations);swat_sim_close(&sim);
    printf("PASS follow security%d: bounded sector watch, ordinary collision-based movement, no blind fire, task/hearing priority and fresh replicas\n",variant);
}

enum { SWAT_THREAT_POSTURES=45 };
static void task_threat_memory(int variant) {
    task_fixture();sim.tick=270;sim.config.hostile_fire=true;
    place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){8,0,0});place_actor(2,(b3Pos){-9,0,8});
    place_actor(3,(b3Pos){-7,0,-5});place_actor(4,(b3Pos){0,0,0});place_actor(5,(b3Pos){-7,0,5});
    SwatActor* a=&sim.actors[4];a->controller.yaw=0;a->mind.order=SWAT_ORDER_MOVE;a->mind.goal=(b3Pos){0,0,6};
    if(variant==3) {
        swat_body_set_crouch(&sim.actors[1].controller.body,true);
        sim.actors[1].controller.eye_height=sim.actors[1].controller.body.totalHeight-.2032f;
    }
    SwatInput in[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,in);
    assert(a->mind.target==1 && a->mind.memory_ticks==300);b3Pos observed=a->mind.memory;
    swat_world_box(&sim.world,(b3Pos){4,1.5f,0},swat_v(.1f,1.5f,3),SWAT_CONCRETE,0);
    if(variant==1 || variant==2)place_actor(1,(b3Pos){8,0,variant==1?-3:3});
    a->controller.yaw=.4f;
    if(variant==4)a->mind.memory_ticks=1;
    if(variant==5){a->hearing_ticks=60;a->heard_yaw=SWAT_PI*.5f;}
    swat_sim_bot_inputs(&sim,in);
    assert(a->mind.target==-1 && a->target_actor==-1 && !in[4].fire);
    assert(b3Distance(a->mind.memory,observed)==0);
    if(variant==4)assert(!in[4].aim && in[4].yaw_delta>0 && a->mind.state==SWAT_IDLE);
    else if(variant==5)assert(in[4].yaw_delta>0 && a->mind.state==SWAT_INVESTIGATE);
    else {
        b3Vec3 last=b3SubPos(observed,swat_controller_eye(&a->controller));
        assert(in[4].aim && in[4].yaw_delta==-3*SWAT_RAD && a->mind.state==SWAT_INVESTIGATE);
        assert(in[4].pitch_delta==swat_clamp(b3Atan2(last.y,hypotf(last.x,last.z))-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD));
        assert(fabsf(in[4].forward)+fabsf(in[4].strafe)>.1f);
    }
    assert(a->mind.memory_ticks==(variant==4?0:299));
    // Actual execution must also reject a shot buffered before sight was lost.
    a->arsenal.fire_buffer_ticks=4;a->arsenal.slots[a->arsenal.active].mode=SWAT_SEMI;
    swat_sim_step_inputs(&sim,in);assert(!a->arsenal.shots && !a->arsenal.fire_buffer_ticks);
    task_state(455+variant,0);swat_sim_close(&sim);
    printf("PASS observed threat memory%d: bounded last-seen view, independent movement, no hidden tracking or blind buffered fire\n",variant);
}
static void task_threat_posture(int variant) {
    assert(variant>=0 && variant<SWAT_THREAT_POSTURES);
    if(variant>=37){task_visible_aim(variant-37);return;}
    if(variant>=31){task_follow_security(variant-31);return;}
    if(variant>=25){task_threat_memory(variant-25);return;}
    task_fixture();sim.tick=variant==15?0:270;sim.config.hostile_fire=true;
    place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){8,0,0});place_actor(2,(b3Pos){-9,0,8});place_actor(3,(b3Pos){-7,0,-5});place_actor(4,(b3Pos){0,0,0});place_actor(5,(b3Pos){-7,0,5});sim.actors[2].gear.surrendered=true;
    SwatActor* a=&sim.actors[4];SwatActor* g=&sim.actors[1];a->controller.yaw=0;a->controller.pitch=0;a->controller.ads=1;a->visible_ticks=40;a->mind.target=a->target_actor=1;g->controller.yaw=SWAT_PI;g->controller.pitch=0;g->controller.ads=1;
    bool want=true,fire=true;
    if(variant==1 || variant==15){g->controller.ads=0;want=fire=false;}
    if(variant==2){g->controller.ready_blend=-1;want=fire=false;}
    if(variant==3){g->controller.yaw=0;want=fire=false;}
    if(variant==4){g->gear.surrendered=true;want=fire=false;}
    if(variant==5){g->gear.restrained=true;want=fire=false;}
    if(variant==6 || variant==7)swat_world_box(&sim.world,(b3Pos){4,1.5f,0},swat_v(.02f,1.5f,2),variant==6?SWAT_CONCRETE:SWAT_GLASS,variant==6?0:8);
    if(variant==6)want=fire=false;
    if(variant==8){place_actor(4,(b3Pos){0,0,3});a->controller.yaw=b3Atan2(-3,8);place_actor(2,(b3Pos){0,0,0});swat_world_box(&sim.world,(b3Pos){4,1.5f,0},swat_v(.15f,1.5f,.5f),SWAT_CONCRETE,0);want=fire=false;}
    if((variant>=9 && variant<=11) || variant==17){place_actor(4,(b3Pos){0,0,1.5f});a->controller.yaw=b3Atan2(-1.5f,8);if(variant==9 || variant==17)place_actor(3,(b3Pos){2,0,0});else place_actor(2,(b3Pos){2,0,0});}
    if(variant==11){swat_world_box(&sim.world,(b3Pos){1,1.5f,.75f},swat_v(.15f,1.5f,.3f),SWAT_CONCRETE,0);want=fire=false;}
    if(variant==12){g->arsenal.slots[0].chambered=false;g->arsenal.slots[0].magazine=g->arsenal.slots[0].reserve=0;}
    if(variant==13){g->target_actor=5;g->mind.target=5;}
    if(variant==14){a->controller.yaw=SWAT_PI;want=fire=false;}
    if(variant==16){g->role=SWAT_CIVILIAN;want=fire=false;}
    if(variant==17){sim.actors[3].alive=false;sim.actors[3].health=0;b3Body_Disable(sim.actors[3].controller.body.body);want=fire=false;}
    if(variant==18){place_actor(3,(b3Pos){4,0,0});swat_body_set_crouch(&sim.actors[3].controller.body,true);a->controller.pitch=b3Atan2(-.7f,4);fire=false;}
    if(variant==21){place_actor(0,(b3Pos){2,0,-1.5f});search_aim(0,swat_controller_eye(&g->controller));}
    bool observed=swat_sim_observed_weapon_threat(&sim,4,1,24);SwatInput in[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,in);
    printf("THREAT_POSTURE variant%d observed%d want%d input%d wantFire%d\n",variant,observed,want,in[4].fire,fire);fflush(stdout);
    assert(observed==want && in[4].fire==fire);
    if(variant>=19) {
        // Keep the selected attack input. The gunman's next ordinary input
        // changes pose/compliance before actual weapon execution.
        in[0]=swat_neutral_input();in[1]=swat_neutral_input();in[1].aim=true;
        if(variant==20){in[1].aim=false;in[1].ready=SWAT_LOW_READY;}
        if(variant==21)in[0].command=true;
        if(variant==22)in[1].yaw_delta=6*SWAT_RAD;
        if(variant==23 || variant==24){a->arsenal.fire_buffer_ticks=4;a->arsenal.slots[a->arsenal.active].mode=SWAT_SEMI;in[4].fire=false;if(variant==23){in[1].aim=false;in[1].ready=SWAT_LOW_READY;}else sim.config.hostile_fire=false;}
        swat_sim_step_inputs(&sim,in);
        printf("THREAT_EXEC variant%d shots%d targetHealth%.3f surrendered%d ready%.6f\n",variant,a->arsenal.shots,g->health,g->gear.surrendered,g->controller.ready_blend);
        assert(a->arsenal.shots==(variant==19?1:0));assert(g->health==(variant==19?66:100));assert(!sim.debrief.roe_violations);
        if(variant==21)assert(g->gear.surrendered);
        if(variant==23 || variant==24)assert(!a->arsenal.fire_buffer_ticks);
    }
    task_state(430+variant,0);swat_sim_close(&sim);printf("PASS observed threat posture%d: current visible weapon/body geometry, lawful input gate and execution-time recheck\n",variant);
}
