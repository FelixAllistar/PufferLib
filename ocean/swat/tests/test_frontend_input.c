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
static bool keys[512],buttons[8],key_pressed[512],button_pressed[8];
static Vector2 mouse;
static FILE* trace;
static const char* journal_path;
static int test_case;
bool IsWindowFocused(void){return true;}
Vector2 GetMouseDelta(void){return mouse;}
bool IsKeyDown(int key){return key>=0 && key<512 && keys[key];}
bool IsKeyPressed(int key){return key>=0 && key<512 && key_pressed[key];}
bool IsMouseButtonDown(int button){return button>=0 && button<8 && buttons[button];}
bool IsMouseButtonPressed(int button){return button>=0 && button<8 && button_pressed[button];}

static void fixture(void) {
    memset(&sim,0,sizeof(sim));memset(&app,0,sizeof(app));memset(keys,0,sizeof(keys));memset(buttons,0,sizeof(buttons));mouse=(Vector2){0};
    memset(key_pressed,0,sizeof(key_pressed));memset(button_pressed,0,sizeof(button_pressed));app.settings=swat_settings_defaults();
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
    app.settings=swat_settings_defaults();
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

// Verbatim copy of swat_frontend_input before the binding table, with its
// hard-coded keys. Default bindings must reproduce it exactly: the same input
// and the same frontend state on every frame.
static SwatInput legacy_frontend_input(SwatFrontend* app, const SwatSim* sim) {
    SwatInput in=swat_neutral_input();
    if(!swat_frontend_playing(app) || app->actor<0 ||
       app->actor>=sim->actor_count || !sim->actors[app->actor].present) return in;
    if(!app->captured && !app->camera_pointer) return in;
    // Do not feed pointer warps/focus changes or the menu's click into gameplay.
    Vector2 mouse=GetMouseDelta();
    if(app->discard_mouse_frames>0 || app->camera_pointer) {
        if(app->discard_mouse_frames>0) app->discard_mouse_frames--;
        mouse=(Vector2){0};
    }
    SwatLookDelta look=swat_settings_look(&app->settings,mouse.x,mouse.y,sim->actors[app->actor].controller.ads);
    in.yaw_delta=look.yaw; in.pitch_delta=look.pitch;
    in.forward=(float)(IsKeyDown(KEY_W)-IsKeyDown(KEY_S));
    in.strafe=(float)(IsKeyDown(KEY_D)-IsKeyDown(KEY_A));
    in.lean=(float)(IsKeyDown(KEY_E)-IsKeyDown(KEY_Q));
    in.crouch=IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C);
    in.gait=IsKeyDown(KEY_LEFT_SHIFT) ? SWAT_SPRINT :
        (IsKeyDown(KEY_LEFT_ALT) ? SWAT_SLOW : SWAT_WALK);
    in.jump=IsKeyDown(KEY_SPACE);
    if(!app->wait_for_release) {
        in.fire=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    }
    in.reload=IsKeyDown(KEY_R);
    in.cancel_reload=IsKeyDown(KEY_BACKSLASH) && IsKeyDown(KEY_LEFT_ALT);
    if(IsKeyPressed(KEY_HOME)) app->ready=app->ready==SWAT_LOW_READY ? SWAT_READY : SWAT_LOW_READY;
    if(IsKeyPressed(KEY_END)) app->ready=app->ready==SWAT_HIGH_READY ? SWAT_READY : SWAT_HIGH_READY;
    in.ready=app->ready;
    in.selector=IsKeyDown(KEY_V);
    bool equipment_modifier=IsKeyDown(KEY_LEFT_ALT);
    if(equipment_modifier && (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_TWO))) {
        app->selected_gadget=(app->selected_gadget+(IsKeyPressed(KEY_TWO) ? 1 : 7))%8;
        app->wait_for_release=true; in.fire=false;
    }
    in.weapon=equipment_modifier ? 0 : (IsKeyDown(KEY_ONE) ? 1 : (IsKeyDown(KEY_TWO) ? 2 : 0));
    if(in.weapon) app->selected_gadget=0;
    in.pepper_spray=IsKeyDown(KEY_SIX);
    in.inspect=IsKeyDown(KEY_G); in.command=IsKeyDown(KEY_Y); in.melee=IsKeyDown(KEY_B);
    in.throwable=IsKeyDown(KEY_FOUR) ? 1 : (IsKeyDown(KEY_FIVE) ? 2 : 0);
    in.taser=IsKeyDown(KEY_T);
    in.door_tool=IsKeyDown(KEY_K) ? SWAT_DETONATE_CHARGE :
        (IsKeyDown(KEY_SEVEN) ? SWAT_PLACE_CHARGE : (IsKeyDown(KEY_L) ? SWAT_LOCKPICK : SWAT_DOOR_NONE));
    if(app->screen==SWAT_SCREEN_GAME && !app->camera_pointer) {
        if(IsKeyDown(KEY_NINE)) in.door_tool=equipment_modifier ? SWAT_REMOVE_WEDGE : SWAT_WEDGE;
        if(IsKeyDown(KEY_EIGHT)) in.door_tool=SWAT_DISARM;
        SwatContext context=swat_context(sim,app->actor);
        bool use=IsKeyDown(KEY_F) || (!app->wait_for_release && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));
        bool door=context.hit.kind==SWAT_HIT_WORLD && context.action!=SWAT_CONTEXT_NONE && context.action!=SWAT_CONTEXT_WALL && context.action!=SWAT_CONTEXT_CHARGE && context.hit.distance<=2.2f;
        bool physical=door || context.action==SWAT_CONTEXT_EVIDENCE || context.action==SWAT_CONTEXT_SECURED || context.action==SWAT_CONTEXT_DEVICE || context.action==SWAT_CONTEXT_LIGHT;
        in.interact=use && physical; in.peek=equipment_modifier;
        in.command|=use && !physical;
        bool right=!app->wait_for_release && IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        if(!right && app->right_held && (app->right_action==SWAT_RIGHT_CUFF || app->right_action==SWAT_RIGHT_PICK) &&
           IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            app->wait_for_release=true; in.fire=false;
        }
        if(right && !app->right_held) {
            app->right_action=SWAT_RIGHT_AIM; app->right_target=context.hit.index;
            if(!IsKeyDown(KEY_Z) && context.ready) {
                if(context.action==SWAT_CONTEXT_CUFF) app->right_action=SWAT_RIGHT_CUFF;
                else if(context.action==SWAT_CONTEXT_LOCKED) app->right_action=SWAT_RIGHT_PICK;
            }
        }
        app->right_held=right;
        if(!right) { app->right_action=SWAT_RIGHT_NONE; app->right_target=-1; }
        in.aim=(right && app->right_action==SWAT_RIGHT_AIM) || IsKeyDown(KEY_Z);
        if(right && (app->right_action==SWAT_RIGHT_CUFF || app->right_action==SWAT_RIGHT_PICK)) {
            // Keep the chosen action for the entire press. Finishing a pick or
            // cuff, or looking away, must not turn that same press into ADS.
            in.aim=in.fire=in.reload=in.melee=false;
            bool same=context.ready && context.hit.index==app->right_target;
            if(app->right_action==SWAT_RIGHT_CUFF) in.interact=same && context.action==SWAT_CONTEXT_CUFF;
            else in.door_tool=same && context.action==SWAT_CONTEXT_LOCKED ? SWAT_LOCKPICK : SWAT_DOOR_NONE;
        }
    } else if(!app->wait_for_release) in.aim=IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if(app->selected_gadget && app->screen==SWAT_SCREEN_GAME) {
        bool use=in.fire; in.fire=in.reload=false;
        if(app->selected_gadget==1) in.pepper_spray|=use;
        else if(app->selected_gadget>=4) { in.device_deploy=use ? app->selected_gadget-3 : 0; }
        else if(use && !app->gadget_door_action) {
            SwatContext target=swat_context(sim,app->actor);
            app->gadget_door_action=app->selected_gadget==3 ? SWAT_DISARM :
                (target.action==SWAT_CONTEXT_WEDGED ? SWAT_REMOVE_WEDGE : SWAT_WEDGE);
        }
        if(app->selected_gadget>1 && app->selected_gadget<4 && use) in.door_tool=app->gadget_door_action;
        if(!use) app->gadget_door_action=0;
    }
    if(app->camera_pointer) in=swat_neutral_input();
    in.squad_order=app->squad_release?0:app->squad_pending;
    in.squad_team=app->squad_pending?app->squad_pending_team:app->squad_team;in.squad_queue=app->squad_queue;
    in.squad_execute=IsKeyDown(KEY_J);
    if(app->loadout_pending) {
        in.loadout=app->selected_kit+1;
        if(sim->actors[app->actor].gear.kit==app->selected_kit) app->loadout_pending=false;
    }
    if(app->profiles_pending) {
        in.primary_profile=app->selected_primary+1; in.sight_profile=app->selected_sight+1;
        in.magazine_inventory=app->magazine_inventory;
        if(sim->actors[app->actor].arsenal.primary==app->selected_primary && sim->actors[app->actor].arsenal.sight==app->selected_sight &&
           (sim->actors[app->actor].arsenal.slots[0].use_magazines==app->magazine_inventory)) app->profiles_pending=false;
    }
    if(app->screen==SWAT_SCREEN_SCOPE) {
        SwatInput scope=swat_neutral_input();
        if(app->selected_sniper>=SWAT_SNIPERS) {
            scope.device_control=true; scope.device_unit=app->selected_sniper-SWAT_SNIPERS;
            scope.yaw_delta=in.yaw_delta; scope.pitch_delta=in.pitch_delta; scope.forward=in.forward; scope.strafe=in.strafe;
            scope.jump=in.jump; scope.crouch=in.crouch; scope.command=IsKeyDown(KEY_F) || IsKeyDown(KEY_Y);
        } else {
        scope.sniper_control=true; scope.sniper_unit=app->selected_sniper;
        scope.yaw_delta=in.yaw_delta*.22f; scope.pitch_delta=in.pitch_delta*.22f;
        scope.fire=in.fire; scope.reload=in.reload;
        scope.sniper_order=app->camera_pointer ? 0 : ((IsKeyDown(KEY_Y) || IsKeyDown(KEY_F)) ? SWAT_SNIPER_DESIGNATE :
            (IsKeyDown(KEY_SPACE) ? SWAT_SNIPER_EXECUTE : (IsKeyDown(KEY_H) ? SWAT_SNIPER_HOLD : 0)));
        }
        in=scope;
    } else if(app->leader && !app->camera_pointer) {
        in.sniper_order=IsKeyDown(KEY_X) ? SWAT_SNIPER_EXECUTE : (IsKeyDown(KEY_H) ? SWAT_SNIPER_HOLD : 0);
    }
    if(app->leader) for(int i=0;i<SWAT_SNIPERS;i++) if(app->sniper_pending[i]) {
        const SwatSniper* sniper=&sim->snipers[i];
        if(sniper->deployed && sniper->post==app->sniper_post[i] && sniper->rifle==app->sniper_rifle[i]) {
            app->sniper_pending[i]=false; continue;
        }
        bool unavailable=app->sniper_post[i]>=sim->mission.overwatch_count ||
            (sniper->deployed && (!sim->actors[swat_sniper_actor(i)].alive ||
             (sim->actors[swat_sniper_actor(i)].arsenal.shots>0 && sniper->rifle!=app->sniper_rifle[i])));
        for(int other=0;other<SWAT_SNIPERS;other++) if(other!=i && sim->snipers[other].deployed &&
            sim->snipers[other].post==app->sniper_post[i]) unavailable=true;
        if(unavailable) {
            app->sniper_pending[i]=false;
            snprintf(app->notice,sizeof(app->notice),"Sniper assignment unavailable this round."); continue;
        }
        in.sniper_order=SWAT_SNIPER_ASSIGN; in.sniper_unit=i;
        in.sniper_post=app->sniper_post[i]; in.sniper_rifle=app->sniper_rifle[i];
        in.sniper_control=app->screen==SWAT_SCREEN_SCOPE; in.fire=false;
        if(in.sniper_control) in.yaw_delta=in.pitch_delta=0;
        break;
    }
    if(app->loadout_pending) in.loadout=app->selected_kit+1;
    return in;
}

