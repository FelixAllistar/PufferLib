// Room plans are fixture architecture. Search/cuff/retrieval use ordinary bot
// inputs. Variant 5 removes cover through authority damage; 11 withdraws a
// fixture threat after verifying that it takes precedence over the room task.
static void task_room_search(int variant) {
    task_fixture();sim.tick=240;sim.config.max_ticks=15000;
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;
    place_actor(1,(b3Pos){8,0,8});place_actor(2,(b3Pos){9,0,9});
    place_actor(0,(b3Pos){-3,0,0});place_actor(3,(b3Pos){-2,0,-1.5f});
    place_actor(4,(b3Pos){-3,0,-4});place_actor(5,(b3Pos){-3,0,4});
    sim.world.rooms[0]=(SwatRoom){{3,1.5f,0},{3,1.5f,3},SWAT_PLASTER,SWAT_CARPET};sim.world.room_count=1;
    swat_world_box(&sim.world,(b3Pos){6,1.5f,0},swat_v(.08f,1.5f,3),SWAT_CONCRETE,0);
    for(int sign=-1;sign<=1;sign+=2) {
        swat_world_box(&sim.world,(b3Pos){3,1.5f,sign*3},swat_v(3,1.5f,.08f),SWAT_CONCRETE,0);
        swat_world_box(&sim.world,(b3Pos){0,1.5f,sign*1.94f},swat_v(.08f,1.5f,1.06f),SWAT_CONCRETE,0);
    }
    int door=swat_world_box(&sim.world,(b3Pos){0,1.3f,0},swat_v(.04f,1.3f,.8f),SWAT_WOOD,120);
    sim.world.objects[door].door=true;sim.world.objects[door].hinge=(b3Pos){0,1.3f,-.8f};
    if(variant==1) {
        int desk=swat_world_box(&sim.world,(b3Pos){3,.5f,0},swat_v(1,.5f,1.2f),SWAT_WOOD,100);
        sim.world.objects[desk].part=SWAT_PART_FIXTURE;
    }
    if(variant==12) {
        int bed=swat_world_box(&sim.world,(b3Pos){3.6f,.5f,-.5f},swat_v(1.2f,.5f,2.5f),SWAT_WOOD,100);sim.world.objects[bed].part=SWAT_PART_FIXTURE;
        sim.actors[1].role=SWAT_CIVILIAN;place_actor(1,(b3Pos){3.6f,0,2.5f});
    }
    if(variant==4 || variant==5) {
        place_actor(1,(b3Pos){4.5f,0,1.7f});
        if(variant==4)place_actor(2,(b3Pos){4.5f,0,-1.7f});
    }
    int cover=-1;
    if(variant==5)cover=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.08f,1.5f,3),SWAT_DRYWALL,80);
    if(variant==8)swat_world_box(&sim.world,(b3Pos){-1,1.5f,0},swat_v(.08f,1.5f,3),SWAT_CONCRETE,0);
    sim.actors[0].controller.yaw=sim.actors[0].controller.pitch=0;
    if(variant==9) {
        place_actor(0,(b3Pos){1.3f,0,2});
        b3Vec3 d=b3SubPos((b3Pos){3,0,0},swat_controller_eye(&sim.actors[0].controller));
        sim.actors[0].controller.yaw=atan2f(d.z,d.x);sim.actors[0].controller.pitch=atan2f(d.y,hypotf(d.x,d.z));
    }
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_ROOM_SEARCH;in.squad_team=1;in.squad_queue=variant==2 || variant==7;
    swat_sim_step(&sim,&in);
    if(variant==8) {
        assert(!sim.actors[3].mind.pending_order);
        swat_sim_close(&sim);puts("PASS room search: opaque exterior cover cannot select the room behind it");return;
    }
    assert(sim.actors[3].mind.pending_order==SWAT_ORDER_ROOM_SEARCH && sim.actors[3].mind.pending_room==1);
    assert(!sim.actors[4].mind.pending_order && !sim.actors[5].mind.pending_order);
    assert(swat_squad_order_accepted(&sim,1,SWAT_ORDER_ROOM_SEARCH));
    if(variant==7) {
        assert(swat_sim_set_player(&sim,1,true));
        assert(!sim.actors[3].mind.bot && !sim.actors[3].mind.pending_order && !sim.actors[3].mind.pending_room);
        swat_sim_close(&sim);puts("PASS room search: joining human cancels the queued room identity");return;
    }
    SwatMind* mind=&sim.actors[3].mind;
    b3Pos last=swat_body_feet_position(&sim.actors[3].controller.body);float min_x=100,max_x=-100,min_z=100,max_z=-100;
    unsigned int looks=0;bool interrupted=false,heard=false,threat=false,escorted=false;int frozen_step=0,frozen_scan=0,heard_ticks=0,threat_ticks=0;
    int t;
    for(t=0;t<10000;t++) {
        in=swat_neutral_input();
        if(variant==2 && t==100)in.squad_execute=true;
        if(variant==3 && !interrupted && mind->room_scan>12) {in.squad_order=SWAT_ORDER_HOLD;in.squad_team=1;interrupted=true;}
        if(variant==6 && !heard && mind->room_scan>12) {
            b3Pos p=swat_controller_eye(&sim.actors[3].controller);swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,b3OffsetPos(p,swat_v(.5f,0,0)),1,24);
            frozen_step=mind->room_step;frozen_scan=mind->room_scan;heard=true;
        }
        if(variant==10 && mind->room_scan>12) {
            assert(swat_sim_set_player(&sim,1,true));
            assert(!mind->bot && mind->order==SWAT_ORDER_HOLD && !mind->order_room && !mind->room_step && !mind->room_task);
            swat_sim_close(&sim);puts("PASS room search: active human takeover stops automation and clears room progress");return;
        }
        if(variant==11 && !threat && mind->room_scan>12) {
            // A visible active threat interrupts the achieved search view.
            b3Pos p=swat_controller_eye(&sim.actors[3].controller);b3Vec3 n=swat_controller_aim(&sim.actors[3].controller);
            b3Pos point=b3OffsetPos(p,swat_mul(n,2));point.y=0;place_actor(1,point);sim.actors[1].gear.surrendered=false;sim.actors[1].mind.resolve=1;
            threat=true;frozen_step=mind->room_step;frozen_scan=mind->room_scan;
        }
        // Wait for actual threat tracking, independent of doorway travel time.
        if(variant==11 && threat_ticks>=90)sim.actors[1].gear.surrendered=true;
        swat_sim_step(&sim,&in);task_state(80+variant,t);
        b3Pos p=swat_body_feet_position(&sim.actors[3].controller.body);
        assert(hypotf(p.x-last.x,p.z-last.z)<.18f);last=p;
        if(swat_world_room(&sim.world,b3OffsetPos(p,swat_v(0,.9f,0)))==0) {
            min_x=fminf(min_x,p.x);max_x=fmaxf(max_x,p.x);min_z=fminf(min_z,p.z);max_z=fmaxf(max_z,p.z);
            int sector=(int)floorf((sim.actors[3].controller.yaw+SWAT_PI)/(SWAT_PI*.25f));looks|=1u<<(sector&7);
        }
        if(variant==12 && sim.actors[1].mind.escort_owner==3)escorted=true;
        if(variant==2 && t==90)assert(mind->queued && !mind->order_room && !mind->room_step);
        if(variant==3 && interrupted && mind->order==SWAT_ORDER_HOLD && !mind->pending_order) {
            assert(!mind->order_room && !mind->room_step);swat_sim_close(&sim);puts("PASS room search: Hold cancels the route without claiming completion");return;
        }
        if(variant==5 && t<2500)assert(!sim.actors[1].gear.restrained && !sim.evidence[1].collected && !mind->order_actor);
        if(variant==5 && t==2500)assert(swat_world_damage(&sim.world,cover,1000));
        if(variant==6 && heard && sim.actors[3].hearing_ticks>10){if(!heard_ticks){frozen_step=mind->room_step;frozen_scan=mind->room_scan;}heard_ticks++;assert(mind->room_step==frozen_step && mind->room_scan==frozen_scan);}
        if(variant==11 && threat && sim.actors[3].target_actor==1 && !sim.actors[1].gear.surrendered){if(!threat_ticks){frozen_step=mind->room_step;frozen_scan=mind->room_scan;}threat_ticks++;assert(mind->room_step==frozen_step && mind->room_scan==frozen_scan);}
        if(mind->order==SWAT_ORDER_HOLD && mind->room_step==SWAT_ROOM_SEARCH_POINTS)break;
    }
    fprintf(stderr,"Room variant%d tick%d step%d subtask%d blocked%d point%.3f %.3f feet%.3f %.3f span%.2f %.2f yawmask%x\n",variant,t,mind->room_step,mind->room_task,mind->room_blocked,mind->room_point.x,mind->room_point.z,last.x,last.z,max_x-min_x,max_z-min_z,looks);
    if(t==10000) {
        fprintf(stderr,"Room control order%d state%d scan%d roomgoal%d listening%d bot%d surrendered%d stunned%d cuff%d resolve%.3f\n",mind->order,mind->state,mind->room_scan,mind->room_goal,sim.actors[3].hearing_ticks,mind->bot,sim.actors[3].gear.surrendered,sim.actors[3].gear.stunned_ticks,sim.actors[3].gear.cuff_ticks,sim.actors[1].mind.resolve);
        fprintf(stderr,"Room door angle%.4f open%d blocked%d\n",sim.world.objects[door].door_angle,sim.world.objects[door].door_open,swat_world_door_blocked(&sim.world,door));
        SwatInput diagnostic[SWAT_MAX_ACTORS]={0};swat_sim_bot_inputs(&sim,diagnostic);b3Pos here=swat_body_feet_position(&sim.actors[3].controller.body);SwatTraceResult trace=swat_body_trace_body(&sim.actors[3].controller.body,here,mind->waypoint,1,1);
        SwatTag* tag=trace.hit?b3Body_GetUserData(b3Shape_GetBody(trace.shapeId)):NULL;
        fprintf(stderr,"Room input f%.3f s%.3f yaw%.3f crouch%d waypoint %.3f %.3f %.3f bodycast%.3f owner%d upper%.3f %.3f %.3f\n",diagnostic[3].forward,diagnostic[3].strafe,diagnostic[3].yaw_delta,diagnostic[3].crouch,mind->waypoint.x,mind->waypoint.y,mind->waypoint.z,trace.fraction,tag?tag->index:-1,sim.actors[3].controller.body.upperOffset.x,sim.actors[3].controller.body.upperOffset.y,sim.actors[3].controller.body.upperOffset.z);
        int from=swat_navigation_nearest(sim.navigation,here),to=swat_navigation_nearest(sim.navigation,mind->goal);b3Pos next;
        bool bounded=swat_navigation_next_for_actor(&sim,3,mind->goal,&next);int order=mind->order;mind->order=SWAT_ORDER_HOLD;bool plain=swat_navigation_next_for_actor(&sim,3,mind->goal,&next);mind->order=order;
        fprintf(stderr,"Room routes bounded%d plain%d from%d %.3f %.3f %.3f to%d %.3f %.3f %.3f\n",bounded,plain,from,sim.navigation->x[from],sim.navigation->height[from],sim.navigation->z[from],to,sim.navigation->x[to],sim.navigation->height[to],sim.navigation->z[to]);
        SwatContext c=swat_context(&sim,3);int target=mind->order_actor-1;
        fprintf(stderr,"Room stalled target%d goal%.3f %.3f task%.3f %.3f %.3f context%d kind%d index%d ready%d yaw%.3f pitch%.3f weapon %.3f %.3f %.3f cuff1%d cuff2%d\n",target,mind->goal.x,mind->goal.z,mind->task_point.x,mind->task_point.y,mind->task_point.z,c.action,c.hit.kind,c.hit.index,c.ready,sim.actors[3].controller.yaw,sim.actors[3].controller.pitch,sim.evidence[1].position.x,sim.evidence[1].position.y,sim.evidence[1].position.z,sim.actors[1].gear.restrained,sim.actors[2].gear.restrained);
    }
    assert(t<10000 && mind->order_room==1 && !mind->order_actor && !mind->room_task);
    assert(max_x-min_x>3.7f && max_z-min_z>3.5f && looks==255);
    if(variant==4)assert(sim.actors[1].gear.restrained && sim.actors[2].gear.restrained && sim.evidence[1].collected && sim.debrief.evidence==1);
    if(variant==5)assert(sim.actors[1].gear.restrained && sim.evidence[1].collected);
    if(variant==12)assert(escorted && sim.actors[1].gear.restrained && sim.actors[1].mind.escort_owner<0 && b3Distance((b3Pos){3.6f,0,2.5f},swat_body_feet_position(&sim.actors[1].controller.body))>.8f);
    if(variant==6)assert(heard && heard_ticks>30);
    if(variant==11)assert(threat && threat_ticks>10 && sim.actors[1].gear.restrained);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    assert(!sim.debrief.roe_violations && !sim.debrief.unlawful_damage);
    assert(sim.actors[4].mind.order==SWAT_ORDER_HOLD && sim.actors[5].mind.order==SWAT_ORDER_HOLD);
    swat_sim_close(&sim);printf("PASS room search variant%d: physical room coverage, bounded full-view scans, observed follow-up tasks and truthful route completion\n",variant);
}
// Actual furnished motel room, independent of socket scheduling. Fixture
// placement/surrender ends before the command; work uses ordinary bot inputs.
static void task_room_motel(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=18000;swat_config_human(&cfg);
    swat_sim_init(&sim,cfg,81);int door=-1;
    for(int i=1;i<sim.world.count;i++)if(sim.world.objects[i].door){door=i;break;}
    assert(door>0);b3Pos point=sim.world.objects[door].center;
    for(int i=0;i<sim.actor_count;i++) {
        SwatActor* a=&sim.actors[i];
        if(a->role==SWAT_OFFICER && a->mind.bot){a->mind.order=SWAT_ORDER_HOLD;a->mind.team=i==3?1:2;}
        if(a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN)a->gear.surrendered=true;
    }
    assert(swat_sim_set_player(&sim,1,true) && swat_sim_set_player(&sim,2,true));
    place_actor(0,b3OffsetPos(point,swat_v(0,-sim.world.objects[door].half.y,1.4f)));
    place_actor(4,b3OffsetPos(point,swat_v(.8f,-sim.world.objects[door].half.y,1.3f)));
    SwatController* leader=&sim.actors[0].controller;b3Vec3 aim=b3SubPos(point,swat_controller_eye(leader));
    leader->yaw=b3Atan2(aim.z,aim.x);leader->pitch=b3Atan2(aim.y,hypotf(aim.x,aim.z));
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_ROOM_SEARCH;in.squad_team=2;swat_sim_step(&sim,&in);
    assert(sim.actors[5].mind.pending_room==2);
    static SwatMap map;static SwatSnapshot snapshot,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);
    b3Pos original=swat_body_feet_position(&sim.actors[2].controller.body);int escorted=0,t;
    for(t=0;t<10000;t++) {
        swat_sim_step(&sim,&(SwatInput){0});
        if(task_trace)assert(fprintf(task_trace,"93 %d %08x\n",t,swat_replay_digest(&sim))>0);
        if(sim.actors[2].mind.escort_owner==5)escorted++;
        if(t%30==0) {
            swat_capture_snapshot(&sim,1,&snapshot);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
            assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
            assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));
        }
        if(sim.actors[5].mind.order==SWAT_ORDER_HOLD && sim.actors[5].mind.room_step==SWAT_ROOM_SEARCH_POINTS)break;
    }
    SwatMind* m=&sim.actors[5].mind;
    fprintf(stderr,"Motel room tick%d step%d task%d blocked%d feet%.3f %.3f goal%.3f %.3f escorted%d\n",t,m->room_step,m->room_task,m->room_blocked,swat_body_feet_position(&sim.actors[5].controller.body).x,swat_body_feet_position(&sim.actors[5].controller.body).z,m->goal.x,m->goal.z,escorted);
    assert(t<10000 && m->order_room==2 && sim.actors[2].gear.restrained && escorted>20 && sim.actors[2].mind.escort_owner<0);
    assert(b3Distance(original,swat_body_feet_position(&sim.actors[2].controller.body))>.8f);
    assert(!sim.actors[1].gear.restrained && !sim.evidence[1].collected && !sim.debrief.roe_violations);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    swat_sim_close(&replica);swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);swat_capture_snapshot(&sim,1,&snapshot);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));swat_sim_close(&replica);swat_sim_close(&sim);
    puts("PASS actual motel room search: ordinary entry/scans/cuff/escort/stop finish the furnished room and preserve a fresh encoded replica without hidden adjacent-room work");
}
