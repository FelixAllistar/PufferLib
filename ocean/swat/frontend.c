#include "frontend.h"
#include "net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

static const Color menu_gold={226,181,82,255};
static const Color menu_paper={224,231,229,255};
static const Color menu_muted={151,170,181,255};

static void swat_menu_style(void) {
    GuiLoadStyleDefault();
    GuiSetStyle(DEFAULT,TEXT_SIZE,20);
    GuiSetStyle(DEFAULT,TEXT_SPACING,1);
    GuiSetStyle(DEFAULT,BORDER_COLOR_NORMAL,0x455660ff);
    GuiSetStyle(DEFAULT,BASE_COLOR_NORMAL,0x18252eff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_NORMAL,0xe0e7e5ff);
    GuiSetStyle(DEFAULT,BORDER_COLOR_FOCUSED,0xe2b552ff);
    GuiSetStyle(DEFAULT,BASE_COLOR_FOCUSED,0x283945ff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_FOCUSED,0xffd580ff);
    GuiSetStyle(DEFAULT,BORDER_COLOR_PRESSED,0xe2b552ff);
    GuiSetStyle(DEFAULT,BASE_COLOR_PRESSED,0xe2b552ff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_PRESSED,0x0c131aff);
    GuiSetStyle(DEFAULT,BORDER_COLOR_DISABLED,0x293942ff);
    GuiSetStyle(DEFAULT,BASE_COLOR_DISABLED,0x142029ff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_DISABLED,0x536570ff);
    GuiSetStyle(DEFAULT,BORDER_WIDTH,1);
    GuiSetStyle(SLIDER,SLIDER_WIDTH,18);
    GuiSetStyle(CHECKBOX,CHECK_PADDING,5);
}

void swat_frontend_init(SwatFrontend* app, const char* settings_path) {
    SetExitKey(KEY_NULL); // Escape belongs to this player's menu, not WindowShouldClose.
    memset(app,0,sizeof(*app));
    app->settings=swat_settings_defaults();
    app->screen=SWAT_SCREEN_MAIN;
    app->settings_back=SWAT_SCREEN_MAIN;
    app->leader=true;
    app->camera_open=true;
    app->sniper_post[1]=1; app->sniper_rifle[1]=1;
    app->layout_difficulty=1; app->layout_generator=SWAT_LAYOUT_NEURAL;
    snprintf(app->layout_seed,sizeof(app->layout_seed),"1");
    snprintf(app->address,sizeof(app->address),"127.0.0.1");
    snprintf(app->port,sizeof(app->port),"%d",SWAT_DEFAULT_PORT);
    if(settings_path) {
        if(strlen(settings_path)<sizeof(app->settings_path))
            strcpy(app->settings_path,settings_path);
        else snprintf(app->notice,sizeof(app->notice),"Settings path is too long; using defaults.");
    } else if(!swat_settings_default_path(app->settings_path,sizeof(app->settings_path))) {
        snprintf(app->notice,sizeof(app->notice),"No settings directory; choose a path with --settings.");
    }
    if(app->settings_path[0]) {
        if(!swat_settings_load(&app->settings,app->settings_path) && errno!=ENOENT)
            snprintf(app->notice,sizeof(app->notice),"Could not load settings; using defaults.");
    }
    app->saved_settings=app->settings;
    swat_menu_style();
    SetTargetFPS(app->settings.frame_limit);
    EnableCursor();
}

void swat_frontend_set_screen(SwatFrontend* app, SwatScreen screen) {
    if(app->screen==screen) return;
    if(screen==SWAT_SCREEN_MAIN && (app->networked || app->screen==SWAT_SCREEN_CONNECT))
        app->disconnect_requested=true;
    app->screen=screen;
    app->right_held=false; app->right_action=SWAT_RIGHT_NONE; app->right_target=-1;
    if(screen==SWAT_SCREEN_SCOPE) app->camera_open=true;
    app->reset_input=true;
    app->discard_mouse_frames=2;
    app->wait_for_release=true;
    // This vendored raygui version has no public cancel-drag API. Its
    // implementation lives in this translation unit; abandon a slider drag
    // when Escape changes pages so it cannot disable every button afterward.
    guiControlExclusiveMode=false;
    guiControlExclusiveRec=(Rectangle){0};
    // Release immediately on opening a menu, including menus reached at the
    // end of a simulation tick. Capture happens on a focused frame in update.
    if(screen!=SWAT_SCREEN_GAME && screen!=SWAT_SCREEN_SCOPE && app->captured) {
        EnableCursor();
        app->captured=false;
    }
}

static bool swat_menu_save(SwatFrontend* app) {
    if(!swat_settings_save(&app->settings,app->settings_path)) {
        snprintf(app->notice,sizeof(app->notice),"Could not save settings: %.120s",strerror(errno));
        return false;
    }
    app->saved_settings=app->settings;
    snprintf(app->notice,sizeof(app->notice),"Settings saved.");
    return true;
}

