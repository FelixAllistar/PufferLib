// Interactive regression test: opens its own short-lived game window. On
// Windows also checks the real OS confinement rectangle, not just Raylib flags.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define CloseWindow WinCloseWindow
#define ShowCursor WinShowCursor
#define Rectangle WinRectangle
#define DrawText WinDrawText
#define LoadImage WinLoadImage
#include <windows.h>
#undef CloseWindow
#undef ShowCursor
#undef Rectangle
#undef DrawText
#undef DrawTextEx
#undef LoadImage
#endif
#include "frontend.h"
#include "sound_view.h"
#include "raygui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s (screen=%d back=%d tick=%d notice=%s mouse=%.0f,%.0f focused=%d captured=%d wait=%d locked=%d)\n", \
        __FILE__,__LINE__,#condition,app->screen,app->settings_back,sim->tick,app->notice, \
        GetMousePosition().x,GetMousePosition().y,IsWindowFocused(),app->captured,app->wait_for_release,GuiIsLocked()); \
    Image failure=LoadImageFromScreen(); ExportImage(failure,"build/swat/windows/gui-failure.png"); UnloadImage(failure); \
    return false; } } while(0)

// Raylib 5.5's documented automation file event IDs (enum lives in rcore.c).
enum { TEST_KEY_UP=1, TEST_KEY_DOWN=2, TEST_MOUSE_UP=5, TEST_MOUSE_DOWN=6, TEST_MOUSE_POSITION=7 };
static SwatSoundView test_sound;
static void event(unsigned int type, int a, int b) {
    PlayAutomationEvent((AutomationEvent){0,type,{a,b,0,0}});
}

static void frame(SwatFrontend* app, SwatView* view, SwatSim* sim) {
    swat_frontend_update(app,sim,false);
    if(app->restart_requested) { swat_sim_reset(sim); app->restart_requested=false; }
    if(app->scenario_requested) { sim->config=app->scenario; swat_sim_reset(sim); app->scenario_requested=false; }
    app->reset_input=false;
    if(swat_frontend_playing(app)) {
        SwatInput in=swat_frontend_input(app,sim);
        swat_sim_step(sim,&in);
    }
    swat_sound_view_update(&test_sound,sim,app->actor,app->settings.master_volume,0,0);
    view->actor=app->actor; view->planning=app->screen==SWAT_SCREEN_PLAN;
    view->weapon_size=app->settings.weapon_size;
    view->weapon_ads_relief=app->settings.weapon_ads_relief;
    view->weapon_preview_ads=app->screen==SWAT_SCREEN_SETTINGS && app->weapon_settings && app->weapon_preview_ads;
    view->weapon_horizontal=app->settings.weapon_horizontal; view->weapon_vertical=app->settings.weapon_vertical;
    view->plan_preview=app->plan_preview; view->plan_yaw=app->plan_yaw;
    view->scope=app->screen==SWAT_SCREEN_SCOPE; view->sniper_unit=app->selected_sniper;
    view->sniper_camera=swat_frontend_playing(app) && app->camera_open;
    view->camera_expansion=app->camera_expansion;
    BeginDrawing();
    swat_view_draw(view,sim,false,app->settings.vertical_fov);
    swat_frontend_draw(app,view,sim,false);
    EndDrawing();
}

static void frames(SwatFrontend* app, SwatView* view, SwatSim* sim, int count) {
    for(int i=0;i<count;i++) frame(app,view,sim);
}

static void click(SwatFrontend* app, SwatView* view, SwatSim* sim, int x, int y) {
    SetMousePosition(x,y);
    // Let the real OS cursor-position callback settle before injecting the
    // click. Otherwise a delayed warp can overwrite the automation position.
    frames(app,view,sim,2);
    event(TEST_MOUSE_POSITION,x,y);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_LEFT,0); frame(app,view,sim);
    // OS warp callbacks may arrive in the down frame; pin the injected release
    // to the same UI target as the press without changing gameplay input tests.
    event(TEST_MOUSE_POSITION,x,y);
    event(TEST_MOUSE_UP,MOUSE_BUTTON_LEFT,0); frame(app,view,sim);
    frame(app,view,sim);
}

static void escape(SwatFrontend* app, SwatView* view, SwatSim* sim) {
#if defined(_WIN32)
    // Go through the real GLFW key callback, including Raylib's exit-key path.
    HWND window=(HWND)GetWindowHandle();
    LPARAM scan=(LPARAM)MapVirtualKeyA(VK_ESCAPE,MAPVK_VK_TO_VSC)<<16;
    PostMessageA(window,WM_KEYDOWN,VK_ESCAPE,scan|1);
    frames(app,view,sim,2);
    PostMessageA(window,WM_KEYUP,VK_ESCAPE,scan|((LPARAM)3<<30)|1);
    frames(app,view,sim,2);
#else
    event(TEST_KEY_DOWN,KEY_ESCAPE,0); frame(app,view,sim);
    event(TEST_KEY_UP,KEY_ESCAPE,0); frame(app,view,sim);
#endif
}