static bool same_input(const SwatInput* a,const SwatInput* b) {
#define SAME(field) if(a->field!=b->field){fprintf(stderr,"input field %s differs\n",#field);return false;}
    SAME(forward)SAME(strafe)SAME(yaw_delta)SAME(pitch_delta)SAME(lean)SAME(gait)SAME(crouch)SAME(jump)SAME(aim)
    SAME(fire)SAME(reload)SAME(interact)SAME(selector)SAME(weapon)SAME(inspect)SAME(command)SAME(melee)SAME(loadout)
    SAME(throwable)SAME(taser)SAME(pepper_spray)SAME(peek)SAME(door_tool)SAME(sniper_order)SAME(sniper_unit)
    SAME(sniper_post)SAME(sniper_rifle)SAME(sniper_control)SAME(ready)SAME(cancel_reload)SAME(primary_profile)
    SAME(sight_profile)SAME(magazine_inventory)SAME(squad_order)SAME(squad_team)SAME(squad_queue)SAME(squad_execute)
    SAME(device_deploy)SAME(device_unit)SAME(device_control)
#undef SAME
    return true;
}
static bool same_state(const SwatFrontend* a,const SwatFrontend* b) {
    return a->ready==b->ready && a->selected_gadget==b->selected_gadget && a->wait_for_release==b->wait_for_release &&
        a->right_held==b->right_held && a->right_action==b->right_action && a->right_target==b->right_target &&
        a->gadget_door_action==b->gadget_door_action && a->squad_pending==b->squad_pending &&
        a->squad_release==b->squad_release && a->loadout_pending==b->loadout_pending &&
        a->profiles_pending==b->profiles_pending && a->discard_mouse_frames==b->discard_mouse_frames;
}
static SwatFrontend legacy_app,base_app;
static void compare_frame(int label) {
    SwatInput expected=legacy_frontend_input(&legacy_app,&sim),actual=swat_frontend_input(&app,&sim);
    if(!same_input(&expected,&actual) || !same_state(&legacy_app,&app)) {
        fprintf(stderr,"default bindings differ from the legacy keys in case %d\n",label); assert(false);
    }
}
#define MOUSE_CODE(button) (SWAT_BIND_MOUSE+(button))
static const int default_codes[SWAT_BIND_ACTIONS][SWAT_BIND_SLOTS]={
    [SWAT_BIND_FORWARD]={KEY_W},[SWAT_BIND_BACK]={KEY_S},[SWAT_BIND_LEFT]={KEY_A},[SWAT_BIND_RIGHT]={KEY_D},
    [SWAT_BIND_LEAN_LEFT]={KEY_Q},[SWAT_BIND_LEAN_RIGHT]={KEY_E},[SWAT_BIND_CROUCH]={KEY_LEFT_CONTROL,KEY_C},
    [SWAT_BIND_SPRINT]={KEY_LEFT_SHIFT},[SWAT_BIND_MODIFIER]={KEY_LEFT_ALT},[SWAT_BIND_JUMP]={KEY_SPACE},
    [SWAT_BIND_FIRE]={MOUSE_CODE(MOUSE_BUTTON_LEFT)},[SWAT_BIND_AIM_CONTEXT]={MOUSE_CODE(MOUSE_BUTTON_RIGHT)},
    [SWAT_BIND_AIM]={KEY_Z},[SWAT_BIND_USE]={KEY_F,MOUSE_CODE(MOUSE_BUTTON_MIDDLE)},[SWAT_BIND_RELOAD]={KEY_R},
    [SWAT_BIND_CANCEL_RELOAD]={KEY_BACKSLASH},[SWAT_BIND_PRIMARY]={KEY_ONE},[SWAT_BIND_SIDEARM]={KEY_TWO},
    [SWAT_BIND_FLASHBANG]={KEY_FOUR},[SWAT_BIND_GAS]={KEY_FIVE},[SWAT_BIND_PEPPER_SPRAY]={KEY_SIX},
    [SWAT_BIND_PLACE_CHARGE]={KEY_SEVEN},[SWAT_BIND_DISARM]={KEY_EIGHT},[SWAT_BIND_WEDGE]={KEY_NINE},
    [SWAT_BIND_DETONATE]={KEY_K},[SWAT_BIND_LOCKPICK]={KEY_L},[SWAT_BIND_TASER]={KEY_T},[SWAT_BIND_INSPECT]={KEY_G},
    [SWAT_BIND_COMMAND]={KEY_Y},[SWAT_BIND_MELEE]={KEY_B},[SWAT_BIND_SELECTOR]={KEY_V},
    [SWAT_BIND_LOW_READY]={KEY_HOME},[SWAT_BIND_HIGH_READY]={KEY_END},[SWAT_BIND_SQUAD_MENU]={KEY_M},
    [SWAT_BIND_SQUAD_EXECUTE]={KEY_J},[SWAT_BIND_SNIPER_EXECUTE]={KEY_X},[SWAT_BIND_SNIPER_HOLD]={KEY_H},
    [SWAT_BIND_CAMERA]={KEY_N},[SWAT_BIND_PLANNING]={KEY_P},
};
static void hold(int code,bool down,bool edge) {
    if(code>=SWAT_BIND_MOUSE){buttons[code-SWAT_BIND_MOUSE]=down;button_pressed[code-SWAT_BIND_MOUSE]=edge;}
    else {keys[code]=down;key_pressed[code]=edge;}
}
static void release_all(void){memset(keys,0,sizeof(keys));memset(buttons,0,sizeof(buttons));memset(key_pressed,0,sizeof(key_pressed));memset(button_pressed,0,sizeof(button_pressed));}
static void default_bindings(void) {
    SwatSettings defaults=swat_settings_defaults();
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) for(int s=0;s<SWAT_BIND_SLOTS;s++) assert(defaults.bindings[a][s]==default_codes[a][s]);
    assert(!swat_bindings_first_conflict(&defaults,NULL,NULL,NULL));
    // Names resolve to raylib's own values, so a saved name means the same key.
    assert(swat_bind_code_parse("ctrl")==KEY_LEFT_CONTROL && swat_bind_code_parse("Right Arrow")==KEY_RIGHT);
    assert(swat_bind_code_parse("Keypad Enter")==KEY_KP_ENTER && swat_bind_code_parse("F12")==KEY_F12);
    assert(swat_bind_code_parse("Menu")==KEY_KB_MENU && swat_bind_code_parse("Grave")==KEY_GRAVE);
    assert(swat_bind_code_parse("Page Down")==KEY_PAGE_DOWN && swat_bind_code_parse("Keypad Equal")==KEY_KP_EQUAL);
    assert(swat_bind_code_parse("Mouse 7")==MOUSE_CODE(MOUSE_BUTTON_BACK));
    assert(swat_bind_code_reserved(KEY_ESCAPE) && swat_bind_code_reserved(KEY_TAB) && swat_bind_code_reserved(KEY_F5));
    int codes[160],count=0;
    for(int code=1;code<SWAT_BIND_MOUSE+SWAT_BIND_MOUSE_BUTTONS;code++) if(swat_bind_code_name(code)) {assert(count<160);codes[count++]=code;}
    fixture();base_app=app;
    // Every named key and button alone, with and without the modifier, on foot
    // and in the scope, and while a UI click is still waiting for release.
    int label=0;
    for(int scope=0;scope<2;scope++) for(int wait=0;wait<2;wait++) for(int alt=0;alt<2;alt++) for(int i=0;i<count;i++) {
        app=base_app;release_all();mouse=(Vector2){3,-2};
        app.screen=scope ? SWAT_SCREEN_SCOPE : SWAT_SCREEN_GAME;app.wait_for_release=wait;
        hold(codes[i],true,true);if(alt)hold(KEY_LEFT_ALT,true,true);
        legacy_app=app;compare_frame(label++);
    }
    // Long random chords exercise held state: ready toggles, gadget cycling,
    // RMB cuff/pick ownership and the UI-release gate.
    static const int pool[]={KEY_W,KEY_S,KEY_A,KEY_D,KEY_Q,KEY_E,KEY_LEFT_CONTROL,KEY_C,KEY_LEFT_SHIFT,KEY_LEFT_ALT,KEY_SPACE,
        KEY_Z,KEY_F,KEY_R,KEY_BACKSLASH,KEY_ONE,KEY_TWO,KEY_FOUR,KEY_FIVE,KEY_SIX,KEY_SEVEN,KEY_EIGHT,KEY_NINE,KEY_K,KEY_L,
        KEY_T,KEY_G,KEY_Y,KEY_B,KEY_V,KEY_HOME,KEY_END,KEY_J,KEY_X,KEY_H,KEY_UP,KEY_I,
        MOUSE_CODE(MOUSE_BUTTON_LEFT),MOUSE_CODE(MOUSE_BUTTON_RIGHT),MOUSE_CODE(MOUSE_BUTTON_MIDDLE),MOUSE_CODE(MOUSE_BUTTON_SIDE)};
    const int pool_count=(int)(sizeof(pool)/sizeof(pool[0]));
    uint32_t rng=12345;
    for(int scope=0;scope<2;scope++) {
        app=base_app;release_all();app.screen=scope ? SWAT_SCREEN_SCOPE : SWAT_SCREEN_GAME;legacy_app=app;
        for(int frame=0;frame<1500;frame++) {
            for(int i=0;i<pool_count;i++) {
                rng=rng*1664525u+1013904223u;
                bool down=pool[i]>=SWAT_BIND_MOUSE ? buttons[pool[i]-SWAT_BIND_MOUSE] : keys[pool[i]];
                bool flip=(rng>>24)<(down ? 40u : 12u);
                hold(pool[i],down!=flip,!down && flip);
            }
            rng=rng*1664525u+1013904223u;
            mouse=(Vector2){(float)((rng>>8)%9)-4.0f,(float)((rng>>16)%7)-3.0f};
            int gate=(int)((rng>>4)&63);
            if(gate==0) legacy_app.wait_for_release=app.wait_for_release=true;
            else if(gate<4) legacy_app.wait_for_release=app.wait_for_release=false;
            bool pointer=((rng>>12)&31)==0;legacy_app.camera_pointer=app.camera_pointer=pointer;
            compare_frame(100000+scope*10000+frame);
        }
    }
    swat_sim_close(&sim);
    puts("PASS frontend bindings: defaults match raylib keys and reproduce the hard-coded input exactly, alone and in held chords");
}
static void rebound_controls(void) {
    fixture();
    app.settings.bindings[SWAT_BIND_FORWARD][0]=KEY_UP;
    app.settings.bindings[SWAT_BIND_FIRE][0]=MOUSE_CODE(MOUSE_BUTTON_SIDE);
    app.settings.bindings[SWAT_BIND_COMMAND][0]=KEY_U;
    keys[KEY_W]=true;buttons[MOUSE_BUTTON_LEFT]=true;
    SwatInput in=swat_frontend_input(&app,&sim);assert(in.forward==0 && !in.fire);
    release_all();keys[KEY_UP]=true;buttons[MOUSE_BUTTON_SIDE]=true;
    in=swat_frontend_input(&app,&sim);assert(in.forward==1 && in.fire);
    app.wait_for_release=true;in=swat_frontend_input(&app,&sim);
    assert(in.forward==1 && !in.fire); // A rebound mouse button still waits out a UI click.
    app.wait_for_release=false;release_all();app.screen=SWAT_SCREEN_SCOPE;app.selected_sniper=0;
    keys[KEY_Y]=true;in=swat_frontend_input(&app,&sim);assert(!in.sniper_order);
    release_all();keys[KEY_U]=true;in=swat_frontend_input(&app,&sim);assert(in.sniper_order==SWAT_SNIPER_DESIGNATE);
    swat_sim_close(&sim);puts("PASS frontend bindings: rebound keys and buttons replace the defaults on foot and in the scope");
}
static void toggles(void) {
    fixture();app.settings.crouch_toggle=app.settings.aim_toggle=true;
    const bool crouch_down[]={true,true,false,true,false,false},crouch_expected[]={true,true,true,false,false,false};
    for(int f=0;f<6;f++) {
        keys[KEY_C]=crouch_down[f];keys[KEY_Z]=crouch_down[f];
        SwatInput in=swat_frontend_input(&app,&sim);
        assert(in.crouch==crouch_expected[f] && in.aim==crouch_expected[f]);
    }
    // A pause or menu between presses requires a release before the next flip.
    keys[KEY_C]=true;SwatInput in=swat_frontend_input(&app,&sim);assert(in.crouch);
    app.captured=false;assert(!swat_frontend_input(&app,&sim).crouch);app.captured=true;
    in=swat_frontend_input(&app,&sim);assert(in.crouch);
    keys[KEY_C]=false;in=swat_frontend_input(&app,&sim);assert(in.crouch);
    app.settings.crouch_toggle=false;in=swat_frontend_input(&app,&sim);assert(!in.crouch);
    swat_sim_close(&sim);puts("PASS frontend bindings: toggle crouch and toggle aim latch on fresh presses and hold resumes when disabled");
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
    void(*cases[])(void)={zero_tick,multiple_ticks,repeated_target,rejected_target,selection,scope_and_failure,controls,recorded_input,
        default_bindings,rebound_controls,toggles};
    for(test_case=0;test_case<(int)(sizeof(cases)/sizeof(cases[0]));test_case++)cases[test_case]();
    if(trace)assert(!fclose(trace));
    return 0;
}
