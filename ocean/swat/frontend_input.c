#include "frontend.h"
#include <stdio.h>

bool swat_frontend_playing(const SwatFrontend* app) {
    return (app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_SCOPE) && IsWindowFocused() && !app->quit;
}

void swat_frontend_cancel_squad(SwatFrontend* app) {
    app->squad_pending=0;app->squad_release=false;
}
void swat_frontend_squad_request(SwatFrontend* app,int order,bool queued) {
    if(order<=0 || order>=SWAT_SQUAD_ORDERS)return;
    if(!app->leader) {
        snprintf(app->notice,sizeof(app->notice),"Only the team leader can issue squad orders.");app->notice_seconds=3;return;
    }
    app->squad_pending=order;app->squad_pending_team=app->squad_team;app->squad_queue=queued;
}
void swat_frontend_input_submitted(SwatFrontend* app,SwatInput* input) {
    if(input->squad_order) {
        if(app->squad_pending==input->squad_order && app->squad_pending_team==input->squad_team && app->squad_queue==input->squad_queue)
            app->squad_pending=0;
        app->squad_release=true;
    } else app->squad_release=false;
    input->squad_order=0;
}

SwatInput swat_frontend_input(SwatFrontend* app, const SwatSim* sim) {
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