#if defined(_WIN32)
static void focus_test_window(void) {
    HWND window=(HWND)GetWindowHandle();
    DWORD own=GetCurrentThreadId(),foreground=GetWindowThreadProcessId(GetForegroundWindow(),NULL);
    bool attached=foreground && foreground!=own && AttachThreadInput(own,foreground,TRUE);
    ShowWindow(window,SW_RESTORE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetFocus(window);
    if(attached) AttachThreadInput(own,foreground,FALSE);
}
#endif

static bool confined_to_window(void) {
#if defined(_WIN32)
    RECT clip,client;
    HWND window=(HWND)GetWindowHandle();
    if(!GetClipCursor(&clip) || !GetClientRect(window,&client)) return false;
    POINT first={client.left,client.top},last={client.right,client.bottom};
    ClientToScreen(window,&first); ClientToScreen(window,&last);
    bool confined=clip.left==first.x && clip.top==first.y && clip.right==last.x && clip.bottom==last.y;
    if(IsCursorHidden() && !confined) fprintf(stderr,
        "Cursor bounds: clip=(%ld,%ld,%ld,%ld) client=(%ld,%ld,%ld,%ld) focused=%d foreground=%d\n",
        clip.left,clip.top,clip.right,clip.bottom,first.x,first.y,last.x,last.y,
        IsWindowFocused(),GetForegroundWindow()==window);
    return confined;
#else
    return IsCursorHidden();
#endif
}

static bool run_checks(SwatFrontend* app, SwatView* view, SwatSim* sim,
                       const char* preferences, const char* screenshot) {
#if defined(_WIN32)
    // A launch from a background WSL console can leave GLFW's initial focus
    // flag set before Win32 grants foreground ownership. Automation clicks do
    // not activate the real window, so establish and verify that prerequisite.
    focus_test_window();
#endif
    frames(app,view,sim,20);
#if defined(_WIN32)
    for(int attempt=0;attempt<10 && GetForegroundWindow()!=(HWND)GetWindowHandle();attempt++) {
        focus_test_window(); frames(app,view,sim,3);
    }
#endif
    CHECK(IsWindowFocused());
#if defined(_WIN32)
    CHECK(GetForegroundWindow()==(HWND)GetWindowHandle());
#endif
    CHECK(app->screen==SWAT_SCREEN_MAIN && !app->captured && !IsCursorHidden() && sim->tick==0);

    click(app,view,sim,720,425); // Host setup can be cancelled without starting a round.
    CHECK(app->screen==SWAT_SCREEN_CONNECT && app->hosting && !app->captured);
    escape(app,view,sim);
    CHECK(app->screen==SWAT_SCREEN_MAIN && app->disconnect_requested && sim->tick==0);
    app->disconnect_requested=false;
    click(app,view,sim,720,485);
    CHECK(app->screen==SWAT_SCREEN_CONNECT && !app->hosting);
    click(app,view,sim,720,509); // Join request: runtime owns the actual socket connection.
    CHECK(app->join_requested && app->connect_pending && !app->host_requested);
    app->join_requested=false;
    escape(app,view,sim); app->disconnect_requested=false;

    click(app,view,sim,720,425);
    click(app,view,sim,720,423);
    CHECK(app->host_requested && app->connect_pending && !app->join_requested);
    app->host_requested=false;
    escape(app,view,sim); app->disconnect_requested=false;

    click(app,view,sim,720,365); // Main -> Settings.
    CHECK(app->screen==SWAT_SCREEN_SETTINGS);
    click(app,view,sim,790,244); // Sensitivity slider.
    click(app,view,sim,746,584); // Invert vertical.
    CHECK(app->settings.sensitivity>0.08f && app->settings.invert_y);
    float sensitivity=app->settings.sensitivity;
    click(app,view,sim,856,191); CHECK(app->weapon_settings);
    click(app,view,sim,320,220); // Larger first-person view, preview stays visible.
    click(app,view,sim,160,282); click(app,view,sim,390,343);
    CHECK(app->settings.weapon_size>2 && app->settings.weapon_horizontal>-.055f && app->settings.weapon_vertical>.10f);
    CHECK(app->settings.sensitivity==sensitivity && app->settings.invert_y);
    click(app,view,sim,350,405); CHECK(app->settings.weapon_ads_relief>.18f);
    click(app,view,sim,230,438); CHECK(app->weapon_preview_ads && view->weapon_preview_ads && sim->tick==0);
    CHECK(sim->actors[0].controller.ads==0); // Display preview cannot aim/fire the simulation.
    click(app,view,sim,130,159); CHECK(!app->weapon_settings);
    CHECK(!view->weapon_preview_ads);
    if(screenshot) {
        Image image=LoadImageFromScreen();
        bool saved=ExportImage(image,screenshot); UnloadImage(image);
        CHECK(saved);
    }
    click(app,view,sim,910,639); // Save & back, including the weapon view.
    CHECK(app->screen==SWAT_SCREEN_MAIN && sim->tick==0);
    SwatSettings loaded;
    CHECK(swat_settings_load(&loaded,preferences));
    CHECK(swat_settings_equal(&loaded,&app->settings));
    swat_frontend_close(app);
    swat_frontend_init(app,preferences);
    CHECK(swat_settings_equal(&loaded,&app->settings));
    app->settings=swat_settings_defaults(); // Use ordinary axes for the look test.
    frame(app,view,sim);
    click(app,view,sim,720,305); // Start.
    frames(app,view,sim,4);
    CHECK(app->screen==SWAT_SCREEN_GAME && app->captured && IsCursorHidden());
    CHECK(confined_to_window());
    CHECK(fabsf(sim->actors[0].controller.yaw)<0.001f && fabsf(sim->actors[0].controller.pitch)<0.001f);
    event(TEST_KEY_DOWN,KEY_F5,0); frame(app,view,sim);
    CHECK(app->save_requested); app->save_requested=false; event(TEST_KEY_UP,KEY_F5,0); frame(app,view,sim);
    event(TEST_KEY_DOWN,KEY_F9,0); frame(app,view,sim);
    CHECK(app->resume_requested); app->resume_requested=false; event(TEST_KEY_UP,KEY_F9,0); frame(app,view,sim);
    Vector2 position=GetMousePosition();
    event(TEST_MOUSE_POSITION,(int)position.x+40,(int)position.y-30);
    frame(app,view,sim);
    float yaw=sim->actors[0].controller.yaw,pitch=sim->actors[0].controller.pitch;
    CHECK(yaw>0 && pitch>0 && yaw<0.1f && pitch<0.1f);
    frames(app,view,sim,15);
    CHECK(fabsf(sim->actors[0].controller.yaw-yaw)<0.0001f);
    CHECK(fabsf(sim->actors[0].controller.pitch-pitch)<0.0001f);

    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_LEFT,0); frame(app,view,sim);
    escape(app,view,sim); // Pause while holding the trigger.
    CHECK(app->screen==SWAT_SCREEN_PAUSE && !app->captured && !IsCursorHidden());
    CHECK(!WindowShouldClose() && !confined_to_window());
    int tick=sim->tick;
    event(TEST_MOUSE_UP,MOUSE_BUTTON_LEFT,0); frame(app,view,sim);
    frames(app,view,sim,15);
    CHECK(app->screen==SWAT_SCREEN_PAUSE && sim->tick==tick && !app->restart_requested);
    app->networked=true; app->leader=false;
    click(app,view,sim,720,425); // Non-leader cannot restart the shared round.
    CHECK(!app->restart_requested && app->screen==SWAT_SCREEN_PAUSE);
    event(TEST_KEY_DOWN,KEY_BACKSPACE,0); frame(app,view,sim);
    event(TEST_KEY_UP,KEY_BACKSPACE,0); frame(app,view,sim);
    CHECK(!app->restart_requested);
    app->networked=false; app->leader=true;
    SwatInput neutral=swat_frontend_input(app,sim);
    CHECK(!neutral.fire && neutral.forward==0 && neutral.yaw_delta==0);

    click(app,view,sim,720,365); // Pause -> Settings.
    CHECK(app->screen==SWAT_SCREEN_SETTINGS);
    event(TEST_MOUSE_POSITION,750,244);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_LEFT,0); frame(app,view,sim);
    escape(app,view,sim); // Escape while dragging must cancel raygui's exclusive drag.
    CHECK(app->screen==SWAT_SCREEN_PAUSE);
    event(TEST_MOUSE_UP,MOUSE_BUTTON_LEFT,0); frames(app,view,sim,2);
    click(app,view,sim,720,305); // Resume must still be clickable.
    CHECK(app->screen==SWAT_SCREEN_GAME && app->captured && confined_to_window());

