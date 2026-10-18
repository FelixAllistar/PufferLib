// Real transport acceptance; fixture edits establish a visible surrendered
// occupant and damage events. Remote controls and handoff use ordinary packets.
static void motel_state(const SwatSim* authority,const SwatSim* replica,int phase){
    static SwatSnapshot expected,actual;
    static unsigned char left[SWAT_NET_PACKET_MAX],right[SWAT_NET_PACKET_MAX];
    assert(replica->world.motel&&replica->tick==authority->tick);
    swat_capture_snapshot(authority,1,&expected);swat_capture_snapshot(replica,1,&actual);
    // Sound logs retain history; hearing/playback memory applies the join floor.
    // Check that separately from the encoded simulation state below.
    expected.sound_count=actual.sound_count=0;
    size_t a=swat_encode_snapshot(left,sizeof(left),&expected),b=swat_encode_snapshot(right,sizeof(right),&actual);
    if(a!=b || memcmp(left,right,a))fprintf(stderr,"Motel UDP state mismatch phase%d tick%d sizes%zu/%zu\n",phase,authority->tick,a,b);
    assert(a&&a==b&&!memcmp(left,right,a));
    uint32_t digest=2166136261u;for(size_t i=0;i<a;i++){digest^=left[i];digest*=16777619u;}
    printf("MOTEL_STATE %d %d %08x\n",phase,authority->tick,digest);
}
static float motel_aperture(const SwatWorld* world){
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    return b3World_CastMover(world->id,(b3Pos){-12.7f,.02f,-1.17f},&capsule,swat_v(1.7f,0,0),b3DefaultQueryFilter(),NULL,NULL);
}
static int motel_sound_count(const SwatSim* s,uint32_t id){
    int count=0;for(int i=0;i<s->sounds.count;i++)count+=swat_sound_at(&s->sounds,i)->id==id;
    return count;
}
static bool motel_hear_event(const SwatSim* s,uint32_t id,SwatHearingMemory* memory){
    const SwatSoundEvent* event=NULL;
    for(int i=0;i<s->sounds.count;i++)if(swat_sound_at(&s->sounds,i)->id==id)event=swat_sound_at(&s->sounds,i);
    assert(event);SwatSoundLog isolated={0};swat_sound_append(&isolated,*event);
    SwatHeardSound heard;b3Pos listener=b3OffsetPos(event->position,swat_v(.3f,0,0));
    return swat_hearing_next(&s->world,&isolated,s->tick+1,listener,5,memory,&heard);
}
static void motel_network(void){
    static SwatSim authority,replicas[3];SwatNetServer server;SwatNetClient clients[3]={0};
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=cfg.randomize=false;cfg.max_ticks=6000;swat_config_human(&cfg);
    swat_sim_init(&authority,cfg,81);
    // Windows can reserve whole UDP ranges. Spread retries across the range
    // instead of trying 32 adjacent ports inside one reservation.
    uint32_t port_seed=enet_time_get();int port=0;bool opened=false;
    for(int attempt=0;attempt<32 && !opened;attempt++) {
        port=30000+(int)((port_seed+(uint32_t)attempt*4729u)%25000u);
        opened=swat_server_open(&server,&authority,port,true);
    }
    if(!opened)fprintf(stderr,"Motel UDP bind failed: %s\n",server.error);
    assert(server.transport&&server.local_slot==0);
    for(int i=3;i<6;i++)authority.actors[i].mind.order=SWAT_ORDER_HOLD;
    swat_sim_spawn_actor(&authority,7,SWAT_CIVILIAN,(b3Pos){1.6f,-.08f,7.2f},0);authority.actors[7].gear.surrendered=true;
    b3Vec3 aim=b3SubPos(swat_controller_eye(&authority.actors[7].controller),swat_controller_eye(&authority.actors[0].controller));
    authority.actors[0].controller.yaw=b3Atan2(aim.z,aim.x);authority.actors[0].controller.pitch=b3Atan2(aim.y,hypotf(aim.x,aim.z));
    SwatConfig placeholder=swat_default_config();placeholder.hostile_fire=false;
    for(int i=0;i<3;i++)swat_sim_init(&replicas[i],placeholder,(uint32_t)(100+i));
    // Force more loading time than the unmodified six-second peer deadline.
    // Production progress services this client's socket; QA services only the
    // separate authority/other hosts while the same thread is constructing it.
    loading_pause_budget=6500;loading_pause_services=0;
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port));ready(&server,clients,1);assert(clients[0].slot==1);
    assert(!loading_pause_budget && loading_pause_services>=100 && loading_pause_elapsed>=6500);
    assert(authority.tick==0 && replicas[0].tick==0);
    assert(!replicas[0].world.build_progress && !replicas[0].world.build_context);
    printf("PASS real UDP slow motel load: %u ms across %d serviced construction boundaries, ordinary six-second deadlines, tick zero and cleared hooks\n",loading_pause_elapsed,loading_pause_services);
    SwatInput inputs[3];for(int i=0;i<3;i++)inputs[i]=swat_neutral_input();
    SwatInput host=swat_neutral_input();host.squad_order=SWAT_ORDER_CUFF;host.squad_team=1;host.squad_queue=true;
    ticks(&server,clients,inputs,1,2,&host);host=swat_neutral_input();ticks(&server,clients,inputs,1,2,&host);synchronize(&server,clients,1);
    int handoff=swat_player_actor(2);
    assert(authority.actors[handoff].mind.pending_order==SWAT_ORDER_CUFF&&authority.actors[handoff].mind.pending_actor==8&&authority.actors[handoff].mind.queued);
    assert(replicas[0].actors[handoff].mind.pending_actor==8);motel_state(&authority,&replicas[0],0);
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port));ready(&server,clients,2);assert(clients[1].slot==2);
    host.squad_execute=true;ticks(&server,clients,inputs,2,2,&host);host=swat_neutral_input();synchronize(&server,clients,2);
    for(int i=0;i<2;i++){
        assert(!replicas[i].actors[handoff].mind.bot&&!replicas[i].actors[handoff].mind.pending_order&&!replicas[i].actors[handoff].mind.pending_actor);
        assert(!replicas[i].actors[7].gear.restrained);motel_state(&authority,&replicas[i],1);
    }
    puts("PASS real UDP motel handoff: queued directed target reaches the observer; joining human clears the task before Execute on authority and both peers");fflush(stdout);
    b3Pos before[2];for(int i=0;i<2;i++)before[i]=swat_body_feet_position(&authority.actors[clients[i].actor].controller.body);
    inputs[0].strafe=.5f;inputs[1].forward=.5f;ticks(&server,clients,inputs,2,24,&host);
    inputs[0]=inputs[1]=swat_neutral_input();ticks(&server,clients,inputs,2,2,&host);synchronize(&server,clients,2);
    for(int i=0;i<2;i++)assert(b3Distance(before[i],swat_body_feet_position(&authority.actors[clients[i].actor].controller.body))>.2f);
    int first=SWAT_MOTEL_DUMPSTER_FIRST;assert(swat_world_damage(&authority.world,first+1,100000));
    assert(motel_aperture(&authority.world)<.5f);
    SwatHit wall=swat_world_ray(&authority.world,(b3Pos){-12.7f,1,-1.17f},swat_v(1,0,0),1.5f,b3_nullBodyId);
    assert(wall.hit&&swat_world_breachable(&authority.world.objects[wall.index]));assert(swat_world_breach(&authority.world,wall.index,wall.point)>0);
    swat_sound_surface(&authority.sounds,authority.tick,0,SWAT_SOUND_BREACH,wall.point,.8f,24,authority.world.objects[wall.index].material);
    uint32_t breach_cue=authority.sounds.next_id-1;
    ticks(&server,clients,inputs,2,6,&host);synchronize(&server,clients,2);assert(motel_aperture(&authority.world)>.999f);
    for(int i=0;i<2;i++){
        assert(motel_sound_count(&replicas[i],breach_cue)==1);
        SwatHearingMemory heard={0};assert(motel_hear_event(&replicas[i],breach_cue,&heard));
        assert(!motel_hear_event(&replicas[i],breach_cue,&heard));
    }
    uint32_t sound_floor=authority.sounds.next_id-1;
    assert(swat_client_open(&clients[2],&replicas[2],"127.0.0.1",port));ready(&server,clients,3);assert(clients[2].slot==3);
    assert(clients[2].sound_floor==sound_floor&&sound_floor>=breach_cue);
    // Existing peers learn the changed human/bot roster on the next ordinary
    // broadcast; the joining peer's reliable baseline is already current.
    ticks(&server,clients,inputs,3,2,&host);synchronize(&server,clients,3);
    for(int i=0;i<3;i++){
        assert(replicas[i].world.objects[first].active&&!replicas[i].world.objects[first+1].active&&replicas[i].world.objects[first+2].active);
        assert(replicas[i].world.objects[first].query_mesh.data->triangleCount==1964);
        assert(motel_aperture(&replicas[i].world)>.999f);motel_state(&authority,&replicas[i],2);
    }
    puts("PASS real UDP motel late join: independent remote movement, fragmented full motel baseline, separate lid damage and matching physical masonry aperture on all three peers");fflush(stdout);
    assert(motel_sound_count(&replicas[2],breach_cue)==1);
    SwatHearingMemory fresh={0},late={.minimum_id=clients[2].sound_floor};
    assert(motel_hear_event(&replicas[2],breach_cue,&fresh));
    assert(!motel_hear_event(&replicas[2],breach_cue,&late));
    swat_sound_surface(&authority.sounds,authority.tick,0,SWAT_SOUND_IMPACT,(b3Pos){-4.9f,.7f,-8.4f},.5f,12,SWAT_STEEL);
    uint32_t impact_cue=authority.sounds.next_id-1;
    assert(swat_world_damage(&authority.world,first,100000));ticks(&server,clients,inputs,3,6,&host);synchronize(&server,clients,3);
    for(int i=0;i<3;i++){
        for(int p=0;p<3;p++)assert(!replicas[i].world.objects[first+p].active);
        assert(motel_sound_count(&replicas[i],impact_cue)==1);
        motel_state(&authority,&replicas[i],3);
    }
    assert(motel_hear_event(&replicas[2],impact_cue,&late));
    assert(!motel_hear_event(&replicas[2],impact_cue,&late));
    puts("PASS real UDP motel sounds: existing peers consume the breach once; late join suppresses that history and receives the subsequent metal impact exactly once");fflush(stdout);
    uint32_t old_epoch=server.epoch;swat_server_restart(&server);ready(&server,clients,3);assert(server.epoch==old_epoch+1&&authority.tick==0);
    for(int i=0;i<3;i++){
        assert(clients[i].epoch==server.epoch&&replicas[i].episode==(int)server.epoch&&replicas[i].world.count==SWAT_MOTEL_OBJECTS);
        for(int p=0;p<3;p++)assert(replicas[i].world.objects[first+p].active);
        assert(motel_aperture(&replicas[i].world)<.5f);motel_state(&authority,&replicas[i],4);
    }
    puts("PASS real UDP motel reset: supported body/lid removal replicates; the next epoch restores intact cover and closed masonry on all peers");fflush(stdout);
    swat_client_close(&clients[1]);uint32_t start=enet_time_get();
    while((server.player_mask&(1u<<2))&&enet_time_get()-start<2000)idle(&server,clients,3);
    assert(!(server.player_mask&(1u<<2))&&authority.actors[handoff].mind.bot&&!authority.actors[handoff].mind.pending_actor);
    for(int i=0;i<3;i++)swat_client_close(&clients[i]);
    swat_server_close(&server);
    for(int i=0;i<3;i++)swat_sim_close(&replicas[i]);
    swat_sim_close(&authority);
    puts("PASS real UDP motel disconnect: vacated officer returns to bot control without a stale directed task; all transports and worlds close");
}
