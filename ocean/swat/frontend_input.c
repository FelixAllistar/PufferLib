#include "frontend.h"
#include <stdio.h>

static bool swat_bind_code_down(const SwatFrontend* app,int code,bool gate_mouse) {
    if(code>=SWAT_BIND_MOUSE)
        return (!gate_mouse || !app->wait_for_release) && IsMouseButtonDown(code-SWAT_BIND_MOUSE);
    return code>0 && IsKeyDown(code);
}
static bool swat_bind_held(const SwatFrontend* app,int action,bool gate_mouse) {
    if(action<0 || action>=SWAT_BIND_ACTIONS) return false;
    const int* pair=app->settings.bindings[action];
    return swat_bind_code_down(app,pair[0],gate_mouse) || swat_bind_code_down(app,pair[1],gate_mouse);
}
bool swat_frontend_bind_down(const SwatFrontend* app,int action) {
    return swat_bind_held(app,action,true);
}
// Keyboard half of a binding: scope and device controls keep the mouse for the rifle.
static bool swat_bind_key_down(const SwatFrontend* app,int action) {
    const int* pair=app->settings.bindings[action];
    for(int slot=0;slot<SWAT_BIND_SLOTS;slot++)
        if(pair[slot]>0 && pair[slot]<SWAT_BIND_MOUSE && IsKeyDown(pair[slot])) return true;
    return false;
}
bool swat_frontend_bind_pressed(const SwatFrontend* app,int action) {
    if(action<0 || action>=SWAT_BIND_ACTIONS) return false;
    const int* pair=app->settings.bindings[action];
    for(int slot=0;slot<SWAT_BIND_SLOTS;slot++) {
        int code=pair[slot];
        if(code>=SWAT_BIND_MOUSE) {
            if(!app->wait_for_release && IsMouseButtonPressed(code-SWAT_BIND_MOUSE)) return true;
        } else if(code>0 && IsKeyPressed(code)) return true;
    }
    return false;
}
const char* swat_frontend_bind_label(const SwatFrontend* app,int action) {
    if(action<0 || action>=SWAT_BIND_ACTIONS) return "-";
    const int* pair=app->settings.bindings[action];
    const char* name=swat_bind_code_name(pair[0] ? pair[0] : pair[1]);
    return name ? name : "-";
}
// A toggle flips on each fresh press of the held state it replaces.
static bool swat_toggle(bool enabled,bool hold,bool* latched,bool* previous) {
    if(!enabled) *latched=false;
    else if(hold && !*previous) *latched=!*latched;
    *previous=hold;
    return enabled ? *latched : hold;
}
#define BIND(action) swat_frontend_bind_down(app,SWAT_BIND_##action)
#define PRESSED(action) swat_frontend_bind_pressed(app,SWAT_BIND_##action)

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
    // After any gap in polling, a toggle needs a release before it flips again.
    if(!swat_frontend_playing(app) || app->actor<0 ||
       app->actor>=sim->actor_count || !sim->actors[app->actor].present ||
       (!app->captured && !app->camera_pointer)) {
        app->crouch_hold_prev=app->aim_hold_prev=true; return in;
    }
    // Do not feed pointer warps/focus changes or the menu's click into gameplay.
    Vector2 mouse=GetMouseDelta();
    if(app->discard_mouse_frames>0 || app->camera_pointer) {
        if(app->discard_mouse_frames>0) app->discard_mouse_frames--;
        mouse=(Vector2){0};
    }
    SwatLookDelta look=swat_settings_look(&app->settings,mouse.x,mouse.y,sim->actors[app->actor].controller.ads);
    in.yaw_delta=look.yaw; in.pitch_delta=look.pitch;
    in.forward=(float)(BIND(FORWARD)-BIND(BACK));
    in.strafe=(float)(BIND(RIGHT)-BIND(LEFT));
    in.lean=(float)(BIND(LEAN_RIGHT)-BIND(LEAN_LEFT));
    // Toggle crouch is for the officer on foot; remote devices keep "hold to descend".
    bool crouch=BIND(CROUCH);
    if(app->screen==SWAT_SCREEN_GAME)
        in.crouch=swat_toggle(app->settings.crouch_toggle,crouch,&app->crouch_latched,&app->crouch_hold_prev);
    else { in.crouch=crouch; app->crouch_hold_prev=crouch; }
    bool equipment_modifier=BIND(MODIFIER);
    in.gait=BIND(SPRINT) ? SWAT_SPRINT : (equipment_modifier ? SWAT_SLOW : SWAT_WALK);
    in.jump=BIND(JUMP);
    in.fire=BIND(FIRE);
    in.reload=BIND(RELOAD);
    in.cancel_reload=BIND(CANCEL_RELOAD) && equipment_modifier;
    if(PRESSED(LOW_READY)) app->ready=app->ready==SWAT_LOW_READY ? SWAT_READY : SWAT_LOW_READY;
    if(PRESSED(HIGH_READY)) app->ready=app->ready==SWAT_HIGH_READY ? SWAT_READY : SWAT_HIGH_READY;
    in.ready=app->ready;
    in.selector=BIND(SELECTOR);
    bool next_gadget=PRESSED(SIDEARM);
    if(equipment_modifier && (PRESSED(PRIMARY) || next_gadget)) {
        app->selected_gadget=(app->selected_gadget+(next_gadget ? 1 : 7))%8;
        app->wait_for_release=true; in.fire=false;
    }
    in.weapon=equipment_modifier ? 0 : (BIND(PRIMARY) ? 1 : (BIND(SIDEARM) ? 2 : 0));
    if(in.weapon) app->selected_gadget=0;
    in.pepper_spray=BIND(PEPPER_SPRAY);
    in.inspect=BIND(INSPECT); in.command=BIND(COMMAND); in.melee=BIND(MELEE);
    in.throwable=BIND(FLASHBANG) ? 1 : (BIND(GAS) ? 2 : 0);
    in.taser=BIND(TASER);
    in.door_tool=BIND(DETONATE) ? SWAT_DETONATE_CHARGE :
        (BIND(PLACE_CHARGE) ? SWAT_PLACE_CHARGE : (BIND(LOCKPICK) ? SWAT_LOCKPICK : SWAT_DOOR_NONE));
    if(app->screen==SWAT_SCREEN_GAME && !app->camera_pointer) {
        if(BIND(WEDGE)) in.door_tool=equipment_modifier ? SWAT_REMOVE_WEDGE : SWAT_WEDGE;
        if(BIND(DISARM)) in.door_tool=SWAT_DISARM;
        SwatContext context=swat_context(sim,app->actor);
        bool use=BIND(USE);
        bool door=context.hit.kind==SWAT_HIT_WORLD && context.action!=SWAT_CONTEXT_NONE && context.action!=SWAT_CONTEXT_WALL && context.action!=SWAT_CONTEXT_CHARGE && context.hit.distance<=2.2f;
        bool physical=door || context.action==SWAT_CONTEXT_EVIDENCE || context.action==SWAT_CONTEXT_SECURED || context.action==SWAT_CONTEXT_DEVICE || context.action==SWAT_CONTEXT_LIGHT;
        in.interact=use && physical; in.peek=equipment_modifier;
        in.command|=use && !physical;
        bool right=BIND(AIM_CONTEXT);
        if(!right && app->right_held && (app->right_action==SWAT_RIGHT_CUFF || app->right_action==SWAT_RIGHT_PICK) &&
           swat_bind_held(app,SWAT_BIND_FIRE,false)) {
            app->wait_for_release=true; in.fire=false;
        }
        if(right && !app->right_held) {
            app->right_action=SWAT_RIGHT_AIM; app->right_target=context.hit.index;
            if(!BIND(AIM) && context.ready) {
                if(context.action==SWAT_CONTEXT_CUFF) app->right_action=SWAT_RIGHT_CUFF;
                else if(context.action==SWAT_CONTEXT_LOCKED) app->right_action=SWAT_RIGHT_PICK;
            }
        }
        app->right_held=right;
        if(!right) { app->right_action=SWAT_RIGHT_NONE; app->right_target=-1; }
        in.aim=(right && app->right_action==SWAT_RIGHT_AIM) || BIND(AIM);
        in.aim=swat_toggle(app->settings.aim_toggle,in.aim,&app->aim_latched,&app->aim_hold_prev);
        if(right && (app->right_action==SWAT_RIGHT_CUFF || app->right_action==SWAT_RIGHT_PICK)) {
            // Keep the chosen action for the entire press. Finishing a pick or
            // cuff, or looking away, must not turn that same press into ADS.
            in.aim=in.fire=in.reload=in.melee=false;
            bool same=context.ready && context.hit.index==app->right_target;
            if(app->right_action==SWAT_RIGHT_CUFF) in.interact=same && context.action==SWAT_CONTEXT_CUFF;
            else in.door_tool=same && context.action==SWAT_CONTEXT_LOCKED ? SWAT_LOCKPICK : SWAT_DOOR_NONE;
        }
    } else in.aim=BIND(AIM_CONTEXT);
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
    in.squad_execute=BIND(SQUAD_EXECUTE);
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
            scope.jump=in.jump; scope.crouch=in.crouch;
            scope.command=swat_bind_key_down(app,SWAT_BIND_USE) || swat_bind_key_down(app,SWAT_BIND_COMMAND);
        } else {
        scope.sniper_control=true; scope.sniper_unit=app->selected_sniper;
        scope.yaw_delta=in.yaw_delta*.22f; scope.pitch_delta=in.pitch_delta*.22f;
        scope.fire=in.fire; scope.reload=in.reload;
        bool designate=swat_bind_key_down(app,SWAT_BIND_COMMAND) || swat_bind_key_down(app,SWAT_BIND_USE);
        scope.sniper_order=app->camera_pointer ? 0 : (designate ? SWAT_SNIPER_DESIGNATE :
            (swat_bind_key_down(app,SWAT_BIND_JUMP) ? SWAT_SNIPER_EXECUTE : (BIND(SNIPER_HOLD) ? SWAT_SNIPER_HOLD : 0)));
        }
        in=scope;
    } else if(app->leader && !app->camera_pointer) {
        in.sniper_order=BIND(SNIPER_EXECUTE) ? SWAT_SNIPER_EXECUTE : (BIND(SNIPER_HOLD) ? SWAT_SNIPER_HOLD : 0);
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

