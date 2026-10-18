// Fixture setup establishes a closed door; tasks run ordinary bot inputs.
// Variant 10 also exercises the shared tool API's wrong-door rejection directly.
static void task_equipment(int variant) {
    task_fixture();sim.tick=240;
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;
    place_actor(1,(b3Pos){8,0,8});place_actor(2,(b3Pos){9,0,9});
    place_actor(0,(b3Pos){-4,0,0});place_actor(3,(b3Pos){-2,0,-1.5f});
    place_actor(4,(b3Pos){-3,0,-4});place_actor(5,(b3Pos){-3,0,4});
    int door=swat_world_box(&sim.world,(b3Pos){0,1.3f,0},swat_v(.04f,1.3f,.8f),SWAT_WOOD,120);
    SwatObject* target=&sim.world.objects[door];target->door=true;target->locked=variant!=1 && variant!=7 && variant!=8;
    target->hinge=b3OffsetPos(target->center,swat_v(0,0,-.8f));
    int order=target->locked?SWAT_ORDER_PICK:SWAT_ORDER_WEDGE;
    if(variant==5)swat_equipment_kit(&sim.actors[3].gear,2);
    if(variant==6)for(int i=3;i<6;i++)swat_equipment_kit(&sim.actors[i].gear,2);
    if(variant==7)sim.actors[3].gear.wedges=0;
    if(variant==8)for(int i=3;i<6;i++)sim.actors[i].gear.wedges=0;
    if(variant==11)swat_world_box(&sim.world,(b3Pos){-1,1.5f,0},swat_v(.04f,1.5f,5),SWAT_CONCRETE,0);
    sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=0;
    if(variant<2) {
        // Different visible contact heights still name the same door task.
        float height=variant?1.8f:.8f;
        sim.actors[0].controller.pitch=b3Atan2(height-swat_controller_eye(&sim.actors[0].controller).y,4);
        SwatHit seen=swat_context_hit(&sim,0,24);
        assert(seen.kind==SWAT_HIT_WORLD && seen.index==door && fabsf(seen.point.y-target->center.y)>.25f);
    }
    SwatInput in=swat_neutral_input();in.squad_order=order;in.squad_queue=variant==2 || variant==4 || variant==12;
    swat_sim_step(&sim,&in);
    int recipient=-1,recipients=0;
    for(int i=3;i<6;i++)if(sim.actors[i].mind.pending_order==order){recipient=i;recipients++;}
    fprintf(stderr,"Equipment variant%d order%d recipients%d selected%d door%d\n",variant,order,recipients,recipient,door);
    if(variant==6 || variant==8 || variant==11) {
        assert(!recipients);
        for(int t=0;t<240;t++){swat_sim_step(&sim,&(SwatInput){0});task_state(60+variant,t);}
        assert(target->locked==(variant!=8) && target->wedge_owner<0);
        if(variant==6) {
            // Entry has no picks: the human and NPC paths share the same rule.
            swat_equipment_kit(&sim.actors[0].gear,2);place_actor(0,(b3Pos){-1,0,0});
            in=swat_neutral_input();in.door_tool=SWAT_LOCKPICK;
            for(int t=0;t<200;t++)swat_sim_step(&sim,&in);
            assert(target->locked && !sim.actors[0].gear.door_ticks);
        }
        swat_sim_close(&sim);printf("PASS squad equipment variant%d: unavailable picks, finite stock or opaque cover rejects the task\n",variant);return;
    }
    assert(recipients==1 && recipient==((variant==5 || variant==7)?4:3));
    assert(sim.actors[recipient].mind.pending_door==door);
    assert(b3Distance(sim.actors[recipient].mind.pending_goal,target->center)<1e-6f);
    if(variant==10) {
        for(int t=0;t<20;t++)swat_sim_step(&sim,&(SwatInput){0});
        assert(sim.actors[recipient].mind.order==SWAT_ORDER_PICK && sim.actors[recipient].mind.order_door==door);
        int other=swat_world_box(&sim.world,(b3Pos){0,1.3f,4},swat_v(.04f,1.3f,.8f),SWAT_WOOD,120);
        sim.world.objects[other].door=sim.world.objects[other].locked=true;
        sim.world.objects[other].hinge=b3OffsetPos(sim.world.objects[other].center,swat_v(0,0,-.8f));
        place_actor(recipient,(b3Pos){-1,0,4});sim.actors[recipient].controller.yaw=sim.actors[recipient].controller.pitch=0;
        assert(swat_context_hit(&sim,recipient,1.7f).index==other);
        for(int n=0;n<200;n++){in=swat_neutral_input();in.door_tool=SWAT_LOCKPICK;swat_door_tools(&sim,recipient,&in);}
        assert(target->locked && sim.world.objects[other].locked && !sim.actors[recipient].gear.door_ticks);
        task_state(60+variant,0);swat_sim_close(&sim);puts("PASS squad equipment: shared physical tool entrypoint rejects another real door while assigned elsewhere");return;
    }
    if(variant==12) {
        assert(swat_sim_set_player(&sim,1,true));
        assert(!sim.actors[recipient].mind.bot && !sim.actors[recipient].mind.pending_order && !sim.actors[recipient].mind.pending_door);
        for(int t=0;t<240;t++){swat_sim_step(&sim,&(SwatInput){0});task_state(60+variant,t);}
        assert(target->locked && !sim.actors[recipient].gear.door_ticks);
        swat_sim_close(&sim);puts("PASS squad equipment: human takeover cancels a queued door task and its replicated target");return;
    }
    int interrupted=0,cover=-1;bool heard=false;
    for(int t=0;t<1200;t++) {
        in=swat_neutral_input();
        if(variant==2 && t==100)in.squad_execute=true;
        if(variant==4 && t==30) {
            cover=swat_world_box(&sim.world,(b3Pos){-1.35f,1.5f,0},swat_v(.035f,1.5f,5),SWAT_CONCRETE,100);
            in.squad_execute=true;
        }
        if(variant==4 && t==260)assert(swat_world_damage(&sim.world,cover,1000));
        if(variant==3 && !interrupted && sim.actors[recipient].gear.door_ticks>25) {
            in.squad_order=SWAT_ORDER_HOLD;in.squad_team=1;interrupted=1;
        }
        if(variant==9 && !heard && sim.actors[recipient].gear.door_ticks>25) {
            b3Pos p=swat_controller_eye(&sim.actors[recipient].controller);
            swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,b3OffsetPos(p,swat_v(-3,0,0)),1,24);heard=true;
        }
        swat_sim_step(&sim,&in);task_state(60+variant,t);
        if(variant==2 && t==80)assert(target->locked && !sim.actors[recipient].gear.door_ticks && sim.actors[recipient].mind.queued);
        if(variant==4 && t==200)assert(target->locked && !sim.actors[recipient].gear.door_ticks);
        if(variant==9 && heard && sim.actors[recipient].hearing_ticks>10)assert(!sim.actors[recipient].gear.door_ticks && target->locked);
        if(variant==3 && interrupted && !sim.actors[recipient].mind.pending_order && sim.actors[recipient].mind.order==SWAT_ORDER_HOLD) {
            for(int n=0;n<100;n++){swat_sim_step(&sim,&(SwatInput){0});task_state(60+variant,1200+n);}
            assert(target->locked && !sim.actors[recipient].gear.door_ticks);break;
        }
        bool done=order==SWAT_ORDER_PICK?!target->locked:target->wedge_owner==recipient;
        if(variant!=3 && done && sim.actors[recipient].mind.order==SWAT_ORDER_HOLD)break;
        if(t==1199) {
            b3Pos p=swat_body_feet_position(&sim.actors[recipient].controller.body);
            fprintf(stderr,"Equipment stalled %.3f %.3f yaw%.3f pitch%.3f toolticks%d target%d order%d\n",p.x,p.z,sim.actors[recipient].controller.yaw,sim.actors[recipient].controller.pitch,sim.actors[recipient].gear.door_ticks,sim.actors[recipient].gear.door_target,sim.actors[recipient].mind.order);
            assert(false);
        }
    }
    if(variant!=3) {
        assert(sim.actors[recipient].mind.order==SWAT_ORDER_HOLD && !target->door_open);
        if(order==SWAT_ORDER_PICK)assert(!target->locked && sim.actors[recipient].gear.used_tools);
        else assert(target->wedge_owner==recipient && sim.actors[recipient].gear.wedges==1);
    }
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    swat_sim_close(&sim);printf("PASS squad equipment variant%d: one capable operator, fixed door identity, physical reach, held tool and completion/interruptions\n",variant);
}