#if defined(_WIN32)
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN); // Keep this test pumping while minimized.
    HWND window=(HWND)GetWindowHandle();
    ShowWindow(window,SW_MINIMIZE);
    frames(app,view,sim,4);
    CHECK(app->screen==SWAT_SCREEN_PAUSE && !app->captured && !confined_to_window());
    tick=sim->tick;
    ShowWindow(window,SW_RESTORE); focus_test_window();
    frames(app,view,sim,12);
    CHECK(app->screen==SWAT_SCREEN_PAUSE && sim->tick==tick && !app->captured);
#else
    escape(app,view,sim);
#endif
    click(app,view,sim,720,545); // Escape menu -> Quit.
    CHECK(app->quit && !app->captured && !confined_to_window());
    puts("PASS frontend: solo start/pause/resume/quit, host/join setup and cancel, leader restart controls, actual cursor confinement/release, stationary mouse, direction, focus loss, menu click isolation, slider cancel and saved settings");
    return true;
}

static bool run_house(SwatFrontend* app,SwatView* view,SwatSim* sim,const char* plan_png,const char* wand_png,const char* scope_png,const char* camera_png,const char* charge_png,const char* pick_png,const char* cuff_png) {
    app->quit=false; app->networked=false; app->actor=0; app->selected_kit=0;
    sim->config.mission=SWAT_HOUSE; swat_sim_reset(sim);
    swat_frontend_set_screen(app,SWAT_SCREEN_MAIN); frames(app,view,sim,3);
    click(app,view,sim,720,305);
    CHECK(app->screen==SWAT_SCREEN_PLAN && !app->captured && sim->tick==0);
    click(app,view,sim,1180,174); CHECK(app->plan_tab==1);
    click(app,view,sim,1220,365); CHECK(app->selected_kit==1 && app->loadout_pending);
    click(app,view,sim,1080,174); CHECK(app->plan_tab==0);
    if(plan_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,plan_png); UnloadImage(picture); CHECK(saved); }
    click(app,view,sim,1220,350); CHECK(app->plan_preview==1);
    click(app,view,sim,1220,750); frames(app,view,sim,4);
    CHECK(app->screen==SWAT_SCREEN_GAME && app->captured && sim->actors[0].gear.kit==1);
    CHECK(sim->actors[0].arsenal.primary==2 && sim->actors[0].arsenal.shots==0);
    event(TEST_KEY_DOWN,KEY_P,0); frame(app,view,sim);
    event(TEST_KEY_UP,KEY_P,0); frame(app,view,sim);
    CHECK(app->screen==SWAT_SCREEN_PLAN && !app->captured);
    int tick=sim->tick; frames(app,view,sim,5); CHECK(sim->tick==tick);
    escape(app,view,sim); frames(app,view,sim,3); CHECK(app->captured);
    SwatController* c=&sim->actors[0].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){3.3f,c->body.totalHeight*.5f+.01f,-2},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0)); c->yaw=c->pitch=0;
    event(TEST_KEY_DOWN,KEY_G,0); frames(app,view,sim,30);
    CHECK(sim->actors[0].gear.inspecting && swat_sim_inspection_camera(sim,0).x>4.1f);
    CHECK(sim->actors[0].gear.wand_mode==SWAT_WAND_UNDER && sim->actors[0].controller.body.crouched);
    if(wand_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,wand_png); UnloadImage(picture); CHECK(saved); }
    b3Pos lens=swat_sim_inspection_camera(sim,0); float body_yaw=c->yaw;
    Vector2 position=GetMousePosition(); event(TEST_MOUSE_POSITION,(int)position.x+100,(int)position.y);
    frame(app,view,sim);
    CHECK(b3Distance(lens,swat_sim_inspection_camera(sim,0))<.01f && c->yaw==body_yaw && sim->actors[0].gear.wand_yaw>0);
    event(TEST_KEY_UP,KEY_G,0); frame(app,view,sim);
    CHECK(!sim->actors[0].gear.inspecting);
    event(TEST_KEY_DOWN,KEY_P,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_P,0); frame(app,view,sim);
    click(app,view,sim,1270,174); CHECK(app->plan_tab==2);
    click(app,view,sim,1220,394); CHECK(app->sniper_post[0]==2);
    click(app,view,sim,1220,565); CHECK(app->sniper_pending[0]);
    escape(app,view,sim); frames(app,view,sim,5);
    CHECK(app->screen==SWAT_SCREEN_GAME && app->captured && app->camera_open && view->camera_target.id);
    CHECK(view->camera_target.texture.width==512 && view->camera_target.texture.height==288);
    CHECK(sim->snipers[0].deployed && sim->snipers[0].post==2);
    b3Pos walking=swat_body_feet_position(&c->body);
    event(TEST_KEY_DOWN,KEY_S,0); frames(app,view,sim,12); event(TEST_KEY_UP,KEY_S,0); frame(app,view,sim);
    CHECK(b3Distance(walking,swat_body_feet_position(&c->body))>.15f);
    int officer_shots=sim->actors[0].arsenal.shots;
    event(TEST_KEY_DOWN,KEY_TAB,0); frames(app,view,sim,3);
    CHECK(app->screen==SWAT_SCREEN_GAME && app->camera_pointer && !app->captured && !confined_to_window());
    tick=sim->tick; frames(app,view,sim,3); CHECK(sim->tick==tick+3);
    SwatCameraLayout camera=swat_camera_layout(GetScreenWidth(),GetScreenHeight(),0,true);
    CHECK(view->hud_font.texture.id);
    click(app,view,sim,(int)camera.next.x+16,(int)camera.next.y+14);
    CHECK(app->selected_sniper==1 && sim->actors[0].arsenal.active==0);
    camera=swat_camera_layout(GetScreenWidth(),GetScreenHeight(),0,false);
    click(app,view,sim,(int)camera.previous.x+16,(int)camera.previous.y+14);
    CHECK(app->selected_sniper==0);
    camera=swat_camera_layout(GetScreenWidth(),GetScreenHeight(),0,true);
    click(app,view,sim,(int)camera.unit[1].x+20,(int)camera.unit[1].y+14);
    CHECK(app->selected_sniper==1 && sim->actors[0].arsenal.active==0);
    camera=swat_camera_layout(GetScreenWidth(),GetScreenHeight(),0,false);
    click(app,view,sim,(int)camera.takeover.x+70,(int)camera.takeover.y+14);
    CHECK(sim->snipers[1].deployed && sim->actors[0].arsenal.shots==officer_shots);
    camera=swat_camera_layout(GetScreenWidth(),GetScreenHeight(),0,true);
    click(app,view,sim,(int)camera.unit[0].x+20,(int)camera.unit[0].y+14);
    if(camera_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,camera_png); UnloadImage(picture); CHECK(saved); }
    app->networked=true; app->leader=false;
    click(app,view,sim,(int)camera.feed.x+180,(int)camera.feed.y+100);
    CHECK(app->screen==SWAT_SCREEN_GAME && sim->actors[0].arsenal.shots==officer_shots);
    app->networked=false; app->leader=true;
    click(app,view,sim,(int)camera.feed.x+180,(int)camera.feed.y+100);
    CHECK(app->screen==SWAT_SCREEN_SCOPE && !app->captured && app->camera_pointer);
    event(TEST_KEY_UP,KEY_TAB,0); frames(app,view,sim,5);
    CHECK(app->screen==SWAT_SCREEN_SCOPE && app->captured && sim->snipers[0].deployed && sim->snipers[0].post==2);
    CHECK(sim->actors[0].arsenal.shots==officer_shots && sim->actors[swat_sniper_actor(0)].arsenal.shots==0);
    event(TEST_KEY_DOWN,KEY_Y,0); frames(app,view,sim,30); event(TEST_KEY_UP,KEY_Y,0); frame(app,view,sim);
    CHECK(sim->snipers[0].target==1);
    CHECK(sim->snipers[0].status==SWAT_SNIPER_STEADYING || sim->snipers[0].status==SWAT_SNIPER_READY);
    frames(app,view,sim,20);
    CHECK(sim->snipers[0].status==SWAT_SNIPER_READY && sim->actors[swat_sniper_actor(0)].controller.ads>=.98f);
    if(scope_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,scope_png); UnloadImage(picture); CHECK(saved); }
    b3Pos officer=swat_body_feet_position(&c->body);
    event(TEST_KEY_DOWN,KEY_SPACE,0); frames(app,view,sim,3); event(TEST_KEY_UP,KEY_SPACE,0); frame(app,view,sim);
    CHECK(sim->actors[swat_sniper_actor(0)].arsenal.shots==1 && !sim->actors[1].alive);
    CHECK(b3Distance(officer,swat_body_feet_position(&c->body))<.02f);
    CHECK(app->camera_expansion>.99f);
    CHECK(view->camera_target.texture.width==1024 && view->camera_target.texture.height==576);
    escape(app,view,sim); frames(app,view,sim,25);
    CHECK(app->screen==SWAT_SCREEN_GAME && app->captured && app->camera_open && app->camera_expansion<.01f);
    CHECK(view->camera_target.texture.width==512);
    const int live_keys[]={KEY_PERIOD,KEY_COMMA,KEY_SLASH,KEY_BACKSLASH,KEY_PAGE_DOWN,KEY_PAGE_UP};
    for(int i=0;i<6;i++) {
        event(TEST_KEY_DOWN,live_keys[i],0); frame(app,view,sim); event(TEST_KEY_UP,live_keys[i],0); frame(app,view,sim);
        CHECK(app->screen==SWAT_SCREEN_GAME && app->selected_sniper==(i%2 ? 0 : 1) && sim->actors[0].arsenal.active==0);
    }
    float preview_yaw=c->yaw; Vector2 preview_pointer=GetMousePosition();
    event(TEST_MOUSE_POSITION,(int)preview_pointer.x+100,(int)preview_pointer.y);
    event(TEST_KEY_DOWN,KEY_RIGHT_BRACKET,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_RIGHT_BRACKET,0); frame(app,view,sim);
    CHECK(app->selected_sniper==1 && sim->actors[0].arsenal.active==0 && fabsf(c->yaw-preview_yaw)>.001f);
    event(TEST_KEY_DOWN,KEY_TAB,0); frames(app,view,sim,3);
    click(app,view,sim,(int)camera.close.x+14,(int)camera.close.y+13);
    CHECK(!app->camera_open && app->screen==SWAT_SCREEN_GAME);
    event(TEST_KEY_UP,KEY_TAB,0); frames(app,view,sim,3);
    CHECK(app->captured && confined_to_window());
    event(TEST_KEY_DOWN,KEY_COMMA,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_COMMA,0); frame(app,view,sim);
    CHECK(app->camera_open && app->captured && app->selected_sniper==0);
    event(TEST_KEY_DOWN,KEY_PERIOD,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_PERIOD,0); frame(app,view,sim);
    CHECK(app->selected_sniper==1);
    event(TEST_KEY_DOWN,KEY_N,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_N,0); frame(app,view,sim);
    CHECK(!app->camera_open);
    event(TEST_KEY_DOWN,KEY_N,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_N,0); frame(app,view,sim);
    CHECK(app->camera_open);
    event(TEST_KEY_DOWN,KEY_ENTER,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_ENTER,0); frame(app,view,sim);
    CHECK(app->screen==SWAT_SCREEN_SCOPE && app->selected_sniper==1);
    event(TEST_KEY_DOWN,KEY_ENTER,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_ENTER,0); frame(app,view,sim);
    CHECK(app->screen==SWAT_SCREEN_GAME);
    b3Body_SetTransform(c->body.body,(b3Pos){3.1f,c->body.totalHeight*.5f+.01f,-2},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0)); c->yaw=c->pitch=0;
    frames(app,view,sim,3);
    SwatHit door=swat_world_ray(&sim->world,swat_controller_eye(c),swat_controller_aim(c),1.7f,c->body.body);
    CHECK(door.kind==SWAT_HIT_WORLD && sim->world.objects[door.index].locked);
    event(TEST_KEY_DOWN,KEY_F,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_F,0); frame(app,view,sim);
    CHECK(sim->world.objects[door.index].locked && !sim->world.objects[door.index].door_open);
    event(TEST_KEY_DOWN,KEY_Z,0); frames(app,view,sim,20);
    CHECK(c->ads>.5f && !sim->actors[0].gear.door_ticks);
    event(TEST_KEY_UP,KEY_Z,0); frames(app,view,sim,20);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_RIGHT,0); frames(app,view,sim,30);
    CHECK(app->right_action==SWAT_RIGHT_PICK && sim->actors[0].gear.door_ticks==30 && c->ads<.01f);
    if(pick_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,pick_png); UnloadImage(picture); CHECK(saved); }
    event(TEST_MOUSE_UP,MOUSE_BUTTON_RIGHT,0); frame(app,view,sim);
    CHECK(!sim->actors[0].gear.door_ticks && sim->world.objects[door.index].locked);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_RIGHT,0); event(TEST_MOUSE_DOWN,MOUSE_BUTTON_LEFT,0);
    frames(app,view,sim,180);
    CHECK(!sim->world.objects[door.index].locked && !sim->world.objects[door.index].door_open && sim->actors[0].arsenal.shots==officer_shots);
    frames(app,view,sim,5); CHECK(app->right_action==SWAT_RIGHT_PICK && c->ads<.01f && sim->actors[0].arsenal.shots==officer_shots);
    event(TEST_MOUSE_UP,MOUSE_BUTTON_RIGHT,0); frames(app,view,sim,4);
    CHECK(app->wait_for_release && sim->actors[0].arsenal.shots==officer_shots);
    event(TEST_MOUSE_UP,MOUSE_BUTTON_LEFT,0); frames(app,view,sim,2);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_RIGHT,0); frames(app,view,sim,20);
    CHECK(app->right_action==SWAT_RIGHT_AIM && c->ads>.5f);
    event(TEST_MOUSE_UP,MOUSE_BUTTON_RIGHT,0); frames(app,view,sim,20);
    event(TEST_KEY_DOWN,KEY_SEVEN,0); frames(app,view,sim,90); event(TEST_KEY_UP,KEY_SEVEN,0); frame(app,view,sim);
    CHECK(sim->world.objects[door.index].breach_owner==0 && sim->actors[0].gear.breaching_charges==0);
    if(charge_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,charge_png); UnloadImage(picture); CHECK(saved); }
    b3Body_SetTransform(c->body.body,(b3Pos){.5f,c->body.totalHeight*.5f+.01f,-2},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
    event(TEST_KEY_DOWN,KEY_K,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_K,0); frame(app,view,sim);
    CHECK(!sim->world.objects[door.index].active && sim->actors[0].health==100);
    SwatController* civilian=&sim->actors[2].controller;
    CHECK(sim->actors[2].alive && !sim->actors[2].gear.restrained && !sim->actors[2].gear.surrendered);
    b3Body_SetTransform(civilian->body.body,(b3Pos){1.8f,civilian->body.totalHeight*.5f+.01f,-2},b3Quat_identity);
    b3Body_SetLinearVelocity(civilian->body.body,swat_v(0,0,0)); civilian->yaw=SWAT_PI;
    frames(app,view,sim,3);
    event(TEST_KEY_DOWN,KEY_F,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_F,0); frames(app,view,sim,3);
    CHECK(sim->actors[2].gear.surrendered && !sim->actors[2].gear.restrained);
    c->pitch=atan2f(.6f-swat_controller_eye(c).y,1.3f);
    CHECK(swat_context(sim,0).action==SWAT_CONTEXT_CUFF && swat_context(sim,0).ready);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_RIGHT,0); frames(app,view,sim,24);
    CHECK(app->right_action==SWAT_RIGHT_CUFF && sim->actors[0].gear.cuff_ticks==24 && c->ads<.01f);
    if(cuff_png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,cuff_png); UnloadImage(picture); CHECK(saved); }
    event(TEST_MOUSE_UP,MOUSE_BUTTON_RIGHT,0); frame(app,view,sim);
    CHECK(!sim->actors[0].gear.cuff_ticks && !sim->actors[2].gear.restrained);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_RIGHT,0); event(TEST_MOUSE_DOWN,MOUSE_BUTTON_LEFT,0); frames(app,view,sim,72);
    CHECK(sim->actors[2].gear.restrained && sim->actors[0].arsenal.shots==officer_shots);
    frames(app,view,sim,5); CHECK(app->right_action==SWAT_RIGHT_CUFF && c->ads<.01f && sim->actors[0].arsenal.shots==officer_shots);
    event(TEST_MOUSE_UP,MOUSE_BUTTON_RIGHT,0); event(TEST_MOUSE_UP,MOUSE_BUTTON_LEFT,0); frame(app,view,sim);
    c->pitch=0;
    event(TEST_KEY_DOWN,KEY_M,0); frames(app,view,sim,3);
    CHECK(app->squad_pointer && !app->captured && !confined_to_window());
    Image squad_picture=LoadImageFromScreen(); CHECK(ExportImage(squad_picture,"build/swat/windows/squad-menu.png")); UnloadImage(squad_picture);
    click(app,view,sim,(int)(GetScreenWidth()*.63f)+45,GetScreenHeight()/2-105+23+8);
    CHECK(sim->actors[0].arsenal.shots==officer_shots && app->screen==SWAT_SCREEN_GAME);
    event(TEST_KEY_UP,KEY_M,0); frames(app,view,sim,5);
    CHECK(!app->squad_pointer && app->captured && confined_to_window() && !app->wait_for_release);
    printf("PASS house frontend: planning/loadout, under-door lens, live camera keyboard/button cycles without weapon changes, pointer without pause, leader-only floating scope, mark/execute, minimal text controls, F use/compliance, Z aim, held/aborted RMB pick/cuff and no completion click leakage; HRTF=%s\n",test_sound.spatial ? "active" : "fallback");
    return true;
}

