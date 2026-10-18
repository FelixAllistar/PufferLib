// Real frontend input collection with inert OS input; no window, renderer or GPU.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define CloseWindow WinCloseWindow
#define ShowCursor WinShowCursor
#define Rectangle WinRectangle
#define DrawText WinDrawText
#define LoadImage WinLoadImage
#include "enet/enet.h"
#undef CloseWindow
#undef ShowCursor
#undef Rectangle
#undef DrawText
#undef DrawTextEx
#undef LoadImage
#else
#include "enet/enet.h"
#endif
#include "frontend.h"
#include "net.h"
#include "replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static SwatFrontend app;
static bool keys[512],buttons[8];
static Vector2 mouse;
static FILE* trace;
static const char* journal_path;
static int test_case;
bool IsWindowFocused(void){return true;}
Vector2 GetMouseDelta(void){return mouse;}
bool IsKeyDown(int key){return key>=0 && key<512 && keys[key];}
bool IsKeyPressed(int key){return false;}
bool IsMouseButtonDown(int button){return button>=0 && button<8 && buttons[button];}

static void fixture(void) {
    memset(&sim,0,sizeof(sim));memset(&app,0,sizeof(app));memset(keys,0,sizeof(keys));memset(buttons,0,sizeof(buttons));mouse=(Vector2){0};
    sim.config=swat_default_config();sim.config.mission=SWAT_HOUSE;sim.config.tactical_rules=true;sim.config.hostile_fire=false;sim.config.max_ticks=2000;
    sim.extraction=(b3Pos){-10,0,-10};sim.reset_seed=42;swat_world_init(&sim.world);
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(12,.5f,12),SWAT_CONCRETE,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
    swat_sim_spawn_actor(&sim,1,SWAT_CIVILIAN,(b3Pos){4,0,0},SWAT_PI);
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,(b3Pos){0,0,4},-SWAT_PI*.5f);
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;
    for(int i=3;i<6;i++) {
        swat_sim_spawn_actor(&sim,i,SWAT_OFFICER,(b3Pos){-2,0,(i-3)*2-2},0);
        sim.actors[i].mind.bot=true;sim.actors[i].mind.team=i==3?1:2;sim.actors[i].mind.order=SWAT_ORDER_HOLD;
    }
    sim.actor_count=6;
    for(int t=0;t<10;t++)swat_sim_step(&sim,&(SwatInput){0});
    b3Vec3 aim=b3SubPos(swat_controller_eye(&sim.actors[1].controller),swat_controller_eye(&sim.actors[0].controller));
    sim.actors[0].controller.yaw=atan2f(aim.z,aim.x);sim.actors[0].controller.pitch=atan2f(aim.y,hypotf(aim.x,aim.z));
    app.screen=SWAT_SCREEN_GAME;app.captured=true;app.leader=true;app.settings.sensitivity=.08f;app.settings.ads_multiplier=1;app.settings.vertical_multiplier=1;
}
static void local_step(SwatInput* in) {
    swat_sim_step(&sim,in);
    if(trace)assert(fprintf(trace,"%d %d %08x\n",test_case,sim.tick,swat_replay_digest(&sim))>0);
    swat_frontend_input_submitted(&app,in);
}
static void zero_tick(void) {
    fixture();swat_frontend_squad_request(&app,SWAT_ORDER_HOLD,false);int tick=sim.tick;
    for(int frame=0;frame<8;frame++) {
        SwatInput in=swat_frontend_input(&app,&sim);assert(in.squad_order==SWAT_ORDER_HOLD && app.squad_pending==SWAT_ORDER_HOLD && sim.tick==tick);
    }
    SwatInput in=swat_frontend_input(&app,&sim);local_step(&in);
    assert(!app.squad_pending && !in.squad_order);
    swat_sim_close(&sim);puts("PASS frontend input: repeated active order survives render frames without a tick");
}
static void multiple_ticks(void) {
    fixture();app.squad_team=1;swat_frontend_squad_request(&app,SWAT_ORDER_CUFF,true);
    SwatInput in=swat_frontend_input(&app,&sim);int issued=sim.tick,deadline=0;
    for(int t=0;t<6;t++) {
        assert(in.squad_order==(t?0:SWAT_ORDER_CUFF));local_step(&in);
        assert(sim.actors[3].mind.pending_order==SWAT_ORDER_CUFF && sim.actors[3].mind.pending_actor==2 && sim.actors[3].mind.queued);
        if(!t)deadline=sim.actors[3].mind.command_tick;
        assert(deadline>issued && sim.actors[3].mind.command_tick==deadline);
    }
    assert(!app.squad_pending && !app.squad_release);
    swat_sim_close(&sim);puts("PASS frontend input: one order pulse across multiple fixed ticks preserves the queued target");
}
static void repeated_target(void) {
    fixture();app.squad_team=1;swat_frontend_squad_request(&app,SWAT_ORDER_CUFF,true);
    SwatInput in=swat_frontend_input(&app,&sim);local_step(&in);assert(sim.actors[3].mind.pending_actor==2);
    // Fixture view change: the next request names another real visible person.
    sim.actors[0].controller.yaw=SWAT_PI*.5f;
    swat_frontend_squad_request(&app,SWAT_ORDER_CUFF,true);
    for(int frame=0;frame<4;frame++){in=swat_frontend_input(&app,&sim);assert(!in.squad_order && app.squad_pending==SWAT_ORDER_CUFF);}
    local_step(&in);assert(sim.actors[3].mind.pending_actor==2);
    in=swat_frontend_input(&app,&sim);assert(in.squad_order==SWAT_ORDER_CUFF);local_step(&in);
    assert(sim.actors[3].mind.pending_actor==3 && sim.actors[3].mind.queued && !app.squad_pending);
    swat_sim_close(&sim);puts("PASS frontend input: adjacent same-order requests receive a neutral tick and retarget the officer");
}
static void rejected_target(void) {
    fixture();app.squad_team=1;sim.actors[0].controller.yaw=-SWAT_PI*.5f;
    swat_frontend_squad_request(&app,SWAT_ORDER_SEARCH,true);SwatInput in=swat_frontend_input(&app,&sim);local_step(&in);
    assert(!app.squad_pending && !sim.actors[3].mind.pending_order);
    // A weapon becomes available after the rejected request. It must not turn
    // that old click into an order when the view later crosses it.
    SwatController* owner=&sim.actors[1].controller;
    b3Body_SetTransform(owner->body.body,(b3Pos){6,owner->body.totalHeight*.5f,2},b3Quat_identity);
    sim.evidence[1]=(SwatEvidence){.dropped=true,.actor=1,.position={4,.055f,0}};
    sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=atan2f(.055f-swat_controller_eye(&sim.actors[0].controller).y,4);
    for(int t=0;t<3;t++){in=swat_frontend_input(&app,&sim);assert(!in.squad_order);local_step(&in);assert(!sim.actors[3].mind.pending_order);}
    swat_frontend_squad_request(&app,SWAT_ORDER_SEARCH,true);in=swat_frontend_input(&app,&sim);local_step(&in);
    assert(sim.actors[3].mind.pending_order==SWAT_ORDER_SEARCH && sim.actors[3].mind.pending_actor==2 && !app.squad_pending);
    swat_sim_close(&sim);puts("PASS frontend input: rejected target consumes its click; a fresh request can select the later weapon");
}
static void selection(void) {
    fixture();app.squad_team=1;swat_frontend_squad_request(&app,SWAT_ORDER_HOLD,true);app.squad_team=2;
    SwatInput old=swat_frontend_input(&app,&sim);assert(old.squad_team==1 && old.squad_queue);
    swat_frontend_squad_request(&app,SWAT_ORDER_MOVE,false);
    swat_frontend_input_submitted(&app,&old);assert(app.squad_pending==SWAT_ORDER_MOVE && app.squad_pending_team==2 && !app.squad_queue);
    SwatInput in=swat_frontend_input(&app,&sim);assert(!in.squad_order);local_step(&in);
    in=swat_frontend_input(&app,&sim);assert(in.squad_order==SWAT_ORDER_MOVE && in.squad_team==2 && !in.squad_queue);
    swat_frontend_cancel_squad(&app);assert(!app.squad_pending && !app.squad_release);
    app.leader=false;swat_frontend_squad_request(&app,SWAT_ORDER_HOLD,false);assert(!app.squad_pending && app.notice_seconds>0);
    swat_sim_close(&sim);puts("PASS frontend input: team/queue capture, newer intent, cancellation and leader-only requests");
}
static void scope_and_failure(void) {
    fixture();app.squad_team=1;swat_frontend_squad_request(&app,SWAT_ORDER_HOLD,false);
    app.screen=SWAT_SCREEN_SCOPE;SwatInput in=swat_frontend_input(&app,&sim);assert(!in.squad_order);local_step(&in);assert(app.squad_pending==SWAT_ORDER_HOLD);
    app.screen=SWAT_SCREEN_GAME;in=swat_frontend_input(&app,&sim);SwatNetClient offline={0};
    bool submitted=swat_client_input(&offline,&in);if(submitted)swat_frontend_input_submitted(&app,&in);
    assert(!submitted && app.squad_pending==SWAT_ORDER_HOLD && in.squad_order==SWAT_ORDER_HOLD);
    SwatNetClient full={.status=SWAT_NET_ACTIVE};full.pending_count=(int)(sizeof(full.pending)/sizeof(full.pending[0]));
    submitted=swat_client_input(&full,&in);if(submitted)swat_frontend_input_submitted(&app,&in);
    assert(!submitted && full.status==SWAT_NET_FAILED && app.squad_pending==SWAT_ORDER_HOLD && in.squad_order==SWAT_ORDER_HOLD);
    in=swat_frontend_input(&app,&sim);local_step(&in);assert(!app.squad_pending);
    swat_sim_close(&sim);puts("PASS frontend input: scope suppression, offline transport and a full acknowledgement queue retain unsent intent");
}
static void controls(void) {
    fixture();keys[KEY_W]=keys[KEY_R]=keys[KEY_Z]=true;buttons[MOUSE_BUTTON_LEFT]=true;mouse=(Vector2){2,3};
    swat_frontend_squad_request(&app,SWAT_ORDER_HOLD,false);SwatInput in=swat_frontend_input(&app,&sim);
    assert(in.forward==1 && in.aim && in.fire && in.reload && in.yaw_delta && in.pitch_delta);
    float yaw=in.yaw_delta,pitch=in.pitch_delta;swat_frontend_input_submitted(&app,&in);
    assert(!in.squad_order && in.forward==1 && in.aim && in.fire && in.reload && in.yaw_delta==yaw && in.pitch_delta==pitch);
    swat_sim_close(&sim);puts("PASS frontend input: command consumption preserves movement, aim, fire, reload and mouse input");
}
static void recorded_input(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_HOUSE;cfg.randomize=false;cfg.hostile_fire=false;swat_config_human(&cfg);
    swat_sim_init(&sim,cfg,77);memset(&app,0,sizeof(app));memset(keys,0,sizeof(keys));memset(buttons,0,sizeof(buttons));mouse=(Vector2){0};
    app.screen=SWAT_SCREEN_GAME;app.captured=app.leader=true;
    SwatReplay recording;assert(swat_replay_record(&recording,journal_path,&sim));
    for(int t=0;t<60;t++) {
        if(t==0 || t==1){app.squad_team=t+1;swat_frontend_squad_request(&app,SWAT_ORDER_HOLD,t==0);}
        keys[KEY_J]=t==8;
        SwatInput in=swat_frontend_input(&app,&sim);swat_sim_step(&sim,&in);
        // The application records the consumed tick before preparing neutral
        // order input for another tick in this render frame.
        assert(swat_replay_append(&recording,&in,&sim));
        if(trace)assert(fprintf(trace,"%d %d %08x\n",test_case,sim.tick,swat_replay_digest(&sim))>0);
        swat_frontend_input_submitted(&app,&in);
    }
    assert(swat_replay_close(&recording));swat_sim_close(&sim);
    SwatReplay reader;assert(swat_replay_open(&reader,journal_path));swat_sim_init(&sim,reader.config,reader.seed);
    SwatInput in;uint32_t expected;int status,count=0;
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        assert(in.squad_order==((count==0 || count==2)?SWAT_ORDER_HOLD:0));
        if(count==0)assert(in.squad_team==1 && in.squad_queue);
        if(count==2)assert(in.squad_team==2 && !in.squad_queue);
        assert(in.squad_execute==(count==8));
        swat_sim_step(&sim,&in);assert(swat_replay_digest(&sim)==expected);count++;
    }
    assert(status==0 && count==60 && swat_replay_close(&reader));swat_sim_close(&sim);
    puts("PASS frontend input: real-house journal retains exact request/release/execute inputs and replays every state");
}

