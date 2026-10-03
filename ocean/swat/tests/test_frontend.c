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
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s (screen=%d back=%d tick=%d notice=%s mouse=%.0f,%.0f)\n", \
        __FILE__,__LINE__,#condition,app->screen,app->settings_back,sim->tick,app->notice, \
        GetMousePosition().x,GetMousePosition().y); return false; } } while(0)

// Raylib 5.5's documented automation file event IDs (enum lives in rcore.c).
enum { TEST_KEY_UP=1, TEST_KEY_DOWN=2, TEST_MOUSE_UP=5, TEST_MOUSE_DOWN=6, TEST_MOUSE_POSITION=7 };
static SwatSoundView test_sound;
static void event(unsigned int type, int a, int b) {
    PlayAutomationEvent((AutomationEvent){0,type,{a,b,0,0}});
}

static void frame(SwatFrontend* app, SwatView* view, SwatSim* sim) {
    swat_frontend_update(app,sim,false);
    if(app->restart_requested) { swat_sim_reset(sim); app->restart_requested=false; }
    app->reset_input=false;
    if(swat_frontend_playing(app)) {
        SwatInput in=swat_frontend_input(app,sim);
        swat_sim_step(sim,&in);
    }
    swat_sound_view_update(&test_sound,sim,app->actor,app->settings.master_volume,0,0);
    BeginDrawing();
    swat_view_draw(view,sim,false,app->settings.vertical_fov);
    swat_frontend_draw(app,sim,false);
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

static bool confined_to_window(void) {
#if defined(_WIN32)
    RECT clip,client;
    HWND window=(HWND)GetWindowHandle();
    if(!GetClipCursor(&clip) || !GetClientRect(window,&client)) return false;
    POINT first={client.left,client.top},last={client.right,client.bottom};
    ClientToScreen(window,&first); ClientToScreen(window,&last);
    return clip.left==first.x && clip.top==first.y && clip.right==last.x && clip.bottom==last.y;
#else
    return IsCursorHidden();
#endif
}

static bool run_checks(SwatFrontend* app, SwatView* view, SwatSim* sim,
                       const char* preferences, const char* screenshot) {
    frames(app,view,sim,20);
    CHECK(IsWindowFocused());
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
    click(app,view,sim,790,225); // Sensitivity slider.
    click(app,view,sim,746,565); // Invert vertical.
    CHECK(app->settings.sensitivity>0.08f && app->settings.invert_y);
    if(screenshot) {
        Image image=LoadImageFromScreen();
        bool saved=ExportImage(image,screenshot); UnloadImage(image);
        CHECK(saved);
    }
    click(app,view,sim,910,620); // Save & back.
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
    event(TEST_MOUSE_POSITION,750,225);
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
    ShowWindow(window,SW_RESTORE); SetForegroundWindow(window);
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
    swat_frontend_close(&app);
    swat_sound_view_close(&test_sound);
    swat_sim_close(sim); free(sim);
    swat_view_close(&view);
    remove(argv[1]); // The path is explicitly test-owned.
    return ok ? 0 : 1;
}
