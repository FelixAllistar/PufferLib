static void motel_occupancy_network(void) {
    static SwatSim authority,replicas[2];SwatNetServer server;SwatNetClient clients[2]={0};
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);
    swat_sim_init(&authority,cfg,81);uint32_t port_seed=enet_time_get();bool opened=false;int port=0;
    for(int n=0;n<32 && !opened;n++){port=30000+(int)((port_seed+(uint32_t)n*4729u)%25000u);opened=swat_server_open(&server,&authority,port,false);}
    if(!opened)fprintf(stderr,"Occupancy UDP bind failed: %s\n",server.error);
    assert(opened);
    SwatConfig placeholder=swat_default_config();placeholder.hostile_fire=false;
    for(int i=0;i<2;i++)swat_sim_init(&replicas[i],placeholder,101+i);
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port));ready(&server,clients,1);assert(clients[0].slot==0);
    for(int p=1;p<SWAT_MOTEL_OCCUPANCIES;p++) {
        // A queued work order belongs to this epoch and must not survive the
        // level reset. Population changes use the ordinary leader packet.
        authority.actors[4].mind.queued=true;authority.actors[4].mind.pending_order=SWAT_ORDER_HOLD;
        cfg=authority.config;cfg.motel_occupancy=p;cfg.layout_seed=947;
        uint32_t epoch=server.epoch;swat_client_scenario(&clients[0],&cfg);uint32_t start=enet_time_get();
        while(server.epoch==epoch && enet_time_get()-start<15000)idle(&server,clients,1);
        assert(server.epoch==epoch+1);ready(&server,clients,1);
        assert(authority.config.motel_occupancy==p && replicas[0].config.motel_occupancy==p && replicas[0].config.layout_seed==947);
        assert(authority.world.count==SWAT_MOTEL_OBJECTS && replicas[0].world.count==SWAT_MOTEL_OBJECTS);
        assert(authority.actors[9].present==(p==SWAT_MOTEL_BUSY) && replicas[0].actors[9].present==authority.actors[9].present);
        assert(!authority.actors[4].mind.queued && !replicas[0].actors[4].mind.queued);
        motel_state(&authority,&replicas[0],10+p);
    }
    puts("PASS real UDP occupancy: authenticated leader switches dispersed/mixed/busy situations with matching state, original geometry and cleared queued tasks");fflush(stdout);
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port));ready(&server,clients,2);assert(clients[1].slot==1);
    SwatInput inputs[2]={swat_neutral_input(),swat_neutral_input()};ticks(&server,clients,inputs,2,2,NULL);synchronize(&server,clients,2);
    for(int i=0;i<2;i++){assert(replicas[i].config.motel_occupancy==SWAT_MOTEL_BUSY && replicas[i].actors[9].present);motel_state(&authority,&replicas[i],14);}
    puts("PASS real UDP occupied late join: fourth civilian and full busy-state baseline reach both peers");fflush(stdout);
    uint32_t epoch=server.epoch;cfg.motel_occupancy=SWAT_MOTEL_STANDARD;
    swat_client_scenario(&clients[1],&cfg);for(int i=0;i<20;i++)idle(&server,clients,2);
    assert(server.epoch==epoch && authority.config.motel_occupancy==SWAT_MOTEL_BUSY);
    const int invalid[]={-1,SWAT_MOTEL_OCCUPANCIES,256};
    for(int i=0;i<3;i++){SwatConfig bad=cfg;bad.motel_occupancy=invalid[i];swat_server_scenario(&server,&bad);assert(server.epoch==epoch && authority.config.motel_occupancy==SWAT_MOTEL_BUSY);}
    puts("PASS real UDP occupancy authorization: nonleader and invalid host configurations cannot replace the situation");fflush(stdout);
    swat_client_scenario(&clients[0],&cfg);uint32_t start=enet_time_get();
    while(server.epoch==epoch && enet_time_get()-start<15000)idle(&server,clients,2);
    assert(server.epoch==epoch+1);ready(&server,clients,2);
    for(int i=0;i<2;i++){assert(replicas[i].config.motel_occupancy==0 && !replicas[i].actors[9].present && replicas[i].actor_count==9);motel_state(&authority,&replicas[i],15);}
    cfg.motel_occupancy=SWAT_MOTEL_BUSY;raw_scenario(&clients[0],epoch,&cfg);for(int i=0;i<20;i++)idle(&server,clients,2);
    assert(server.epoch==epoch+1 && authority.config.motel_occupancy==0 && !authority.actors[9].present);
    for(int i=0;i<2;i++){swat_client_close(&clients[i]);swat_sim_close(&replicas[i]);}swat_server_close(&server);swat_sim_close(&authority);
    puts("PASS real UDP occupancy reset: standard removes the extra civilian on all peers; stale busy packets cannot resurrect it");
}
