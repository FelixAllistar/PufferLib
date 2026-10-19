static void task_threat_network(int mirror) {
    task_fixture();equipment_load_delay=0;static SwatSim peers[2];SwatNetServer server;SwatNetClient clients[2]={0};
    int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&sim,port,false) && ++attempt<32)port=30000+(port-30000+4729)%25000;
    assert(server.transport);swat_sim_close(&sim);task_fixture();sim.tick=240;sim.config.hostile_fire=true;
    place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){8,0,0});place_actor(2,(b3Pos){-9,0,8});sim.actors[2].gear.surrendered=true;
    place_actor(3,(b3Pos){-7,0,-5});place_actor(4,(b3Pos){0,0,0});place_actor(5,(b3Pos){2.8f,0,-2*mirror});
    sim.actors[3].mind.team=0;sim.actors[4].mind.team=2;sim.actors[5].mind.team=1;sim.actors[4].controller.yaw=0;sim.actors[1].controller.yaw=SWAT_PI;
    swat_world_box(&sim.world,(b3Pos){5,1.5f,-1.1f*mirror},swat_v(.15f,1.5f,.7f),SWAT_CONCRETE,0);
    search_aim(0,(b3Pos){2.8f,0,2*mirror});assert(swat_client_open(&clients[0],&peers[0],"127.0.0.1",port));equipment_net_ready(&server,clients,1);assert(clients[0].slot==0);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_MOVE;in.squad_team=1;in.squad_queue=true;equipment_remote_tick(&server,clients,1,in);
    for(int t=0;t<10;t++){equipment_remote_tick(&server,clients,1,swat_neutral_input());}
    assert(sim.actors[5].mind.pending_order==SWAT_ORDER_MOVE);
    bool finished=false;int count=1,t;for(t=0;t<1800 && sim.end==SWAT_RUNNING;t++) {
        in=swat_neutral_input();in.squad_execute=t==0;equipment_remote_tick(&server,clients,count,in);
        assert(sim.actors[5].health==100 && sim.actors[4].alive && !sim.debrief.roe_violations && !fire_lane_body_on_ray(4,5));
        if(t==50){assert(swat_client_open(&clients[1],&peers[1],"127.0.0.1",port));equipment_net_ready(&server,clients,2);count=2;assert(clients[1].slot==1);}
        if(b3Distance(swat_body_feet_position(&sim.actors[5].controller.body),sim.actors[5].mind.goal)<.65f){finished=true;break;}
    }
    assert(finished && count==2 && sim.actors[4].arsenal.shots>0 && (!sim.actors[1].alive || sim.actors[1].gear.surrendered));
    if(mirror==1)assert(sim.actors[1].arsenal.shots>0);
    swat_server_publish(&server);uint32_t start=enet_time_get();bool ready=false;while(!ready && enet_time_get()-start<15000){equipment_net_poll(&server,clients,count);ready=peers[0].tick==sim.tick && peers[1].tick==sim.tick;}assert(ready);
    for(int p=0;p<2;p++)assert(peers[p].config.hostile_fire);
    for(int p=0;p<2;p++)for(int i=4;i<=5;i++) {
        const SwatActor* a=&sim.actors[i],*b=&peers[p].actors[i];b3Pos x=b3Body_GetPosition(a->controller.body.body),y=b3Body_GetPosition(b->controller.body.body);
        assert((float)x.x==(float)y.x && (float)x.y==(float)y.y && (float)x.z==(float)y.z);assert(a->health==b->health && a->alive==b->alive && a->mind.order==b->mind.order && a->controller.yaw==b->controller.yaw);
    }
    printf("THREAT_NET mirror%d tick%d coverShots%d hostileShots%d coverHealth%.3f moverHealth%.3f peers2\n",mirror,t,sim.actors[4].arsenal.shots,sim.actors[1].arsenal.shots,sim.actors[4].health,sim.actors[5].health);
    for(int p=0;p<2;p++){swat_client_close(&clients[p]);swat_sim_close(&peers[p]);}swat_server_close(&server);swat_sim_close(&sim);
    puts("PASS observed threat UDP: remote queued Move/Execute, actual hostile/covering AI, late second peer, protected arrival and exact final wire-float states");
}