static void poll_net(SwatNetServer* server,SwatNetClient* client) {
    swat_server_poll(server);swat_client_poll(client);
    enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;enet_socket_wait(((ENetHost*)server->transport)->socket,&condition,1);
}
static uint32_t send_tick(SwatNetServer* server,SwatNetClient* client,SwatInput* in) {
    assert(swat_client_input(client,in));uint32_t sequence=client->sequence;
    swat_frontend_input_submitted(&app,in);
    uint32_t start=enet_time_get();while(server->slots[client->slot].received<sequence && enet_time_get()-start<2000)poll_net(server,client);
    assert(server->slots[client->slot].received>=sequence);swat_server_tick(server,NULL);return sequence;
}
static void ack(SwatNetServer* server,SwatNetClient* client,uint32_t sequence) {
    uint32_t start=enet_time_get(),pulse=start+20;
    while(client->ack<sequence && enet_time_get()-start<4000) {
        poll_net(server,client);
        // A live server continues publishing held neutral controls. Do not
        // expect the last unreliable snapshot of a stopped simulation to retry.
        uint32_t now=enet_time_get();if(now>=pulse){swat_server_tick(server,NULL);pulse=now+20;}
    }
    assert(client->ack>=sequence && client->pending_count==0);
}
static void network(void) {
    fixture();SwatNetServer server;SwatNetClient client={0};int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&server,&sim,port,false) && ++attempt<32)port=30000+(port-30000+4729)%25000;
    assert(server.transport);
    // Opening a server resets its episode. Establish this small physical
    // fixture afterward, before any peer receives its map or snapshot.
    swat_sim_close(&sim);fixture();
    assert(swat_client_open(&client,&replica,"127.0.0.1",port));
    uint32_t start=enet_time_get();while(client.status!=SWAT_NET_ACTIVE && enet_time_get()-start<5000)poll_net(&server,&client);
    assert(client.status==SWAT_NET_ACTIVE && client.slot==0 && !client.ack);
    app.squad_team=1;swat_frontend_squad_request(&app,SWAT_ORDER_CUFF,true);app.squad_team=2;
    SwatInput in=swat_frontend_input(&app,&replica);assert(in.squad_team==1 && in.squad_queue);
    assert(swat_client_input(&client,&in));uint32_t first=client.sequence;swat_frontend_input_submitted(&app,&in);
    assert(!app.squad_pending && !client.ack && !sim.actors[3].mind.pending_order);
    start=enet_time_get();while(server.slots[0].received<first && enet_time_get()-start<2000)poll_net(&server,&client);
    assert(server.slots[0].received==first);swat_server_tick(&server,NULL);
    assert(sim.actors[3].mind.pending_actor==2 && sim.actors[3].mind.queued);
    in=swat_frontend_input(&app,&replica);assert(!in.squad_order);uint32_t released=send_tick(&server,&client,&in);ack(&server,&client,released);
    assert(replica.actors[3].mind.pending_actor==2 && replica.actors[3].mind.queued);
    printf("FRONTEND_ACK first %u release %u target%d team%d queued%d\n",first,released,replica.actors[3].mind.pending_actor,replica.actors[3].mind.team,replica.actors[3].mind.queued);
    // Re-aim through ordinary reliable controller inputs before selecting the
    // second person; the server retains the real target-selection authority.
    for(int t=0;t<4;t++){in=swat_frontend_input(&app,&replica);in.yaw_delta=.4f;released=send_tick(&server,&client,&in);}ack(&server,&client,released);
    app.squad_team=1;swat_frontend_squad_request(&app,SWAT_ORDER_CUFF,true);app.squad_team=2;
    in=swat_frontend_input(&app,&replica);assert(in.squad_order==SWAT_ORDER_CUFF && in.squad_team==1);uint32_t second=send_tick(&server,&client,&in);
    assert(sim.actors[3].mind.pending_actor==3 && sim.actors[3].mind.queued);
    in=swat_frontend_input(&app,&replica);assert(!in.squad_order);released=send_tick(&server,&client,&in);ack(&server,&client,released);
    assert(replica.actors[3].mind.pending_actor==3 && replica.actors[3].mind.queued && !app.squad_pending);
    printf("FRONTEND_ACK second %u release %u target%d team%d queued%d\n",second,released,replica.actors[3].mind.pending_actor,replica.actors[3].mind.team,replica.actors[3].mind.queued);
    swat_client_close(&client);swat_server_close(&server);swat_sim_close(&replica);swat_sim_close(&sim);
    puts("PASS frontend network: reliable submission, delayed authoritative acknowledgements and repeated directed target identities");
}
int main(int argc,char** argv) {
    setvbuf(stdout,NULL,_IONBF,0);assert(argc<=3);
    if(argc>1 && !strcmp(argv[1],"network")){network();return 0;}
    char recording[1024];journal_path="frontend-input-test.sgrp";
    if(argc>1){assert(!strcmp(argv[1],"trace") && argc==3);trace=fopen(argv[2],"w");assert(trace);assert(snprintf(recording,sizeof(recording),"%s.sgrp",argv[2])<(int)sizeof(recording));journal_path=recording;}
    void(*cases[])(void)={zero_tick,multiple_ticks,repeated_target,rejected_target,selection,scope_and_failure,controls,recorded_input};
    for(test_case=0;test_case<8;test_case++)cases[test_case]();
    if(trace)assert(!fclose(trace));
    return 0;
}
