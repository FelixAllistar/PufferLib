// Exterior props v2 review capture: the real motel mission through the
// production cinematic view from either six fixed lot/street views or one
// free camera (for matching a reference photo). Presentation review only.
//   props_ext_v2_capture OUT_DIR [--shot N] [--eye x,y,z --target x,y,z --fov F --name NAME]
//                        [--width W --height H] [--jpg]
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char* name; Vector3 eye,target; float fov; } Shot;
static Shot shots[]={
    {"lot_from_street", {0,1.7f,40},     {4,1.5f,6},     62},
    {"lot_east",        {24,2.0f,27},    {2,1.2f,4},     60},
    {"lot_west",        {-24,2.0f,24},   {0,1.2f,4},     60},
    {"street_east",     {-30,1.7f,37.5f},{30,2.5f,34},   58},
    {"rear_service",    {-20,1.8f,-20},  {4,1.5f,-6},    60},
    {"aerial",          {34,26,52},      {0,0,8},        50},
};
#define SHOT_COUNT ((int)(sizeof(shots)/sizeof(shots[0])))

static Vector3 vec(const char* s){Vector3 v={0};sscanf(s,"%f,%f,%f",&v.x,&v.y,&v.z);return v;}

int main(int argc,char** argv) {
    if(argc<2){fprintf(stderr,"usage: %s OUT_DIR [--shot N] [--eye x,y,z --target x,y,z --fov F --name NAME] [--width W --height H] [--jpg]\n",argv[0]);return 2;}
    const char* out=argv[1];int only=-1,width=1280,height=720;bool jpg=false,custom=false;
    Shot free_shot={"custom",{0,1.7f,30},{0,1,0},50};
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--shot") && i+1<argc) only=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--eye") && i+1<argc){free_shot.eye=vec(argv[++i]);custom=true;}
        else if(!strcmp(argv[i],"--target") && i+1<argc) free_shot.target=vec(argv[++i]);
        else if(!strcmp(argv[i],"--fov") && i+1<argc) free_shot.fov=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--name") && i+1<argc) free_shot.name=argv[++i];
        else if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--jpg")) jpg=true;
    }
    SetTraceLogLevel(LOG_WARNING);
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.layout_seed=42;
    config.tactical_rules=true;config.squad_bots=3;config.hostile_fire=false;config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT);InitWindow(width,height,"props ext v2 capture");
    SwatView view;memset(&view,0,sizeof(view));swat_view_init(&view,true);assert(view.initialized);view.actor=0;
    SwatInput in=swat_neutral_input();for(int t=0;t<30;t++)swat_sim_step(sim,&in);
    int count=custom?1:SHOT_COUNT;
    for(int s=0;s<count;s++) {
        if(!custom && only>=0 && s!=only)continue;
        const Shot* shot=custom?&free_shot:&shots[s];
        Camera3D cam={shot->eye,shot->target,{0,1,0},shot->fov,CAMERA_PERSPECTIVE};
        for(int f=0;f<3;f++){BeginDrawing();swat_view_draw_cinematic(&view,sim,cam);EndDrawing();}
        char path[1024];
        if(custom)snprintf(path,sizeof(path),"%s/%s.%s",out,shot->name,jpg?"jpg":"png");
        else snprintf(path,sizeof(path),"%s/%02d_%s.%s",out,s,shot->name,jpg?"jpg":"png");
        Image im=LoadImageFromScreen();ExportImage(im,path);UnloadImage(im);
        printf("wrote %s\n",path);
    }
    swat_view_close(&view);swat_sim_close(sim);free(sim);return 0;
}
