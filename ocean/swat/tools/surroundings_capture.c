// Surroundings review capture: the real motel mission rendered by the
// production cinematic view (lighting, shadows, bloom) from fixed cameras
// that look at the ground, roadside, backdrop and sky. Presentation review
// only: the simulation is initialised and stepped with neutral input.
//   surroundings_capture OUT_DIR [--shot N] [--width W --height H] [--jpg]
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char* name; Vector3 eye,target; float fov; } Shot;
static const Shot shots[]={
    {"overview",      {0,95,62},      {0,0,4},       45},
    {"street",        {-6,1.7f,40},   {2,1.6f,8},    62},
    {"parking",       {14,2.2f,22},   {-6,0.8f,4},   62},
    {"courtyard",     {-8,1.7f,6},    {10,1.4f,-6},  62},
    {"edge_east",     {20,1.8f,-2},   {64,3,-8},     62},
    {"edge_west",     {-18,1.8f,10},  {-64,3,18},    62},
    {"edge_south",    {6,1.8f,30},    {-10,3,60},    62},
    {"edge_north",    {0,1.8f,-20},   {10,3,-60},    62},
    {"sky",           {0,1.7f,34},    {-8,14,-10},   70},
    // Orthographic plan (negative fov = ortho height in metres), for layout.
    {"plan",          {0,120,6.01f},  {0,0,6},       -64},
};
#define SHOT_COUNT ((int)(sizeof(shots)/sizeof(shots[0])))

int main(int argc,char** argv) {
    if(argc<2){fprintf(stderr,"usage: %s OUT_DIR [--shot N] [--width W --height H] [--jpg]\n",argv[0]);return 2;}
    const char* out=argv[1];int only=-1,width=1280,height=720;bool jpg=false;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--shot") && i+1<argc) only=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--jpg")) jpg=true;
    }
    SetTraceLogLevel(LOG_WARNING);
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.layout_seed=42;
    config.tactical_rules=true;config.squad_bots=3;config.hostile_fire=false;config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT);InitWindow(width,height,"surroundings capture");
    SwatView view;memset(&view,0,sizeof(view));swat_view_init(&view,true);assert(view.initialized);view.actor=0;
    SwatInput in=swat_neutral_input();for(int t=0;t<30;t++)swat_sim_step(sim,&in);
    for(int s=0;s<SHOT_COUNT;s++) {
        if(only>=0 && s!=only)continue;
        bool ortho=shots[s].fov<0;
        Camera3D cam={shots[s].eye,shots[s].target,{0,1,0},ortho?-shots[s].fov:shots[s].fov,ortho?CAMERA_ORTHOGRAPHIC:CAMERA_PERSPECTIVE};
        if(ortho)cam.up=(Vector3){0,0,-1};
        double total=0;
        for(int f=0;f<3;f++){double t0=GetTime();BeginDrawing();swat_view_draw_cinematic(&view,sim,cam);EndDrawing();if(f==2)total=GetTime()-t0;}
        char path[1024];snprintf(path,sizeof(path),"%s/%02d_%s.%s",out,s,shots[s].name,jpg?"jpg":"png");
        Image im=LoadImageFromScreen();ExportImage(im,path);UnloadImage(im);
        printf("wrote %s (%.1f ms software GL)\n",path,total*1000);
    }
    swat_view_close(&view);swat_sim_close(sim);free(sim);return 0;
}