static void swat_settings_back(SwatFrontend* app) {
    if(!swat_settings_equal(&app->settings,&app->saved_settings) && !swat_menu_save(app)) return;
    swat_frontend_set_screen(app,app->settings_back);
}

static bool swat_camera_can_control(const SwatFrontend* app,const SwatSim* sim,int unit) {
    int actor=swat_sniper_actor(unit);
    return app->leader && actor>=0 && sim->snipers[unit].deployed && sim->actors[actor].alive;
}

static void swat_camera_select(SwatFrontend* app,const SwatSim* sim,int unit) {
    if(app->screen!=SWAT_SCREEN_SCOPE || swat_camera_can_control(app,sim,unit)) {
        app->camera_open=true;
        app->selected_sniper=unit;
        if(app->screen==SWAT_SCREEN_SCOPE) {
            app->reset_input=true; app->discard_mouse_frames=2;
        }
    }
}

static void swat_camera_cycle(SwatFrontend* app,const SwatSim* sim,int direction) {
    for(int step=1;step<=SWAT_SNIPERS;step++) {
        int unit=(app->selected_sniper+direction*step+SWAT_SNIPERS)%SWAT_SNIPERS;
        if(app->screen!=SWAT_SCREEN_SCOPE || swat_camera_can_control(app,sim,unit)) {
            swat_camera_select(app,sim,unit); return;
        }
    }
}

void swat_frontend_update(SwatFrontend* app, const SwatSim* sim, bool policy) {
    if(app->last_episode!=sim->episode) {
        app->last_episode=sim->episode; app->loadout_pending=true;
        for(int i=0;i<SWAT_SNIPERS;i++) {
            if(sim->snipers[i].deployed) {
                app->sniper_enabled[i]=true; app->sniper_post[i]=sim->snipers[i].post; app->sniper_rifle[i]=sim->snipers[i].rifle;
            }
            if(app->sniper_post[i]>=sim->mission.overwatch_count) app->sniper_post[i]=i;
            app->sniper_pending[i]=app->sniper_enabled[i] && sim->mission.overwatch_count>i;
        }
        snprintf(app->layout_seed,sizeof(app->layout_seed),"%u",sim->config.layout_seed);
        app->layout_difficulty=sim->config.difficulty; app->layout_generator=sim->config.generator;
    }
    if(!policy) swat_feedback_track(&app->feedback,sim,swat_frontend_playing(app));
    if((app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_SCOPE) && (!IsWindowFocused() ||
        (sim->end!=SWAT_RUNNING && !app->restart_requested)))
        swat_frontend_set_screen(app,SWAT_SCREEN_PAUSE);
    if(app->screen==SWAT_SCREEN_SCOPE && (!app->leader ||
       (sim->snipers[app->selected_sniper].deployed && !sim->actors[swat_sniper_actor(app->selected_sniper)].alive)))
        swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
    bool active=app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_SCOPE;
    bool camera=active && app->camera_open && sim->mission.overwatch_count>0 && !policy;
    if(IsWindowFocused() && (IsKeyPressed(KEY_ESCAPE) || (IsKeyPressed(KEY_TAB) && !camera))) {
        if(app->screen==SWAT_SCREEN_GAME) swat_frontend_set_screen(app,SWAT_SCREEN_PAUSE);
        else if(app->screen==SWAT_SCREEN_SCOPE) swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        else if(app->screen==SWAT_SCREEN_SETTINGS) swat_settings_back(app);
        else if(app->screen==SWAT_SCREEN_CONNECT) swat_frontend_set_screen(app,SWAT_SCREEN_MAIN);
        else if(app->screen==SWAT_SCREEN_PLAN) swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        else if(app->screen==SWAT_SCREEN_PAUSE && sim->end==SWAT_RUNNING)
            swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
    }
    if(!policy && IsWindowFocused() && IsKeyPressed(KEY_P) &&
       (app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_PLAN || app->screen==SWAT_SCREEN_SCOPE))
        swat_frontend_set_screen(app,app->screen==SWAT_SCREEN_PLAN ? SWAT_SCREEN_GAME : SWAT_SCREEN_PLAN);
    if(app->screen==SWAT_SCREEN_PLAN && IsWindowFocused())
        app->plan_yaw+=(IsKeyDown(KEY_D)-IsKeyDown(KEY_A))*GetFrameTime();
    if(active && !policy && IsWindowFocused() && sim->mission.overwatch_count>0) {
        if(IsKeyPressed(KEY_N)) {
            if(app->screen==SWAT_SCREEN_SCOPE) {
                swat_frontend_set_screen(app,SWAT_SCREEN_GAME); app->camera_open=false;
            } else app->camera_open=!app->camera_open;
        }
        if(IsKeyPressed(KEY_COMMA) || IsKeyPressed(KEY_PAGE_UP) ||
           IsKeyPressed(KEY_BACKSLASH) || IsKeyPressed(KEY_LEFT_BRACKET)) swat_camera_cycle(app,sim,-1);
        else if(IsKeyPressed(KEY_PERIOD) || IsKeyPressed(KEY_PAGE_DOWN) ||
                IsKeyPressed(KEY_SLASH) || IsKeyPressed(KEY_RIGHT_BRACKET)) swat_camera_cycle(app,sim,1);
        if(app->screen==SWAT_SCREEN_SCOPE) {
            if(IsKeyPressed(KEY_ONE)) swat_camera_select(app,sim,0);
            if(IsKeyPressed(KEY_TWO)) swat_camera_select(app,sim,1);
        }
        if(IsKeyPressed(KEY_ENTER)) {
            if(app->screen==SWAT_SCREEN_SCOPE) swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
            else if(swat_camera_can_control(app,sim,app->selected_sniper))
                swat_frontend_set_screen(app,SWAT_SCREEN_SCOPE);
        }
    }
    active=app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_SCOPE;
    app->camera_pointer=active && app->camera_open && sim->mission.overwatch_count>0 &&
        IsWindowFocused() && IsKeyDown(KEY_TAB) && !policy;
    float target=app->screen==SWAT_SCREEN_SCOPE ? 1 : 0,step=GetFrameTime()*6;
    app->camera_expansion+=swat_clamp(target-app->camera_expansion,-step,step);
    // A UI click must be released before either camera controls or a weapon
    // can consume another click after capture changes.
    if(app->wait_for_release && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
       !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
       !IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) app->wait_for_release=false;
    bool capture=active && IsWindowFocused() && !policy && !app->camera_pointer;
    if(capture!=app->captured) {
        app->right_held=false; app->right_action=SWAT_RIGHT_NONE; app->right_target=-1;
        if(capture) DisableCursor(); else EnableCursor();
        app->captured=capture;
        app->discard_mouse_frames=2;
        app->wait_for_release=true;
        app->reset_input=true;
    }
    if(app->screen==SWAT_SCREEN_GAME && IsWindowFocused() && IsKeyPressed(KEY_BACKSPACE) &&
       (!app->networked || app->leader)) {
        app->restart_requested=true;
        app->reset_input=true;
    }
    SetTargetFPS(app->settings.frame_limit);
}

