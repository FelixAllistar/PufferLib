#include "net.h"
#include "enet/enet.h"
static void equipment_net_poll(SwatNetServer* server,SwatNetClient* clients,int count) {
    swat_server_poll(server);for(int i=0;i<count;i++)swat_client_poll(&clients[i]);
    enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;enet_socket_wait(((ENetHost*)server->transport)->socket,&condition,1);
}
static void equipment_net_ready(SwatNetServer* server,SwatNetClient* clients,int count) {
    uint32_t start=enet_time_get();bool ready=false;
    while(!ready && enet_time_get()-start<15000) {
        equipment_net_poll(server,clients,count);ready=true;
        for(int i=0;i<count;i++)ready&=clients[i].status==SWAT_NET_ACTIVE && clients[i].epoch==server->epoch;
    }
    assert(ready);
}
static void equipment_net_tick(SwatNetServer* server,SwatNetClient* clients,int count,SwatInput host) {
    for(int i=0;i<count;i++)if(clients[i].pending_count<8)assert(swat_client_input(&clients[i],&(SwatInput){0}));
    equipment_net_poll(server,clients,count);swat_server_tick(server,&host);equipment_net_poll(server,clients,count);
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
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port));equipment_net_ready(&server,clients,1);assert(clients[0].slot==1);
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
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port));equipment_net_ready(&server,clients,2);assert(clients[1].slot==2);
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
