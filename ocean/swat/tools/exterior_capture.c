// Fixed exterior review cameras for the Briar Court motel building shell.
// Renders the real motel mission (layout seed 42) with the production view
// (lighting, shadows, bloom) and writes one PNG per view. Presentation only:
// the sim is stepped with neutral input so state is the ordinary start.
//   exterior_capture OUT_DIR [--view N] [--width W --height H]
#include "render.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char* name; Vector3 eye,target; float fov; } ExteriorView;
static const ExteriorView views[]={
    {"ext0_front_wide",   {11.5f,1.75f,13.5f},{-2.5f,1.5f,0.0f},48},
    {"ext1_front_elev",   {-1.0f,1.65f,11.0f},{-1.0f,1.6f,0.0f},58},
    {"ext2_gallery",      {7.3f,1.62f,1.25f},{-9.0f,1.35f,0.6f},62},
    {"ext3_door_detail",  {3.4f,1.55f,3.6f},{1.4f,1.55f,0.0f},50},
    {"ext4_office",       {-6.5f,1.6f,7.0f},{-11.0f,1.4f,0.0f},52},
    {"ext5_rear",         {3.0f,1.75f,-15.0f},{-1.0f,1.5f,-6.0f},55},
    {"ext6_east_sign",    {15.0f,1.8f,9.5f},{6.0f,2.0f,-1.0f},55},
    {"ext7_roofline",     {5.0f,5.5f,9.5f},{-2.0f,2.6f,-1.0f},50},
};
#define VIEW_COUNT (int)(sizeof(views)/sizeof(views[0]))

int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s OUT_DIR [--view N] [--width W --height H]\n",argv[0]); return 2; }
    const char* out=argv[1]; int only=-1,width=1600,height=900;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--view") && i+1<argc) only=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
    }
    SetTraceLogLevel(LOG_WARNING);
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT); InitWindow(width,height,"exterior capture");
    static SwatView view;
    swat_view_init(&view,true); assert(view.initialized); view.actor=0;
    SwatInput in=swat_neutral_input();
    for(int t=0;t<30;t++) swat_sim_step(sim,&in);
    char path[1024];
    for(int v=0;v<VIEW_COUNT;v++) {
        if(only>=0 && only!=v) continue;
        Camera3D cam={views[v].eye,views[v].target,{0,1,0},views[v].fov,CAMERA_PERSPECTIVE};
        for(int f=0;f<4;f++) { BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing(); }
        BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing();
        Image im=LoadImageFromScreen();
        snprintf(path,sizeof(path),"%s/%s.png",out,views[v].name); ExportImage(im,path); UnloadImage(im);
        printf("wrote %s\n",path);
    }
    swat_view_close(&view); swat_sim_close(sim); free(sim); return 0;
}