static bool run_devices(SwatFrontend* app,SwatView* view,SwatSim* sim) {
    SwatController* c=&sim->actors[0].controller; c->yaw=c->pitch=0;
    app->selected_gadget=0; int shots=sim->actors[0].arsenal.shots;
    event(TEST_KEY_DOWN,KEY_LEFT_ALT,0);
    for(int i=0;i<4;i++) { event(TEST_KEY_DOWN,KEY_TWO,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_TWO,0); frame(app,view,sim); }
    event(TEST_KEY_UP,KEY_LEFT_ALT,0); frames(app,view,sim,4); CHECK(app->selected_gadget==4);
    event(TEST_MOUSE_DOWN,MOUSE_BUTTON_LEFT,0); frames(app,view,sim,50); event(TEST_MOUSE_UP,MOUSE_BUTTON_LEFT,0); frames(app,view,sim,4);
    CHECK(sim->devices[0].active && sim->devices[0].kind==SWAT_CAMERA && sim->actors[0].gear.devices[0]==0 && sim->actors[0].arsenal.shots==shots);
    app->selected_sniper=1;
    event(TEST_KEY_DOWN,KEY_PERIOD,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_PERIOD,0); frame(app,view,sim);
    CHECK(app->selected_sniper==SWAT_SNIPERS && app->screen==SWAT_SCREEN_GAME);
    event(TEST_KEY_DOWN,KEY_ENTER,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_ENTER,0); frames(app,view,sim,12);
    CHECK(app->screen==SWAT_SCREEN_SCOPE && app->captured && swat_feed_present(sim,SWAT_SNIPERS));
    b3Pos feet=swat_body_feet_position(&c->body);
    event(TEST_KEY_DOWN,KEY_W,0); frames(app,view,sim,20); event(TEST_KEY_UP,KEY_W,0); frames(app,view,sim,3);
    CHECK(b3Distance(feet,swat_body_feet_position(&c->body))<.1f && sim->actors[0].arsenal.shots==shots);
    Image remote=LoadImageFromScreen(); CHECK(ExportImage(remote,"build/swat/windows/remote-camera.png")); UnloadImage(remote);
    event(TEST_KEY_DOWN,KEY_ENTER,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_ENTER,0); frames(app,view,sim,4);
    CHECK(app->screen==SWAT_SCREEN_GAME);
    event(TEST_KEY_DOWN,KEY_ONE,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_ONE,0); frames(app,view,sim,3);
    CHECK(app->selected_gadget==0 && sim->actors[0].arsenal.shots==shots);
    puts("PASS remote frontend: Alt equipment cycle, held physical camera deployment, live feed cycling, floating takeover, officer/fire isolation and weapon return");
    return true;
}

