static const char* room_network_failure_prefix;
static void room_network_settle(SwatNetServer* server,SwatNetClient* clients,SwatSim* replicas,int count,int tick) {
    // A fixed number of simulation ticks is not a UDP delivery deadline.
    uint32_t start=enet_time_get();bool ready=false;
    while(!ready && enet_time_get()-start<15000) {
        equipment_net_poll(server,clients,count);ready=true;
        for(int i=0;i<count;i++)ready&=clients[i].status==SWAT_NET_ACTIVE && replicas[i].tick>=tick-1;
    }
    assert(ready);
}
// Authored motel and real UDP. Initial actor placement/surrender isolates task
// ownership; all subsequent search, hands-on work and takeover use normal APIs.
static void room_network(void) {
    static SwatSim authority,replicas[3];SwatNetServer server;SwatNetClient clients[3]={0};
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=18000;swat_config_human(&cfg);
    swat_sim_init(&authority,cfg,81);int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&authority,port,true) && ++attempt<32)port=30000+(port-30000+4729)%25000;
    assert(server.transport);int door=-1;
    for(int i=1;i<authority.world.count;i++)if(authority.world.objects[i].door){door=i;break;}
    assert(door>0);b3Pos point=authority.world.objects[door].center;
    for(int i=0;i<authority.actor_count;i++) {
        SwatActor* a=&authority.actors[i];
        if(a->role==SWAT_OFFICER && a->mind.bot){a->mind.order=SWAT_ORDER_HOLD;a->mind.team=i==3?1:2;}
        if(a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN)a->gear.surrendered=true;
    }
    for(int n=0;n<2;n++) {
        int actor=n?4:0;SwatController* c=&authority.actors[actor].controller;
        b3Pos feet=b3OffsetPos(point,swat_v(n?.8f:0,-authority.world.objects[door].half.y,n?1.3f:1.4f));
        b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);
    }
    SwatController* leader=&authority.actors[0].controller;b3Vec3 aim=b3SubPos(point,swat_controller_eye(leader));
    leader->yaw=atan2f(aim.z,aim.x);leader->pitch=atan2f(aim.y,hypotf(aim.x,aim.z));
    assert(swat_context_hit(&authority,0,24).index==door);
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port));equipment_net_ready(&server,clients,1);assert(clients[0].slot==1);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_ROOM_SEARCH;in.squad_team=2;in.squad_queue=true;equipment_net_tick(&server,clients,1,in);
    int room=authority.actors[4].mind.pending_room;assert(room==2);
    for(int n=0;n<24;n++)equipment_net_tick(&server,clients,1,swat_neutral_input());
    room_network_settle(&server,clients,replicas,1,authority.tick);
    assert(replicas[0].actors[4].mind.pending_room==room && replicas[0].actors[4].mind.queued);
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port));equipment_net_ready(&server,clients,2);assert(clients[1].slot==2);
    for(int n=0;n<8;n++)equipment_net_tick(&server,clients,2,swat_neutral_input());
    room_network_settle(&server,clients,replicas,2,authority.tick);
    assert(!authority.actors[4].mind.bot && !authority.actors[4].mind.pending_room);
    for(int n=0;n<2;n++)assert(!replicas[n].actors[4].mind.pending_room && !replicas[n].actors[4].mind.pending_order);
    printf("ROOM_NET queued_takeover room%d actor4 pending0\n",room);
    puts("PASS real UDP room search: queued room identity reaches a peer; joining human cancels that task on authority and both replicas");
    in=swat_neutral_input();in.squad_order=SWAT_ORDER_ROOM_SEARCH;in.squad_team=2;equipment_net_tick(&server,clients,2,in);
    assert(authority.actors[5].mind.pending_room==room);
    int t;for(t=0;t<6000 && authority.actors[5].mind.room_step<1;t++)equipment_net_tick(&server,clients,2,swat_neutral_input());
    if(t==6000) {
        const SwatActor* a=&authority.actors[5];const SwatMind* m=&a->mind;const SwatObject* leaf=&authority.world.objects[door];
        SwatContext hand=swat_context(&authority,5);SwatInput diagnostic[SWAT_MAX_ACTORS]={0};swat_sim_bot_inputs(&authority,diagnostic);
        fprintf(stderr,"Room UDP actor5 stalled step%d task%d blocked%d order%d state%d scan%d goalset%d feet%.3f %.3f %.3f goal%.3f %.3f %.3f waypoint%.3f %.3f %.3f view%.3f %.3f door%.3f open%d leafblocked%d input%.3f %.3f %.3f context%d kind%d index%d\n",m->room_step,m->room_task,m->room_blocked,m->order,m->state,m->room_scan,m->room_goal,swat_body_feet_position(&a->controller.body).x,swat_body_feet_position(&a->controller.body).y,swat_body_feet_position(&a->controller.body).z,m->goal.x,m->goal.y,m->goal.z,m->waypoint.x,m->waypoint.y,m->waypoint.z,a->controller.yaw,a->controller.pitch,leaf->door_angle,leaf->door_open,swat_world_door_blocked(&authority.world,door),diagnostic[5].forward,diagnostic[5].strafe,diagnostic[5].yaw_delta,hand.action,hand.hit.kind,hand.hit.index);
        b3Pos next;bool route=swat_navigation_next_for_actor(&authority,5,m->goal,&next);
        SwatTraceResult sweep=swat_body_trace_body(&a->controller.body,swat_body_feet_position(&a->controller.body),m->waypoint,1,1);SwatTag* tag=sweep.hit?b3Body_GetUserData(b3Shape_GetBody(sweep.shapeId)):NULL;
        fprintf(stderr,"Room entry route%d next%.3f %.3f %.3f sweep%.3f kind%d index%d\n",route,next.x,next.y,next.z,sweep.fraction,tag?tag->kind:0,tag?tag->index:-1);
        if(room_network_failure_prefix) {
            static SwatMap map;static SwatSnapshot snapshot;static unsigned char bytes[SWAT_NET_PACKET_MAX];char path[1024];
            swat_capture_map(&authority,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n);
            assert(snprintf(path,sizeof(path),"%s.map",room_network_failure_prefix)<(int)sizeof(path));FILE* file=fopen(path,"wb");assert(file && fwrite(bytes,1,n,file)==n && !fclose(file));
            swat_capture_snapshot(&authority,1,&snapshot);n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);assert(n);
            assert(snprintf(path,sizeof(path),"%s.snapshot",room_network_failure_prefix)<(int)sizeof(path));file=fopen(path,"wb");assert(file && fwrite(bytes,1,n,file)==n && !fclose(file));
        }
    }
    assert(t<6000 && authority.actors[5].mind.order==SWAT_ORDER_ROOM_SEARCH);
    assert(swat_client_open(&clients[2],&replicas[2],"127.0.0.1",port));equipment_net_ready(&server,clients,3);assert(clients[2].slot==3);
    for(int n=0;n<8;n++)equipment_net_tick(&server,clients,3,swat_neutral_input());
    room_network_settle(&server,clients,replicas,3,authority.tick);
    assert(!authority.actors[5].mind.bot && !authority.actors[5].mind.order_room && !authority.actors[5].mind.room_step);
    for(int n=0;n<3;n++)assert(!replicas[n].actors[5].mind.order_room && !replicas[n].actors[5].mind.room_step);
    printf("ROOM_NET active_takeover room%d actor5 progress0\n",room);
    puts("PASS real UDP room search: an officer physically enters and scans the motel room; late human takeover clears active search progress on all peers");
    swat_client_close(&clients[2]);
    for(t=0;t<200 && !authority.actors[5].mind.bot;t++)equipment_net_tick(&server,clients,2,swat_neutral_input());
    assert(authority.actors[5].mind.bot && !authority.actors[5].mind.order_room);
    in=swat_neutral_input();in.squad_order=SWAT_ORDER_ROOM_SEARCH;in.squad_team=2;equipment_net_tick(&server,clients,2,in);
    assert(authority.actors[5].mind.pending_room==room);
    int escort_ticks=0;b3Pos original_person=swat_body_feet_position(&authority.actors[2].controller.body);
    for(t=0;t<7000 && (authority.actors[5].mind.order!=SWAT_ORDER_HOLD || authority.actors[5].mind.room_step!=SWAT_ROOM_SEARCH_POINTS || replicas[1].actors[5].mind.room_step!=SWAT_ROOM_SEARCH_POINTS);t++){equipment_net_tick(&server,clients,2,swat_neutral_input());if(authority.actors[2].mind.escort_owner==5)escort_ticks++;}
    if(t==7000)fprintf(stderr,"Room UDP completion stalled step%d task%d blocked%d feet%.3f %.3f\n",authority.actors[5].mind.room_step,authority.actors[5].mind.room_task,authority.actors[5].mind.room_blocked,swat_body_feet_position(&authority.actors[5].controller.body).x,swat_body_feet_position(&authority.actors[5].controller.body).z);
    if(t==7000) {
        SwatMind* m=&authority.actors[5].mind;SwatMind* p=&authority.actors[2].mind;
        b3Pos at=swat_body_feet_position(&authority.actors[2].controller.body);
        fprintf(stderr,"Room failure goal %.3f %.3f %.3f work %.3f %.3f %.3f person %.3f %.3f %.3f owner%d personway %.3f %.3f %.3f view %.3f %.3f\n",m->goal.x,m->goal.y,m->goal.z,m->room_point.x,m->room_point.y,m->room_point.z,at.x,at.y,at.z,p->escort_owner,p->waypoint.x,p->waypoint.y,p->waypoint.z,authority.actors[5].controller.yaw,authority.actors[5].controller.pitch);
        if(room_network_failure_prefix) {
            static SwatMap map;static SwatSnapshot snapshot;static unsigned char bytes[SWAT_NET_PACKET_MAX];char path[1024];
            swat_capture_map(&authority,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n);
            assert(snprintf(path,sizeof(path),"%s.map",room_network_failure_prefix)<(int)sizeof(path));FILE* file=fopen(path,"wb");assert(file && fwrite(bytes,1,n,file)==n && !fclose(file));
            swat_capture_snapshot(&authority,1,&snapshot);n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);assert(n);
            assert(snprintf(path,sizeof(path),"%s.snapshot",room_network_failure_prefix)<(int)sizeof(path));file=fopen(path,"wb");assert(file && fwrite(bytes,1,n,file)==n && !fclose(file));
        }
    }
    assert(t<7000 && authority.actors[5].mind.order_room==room && authority.actors[2].gear.restrained);
    assert(escort_ticks>20 && authority.actors[2].mind.escort_owner<0 && b3Distance(original_person,swat_body_feet_position(&authority.actors[2].controller.body))>.8f);
    room_network_settle(&server,clients,replicas,2,authority.tick);
    for(int n=0;n<2;n++)assert(replicas[n].actors[5].mind.order_room==room && replicas[n].actors[5].mind.room_step==SWAT_ROOM_SEARCH_POINTS && replicas[n].actors[2].gear.restrained);
    assert(!authority.actors[1].gear.restrained && !authority.evidence[1].collected && !authority.debrief.roe_violations);
    printf("ROOM_NET completed room%d actor5 points%d cuffed2 moved2\n",room,SWAT_ROOM_SEARCH_POINTS);
    puts("PASS real UDP room search: replacement bot finishes the authored furnished room through physical scans/restraint; both peers share truthful route completion");
    for(int i=0;i<authority.actor_count;i++)assert(authority.actors[i].health==100 && !authority.actors[i].arsenal.shots);
    for(int n=0;n<2;n++)swat_client_close(&clients[n]);
    swat_server_close(&server);
    for(int n=0;n<3;n++)swat_sim_close(&replicas[n]);
    swat_sim_close(&authority);
}