bool swat_frontend_playing(const SwatFrontend* app) {
    return (app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_SCOPE) && IsWindowFocused() && !app->quit;
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
    in.selector=IsKeyDown(KEY_V);
    in.weapon=IsKeyDown(KEY_ONE) ? 1 : (IsKeyDown(KEY_TWO) ? 2 : 0);
    in.inspect=IsKeyDown(KEY_G); in.command=IsKeyDown(KEY_Y); in.melee=IsKeyDown(KEY_B);
    in.throwable=IsKeyDown(KEY_FOUR) ? 1 : (IsKeyDown(KEY_FIVE) ? 2 : 0);
    in.taser=IsKeyDown(KEY_T);
    in.door_tool=IsKeyDown(KEY_K) ? SWAT_DETONATE_CHARGE :
        (IsKeyDown(KEY_SEVEN) ? SWAT_PLACE_CHARGE : (IsKeyDown(KEY_L) ? SWAT_LOCKPICK : SWAT_DOOR_NONE));
    if(app->screen==SWAT_SCREEN_GAME && !app->camera_pointer) {
        SwatContext context=swat_context(sim,app->actor);
        bool use=IsKeyDown(KEY_F) || (!app->wait_for_release && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));
        bool door=context.hit.kind==SWAT_HIT_WORLD && context.action!=SWAT_CONTEXT_NONE && context.hit.distance<=2.2f;
        in.interact=use && door;
        in.command|=use && !door;
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
    if(app->camera_pointer) in=swat_neutral_input();
    if(app->loadout_pending) {
        in.loadout=app->selected_kit+1;
        if(sim->actors[app->actor].gear.kit==app->selected_kit) app->loadout_pending=false;
    }
    if(app->screen==SWAT_SCREEN_SCOPE) {
        SwatInput scope=swat_neutral_input(); scope.sniper_control=true; scope.sniper_unit=app->selected_sniper;
        scope.yaw_delta=in.yaw_delta*.22f; scope.pitch_delta=in.pitch_delta*.22f;
        scope.fire=in.fire; scope.reload=in.reload;
        scope.sniper_order=app->camera_pointer ? 0 : ((IsKeyDown(KEY_Y) || IsKeyDown(KEY_F)) ? SWAT_SNIPER_DESIGNATE :
            (IsKeyDown(KEY_SPACE) ? SWAT_SNIPER_EXECUTE : (IsKeyDown(KEY_H) ? SWAT_SNIPER_HOLD : 0)));
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

static void swat_open_settings(SwatFrontend* app) {
    app->settings_back=app->screen;
    app->notice[0]='\0';
    swat_frontend_set_screen(app,SWAT_SCREEN_SETTINGS);
}

static void swat_menu_quit(SwatFrontend* app) {
    // Closing saves preferences; an unwritable settings file must not trap
    // the player in the game when they explicitly choose Quit.
    app->quit=true;
}

static void swat_setting_slider(float x, float y, float width, const char* name,
                                const char* value, float* number, float lo, float hi) {
    DrawText(name,(int)x,(int)y,18,menu_paper);
    DrawText(value,(int)(x+width-MeasureText(value,18)),(int)y,18,menu_gold);
    GuiSlider((Rectangle){x,y+28,width,22},NULL,NULL,number,lo,hi);
}

static void swat_camera_draw(SwatFrontend* app,const SwatSim* sim) {
    if(!app->camera_open || !sim->mission.overwatch_count) return;
    SwatCameraLayout layout=swat_camera_layout(GetScreenWidth(),GetScreenHeight(),app->camera_expansion);
    int unit=app->selected_sniper,actor=swat_sniper_actor(unit);
    const SwatSniper* sniper=&sim->snipers[unit];
    DrawText(TextFormat("SNIPER %c / %s",'A'+unit,swat_sniper_status(sniper->status)),
        (int)layout.panel.x+42,(int)layout.panel.y+12,16,menu_gold);
    if(sniper->deployed) {
        const char* post=sim->mission.overwatch[sniper->post].name;
        DrawRectangle((int)layout.feed.x,(int)(layout.feed.y+layout.feed.height)-24,(int)layout.feed.width,24,(Color){12,19,26,220});
        DrawText(post,(int)layout.feed.x+8,(int)(layout.feed.y+layout.feed.height)-19,13,menu_paper);
        const SwatWeapon* weapon=&sim->actors[actor].arsenal.slots[0];
        const char* ammo=TextFormat("%d+%d / %d",weapon->magazine,weapon->chambered,weapon->reserve);
        DrawText(ammo,(int)(layout.feed.x+layout.feed.width)-MeasureText(ammo,14)-8,
            (int)(layout.feed.y+layout.feed.height)-20,14,menu_gold);
    }
    bool interactive=app->camera_pointer && IsWindowFocused() && !app->wait_for_release;
    if(!interactive) GuiLock();
    GuiSetStyle(DEFAULT,TEXT_SIZE,15);
    if(GuiButton(layout.close,"X")) {
        if(app->screen==SWAT_SCREEN_SCOPE) swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        app->camera_open=false; app->reset_input=true; app->wait_for_release=true;
    }
    for(int i=0;i<SWAT_SNIPERS;i++) {
        if(app->screen==SWAT_SCREEN_SCOPE && !swat_camera_can_control(app,sim,i)) GuiDisable();
        if(GuiButton(layout.unit[i],TextFormat("%s%c",unit==i ? "> " : "",'A'+i))) swat_camera_select(app,sim,i);
        GuiEnable();
    }
    if(GuiButton(layout.previous,"<")) swat_camera_cycle(app,sim,-1);
    if(GuiButton(layout.next,">")) swat_camera_cycle(app,sim,1);
    bool control=swat_camera_can_control(app,sim,unit);
    bool assign=app->leader && !sniper->deployed && !app->sniper_pending[unit];
    if(!control && !assign) GuiDisable();
    const char* label=app->screen==SWAT_SCREEN_SCOPE ? "Return / Enter" :
        (sniper->deployed ? "Take over / Enter" : (app->sniper_pending[unit] ? "Assigning..." : "Assign sniper"));
    if(GuiButton(layout.takeover,label)) {
        if(app->screen==SWAT_SCREEN_SCOPE) swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        else if(control) swat_frontend_set_screen(app,SWAT_SCREEN_SCOPE);
        else if(assign) app->sniper_enabled[unit]=app->sniper_pending[unit]=true;
    }
    GuiEnable(); GuiUnlock(); GuiSetStyle(DEFAULT,TEXT_SIZE,20);
    if(interactive && control && app->screen==SWAT_SCREEN_GAME && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
       CheckCollisionPointRec(GetMousePosition(),layout.feed)) swat_frontend_set_screen(app,SWAT_SCREEN_SCOPE);
    int x=(int)layout.panel.x+10,y=(int)(layout.panel.y+layout.panel.height)-25;
    bool expanded=app->screen==SWAT_SCREEN_SCOPE && layout.panel.width>700;
    const char* hint=expanded ? "Mouse aim / LMB fire / F-Y mark / Space execute / H clear / R reload / Tab cursor / Esc return" :
        (app->screen==SWAT_SCREEN_SCOPE ? "Hold Tab: cursor   Enter / Esc: return to officer" :
        "Hold Tab: cursor  Comma / Period: feeds  N: close");
    DrawText(hint,x,y,expanded ? 16 : 13,menu_muted);
}

void swat_frontend_draw(SwatFrontend* app, const SwatSim* sim, bool policy) {
    if(app->screen==SWAT_SCREEN_GAME || app->screen==SWAT_SCREEN_SCOPE) {
        if(!policy) swat_camera_draw(app,sim);
        return;
    }
    if(app->screen==SWAT_SCREEN_PLAN) {
        int width=GetScreenWidth(),height=GetScreenHeight();
        float x=width-400,y=120;
        DrawRectangle(width-420,96,420,height-96,(Color){12,21,28,248});
        DrawText("MISSION PLANNING",(int)x,(int)y,23,menu_gold); y+=38;
        const SwatMissionDef* mission=swat_sim_mission(sim);
        const char* tabs[]={"Briefing","Loadout","Snipers","Houses"};
        for(int i=0;i<4;i++) if(GuiButton((Rectangle){x+i*95,y,91,32},tabs[i])) { app->plan_tab=i; app->notice[0]=0; }
        y+=52;
        if(app->plan_tab==0) {
            const char* report=sim->config.mission==SWAT_GENERATED ? TextFormat("%d reported gunmen / three hostages",1+sim->config.difficulty) :
                (sim->config.mission==SWAT_HOUSE ? "Two gunmen / three reported hostages" : "One armed target / one civilian");
            DrawText(report,(int)x,(int)y,16,menu_paper); y+=30;
            DrawText(sim->config.mission!=SWAT_ANNEX ? "Secure occupants. Return to staging." : "Clear the annex. Extract at the far end.",(int)x,(int)y,16,menu_muted); y+=42;
            if(GuiButton((Rectangle){x,y,376,38},"Drone / cutaway model")) app->plan_preview=0;
            y+=48;
            for(int i=0;i<mission->overwatch_count;i++) {
                if(GuiButton((Rectangle){x,y,376,38},mission->overwatch[i].name)) app->plan_preview=i+1;
                y+=46;
            }
            DrawText("Select a post to inspect its sight lines.",(int)x,(int)y+14,16,menu_muted);
            DrawText("Assign your two snipers in the Snipers tab.",(int)x,(int)y+43,15,menu_gold);
        } else if(app->plan_tab==1) {
            DrawText("OFFICER EQUIPMENT",(int)x,(int)y,20,menu_gold); y+=36;
            const SwatActor* actor=&sim->actors[app->actor];
            bool can_equip=actor->arsenal.shots==0 && !actor->gear.used_tools && actor->health==100 &&
                b3Distance(swat_body_feet_position(&actor->controller.body),mission->staging)<3;
            for(int i=0;i<SWAT_KIT_COUNT;i++) {
                const SwatKitDef* kit=swat_kit(i);
                if(!can_equip) GuiDisable();
                if(GuiButton((Rectangle){x,y,376,36},TextFormat("%s%s   %.0f kg   %.0f%% pace",app->selected_kit==i ? "> " : "",kit->name,kit->mass,kit->mobility*100))) {
                    app->selected_kit=i; app->loadout_pending=true;
                }
                GuiEnable(); y+=42;
                DrawText(kit->description,(int)x,(int)y,14,menu_muted); y+=24;
                DrawText(TextFormat("%.0f%% torso protection / %s",kit->torso_protection*100,kit->gas_mask ? "gas mask" : "no mask"),(int)x,(int)y,14,menu_gold); y+=35;
            }
            DrawText(can_equip ? "Kits apply on deployment at staging." : "Used equipment cannot be refilled at staging.",(int)x,(int)y+8,14,menu_muted);
        } else if(app->plan_tab==2) {
            for(int i=0;i<SWAT_SNIPERS;i++)
                if(GuiButton((Rectangle){x+i*192,y,184,35},TextFormat("%sSNIPER %c",app->selected_sniper==i ? "> " : "",'A'+i))) app->selected_sniper=i;
            y+=50;
            int unit=app->selected_sniper; const SwatSniper* sniper=&sim->snipers[unit];
            DrawText(swat_sniper_status(sniper->status),(int)x,(int)y,17,menu_gold); y+=33;
            if(!app->leader || !mission->overwatch_count) GuiDisable();
            for(int i=0;i<mission->overwatch_count;i++) {
                bool occupied=false;
                for(int j=0;j<SWAT_SNIPERS;j++) if(j!=unit && ((app->sniper_enabled[j] && app->sniper_post[j]==i) ||
                    (sim->snipers[j].deployed && sim->snipers[j].post==i))) occupied=true;
                if(occupied) GuiDisable();
                if(GuiButton((Rectangle){x,y,376,34},TextFormat("%s%s%s",app->sniper_post[unit]==i ? "> " : "",mission->overwatch[i].name,occupied ? " / assigned" : ""))) {
                    app->sniper_post[unit]=i; app->plan_preview=i+1;
                }
                if(app->leader && mission->overwatch_count) GuiEnable();
                y+=42;
            }
            y+=8; DrawText("RIFLE",(int)x,(int)y,18,menu_gold); y+=28;
            bool used=sniper->deployed && sim->actors[swat_sniper_actor(unit)].arsenal.shots>0;
            if(used) GuiDisable();
            for(int rifle=0;rifle<2;rifle++) {
                if(GuiButton((Rectangle){x,y,376,34},TextFormat("%s%s",app->sniper_rifle[unit]==rifle ? "> " : "",rifle ? "Marksman / 10 rounds / faster cycle" : "Precision / 4 rounds / heavier hit"))) app->sniper_rifle[unit]=rifle;
                y+=42;
            }
            if(app->leader && mission->overwatch_count) GuiEnable();
            y+=8;
            if(sniper->deployed && !sim->actors[swat_sniper_actor(unit)].alive) GuiDisable();
            if(GuiButton((Rectangle){x,y,376,36},app->sniper_pending[unit] ? "Assignment queued for deployment" : "Assign selected post and rifle")) {
                app->sniper_enabled[unit]=app->sniper_pending[unit]=true;
            }
            y+=46;
            if(!sniper->deployed && !app->sniper_pending[unit]) GuiDisable();
            if(GuiButton((Rectangle){x,y,376,38},"Take scope control")) swat_frontend_set_screen(app,SWAT_SCREEN_SCOPE);
            GuiEnable(); y+=55;
            DrawText("Y mark / Space execute both / LMB fire",(int)x,(int)y,15,menu_muted);
            DrawText("Officer: X execute / H clear marks",(int)x,(int)y+24,15,menu_muted);
            if(!app->leader) DrawText("The session leader controls overwatch.",(int)x,(int)y+48,15,menu_gold);
        } else {
            DrawText("BUILD ANOTHER RESIDENCE",(int)x,(int)y,20,menu_gold); y+=36;
            if(!app->leader) GuiDisable();
            DrawText("Seed",(int)x,(int)y+9,18,menu_paper);
            if(GuiTextBox((Rectangle){x+66,y,310,36},app->layout_seed,sizeof(app->layout_seed),app->seed_edit)) app->seed_edit=!app->seed_edit;
            y+=50;
            const char* difficulty[]={"Compact","Standard","Complex"};
            for(int i=0;i<3;i++) if(GuiButton((Rectangle){x+i*127,y,122,34},TextFormat("%s%s",app->layout_difficulty==i ? "> " : "",difficulty[i]))) app->layout_difficulty=i;
            y+=48;
            for(int i=0;i<2;i++) if(GuiButton((Rectangle){x+i*192,y,184,34},TextFormat("%s%s",app->layout_generator==i ? "> " : "",i ? "Learned" : "Random"))) app->layout_generator=i;
            y+=48;
            bool build=GuiButton((Rectangle){x,y,184,38},"Build this seed");
            bool next=GuiButton((Rectangle){x+192,y,184,38},"Next house"); y+=49;
            bool cedar=GuiButton((Rectangle){x,y,376,34},"Return to Cedar House"); y+=50;
            if(build || next || cedar) {
                char* end; errno=0; unsigned long long seed=strtoull(app->layout_seed,&end,10);
                if(!cedar && (errno || end==app->layout_seed || *end || app->layout_seed[0]=='-' || seed>UINT32_MAX))
                    snprintf(app->notice,sizeof(app->notice),"Use a seed from 0 to 4294967295.");
                else {
                    app->scenario=sim->config; app->scenario.mission=cedar ? SWAT_HOUSE : SWAT_GENERATED;
                    app->scenario.layout_seed=(uint32_t)seed+(next ? 1u : 0u);
                    app->scenario.generator=app->layout_generator; app->scenario.difficulty=app->layout_difficulty;
                    app->scenario_requested=true; app->plan_preview=0; app->seed_edit=false;
                    app->notice[0]=0;
                }
            }
            GuiEnable();
            if(sim->config.mission==SWAT_GENERATED) {
                DrawText(TextFormat("Current: seed %u / %d rooms / %.0f x %.0f m",sim->layout.seed,sim->layout.room_count,sim->layout.width,sim->layout.depth),(int)x,(int)y,15,menu_paper); y+=27;
            }
            DrawText("WHICH HOUSE WAS MORE FUN?",(int)x,(int)y,18,menu_gold); y+=32;
            bool ready=swat_feedback_ready(&app->feedback);
            if(!ready) GuiDisable();
            const char* labels[]={"Previous","This one","Tie"};
            for(int i=0;i<3;i++) if(GuiButton((Rectangle){x+i*127,y,122,34},labels[i])) {
                char path[SWAT_SETTINGS_PATH_SIZE];
                int n=snprintf(path,sizeof(path),"%s.layouts.jsonl",app->settings_path);
                if(!app->settings_path[0] || n<0 || (size_t)n>=sizeof(path) || !swat_feedback_save(&app->feedback,path,i))
                    snprintf(app->notice,sizeof(app->notice),"Could not save this comparison.");
                else snprintf(app->notice,sizeof(app->notice),"Comparison saved beside your settings.");
            }
            GuiEnable(); y+=48;
            DrawText(app->feedback.voted ? "Thanks. Play another house to compare again." :
                "Play two houses for at least 5 seconds each.",(int)x,(int)y,14,menu_muted); y+=24;
            DrawText("Comparisons stay on this computer.",(int)x,(int)y,14,menu_muted); y+=24;
            if(!app->leader) { DrawText("The session leader chooses the next house.",(int)x,(int)y,14,menu_gold); y+=24; }
            if(app->notice[0]) DrawText(app->notice,(int)x,(int)y+7,14,menu_gold);
        }
        if(GuiButton((Rectangle){x,height-82,376,48},sim->tick<2 ? "Deploy" : "Return to officer"))
            swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        DrawRectangle(24,height-86,570,55,(Color){12,21,28,238});
        DrawText(app->plan_preview ? "OPTICAL POST PREVIEW" : "REPORTED LAYOUT / roof removed for planning",40,height-76,17,menu_paper);
        DrawText("A / D orbit model     P / Esc return     Select a planning tab",40,height-53,15,menu_gold);
        return;
    }
    // A trigger held when Escape was pressed must not become a menu click on
    // release. Wait through the release frame, then allow fresh UI presses.
    if(app->wait_for_release && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
       !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
       !IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) app->wait_for_release=false;
    if(app->wait_for_release || !IsWindowFocused()) GuiLock(); else GuiUnlock();
    SwatScreen screen=app->screen; // Layout remains stable when a button changes pages.
    int width=GetScreenWidth(),height=GetScreenHeight();
    DrawRectangle(0,0,width,height,(Color){5,11,16,210});
    bool settings=app->screen==SWAT_SCREEN_SETTINGS;
    float panel_w=settings ? 620.0f : 500.0f;
    float panel_h=settings ? 680.0f : 574.0f;
    float x=(width-panel_w)*0.5f,y=(height-panel_h)*0.5f;
    DrawRectangle((int)x,(int)y,(int)panel_w,(int)panel_h,(Color){12,20,28,250});
    DrawRectangleLines((int)x,(int)y,(int)panel_w,(int)panel_h,(Color){62,79,90,255});
    DrawRectangle((int)x,(int)y,4,(int)panel_h,menu_gold);
    x+=32; y+=28;
    float content_w=panel_w-64;
    DrawText("SWAT  /  GOLD ELEMENT",(int)x,(int)y,24,menu_gold);
    const char* title=settings ? "SETTINGS" : (app->screen==SWAT_SCREEN_CONNECT ?
        (app->hosting ? "HOST CO-OP" : "JOIN CO-OP") : (app->screen==SWAT_SCREEN_MAIN ? "TRAINING ANNEX" :
        (sim->end==SWAT_RUNNING ? "PAUSED" : swat_end_name(sim->end))));
    DrawText(title,(int)x,(int)y+43,30,menu_paper);

    if(settings) {
        y+=94;
        swat_setting_slider(x,y,content_w,"Mouse sensitivity",
            TextFormat("%.3f deg / pixel",app->settings.sensitivity),
            &app->settings.sensitivity,SWAT_SENSITIVITY_MIN,SWAT_SENSITIVITY_MAX);
        y+=61;
        swat_setting_slider(x,y,content_w,"Vertical sensitivity",
            TextFormat("%.2fx",app->settings.vertical_multiplier),&app->settings.vertical_multiplier,0.25f,2);
        y+=61;
        swat_setting_slider(x,y,content_w,"Aiming sensitivity",
            TextFormat("%.2fx",app->settings.ads_multiplier),&app->settings.ads_multiplier,0.1f,1.5f);
        y+=61;
        swat_setting_slider(x,y,content_w,"Field of view (vertical)",
            TextFormat("%.0f degrees",app->settings.vertical_fov),&app->settings.vertical_fov,55,100);
        y+=61;
        float frames=(float)app->settings.frame_limit;
        swat_setting_slider(x,y,content_w,"Frame limit",TextFormat("%d FPS",app->settings.frame_limit),&frames,30,240);
        app->settings.frame_limit=(int)roundf(frames);
        y+=61;
        swat_setting_slider(x,y,content_w,"Master volume",TextFormat("%.0f%%",app->settings.master_volume*100),
            &app->settings.master_volume,0,1);
        y+=61;
        GuiCheckBox((Rectangle){x,y,24,24},"Invert horizontal",&app->settings.invert_x);
        GuiCheckBox((Rectangle){x+content_w*0.53f,y,24,24},"Invert vertical",&app->settings.invert_y);
        y+=49;
        float button_w=(content_w-20)/3;
        if(GuiButton((Rectangle){x,y,button_w,38},"Defaults")) app->settings=swat_settings_defaults();
        if(GuiButton((Rectangle){x+button_w+10,y,button_w,38},"Reload saved")) {
            if(swat_settings_load(&app->settings,app->settings_path)) {
                app->saved_settings=app->settings;
                snprintf(app->notice,sizeof(app->notice),"Saved settings loaded.");
            } else snprintf(app->notice,sizeof(app->notice),"No usable saved settings; defaults loaded.");
        }
        if(GuiButton((Rectangle){x+2*(button_w+10),y,button_w,38},"Save & back") && swat_menu_save(app))
            swat_frontend_set_screen(app,app->settings_back);
        y+=53;
        DrawText(app->notice[0] ? app->notice : "Escape saves changes and goes back.",(int)x,(int)y,14,menu_muted);
        swat_settings_sanitize(&app->settings);
        return;
    }

    if(screen==SWAT_SCREEN_CONNECT) {
        y+=105;
        DrawText(app->hosting ? "Host a four-officer session on this machine." :
            "Connect directly to a host or dedicated server.",(int)x,(int)y,16,menu_muted);
        y+=50;
        if(!app->hosting) {
            DrawText("Server address",(int)x,(int)y,18,menu_paper);
            if(GuiTextBox((Rectangle){x,y+28,content_w,38},app->address,sizeof(app->address),app->address_edit))
                app->address_edit=!app->address_edit;
            y+=86;
        }
        DrawText("UDP port",(int)x,(int)y,18,menu_paper);
        if(GuiTextBox((Rectangle){x,y+28,160,38},app->port,sizeof(app->port),app->port_edit))
            app->port_edit=!app->port_edit;
        y+=96;
        if(app->connect_pending) GuiDisable();
        if(GuiButton((Rectangle){x,y,content_w,48},app->connect_pending ? "Connecting..." :
                     (app->hosting ? "Start host" : "Join session"))) {
            app->host_requested=app->hosting; app->join_requested=!app->hosting;
            app->connect_pending=true; app->address_edit=app->port_edit=false;
        }
        GuiEnable(); y+=60;
        if(GuiButton((Rectangle){x,y,content_w,48},"Back")) swat_frontend_set_screen(app,SWAT_SCREEN_MAIN);
        y+=65;
        DrawText(app->notice[0] ? app->notice : "Escape returns to the main menu.",(int)x,(int)y,15,menu_muted);
        return;
    }

    y+=93;
    DrawText(app->screen==SWAT_SCREEN_MAIN ? "Plan the entry. Equip your team. Get ready." :
        (app->networked ? "Your controls paused. The session continues." : "Simulation paused. Mouse released."),
        (int)x,(int)y,16,menu_muted);
    y+=42;
    if(screen==SWAT_SCREEN_MAIN) {
        if(GuiButton((Rectangle){x,y,content_w,48},policy ? "Watch policy" :
            (sim->config.mission!=SWAT_ANNEX ? "Plan the mission" : "Enter training annex"))) {
            app->networked=false; app->leader=true; app->actor=0;
            app->restart_requested=true;
            swat_frontend_set_screen(app,!policy && sim->config.mission!=SWAT_ANNEX ? SWAT_SCREEN_PLAN : SWAT_SCREEN_GAME);
        }
    } else {
        if(sim->end!=SWAT_RUNNING) GuiDisable();
        if(GuiButton((Rectangle){x,y,content_w,48},"Resume")) swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        GuiEnable();
    }
    y+=60;
    if(GuiButton((Rectangle){x,y,content_w,48},"Settings")) swat_open_settings(app);
    y+=60;
    if(screen!=SWAT_SCREEN_MAIN) {
        if(app->networked && !app->leader) GuiDisable();
        if(GuiButton((Rectangle){x,y,content_w,48},"Restart mission")) {
            app->restart_requested=true;
            swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        }
        GuiEnable();
        y+=60;
        if(GuiButton((Rectangle){x,y,content_w,48},"Main menu")) swat_frontend_set_screen(app,SWAT_SCREEN_MAIN);
        y+=60;
    } else {
        if(policy) GuiDisable();
        if(GuiButton((Rectangle){x,y,content_w,48},"Host co-op")) {
            app->hosting=true; app->connect_pending=false; app->notice[0]='\0';
            swat_frontend_set_screen(app,SWAT_SCREEN_CONNECT);
        }
        y+=60;
        if(GuiButton((Rectangle){x,y,content_w,48},"Join co-op")) {
            app->hosting=false; app->connect_pending=false; app->notice[0]='\0';
            swat_frontend_set_screen(app,SWAT_SCREEN_CONNECT);
        }
        GuiEnable(); y+=60;
    }
    if(GuiButton((Rectangle){x,y,content_w,48},"Quit game")) swat_menu_quit(app);
    y+=64;
    DrawText(app->notice[0] ? app->notice : "Escape / Tab opens the pause menu.",(int)x,(int)y,15,menu_muted);
}

void swat_frontend_close(SwatFrontend* app) {
    if(app->captured) EnableCursor();
    app->captured=false;
    if(!swat_settings_equal(&app->settings,&app->saved_settings) && !swat_menu_save(app))
        fprintf(stderr,"swat: %s\n",app->notice);
}
