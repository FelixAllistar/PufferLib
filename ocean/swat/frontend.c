#include "frontend.h"
#include <errno.h>
#include <stdio.h>
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
    app->screen=screen;
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
    if(screen!=SWAT_SCREEN_GAME && app->captured) {
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

void swat_frontend_update(SwatFrontend* app, const SwatSim* sim, bool policy) {
    if(app->screen==SWAT_SCREEN_GAME && (!IsWindowFocused() ||
        (sim->end!=SWAT_RUNNING && !app->restart_requested)))
        swat_frontend_set_screen(app,SWAT_SCREEN_PAUSE);
    if(IsWindowFocused() && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_TAB))) {
        if(app->screen==SWAT_SCREEN_GAME) swat_frontend_set_screen(app,SWAT_SCREEN_PAUSE);
        else if(app->screen==SWAT_SCREEN_SETTINGS) swat_settings_back(app);
        else if(app->screen==SWAT_SCREEN_PAUSE && sim->end==SWAT_RUNNING)
            swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
    }
    bool capture=app->screen==SWAT_SCREEN_GAME && IsWindowFocused() && !policy;
    if(capture!=app->captured) {
        if(capture) DisableCursor(); else EnableCursor();
        app->captured=capture;
        app->discard_mouse_frames=2;
        app->wait_for_release=true;
        app->reset_input=true;
    }
    if(app->screen==SWAT_SCREEN_GAME && IsWindowFocused() && IsKeyPressed(KEY_BACKSPACE)) {
        app->restart_requested=true;
        app->reset_input=true;
    }
    SetTargetFPS(app->settings.frame_limit);
}

bool swat_frontend_playing(const SwatFrontend* app) {
    return app->screen==SWAT_SCREEN_GAME && IsWindowFocused() && !app->quit;
}

SwatInput swat_frontend_input(SwatFrontend* app, const SwatSim* sim) {
    SwatInput in=swat_neutral_input();
    if(!app->captured || !swat_frontend_playing(app)) return in;
    // Do not feed pointer warps/focus changes or the menu's click into gameplay.
    Vector2 mouse=GetMouseDelta();
    if(app->discard_mouse_frames>0) {
        app->discard_mouse_frames--;
        mouse=(Vector2){0};
    }
    SwatLookDelta look=swat_settings_look(&app->settings,mouse.x,mouse.y,sim->actors[0].controller.ads);
    in.yaw_delta=look.yaw; in.pitch_delta=look.pitch;
    in.forward=(float)(IsKeyDown(KEY_W)-IsKeyDown(KEY_S));
    in.strafe=(float)(IsKeyDown(KEY_D)-IsKeyDown(KEY_A));
    in.lean=(float)(IsKeyDown(KEY_E)-IsKeyDown(KEY_Q));
    in.crouch=IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C);
    in.gait=IsKeyDown(KEY_LEFT_SHIFT) ? SWAT_SPRINT :
        (IsKeyDown(KEY_LEFT_ALT) ? SWAT_SLOW : SWAT_WALK);
    in.jump=IsKeyDown(KEY_SPACE);
    if(app->wait_for_release && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
       !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) app->wait_for_release=false;
    if(!app->wait_for_release) {
        in.aim=IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        in.fire=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    }
    in.reload=IsKeyDown(KEY_R);
    in.interact=IsKeyDown(KEY_F);
    in.selector=IsKeyDown(KEY_V);
    in.weapon=IsKeyDown(KEY_ONE) ? 1 : (IsKeyDown(KEY_TWO) ? 2 : 0);
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

void swat_frontend_draw(SwatFrontend* app, const SwatSim* sim, bool policy) {
    if(app->screen==SWAT_SCREEN_GAME) return;
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
    const char* title=settings ? "SETTINGS" : (app->screen==SWAT_SCREEN_MAIN ? "TRAINING ANNEX" :
        (sim->end==SWAT_RUNNING ? "PAUSED" : swat_end_name(sim->end)));
    DrawText(title,(int)x,(int)y+43,30,menu_paper);

    if(settings) {
        y+=94;
        swat_setting_slider(x,y,content_w,"Mouse sensitivity",
            TextFormat("%.3f deg / pixel",app->settings.sensitivity),
            &app->settings.sensitivity,SWAT_SENSITIVITY_MIN,SWAT_SENSITIVITY_MAX);
        y+=74;
        swat_setting_slider(x,y,content_w,"Vertical sensitivity",
            TextFormat("%.2fx",app->settings.vertical_multiplier),&app->settings.vertical_multiplier,0.25f,2);
        y+=74;
        swat_setting_slider(x,y,content_w,"Aiming sensitivity",
            TextFormat("%.2fx",app->settings.ads_multiplier),&app->settings.ads_multiplier,0.1f,1.5f);
        y+=74;
        swat_setting_slider(x,y,content_w,"Field of view (vertical)",
            TextFormat("%.0f degrees",app->settings.vertical_fov),&app->settings.vertical_fov,55,100);
        y+=74;
        float frames=(float)app->settings.frame_limit;
        swat_setting_slider(x,y,content_w,"Frame limit",TextFormat("%d FPS",app->settings.frame_limit),&frames,30,240);
        app->settings.frame_limit=(int)roundf(frames);
        y+=70;
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

    y+=93;
    DrawText(app->screen==SWAT_SCREEN_MAIN ? "Enter the annex. Tune your controls. Get ready." :
        "Simulation paused. Mouse released.",(int)x,(int)y,16,menu_muted);
    y+=42;
    if(screen==SWAT_SCREEN_MAIN) {
        if(GuiButton((Rectangle){x,y,content_w,48},policy ? "Watch policy" : "Enter training annex")) {
            app->restart_requested=true;
            swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
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
        if(GuiButton((Rectangle){x,y,content_w,48},"Restart mission")) {
            app->restart_requested=true;
            swat_frontend_set_screen(app,SWAT_SCREEN_GAME);
        }
        y+=60;
        if(GuiButton((Rectangle){x,y,content_w,48},"Main menu")) swat_frontend_set_screen(app,SWAT_SCREEN_MAIN);
        y+=60;
    } else {
        DrawText("WASD move   /   Mouse look\nQ / E lean   /   Ctrl crouch\nRMB aim   /   LMB fire   /   F door",
            (int)x,(int)y+6,18,menu_muted);
        y+=120;
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
