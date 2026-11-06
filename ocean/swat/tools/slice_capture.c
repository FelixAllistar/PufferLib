// Vertical-slice capture: the real motel mission and simulation, rendered by
// the production view (lighting, shadows, bloom, character runtimes) from
// scripted cinematic cameras. Each shot stages actors with ordinary sim state
// (positions, aim, surrender/restraint, escort), advances real ticks so every
// animation is live, and writes one PNG.
//   slice_capture OUT_DIR [--list] [--shot N] [--width W --height H]
#include "render.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SwatSim* sim;
static SwatView view;
static int width=1920,height=1080;

static b3Pos feet(int i) { return swat_body_feet_position(&sim->actors[i].controller.body); }
static void place(int i,float x,float z,float yaw) {
    SwatActor* a=&sim->actors[i]; b3Pos f=feet(i);
    b3Body_SetTransform(a->controller.body.body,(b3Pos){x,f.y+a->controller.body.totalHeight*.5f,z},b3Quat_identity);
    b3Body_SetLinearVelocity(a->controller.body.body,swat_v(0,0,0));
    a->controller.yaw=yaw;
}
static void face(int i,b3Pos target) {
    b3Pos f=feet(i); sim->actors[i].controller.yaw=atan2f((float)(target.z-f.z),(float)(target.x-f.x));
}
static Vector3 V(b3Pos p) { return (Vector3){(float)p.x,(float)p.y,(float)p.z}; }
static void frame(Camera3D cam,const char* path) {
    BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing();
    if(path) { Image im=LoadImageFromScreen(); ExportImage(im,path); UnloadImage(im); if(!strstr(path,"/film/")) printf("wrote %s\n",path); }
}
// Advance the sim with neutral inputs (bots and scripted states run), drawing
// every tick so wall-clock-free presentation stays in step.
static void run(int ticks,Camera3D cam,const SwatInput* player) {
    SwatInput in=player ? *player : swat_neutral_input();
    for(int t=0;t<ticks;t++) { swat_sim_step(sim,&in); frame(cam,NULL); }
}
// Advance without drawing (cheap); draw the last frames for presentation state.
static void advance(int ticks,const SwatInput* player) {
    SwatInput in=player ? *player : swat_neutral_input();
    for(int t=0;t<ticks;t++) swat_sim_step(sim,&in);
}
static void open_door(int index,float angle) {
    SwatObject* d=&sim->world.objects[index]; d->door_open=true; d->door_angle=angle;
    d->yaw=d->closed_yaw+angle; d->center=swat_door_center(d,d->yaw); swat_world_place(d,d->yaw);
}
static Camera3D look(float x,float y,float z,float tx,float ty,float tz,float fov) {
    return (Camera3D){{x,y,z},{tx,ty,tz},{0,1,0},fov,CAMERA_PERSPECTIVE};
}
static int find(int role,int nth) {
    for(int i=0,k=0;i<sim->actor_count;i++) if(sim->actors[i].present && (int)sim->actors[i].role==role && k++==nth) return i;
    return -1;
}
static void reset(void) {
    if(sim) swat_sim_close(sim); else sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=100000;
    swat_sim_init(sim,config,42);
}
int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s OUT_DIR [--list] [--shot N]\n",argv[0]); return 2; }
    const char* out=argv[1]; bool list=false,film=false; int only=-1;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--list")) list=true;
        else if(!strcmp(argv[i],"--film")) film=true;
        else if(!strcmp(argv[i],"--shot") && i+1<argc) only=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
    }
    SetTraceLogLevel(LOG_WARNING);
    reset();
    if(list) {
        for(int i=0;i<sim->actor_count;i++) if(sim->actors[i].present) { b3Pos f=feet(i);
            printf("actor %d role %d feet %.2f %.2f %.2f yaw %.2f\n",i,sim->actors[i].role,f.x,f.y,f.z,sim->actors[i].controller.yaw); }
        printf("extraction %.2f %.2f %.2f\n",sim->extraction.x,sim->extraction.y,sim->extraction.z);
        for(int i=0;i<sim->world.count;i++) { const SwatObject* o=&sim->world.objects[i];
            if(o->door && o->active) printf("door %d center %.2f %.2f %.2f yaw %.2f open %d\n",i,o->center.x,o->center.y,o->center.z,o->closed_yaw,o->door_open); }
        for(int r=0;r<sim->world.room_count;r++) printf("room %d\n",r);
        for(int i=0;i<sim->world.count;i++) { const SwatObject* o=&sim->world.objects[i]; if(!o->active) continue;
            if(o->center.y-o->half.y>2.3f || o->center.y+o->half.y<.15f) continue; // plan slice: 0.15..2.3 m
            printf("box %d c %.2f %.2f %.2f h %.2f %.2f %.2f yaw %.3f door %d\n",i,o->center.x,o->center.y,o->center.z,o->half.x,o->half.y,o->half.z,o->yaw,o->door); }
        return 0;
    }
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT); InitWindow(width,height,"slice capture");
    swat_view_init(&view,true); assert(view.initialized); view.actor=0;
    char path[1024];
    #define HOLD(i) (sim->actors[i].mind.bot=false, sim->actors[i].mind.order=0)
    if(film) {
        #include "slice_film.h"
    } else {
        #include "slice_shots.h"
    }
    swat_view_close(&view); swat_sim_close(sim); free(sim); return 0; // view close owns CloseWindow
}
