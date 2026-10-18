#include "net.h"
#include "enet/enet.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
typedef struct EquipmentLoading {SwatNetServer* server;SwatNetClient* clients;int count,own;uint32_t published;} EquipmentLoading;
static uint32_t equipment_load_delay,equipment_load_started,equipment_load_elapsed;
static int equipment_load_services;
static void equipment_loading(void* context) {
    EquipmentLoading* p=context;uint32_t now=enet_time_get();
    // Other processes keep running while this replica constructs its world.
    // Pump only their sockets here; production must service its own client.
    if(now-p->published>=250){swat_server_publish(p->server);p->published=now;}
    assert(enet_host_service((ENetHost*)p->server->transport,NULL,0)>=0);
    for(int i=0;i<p->count;i++)if(i!=p->own && p->clients[i].transport)
        assert(enet_host_service((ENetHost*)p->clients[i].transport,NULL,0)>=0);
    if(equipment_load_delay && p->own==1) {
        if(!equipment_load_services)equipment_load_started=enet_time_get();
        uint32_t delay=equipment_load_delay<40?equipment_load_delay:40;
#ifdef _WIN32
        Sleep(delay);
#else
        struct timespec duration={0,(long)delay*1000000};nanosleep(&duration,NULL);
#endif
        equipment_load_delay-=delay;equipment_load_services++;
        equipment_load_elapsed=enet_time_get()-equipment_load_started;
    }
}
static void equipment_net_poll(SwatNetServer* server,SwatNetClient* clients,int count) {
    EquipmentLoading loading[SWAT_MAX_PLAYERS];assert(count<=SWAT_MAX_PLAYERS);
    for(int i=0;i<count;i++) {
        assert(!clients[i].loading_service && !clients[i].loading_context);
        loading[i]=(EquipmentLoading){server,clients,count,i,enet_time_get()};
        clients[i].loading_service=equipment_loading;clients[i].loading_context=&loading[i];
    }
    swat_server_poll(server);for(int i=0;i<count;i++)swat_client_poll(&clients[i]);
    for(int i=0;i<count;i++){clients[i].loading_service=NULL;clients[i].loading_context=NULL;}
    enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;enet_socket_wait(((ENetHost*)server->transport)->socket,&condition,1);
}
static void equipment_net_ready(SwatNetServer* server,SwatNetClient* clients,int count) {
    uint32_t start=enet_time_get();bool ready=false;unsigned int maps=0;
    for(int i=0;i<count;i++)if(clients[i].has_map)maps|=1u<<i;
    while(!ready && enet_time_get()-start<15000) {
        equipment_net_poll(server,clients,count);ready=true;
        unsigned int rebuilt=0;for(int i=0;i<count;i++)if(clients[i].has_map)rebuilt|=1u<<i;
        // Rebuilding a full motel replica can occupy this shared test thread.
        // Give transport another bounded phase after a newly applied map;
        // production connection/packet deadlines remain unchanged.
        if(rebuilt!=maps){maps=rebuilt;start=enet_time_get();}
        for(int i=0;i<count;i++)ready&=clients[i].status==SWAT_NET_ACTIVE && clients[i].epoch==server->epoch;
    }
    if(!ready)for(int i=0;i<count;i++)fprintf(stderr,"Equipment UDP peer%d status%d epoch%u expected%u error=%s\n",i,clients[i].status,clients[i].epoch,server->epoch,clients[i].error);
    assert(ready);
}
static void equipment_net_tick(SwatNetServer* server,SwatNetClient* clients,int count,SwatInput host) {
    for(int i=0;i<count;i++)if(clients[i].pending_count<8) {
        bool sent=swat_client_input(&clients[i],&(SwatInput){0});
        if(!sent)fprintf(stderr,"Equipment UDP input peer%d tick%d status%d revision%u error=%s\n",i,server->sim->tick,clients[i].status,clients[i].revision,clients[i].error);
        assert(sent);
    }
    equipment_net_poll(server,clients,count);swat_server_tick(server,&host);equipment_net_poll(server,clients,count);
}
// Modified geometry has its own small UDP fixture. The canonical motel check
// below must retain its complete authored mesh bindings and slow-load coverage.
static void damaged_equipment_network(bool clear_glass) {
    task_fixture();equipment_load_delay=0;
    static SwatSim peer;SwatNetServer server;SwatNetClient client={0};
    int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&sim,port,true) && ++attempt<32)port=30000+(port-30000+4729)%25000;
    assert(server.transport);
    // Opening a production server resets its scenario. Establish this explicit
    // fixture afterwards, before connecting any peer or publishing a map.
    swat_sim_close(&sim);task_fixture();sim.tick=240;
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;
    place_actor(1,(b3Pos){8,0,8});place_actor(2,(b3Pos){9,0,9});
    place_actor(0,(b3Pos){-4,0,0});place_actor(3,(b3Pos){-3,0,-4});
    place_actor(4,(b3Pos){-2,0,-1.5f});place_actor(5,(b3Pos){-3,0,4});
    int owner=swat_world_box(&sim.world,(b3Pos){0,1.3f,0},swat_v(.04f,1.3f,.8f),SWAT_WOOD,120);
    SwatObject* door=&sim.world.objects[owner];door->door=door->locked=true;door->hinge=(b3Pos){0,1.3f,-.8f};
    sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=0;
    SwatHit seen=swat_context_hit(&sim,0,24);assert(seen.kind==SWAT_HIT_WORLD && seen.index==owner);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_PICK;in.squad_team=2;in.squad_queue=true;swat_sim_step(&sim,&in);
    assert(sim.actors[4].mind.pending_door==owner && sim.actors[4].mind.queued);
    int cover=swat_world_box(&sim.world,door->center,swat_v(.2f,1.3f,.85f),clear_glass?SWAT_GLASS:SWAT_CONCRETE,100);
    assert(swat_client_open(&client,&peer,"127.0.0.1",port));equipment_net_ready(&server,&client,1);
    assert(client.slot==1);
    for(int n=0;n<8;n++)equipment_net_tick(&server,&client,1,swat_neutral_input());
    assert(peer.actors[4].mind.pending_door==owner && peer.actors[4].mind.queued);
    in=swat_neutral_input();in.squad_execute=true;equipment_net_tick(&server,&client,1,in);
    for(int n=0;n<120;n++)equipment_net_tick(&server,&client,1,swat_neutral_input());
    assert(sim.actors[4].mind.tool_blocked && peer.actors[4].mind.tool_blocked);
    assert(door->locked && !sim.actors[4].gear.door_ticks && !sim.actors[4].gear.used_tools);
    assert(swat_world_damage(&sim.world,cover,1000));
    int t;for(t=0;t<1800 && (door->locked || sim.actors[4].mind.order!=SWAT_ORDER_HOLD || peer.world.objects[owner].locked || peer.actors[4].mind.tool_blocked);t++)equipment_net_tick(&server,&client,1,swat_neutral_input());
    assert(t<1800 && !peer.world.objects[cover].active && sim.actors[4].gear.used_tools);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    printf("PASS real UDP squad equipment: actual obstructed task replicates waiting status without false tool progress, then resumes after cover removal (%s geometry fixture)\n",clear_glass?"clear glass":"opaque");
    swat_client_close(&client);swat_server_close(&server);swat_sim_close(&peer);swat_sim_close(&sim);
}
static void equipment_network(void) {
    static SwatSim authority,replicas[2];SwatNetServer server;SwatNetClient clients[2]={0};
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=10000;swat_config_human(&cfg);
    swat_sim_init(&authority,cfg,81);int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&authority,port,true) && ++attempt<32)port=30000+(port-30000+4729)%25000;
    assert(server.transport);int owner=-1;
    for(int i=1;i<authority.world.count;i++)if(authority.world.objects[i].door){owner=i;break;}
    assert(owner>0);SwatObject* door=&authority.world.objects[owner];door->locked=true;
    for(int i=0;i<authority.actor_count;i++) {
        SwatActor* a=&authority.actors[i];
        if(a->role==SWAT_OFFICER && a->mind.bot){a->mind.order=SWAT_ORDER_HOLD;a->mind.team=i==3?1:2;}
        if(a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN)a->gear.surrendered=true;
    }
    b3Vec3 normal=swat_v(cosf(door->closed_yaw),0,-sinf(door->closed_yaw));
    if(b3Dot(normal,b3SubPos(authority.extraction,door->center))<0)normal=swat_mul(normal,-1);
    b3Vec3 tangent=swat_v(-normal.z,0,normal.x);b3Pos feet=door->center;feet.y-=door->half.y;
    const int actors[]={0,4};const float depth[]={1.4f,1.3f},side[]={0,-.8f};
    for(int n=0;n<2;n++) {
        SwatController* c=&authority.actors[actors[n]].controller;
        b3Pos p=b3OffsetPos(feet,swat_add(swat_mul(normal,depth[n]),swat_mul(tangent,side[n])));
        b3Body_SetTransform(c->body.body,b3OffsetPos(p,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);
    }
    SwatController* leader=&authority.actors[0].controller;b3Vec3 aim=b3SubPos(door->center,swat_controller_eye(leader));
    leader->yaw=atan2f(aim.z,aim.x);leader->pitch=atan2f(aim.y,hypotf(aim.x,aim.z));
    SwatHit hit=swat_context_hit(&authority,0,24);
    fprintf(stderr,"Equipment motel target%d actual%d kind%d\n",owner,hit.index,hit.kind);
    assert(hit.kind==SWAT_HIT_WORLD && hit.index==owner);
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port));equipment_net_ready(&server,clients,1);assert(clients[0].slot==1 && replicas[0].world.motel);
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_PICK;in.squad_team=2;in.squad_queue=true;equipment_net_tick(&server,clients,1,in);
    assert(authority.actors[4].mind.pending_order==SWAT_ORDER_PICK && authority.actors[4].mind.pending_door==owner);
    for(int t=0;t<24;t++)equipment_net_tick(&server,clients,1,swat_neutral_input());
    assert(door->locked && replicas[0].actors[4].mind.pending_door==owner && replicas[0].actors[4].mind.queued);
    printf("EQUIPMENT_NET queued_pick door%d actor4\n",owner);
    in=swat_neutral_input();in.squad_execute=true;equipment_net_tick(&server,clients,1,in);
    int t;for(t=0;t<1600 && (door->locked || authority.actors[4].mind.order!=SWAT_ORDER_HOLD || replicas[0].world.objects[owner].locked);t++)equipment_net_tick(&server,clients,1,swat_neutral_input());
    if(t==1600)fprintf(stderr,"Motel pick stalled actor4 feet %.3f %.3f door_ticks%d order%d\n",swat_body_feet_position(&authority.actors[4].controller.body).x,swat_body_feet_position(&authority.actors[4].controller.body).z,authority.actors[4].gear.door_ticks,authority.actors[4].mind.order);
    assert(t<1600 && !door->door_open && authority.actors[4].gear.used_tools && !authority.actors[4].mind.order_door);
    puts("PASS real UDP squad equipment: queued source-door identity replicates and one officer physically unlocks the authored motel door");
    in=swat_neutral_input();in.squad_order=SWAT_ORDER_WEDGE;in.squad_team=2;in.squad_queue=true;equipment_net_tick(&server,clients,1,in);
    assert(authority.actors[4].mind.pending_door==owner && authority.actors[4].mind.queued);
    // Keep the already connected peer alive across a late map load longer
    // than the unchanged shipping six-second deadline, without stepping play.
    equipment_load_delay=6500;equipment_load_services=0;int before_join=authority.tick;
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port));equipment_net_ready(&server,clients,2);assert(clients[1].slot==2 && replicas[0].world.motel && replicas[1].world.motel);
    if(equipment_load_delay || equipment_load_services<100 || equipment_load_elapsed<6500 || authority.tick!=before_join)
        fprintf(stderr,"Equipment late join: remaining%u services%d elapsed%u tick%d expected%d\n",equipment_load_delay,equipment_load_services,equipment_load_elapsed,authority.tick,before_join);
    assert(!equipment_load_delay && equipment_load_services>=100 && equipment_load_elapsed>=6500 && authority.tick==before_join);
    printf("EQUIPMENT_NET slow_late_join elapsed_ms%u services%d unchanged_tick%d\n",equipment_load_elapsed,equipment_load_services,authority.tick);
    for(int n=0;n<8;n++)equipment_net_tick(&server,clients,2,swat_neutral_input());
    assert(!authority.actors[4].mind.bot && !authority.actors[4].mind.pending_order && !authority.actors[4].mind.pending_door && door->wedge_owner<0);
    for(int n=0;n<2;n++)assert(!replicas[n].actors[4].mind.pending_door && !replicas[n].actors[4].mind.pending_order);
    printf("EQUIPMENT_NET takeover door%d actor4 pending0\n",owner);
    puts("PASS real UDP squad equipment: late human takeover cancels the queued door task on both peers without spending wedge stock");
    in=swat_neutral_input();in.squad_order=SWAT_ORDER_WEDGE;in.squad_team=2;equipment_net_tick(&server,clients,2,in);
    assert(authority.actors[5].mind.pending_order==SWAT_ORDER_WEDGE && authority.actors[5].mind.pending_door==owner);
    for(t=0;t<3000 && (door->wedge_owner!=5 || authority.actors[5].mind.order!=SWAT_ORDER_HOLD || replicas[1].world.objects[owner].wedge_owner!=5);t++)equipment_net_tick(&server,clients,2,swat_neutral_input());
    if(t==3000)fprintf(stderr,"Motel wedge stalled actor5 feet %.3f %.3f tool%d order%d\n",swat_body_feet_position(&authority.actors[5].controller.body).x,swat_body_feet_position(&authority.actors[5].controller.body).z,authority.actors[5].gear.door_ticks,authority.actors[5].mind.order);
    assert(t<3000 && authority.actors[5].gear.wedges==1 && authority.actors[4].gear.wedges==2 && !door->door_open);
    printf("EQUIPMENT_NET wedged door%d actor5 stock1\n",owner);
    puts("PASS real UDP squad equipment: remaining officer approaches physically and spends one wedge; both peers share ownership/state");
    for(int i=0;i<authority.actor_count;i++)assert(authority.actors[i].health==100 && !authority.actors[i].arsenal.shots);
    for(int n=0;n<2;n++)swat_client_close(&clients[n]);
    swat_server_close(&server);
    for(int n=0;n<2;n++)swat_sim_close(&replicas[n]);
    swat_sim_close(&authority);
}
