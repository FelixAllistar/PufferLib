// Isolated read-only art lab. Original F clip timing is not gameplay timing.
#include "character_view.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
int main(int argc,char** argv) {
    const char *asset=NULL,*capture=NULL,*clip=NULL; double time=0; int frames=0; bool playing=false,looping=false,gpu=false; bool valid=true;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--asset") && ++i<argc) asset=argv[i];
        else if(!strcmp(argv[i],"--capture") && ++i<argc) capture=argv[i];
        else if(!strcmp(argv[i],"--clip") && ++i<argc) clip=argv[i];
        else if(!strcmp(argv[i],"--time") && ++i<argc) { char* end; time=strtod(argv[i],&end); valid=valid && end!=argv[i] && !*end; }
        else if(!strcmp(argv[i],"--frames") && ++i<argc) { char* end; long value=strtol(argv[i],&end,10); valid=valid && end!=argv[i] && !*end && value>=0 && value<=36000; frames=valid ? (int)value : 0; }
        else if(!strcmp(argv[i],"--play")) playing=true;
        else if(!strcmp(argv[i],"--loop")) looping=true;
        else if(!strcmp(argv[i],"--gpu")) gpu=true;
        else { fprintf(stderr,"usage: character_lab --asset PRIVATE.glb [--clip EXACT_NAME] [--time SEC] [--play] [--loop] [--gpu] [--frames COUNT] [--capture PNG]\n"); return 2; }
    }
    if(!valid || !asset || !isfinite(time) || frames<0 || frames>36000) return 2;
    SetConfigFlags(FLAG_MSAA_4X_HINT|(capture ? FLAG_WINDOW_HIDDEN : 0)); InitWindow(1200,900,"SWAT character art lab");
    if(!IsWindowReady()) return 1;
    SwatCharacterView view={0}; char error[256]; if(!(gpu ? swat_character_view_init_gpu(&view,asset,error,sizeof(error)) : swat_character_view_init(&view,asset,error,sizeof(error)))) { fprintf(stderr,"Character: %s\n",error); CloseWindow(); return 1; }
    if(!clip) clip=swat_character_clip_name(view.asset,0);
    if(!swat_character_view_sample(&view,clip,time)) { fprintf(stderr,"Unknown clip or invalid pose: %s\n",clip ? clip : "rest"); swat_character_view_close(&view); CloseWindow(); return 1; }
    SwatArtInfo info=swat_character_info(view.asset); int clip_index=-1;
    for(int i=0;i<info.clips;i++) if(clip && !strcmp(clip,swat_character_clip_name(view.asset,i))) clip_index=i;
    double duration=swat_character_clip_duration(view.asset,clip_index); time=fmin(duration,fmax(0,time));
    if(looping && duration<=0) { fprintf(stderr,"Loop preview requires a clip with positive duration.\n"); swat_character_view_close(&view); CloseWindow(); return 2; }
    printf("CHARACTER LAB joints=%d all_influences=%d over_four=%d vertices=%d triangles=%d clip=%s\n",info.joints,info.max_influences,info.vertices_over_four,info.vertices,info.triangles,clip ? clip : "rest");
    static SwatSim sim; SwatEnvironmentArt art={0}; SwatLighting light={0}; swat_lighting_init(&light);
    Camera3D camera={{2.8f,1.8f,3.1f},{0,.94f,0},{0,1,0},35,CAMERA_PERSPECTIVE};
    Vector3 offset=Vector3Subtract(camera.position,camera.target);
    float distance=Vector3Length(offset),yaw=atan2f(offset.x,offset.z),pitch=asinf(offset.y/distance);
    swat_lighting_prepare(&light,&sim,camera.position,false,empty); SetTargetFPS(capture ? 0 : 60);
    bool ok=true; int count=0; double total_sample=0,max_sample=0; int samples=0,wraps=0;
    while(!WindowShouldClose()) {
        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 delta=GetMouseDelta(); yaw-=delta.x*.007f; pitch=Clamp(pitch+delta.y*.007f,-1.35f,1.35f);
        }
        distance=Clamp(distance-GetMouseWheelMove()*.3f,.8f,9);
        camera.position=Vector3Add(camera.target,(Vector3){sinf(yaw)*cosf(pitch)*distance,sinf(pitch)*distance,cosf(yaw)*cosf(pitch)*distance});
        if(IsKeyPressed(KEY_SPACE)) playing=!playing;
        double previous=time;
        if(playing) {
            time+=GetFrameTime();
            if(looping && time>=duration) { double cycles=floor(time/duration); wraps=cycles>INT_MAX-wraps ? INT_MAX : wraps+(int)cycles; time=fmod(time,duration); }
            else time=fmin(duration,time);
        }
        if(IsKeyDown(KEY_RIGHT)) time=fmin(duration,time+GetFrameTime());
        if(IsKeyDown(KEY_LEFT)) time=fmax(0,time-GetFrameTime());
        if(IsKeyPressed(KEY_HOME)) time=0;
        if(time!=previous) { double start=GetTime(); ok=swat_character_view_sample(&view,clip,time); double cost=GetTime()-start; samples++; total_sample+=cost; if(cost>max_sample) max_sample=cost; }
        if(!ok) break;
        BeginDrawing(); ClearBackground((Color){68,77,84,255}); BeginMode3D(camera);
        swat_lighting_begin(&light,&art,&sim.world,camera.position); swat_character_view_draw(&view,&light,MatrixIdentity()); swat_lighting_end(&light,&art); EndMode3D();
        DrawText(TextFormat("%s / %.3f s%s",clip ? clip : "Rest pose",time,looping ? " / loop preview" : ""),24,22,18,RAYWHITE);
        DrawText("Source timing / Space play / arrows scrub / Home restart / drag orbit / wheel zoom",24,48,16,(Color){201,207,209,255});
        EndDrawing(); count++;
        if(capture && count>=3) { Image image=LoadImageFromScreen(); ok=ExportImage(image,capture); UnloadImage(image); break; }
        if(frames && count>=frames) break;
    }
    printf("CHARACTER LAB frames=%d samples=%d wraps=%d sample_mean_ms=%.3f sample_max_ms=%.3f\n",count,samples,wraps,samples ? total_sample*1000/samples : 0,max_sample*1000);
    swat_character_view_close(&view); swat_lighting_close(&light); CloseWindow(); return ok ? 0 : 1;
}
