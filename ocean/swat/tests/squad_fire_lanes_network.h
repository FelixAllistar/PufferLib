static void task_fire_lane_network(int mirror) {
    task_fixture();equipment_load_delay=0;static SwatSim peers[2];SwatNetServer server;SwatNetClient clients[2]={0};
    int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&sim,port,false) && ++attempt<32)port=30000+(port-30000+4729)%25000;
    assert(server.transport);swat_sim_close(&sim);task_fixture();sim.tick=240;
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;place_actor(1,(b3Pos){-8,0,9});place_actor(2,(b3Pos){-9,0,8});
    place_actor(0,(b3Pos){0,0,0});place_actor(4,(b3Pos){2.8f,0,-2*mirror});place_actor(3,(b3Pos){-7,0,-5});place_actor(5,(b3Pos){-7,0,5});
    swat_world_box(&sim.world,(b3Pos){8,1.5f,0},swat_v(.15f,1.5f,4),SWAT_CONCRETE,0);
    search_aim(0,(b3Pos){2.8f,0,2*mirror});
    assert(swat_client_open(&clients[0],&peers[0],"127.0.0.1",port));equipment_net_ready(&server,clients,1);assert(clients[0].slot==0);
    SwatInput command=swat_neutral_input();command.squad_order=SWAT_ORDER_MOVE;command.squad_team=2;command.squad_queue=true;equipment_remote_tick(&server,clients,1,command);
    for(int t=0;t<60;t++)equipment_remote_tick(&server,clients,1,fire_lane_look(0,(b3Pos){8,1.6f,0},true));
    assert(sim.actors[4].mind.pending_order==SWAT_ORDER_MOVE && sim.actors[4].mind.queued);
    bool finished=false;int count=1,t;float min_x=100;
    for(t=0;t<1800 && sim.end==SWAT_RUNNING;t++) {
        command=fire_lane_look(0,(b3Pos){8,1.6f,0},true);command.squad_execute=t==0;command.fire=t<180 && t%12==0;
        equipment_remote_tick(&server,clients,count,command);b3Pos feet=swat_body_feet_position(&sim.actors[4].controller.body);min_x=fminf(min_x,feet.x);
        assert(sim.actors[4].health==100 && !sim.debrief.roe_violations && !fire_lane_body_on_ray(0,4));
        if(t==50) {
            assert(swat_client_open(&clients[1],&peers[1],"127.0.0.1",port));equipment_net_ready(&server,clients,2);count=2;assert(clients[1].slot==1);
        }
        if(b3Distance(feet,sim.actors[4].mind.goal)<.65f){finished=true;break;}
    }
    assert(finished && count==2 && min_x<-.5f && sim.actors[0].arsenal.shots>0);
    swat_server_publish(&server);uint32_t start=enet_time_get();bool ready=false;
    while(!ready && enet_time_get()-start<15000){equipment_net_poll(&server,clients,count);ready=peers[0].tick==sim.tick && peers[1].tick==sim.tick;}
    assert(ready);b3Pos authoritative=b3Body_GetPosition(sim.actors[4].controller.body.body);
    for(int p=0;p<2;p++) {
        const SwatActor* a=&peers[p].actors[4];b3Pos received=b3Body_GetPosition(a->controller.body.body);
        // The existing protocol transmits positions as float32. Compare that
        // exact wire representation, without adding a position tolerance.
        assert((float)authoritative.x==(float)received.x && (float)authoritative.y==(float)received.y && (float)authoritative.z==(float)received.z);
        assert(a->health==100 && a->mind.order==SWAT_ORDER_MOVE && a->controller.yaw==sim.actors[4].controller.yaw);
    }
    printf("FIRE_LANE_NET mirror%d tick%d minX%.6f shots%d peers2\n",mirror,t,min_x,sim.actors[0].arsenal.shots);
    for(int p=0;p<2;p++){swat_client_close(&clients[p]);swat_sim_close(&peers[p]);}swat_server_close(&server);swat_sim_close(&sim);
    puts("PASS real UDP firing lane: remote Move and live shots, late second peer, physical detour/arrival and exact final wire-float pose without friendly injury");
}
