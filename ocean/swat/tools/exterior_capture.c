// Fixed exterior review cameras for the Briar Court motel building shell.
// Renders the real motel mission (layout seed 42) with the production view
// (lighting, shadows, bloom) and writes one PNG per view. Presentation only:
// the sim is stepped with neutral input so state is the ordinary start.
//   exterior_capture OUT_DIR [--view N] [--width W --height H] [--breach] [--lights-off MASK] [--jpg]
//                    [--eye X,Y,Z --target X,Y,Z --fov F --name NAME]   (one custom view)
//                    [--ortho]  custom view is orthographic, F = view height in metres,
//                               up = -z (north up, street at the bottom) for top-down plans
// --breach blows a wall-breacher opening in the rear wall beside the VEXO tag
// first (decal clipping review); it uses the ordinary world breach path.
// SWAT_TIME=night renders the night mode.
#include "render.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    if(argc<2) { fprintf(stderr,"usage: %s OUT_DIR [--view N] [--width W --height H] [--breach] [--lights-off MASK] [--jpg]\n",argv[0]); return 2; }
    const char* out=argv[1]; int only=-1,width=1600,height=900;unsigned lights_off=0;bool jpg=false,breach=false;
    ExteriorView custom={"custom",{0,1.6f,10},{0,1.6f,0},50};bool use_custom=false,ortho=false;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--view") && i+1<argc) only=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--breach")) breach=true;
        else if(!strcmp(argv[i],"--lights-off") && i+1<argc) lights_off=(unsigned)strtoul(argv[++i],NULL,0);
        else if(!strcmp(argv[i],"--jpg")) jpg=true;
        else if(!strcmp(argv[i],"--eye") && i+1<argc) {use_custom=sscanf(argv[++i],"%f,%f,%f",&custom.eye.x,&custom.eye.y,&custom.eye.z)==3;}
        else if(!strcmp(argv[i],"--target") && i+1<argc) {sscanf(argv[++i],"%f,%f,%f",&custom.target.x,&custom.target.y,&custom.target.z);}
        else if(!strcmp(argv[i],"--fov") && i+1<argc) custom.fov=strtof(argv[++i],NULL);
        else if(!strcmp(argv[i],"--name") && i+1<argc) custom.name=argv[++i];
        else if(!strcmp(argv[i],"--ortho")) ortho=true;
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
    // Authoritative room switches (as if an officer pressed F), e.g. 0x8 = Room 103.
    sim->world.room_light_off_mask=lights_off&((1u<<sim->world.room_count)-1u);
    if(breach) {
        b3Pos at={2.3f,1.0f,-6.05f};int section=-1;
        for(int i=0;i<sim->world.count && section<0;i++) {
            const SwatObject* o=&sim->world.objects[i];
            if(!o->active || o->part!=SWAT_PART_SKIN)continue;
            b3Vec3 d=b3SubPos(at,o->center);float c=cosf(o->yaw),sn=sinf(o->yaw);
            if(fabsf(c*d.x-sn*d.z)<=o->half.x+.05f && fabsf(d.y)<=o->half.y && fabsf(sn*d.x+c*d.z)<=o->half.z)section=i;
        }
        int removed=section>=0?swat_world_breach(&sim->world,section,at):0;
        printf("breach section=%d changed=%d\n",section,removed);
        for(int t=0;t<10;t++) swat_sim_step(sim,&in);
    }
    char path[1024];
    for(int v=0;v<(use_custom?1:VIEW_COUNT);v++) {
        if(!use_custom && only>=0 && only!=v) continue;
        const ExteriorView* view_def=use_custom?&custom:&views[v];
        Camera3D cam={view_def->eye,view_def->target,{0,1,0},view_def->fov,CAMERA_PERSPECTIVE};
        if(use_custom && ortho){cam.up=(Vector3){0,0,-1};cam.projection=CAMERA_ORTHOGRAPHIC;}
        for(int f=0;f<4;f++) { BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing(); }
        BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing();
        Image im=LoadImageFromScreen();
        snprintf(path,sizeof(path),"%s/%s.%s",out,view_def->name,jpg?"jpg":"png"); ExportImage(im,path); UnloadImage(im);
        printf("wrote %s\n",path);
    }
    swat_view_close(&view); swat_sim_close(sim); free(sim); return 0;
}