static bool run_generation(SwatFrontend* app,SwatView* view,SwatSim* sim,const char* png) {
    event(TEST_KEY_DOWN,KEY_P,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_P,0); frame(app,view,sim);
    click(app,view,sim,1370,174); CHECK(app->plan_tab==3);
    app->networked=true; app->leader=false;
    click(app,view,sim,1120,412); CHECK(sim->config.mission==SWAT_HOUSE && !app->scenario_requested);
    app->networked=false; app->leader=true;
    click(app,view,sim,1120,412); frames(app,view,sim,3);
    CHECK(sim->config.mission==SWAT_GENERATED && sim->layout.policy_id!=0 && sim->layout.seed==1);
    CHECK(app->screen==SWAT_SCREEN_PLAN && sim->tick==0);
    if(png) { Image picture=LoadImageFromScreen(); bool saved=ExportImage(picture,png); UnloadImage(picture); CHECK(saved); }
    click(app,view,sim,1220,750); frames(app,view,sim,310);
    CHECK(app->feedback.current.played_ticks>=300);
    event(TEST_KEY_DOWN,KEY_P,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_P,0); frame(app,view,sim);
    click(app,view,sim,1320,412); frames(app,view,sim,3);
    CHECK(sim->layout.seed==2 && app->feedback.previous.seed==1 && !swat_feedback_ready(&app->feedback));
    click(app,view,sim,1220,750); frames(app,view,sim,310);
    event(TEST_KEY_DOWN,KEY_P,0); frame(app,view,sim); event(TEST_KEY_UP,KEY_P,0); frame(app,view,sim);
    CHECK(swat_feedback_ready(&app->feedback));
    click(app,view,sim,1220,618); CHECK(app->feedback.voted);
    char path[SWAT_SETTINGS_PATH_SIZE]; snprintf(path,sizeof(path),"%s.layouts.jsonl",app->settings_path);
    FILE* file=fopen(path,"rb"); CHECK(file!=NULL);
    char line[2048]; CHECK(fgets(line,sizeof(line),file)!=NULL); fclose(file);
    CHECK(strstr(line,"\"source\":\"player\"") && strstr(line,"\"choice\":\"b\""));
    remove(path); // Only this test-owned preference path is removed.
    puts("PASS generated frontend: leader-only house selection, learned seeded build, deploy/next, two played houses and explicit local comparison");
    return true;
}

