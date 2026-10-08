#include "net.h"
#include "enet/enet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void idle(SwatNetServer* server,SwatNetClient* clients,int count) {
    swat_server_poll(server);
    for(int i=0;i<count;i++) swat_client_poll(&clients[i]);
    enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;
    enet_socket_wait(((ENetHost*)server->transport)->socket,&condition,1);
}
static void ready(SwatNetServer* server,SwatNetClient* clients,int count) {
    uint32_t start=enet_time_get(); bool all=false;
    while(!all && enet_time_get()-start<5000) {
        idle(server,clients,count); all=true;
        for(int i=0;i<count;i++) all=all && clients[i].status==SWAT_NET_ACTIVE && clients[i].epoch==server->epoch;
    }
    for(int i=0;i<count;i++) {
        if(clients[i].status!=SWAT_NET_ACTIVE) fprintf(stderr,"client %d status %d error %s\n",i,clients[i].status,clients[i].error);
        assert(clients[i].status==SWAT_NET_ACTIVE);
        assert(clients[i].epoch==server->epoch);
    }
}
static void ticks(SwatNetServer* server,SwatNetClient* clients,SwatInput* inputs,int count,int steps,const SwatInput* host) {
    for(int t=0;t<steps;t++) {
        for(int i=0;i<count;i++) if(clients[i].status==SWAT_NET_ACTIVE) assert(swat_client_input(&clients[i],&inputs[i]));
        // This test measures simulated hold durations. Receive every command
        // before advancing its frame so scheduler/UDP latency cannot shorten a
        // lockpick hold or queue an earlier neutral input behind its next hold.
        uint32_t start=enet_time_get(); bool delivered=false;
        while(!delivered && enet_time_get()-start<1000) {
            idle(server,clients,count); delivered=true;
            for(int i=0;i<count;i++) if(clients[i].status==SWAT_NET_ACTIVE)
                delivered=delivered && server->slots[clients[i].slot].received>=clients[i].sequence;
        }
        assert(delivered); swat_server_tick(server,host);
        idle(server,clients,count);
    }
}
static void synchronize(SwatNetServer* server,SwatNetClient* clients,int count) {
    uint32_t start=enet_time_get(); bool done=false;
    while(!done && enet_time_get()-start<2000) {
        idle(server,clients,count); done=true;
        for(int i=0;i<count;i++) done=done && clients[i].replica->tick==server->sim->tick && clients[i].epoch==server->epoch;
    }
    if(!done) for(int i=0;i<count;i++) fprintf(stderr,"Sync failed: authority tick=%d, client %d tick=%d status=%d epoch=%u/%u error=%s\n",
        server->sim->tick,i,clients[i].replica->tick,clients[i].status,clients[i].epoch,server->epoch,clients[i].error);
    assert(done);
}
static void raw_control(SwatNetClient* client,SwatMessage type,uint32_t epoch) {
    unsigned char bytes[32]; size_t length=swat_encode_control(bytes,sizeof(bytes),type,epoch,0);
    ENetPacket* packet=enet_packet_create(bytes,length,ENET_PACKET_FLAG_RELIABLE);
    assert(enet_peer_send((ENetPeer*)client->peer,0,packet)==0);
    enet_host_flush((ENetHost*)client->transport);
}
static void raw_scenario(SwatNetClient* client,uint32_t epoch,const SwatConfig* config) {
    unsigned char bytes[32]; size_t length=swat_encode_scenario(bytes,sizeof(bytes),epoch,config);
    assert(length && enet_peer_send((ENetPeer*)client->peer,0,enet_packet_create(bytes,length,ENET_PACKET_FLAG_RELIABLE))==0);
    enet_host_flush((ENetHost*)client->transport);
}

