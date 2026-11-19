// Interior prop review (swat/art-props-interior): the real motel mission
// rendered by the production view from arbitrary cameras, actors parked in
// the lot. Used for the reference hill-climb of the props_interior_v2 layer.
//   props_interior_capture OUT_DIR [--width W --height H] [--jpg]
//       [--cam NAME PX PY PZ TX TY TZ FOV]...   (any number of cameras)
//       [--dump]   print every active motel object inside the building
//                  footprint (tag, centre, half, yaw, material, part)
// SWAT_TIME=night and SWAT_PROPS_INTERIOR_V2=0 are honoured as in the game.
// Presentation-only review tool; it never feeds back into the simulation.
#include "render.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SwatSim* sim;
static SwatView view;
static int width=1280,height=720;

static void park(int i,float x,float z) {
    SwatActor* a=&sim->actors[i]; b3Pos f=swat_body_feet_position(&a->controller.body);
    b3Body_SetTransform(a->controller.body.body,(b3Pos){x,f.y+a->controller.body.totalHeight*.5f,z},b3Quat_identity);
    b3Body_SetLinearVelocity(a->controller.body.body,swat_v(0,0,0));
    a->mind.bot=false; a->mind.order=0;
}
typedef struct Shot { char name[64]; float p[3],t[3],fov; } Shot;
int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s OUT_DIR [--cam NAME PX PY PZ TX TY TZ FOV]... [--dump] [--jpg]\n",argv[0]); return 2; }
    const char* out=argv[1]; bool jpg=false,dump=false;
    static Shot shots[64]; int count=0;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--jpg")) jpg=true;
        else if(!strcmp(argv[i],"--dump")) dump=true;
        else if(!strcmp(argv[i],"--cam") && i+8<argc && count<64) {
            Shot* s=&shots[count++]; snprintf(s->name,sizeof(s->name),"%s",argv[++i]);
            for(int k=0;k<3;k++) s->p[k]=(float)atof(argv[++i]);
            for(int k=0;k<3;k++) s->t[k]=(float)atof(argv[++i]);
            s->fov=(float)atof(argv[++i]);
        }
    }
    SetTraceLogLevel(LOG_WARNING);
    sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    for(int i=0,k=0;i<sim->actor_count;i++) if(sim->actors[i].present) { park(i,-8.f+1.2f*k,14.f); k++; }
    for(int t=0;t<30;t++) { SwatInput in=swat_neutral_input(); swat_sim_step(sim,&in); }
    if(dump) for(int i=0;i<sim->world.count;i++) {
        const SwatObject* o=&sim->world.objects[i];
        if(!o->active || o->center.x<-12.5f || o->center.x>8.5f || o->center.z<-10.5f || o->center.z>.3f) continue;
        printf("obj %d %.3f %.3f %.3f %.3f %.3f %.3f %.4f %d %d %d\n",i,(float)o->center.x,(float)o->center.y,(float)o->center.z,
            o->half.x,o->half.y,o->half.z,o->yaw,(int)o->material,(int)o->part,(int)o->door);
    }
    if(!count) { swat_sim_close(sim); free(sim); return 0; }
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT); InitWindow(width,height,"props interior capture");
    swat_view_init(&view,true); assert(view.initialized); view.actor=0;
    char path[1024];
    for(int s=0;s<count;s++) {
        const Shot* h=&shots[s];
        Camera3D cam={{h->p[0],h->p[1],h->p[2]},{h->t[0],h->t[1],h->t[2]},{0,1,0},h->fov,CAMERA_PERSPECTIVE};
        for(int t=0;t<6;t++) { BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing(); }
        Image im=LoadImageFromScreen(); snprintf(path,sizeof(path),"%s/%s.%s",out,h->name,jpg?"jpg":"png");
        ExportImage(im,path); UnloadImage(im); printf("wrote %s\n",path);
    }
    swat_view_close(&view); swat_sim_close(sim); free(sim); return 0;
}