int main(int argc, char** argv) {
    if(argc<2) { fprintf(stderr,"usage: test_frontend SETTINGS_TEST_PATH [SCREENSHOT]\n"); return 2; }
    SetTraceLogLevel(LOG_WARNING);
    SwatView view={0}; swat_view_init(&view,false);
    if(!view.initialized) return 2;
    SwatSim* sim=calloc(1,sizeof(*sim));
    if(!sim) { swat_view_close(&view); return 2; }
    SwatConfig config=swat_default_config();
    config.hostile_fire=false; config.randomize=false; config.max_ticks=6000;
    swat_sim_init(sim,config,42);
    SwatFrontend app;
    swat_frontend_init(&app,argv[1]);
    swat_sound_view_init(&test_sound);
    printf("GUI audio stream: %s\n",test_sound.initialized ? "active" : "no output device available");
    app.settings=app.saved_settings=swat_settings_defaults();
    bool ok=run_checks(&app,&view,sim,argv[1],argc>2 ? argv[2] : NULL);
    if(ok) ok=run_house(&app,&view,sim,argc>3 ? argv[3] : NULL,argc>4 ? argv[4] : NULL,argc>5 ? argv[5] : NULL,argc>7 ? argv[7] : NULL,argc>8 ? argv[8] : NULL,argc>9 ? argv[9] : NULL,argc>10 ? argv[10] : NULL);
    if(ok) ok=run_devices(&app,&view,sim);
    if(ok) ok=run_generation(&app,&view,sim,argc>6 ? argv[6] : NULL);
    if(ok) {
        swat_frontend_restored(&app,sim);
        SwatInput restored_input=swat_frontend_input(&app,sim);
        ok=app.last_episode==sim->episode && !app.profiles_pending && !app.loadout_pending &&
            app.selected_primary==sim->actors[0].arsenal.primary && !restored_input.primary_profile && !restored_input.loadout;
        if(ok) puts("PASS save frontend: F5/F9 requests and restored loadout reconciliation without fresh equipment commits");
    }
    swat_frontend_close(&app);
    swat_sound_view_close(&test_sound);
    swat_sim_close(sim); free(sim);
    swat_view_close(&view);
    remove(argv[1]); // The path is explicitly test-owned.
    return ok ? 0 : 1;
}