int main(void) {
    SwatConfig config=swat_default_config(); config.max_ticks=6000; config.hostile_fire=false; config.randomize=false;
    static SwatSim authority,replicas[5]; SwatNetServer server; SwatNetClient clients[5]={0};
    swat_sim_init(&authority,config,42);
    int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&authority,port,false) && ++attempt<32) port++;
    assert(server.transport && server.player_mask==0);
    int cover=swat_world_box(&authority.world,(b3Pos){2,1.5f,0},swat_v(.1f,.4f,.4f),SWAT_WOOD,30);
    for(int i=0;i<5;i++) swat_sim_init(&replicas[i],config,(uint32_t)(100+i));
    for(int i=0;i<2;i++) assert(swat_client_open(&clients[i],&replicas[i],"127.0.0.1",port));
    ready(&server,clients,2);
    assert(clients[0].slot!=clients[1].slot && server.player_mask==3);
    // Identify whichever connection arrived first; the server assigns identity.
    int leader=clients[0].slot==0 ? 0 : 1,other=1-leader;
    SwatInput inputs[5]; for(int i=0;i<5;i++) inputs[i]=swat_neutral_input();
    inputs[leader].fire=true; inputs[leader].aim=true;
    ticks(&server,clients,inputs,2,2,NULL);
    inputs[leader]=swat_neutral_input(); ticks(&server,clients,inputs,2,20,NULL); synchronize(&server,clients,2);
    assert(!authority.world.objects[cover].active && authority.actors[0].arsenal.shots==1);
    for(int i=0;i<2;i++) {
        assert(!replicas[i].world.objects[cover].active && replicas[i].actors[0].arsenal.shots==1);
        assert(replicas[i].sounds.count>=3);
    }
    assert(replicas[0].sounds.count==replicas[1].sounds.count);
    puts("PASS real UDP: two distinct officers, server-side firing/ammo/destruction, both replicas and duplicate-free sound events");

    inputs[leader].strafe=1; inputs[other].forward=1;
    ticks(&server,clients,inputs,2,60,NULL);
    for(int i=0;i<2;i++) inputs[i]=swat_neutral_input();
    ticks(&server,clients,inputs,2,20,NULL); synchronize(&server,clients,2);
    assert(swat_body_feet_position(&authority.actors[0].controller.body).z>2);
    assert(swat_body_feet_position(&authority.actors[3].controller.body).x>2);
    for(int i=0;i<2;i++) for(int a=0;a<authority.actor_count;a++) if(authority.actors[a].present)
        assert(b3Distance(swat_controller_eye(&replicas[i].actors[a].controller),swat_controller_eye(&authority.actors[a].controller))<1e-5f);
    SwatController* officer=&authority.actors[0].controller;
    b3Body_SetTransform(officer->body.body,(b3Pos){5.4f,officer->body.totalHeight*.5f+.02f,0},b3Quat_identity);
    b3Body_SetLinearVelocity(officer->body.body,swat_v(0,0,0)); officer->yaw=officer->pitch=0;
    inputs[leader].interact=true; ticks(&server,clients,inputs,2,2,NULL);
    inputs[leader].interact=false; ticks(&server,clients,inputs,2,70,NULL); synchronize(&server,clients,2);
    assert(authority.world.objects[8].door_open && authority.world.objects[8].door_angle>1.5f);
    for(int i=0;i<2;i++) assert(replicas[i].world.objects[8].door_angle==authority.world.objects[8].door_angle);
    assert(swat_client_open(&clients[2],&replicas[2],"127.0.0.1",port)); ready(&server,clients,3);
    assert(!replicas[2].world.objects[cover].active && replicas[2].world.objects[8].door_angle>1.5f);
    assert(clients[2].sound_floor==authority.sounds.next_id-1);
    puts("PASS real UDP: independent movement, exact replicated poses, shared door collision and late-join damage baseline");

    raw_control(&clients[other],SWAT_MSG_RESTART,server.epoch);
    for(int i=0;i<20;i++) idle(&server,clients,3);
    assert(server.epoch==1); swat_client_restart(&clients[leader]);
    uint32_t start=enet_time_get();
    while(server.epoch==1 && enet_time_get()-start<2000) idle(&server,clients,3);
    assert(server.epoch==2); ready(&server,clients,3);
    for(int i=0;i<3;i++) assert(clients[i].epoch==2 && replicas[i].tick==0 && replicas[i].actors[0].arsenal.shots==0);
    // A stale pre-reset trigger cannot enter the new round.
    SwatCommand stale={1,1000,swat_neutral_input()}; stale.input.fire=true;
    unsigned char bytes[64]; size_t length=swat_encode_command(bytes,sizeof(bytes),&stale);
    assert(enet_peer_send((ENetPeer*)clients[leader].peer,1,enet_packet_create(bytes,length,ENET_PACKET_FLAG_RELIABLE))==0);
    enet_host_flush((ENetHost*)clients[leader].transport);
    ticks(&server,clients,inputs,3,4,NULL); assert(authority.actors[0].arsenal.shots==0);
    assert(swat_client_open(&clients[3],&replicas[3],"127.0.0.1",port)); ready(&server,clients,4);
    assert(server.player_mask==15);
    assert(swat_client_open(&clients[4],&replicas[4],"127.0.0.1",port)); start=enet_time_get();
    while(clients[4].status!=SWAT_NET_FAILED && enet_time_get()-start<3000) idle(&server,clients,5);
    assert(clients[4].status==SWAT_NET_FAILED && strstr(clients[4].error,"four players"));
    swat_client_close(&clients[4]);
    authority.end=SWAT_TIMEOUT; ticks(&server,clients,inputs,4,80,NULL);
    for(int i=0;i<4;i++) assert(clients[i].status==SWAT_NET_ACTIVE && replicas[i].end==SWAT_TIMEOUT);
    swat_server_restart(&server); ready(&server,clients,4);
    swat_client_close(&clients[leader]);
    for(int i=0;i<20;i++) idle(&server,clients,4);
    assert(!authority.actors[0].present && server.player_mask==14 && swat_server_leader(&server)==1);
    puts("PASS real UDP: leader authority, reset epochs/stale inputs, four-player limit, terminal-state connections and disconnect slot cleanup");
    for(int i=0;i<4;i++) swat_client_close(&clients[i]);
    swat_server_close(&server);

    assert(swat_server_open(&server,&authority,port,true));
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port)); ready(&server,clients,1);
    assert(clients[0].slot==1 && server.local_slot==0 && server.player_mask==3);
    SwatInput host=swat_neutral_input(); host.forward=1; inputs[0]=swat_neutral_input(); inputs[0].strafe=1;
    ticks(&server,clients,inputs,1,30,&host);
    assert(swat_body_feet_position(&authority.actors[0].controller.body).x>1);
    assert(swat_body_feet_position(&authority.actors[3].controller.body).z>-.4f);
    swat_server_close(&server); start=enet_time_get();
    while(clients[0].status!=SWAT_NET_FAILED && enet_time_get()-start<2000) swat_client_poll(&clients[0]);
    assert(clients[0].status==SWAT_NET_FAILED && strstr(clients[0].error,"host closed"));
    swat_client_close(&clients[0]);
    authority.config.mission=SWAT_HOUSE;
    assert(swat_server_open(&server,&authority,port,true));
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port)); ready(&server,clients,1);
    assert(replicas[0].config.mission==SWAT_HOUSE && replicas[0].world.count==authority.world.count);
    inputs[0]=swat_neutral_input(); inputs[0].loadout=2; inputs[0].inspect=true;
    host=swat_neutral_input(); ticks(&server,clients,inputs,1,6,&host); synchronize(&server,clients,1);
    assert(authority.actors[3].arsenal.primary==2 && replicas[0].actors[3].gear.inspecting);
    int broken=-1;
    for(int i=0;i<authority.world.count;i++) if(authority.world.objects[i].part==SWAT_PART_SKIN) { broken=i; break; }
    assert(broken>=0 && swat_world_damage(&authority.world,broken,1000));
    swat_sim_damage_region(&authority,3,1,15,SWAT_LEGS,false);
    authority.actors[2].gear.surrendered=authority.actors[2].gear.restrained=true;
    inputs[0].loadout=0; ticks(&server,clients,inputs,1,6,&host); synchronize(&server,clients,1);
    assert(!replicas[0].world.objects[broken].active && replicas[0].actors[3].gear.wounds[SWAT_LEGS]>0);
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port)); ready(&server,clients,2);
    assert(!replicas[1].world.objects[broken].active && replicas[1].actors[2].gear.restrained);
    assert(replicas[1].actors[3].arsenal.primary==2 && replicas[1].actors[3].gear.wounds[SWAT_LEGS]>0);
    puts("PASS real UDP house: fragmented full map/snapshots, authority kit/tool input, wounds, cuffs and destroyed faces at late join");
    inputs[0]=inputs[1]=swat_neutral_input(); inputs[0].throwable=2;
    host.sniper_order=SWAT_SNIPER_ASSIGN; host.sniper_post=2;
    ticks(&server,clients,inputs,2,6,&host); synchronize(&server,clients,2);
    assert(authority.snipers[0].deployed && authority.projectiles[0].active && !authority.projectiles[0].detonated);
    for(int i=0;i<2;i++) {
        assert(replicas[i].snipers[0].post==2 && replicas[i].actors[9].role==SWAT_SNIPER);
        assert(replicas[i].actors[3].gear.gas_grenades==1 && replicas[i].projectiles[0].active);
        assert(b3Distance(replicas[i].projectiles[0].position,authority.projectiles[0].position)<1e-4f);
    }
    host=swat_neutral_input(); inputs[0].throwable=0;
    ticks(&server,clients,inputs,2,120,&host); synchronize(&server,clients,2);
    assert(authority.projectiles[0].detonated);
    assert(swat_client_open(&clients[2],&replicas[2],"127.0.0.1",port)); ready(&server,clients,3);
    assert(replicas[2].projectiles[0].active && replicas[2].projectiles[0].detonated && replicas[2].snipers[0].deployed);
    puts("PASS real UDP tactical: leader sniper assignment, authoritative canister flight/ammunition and active gas cloud at late join");

    int entry=-1;
    for(int i=0;i<authority.world.count;i++) if(authority.world.objects[i].door && authority.world.objects[i].locked) { entry=i; break; }
    assert(entry>=0); SwatObject* leaf=&authority.world.objects[entry];
    officer=&authority.actors[3].controller;
    b3Body_SetTransform(officer->body.body,(b3Pos){leaf->center.x-.9f,officer->body.totalHeight*.5f+.02f,leaf->center.z},b3Quat_identity);
    b3Body_SetLinearVelocity(officer->body.body,swat_v(0,0,0)); officer->yaw=officer->pitch=0;
    // A server scheduling stall must not acknowledge and discard a queued
    // command. Silence may expire a repeated hold only after its queue drains.
    SwatInput pick=swat_neutral_input();pick.door_tool=SWAT_LOCKPICK;
    assert(swat_client_input(&clients[0],&pick));start=enet_time_get();
    SwatRemoteSlot* remote=&server.slots[clients[0].slot];
    while(remote->received<clients[0].sequence && enet_time_get()-start<1000)idle(&server,clients,3);
    assert(remote->received==clients[0].sequence && remote->count==1);
    remote->last_receive_ms=enet_time_get()-1000;
    swat_server_tick(&server,&host);
    assert(remote->ack==clients[0].sequence && authority.actors[3].gear.door_ticks==1);
    swat_server_tick(&server,&host);
    assert(!authority.actors[3].gear.door_ticks);
    puts("PASS real UDP delayed input: queued command executes once, expired repeated hold releases");
    inputs[0].door_tool=SWAT_LOCKPICK; ticks(&server,clients,inputs,3,46,&host); synchronize(&server,clients,3);
    assert(leaf->locked && authority.actors[3].gear.door_ticks>40 && replicas[1].actors[3].gear.door_ticks>40);
    inputs[0]=swat_neutral_input(); ticks(&server,clients,inputs,3,4,&host);
    assert(!authority.actors[3].gear.door_ticks);
    inputs[0].door_tool=SWAT_LOCKPICK; ticks(&server,clients,inputs,3,184,&host); synchronize(&server,clients,3);
    assert(!leaf->locked && !leaf->door_open && !replicas[2].world.objects[entry].locked);
    inputs[0].door_tool=SWAT_PLACE_CHARGE; ticks(&server,clients,inputs,3,94,&host); synchronize(&server,clients,3);
    assert(leaf->breach_owner==3 && authority.actors[3].gear.breaching_charges==0);
    inputs[0]=swat_neutral_input(); inputs[1].door_tool=SWAT_DETONATE_CHARGE;
    ticks(&server,clients,inputs,3,6,&host); assert(leaf->active && leaf->breach_owner==3);
    inputs[1]=swat_neutral_input();
    swat_client_close(&clients[2]);
    for(int i=0;i<20;i++) idle(&server,clients,2);
    assert(swat_client_open(&clients[2],&replicas[2],"127.0.0.1",port)); ready(&server,clients,3);
    assert(replicas[2].world.objects[entry].breach_owner==3 && replicas[2].actors[3].gear.breaching_charges==0);
    b3Body_SetTransform(officer->body.body,(b3Pos){0,officer->body.totalHeight*.5f+.02f,-2},b3Quat_identity);
    b3Body_SetLinearVelocity(officer->body.body,swat_v(0,0,0));
    inputs[0].door_tool=SWAT_DETONATE_CHARGE; ticks(&server,clients,inputs,3,6,&host); synchronize(&server,clients,3);
    assert(!leaf->active && leaf->breach_ticks>0);
    for(int i=0;i<3;i++) assert(!replicas[i].world.objects[entry].active && replicas[i].world.objects[entry].breach_owner<0);
    inputs[0]=swat_neutral_input();
    puts("PASS real UDP door tools: interrupted/finished remote pick, finite mounted charge, owner-only detonation, rejoin baseline and shared breach collision");

    SwatConfig generated=authority.config; generated.mission=SWAT_GENERATED; generated.layout_seed=947; generated.difficulty=2;
    uint32_t old_epoch=server.epoch;
    raw_scenario(&clients[0],old_epoch,&generated); // Listen host remains the leader.
    for(int i=0;i<20;i++) idle(&server,clients,3);
    assert(server.epoch==old_epoch && authority.config.mission==SWAT_HOUSE);
    swat_server_scenario(&server,&generated); ready(&server,clients,3);
    for(int i=0;i<3;i++) {
        assert(clients[i].epoch==old_epoch+1 && replicas[i].config.mission==SWAT_GENERATED);
        assert(replicas[i].layout.fingerprint==authority.layout.fingerprint && replicas[i].layout.policy_id==authority.layout.policy_id);
        assert(replicas[i].world.count==authority.world.count && replicas[i].config.difficulty==2);
        assert(!replicas[i].snipers[0].deployed && !replicas[i].projectiles[0].active);
    }
    for(int i=0;i<3;i++) swat_client_close(&clients[i]);
    swat_server_close(&server);
    // Dedicated sessions accept the authenticated remote leader's scenario request.
    assert(swat_server_open(&server,&authority,port,false));
    assert(swat_client_open(&clients[0],&replicas[0],"127.0.0.1",port)); ready(&server,clients,1);
    generated.layout_seed=948; generated.difficulty=0; old_epoch=server.epoch;
    swat_client_scenario(&clients[0],&generated); start=enet_time_get();
    while(server.epoch==old_epoch && enet_time_get()-start<2000) idle(&server,clients,1);
    assert(server.epoch==old_epoch+1); ready(&server,clients,1);
    assert(replicas[0].layout.seed==948 && swat_sim_hostiles(&replicas[0])==1);
    assert(swat_client_open(&clients[1],&replicas[1],"127.0.0.1",port)); ready(&server,clients,2);
    assert(replicas[1].layout.fingerprint==authority.layout.fingerprint && replicas[1].mission.overwatch_count==authority.mission.overwatch_count);
    raw_scenario(&clients[0],old_epoch,&config);
    for(int i=0;i<20;i++) idle(&server,clients,2);
    assert(authority.config.mission==SWAT_GENERATED && authority.config.layout_seed==948);
    swat_client_close(&clients[0]); swat_client_close(&clients[1]); swat_server_close(&server);
    puts("PASS real UDP generated houses: leader-only scenario changes, epoch cleanup, exact token/seed/model metadata and generated-map late join");
    for(int i=0;i<5;i++) swat_sim_close(&replicas[i]);
    swat_sim_close(&authority);
    puts("PASS real UDP: listen host uses the same authority and closes joining clients cleanly");
    return 0;
}
