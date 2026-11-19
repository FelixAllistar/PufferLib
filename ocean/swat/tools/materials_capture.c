// Materials v3 review captures (presentation only): the real motel mission
// rendered by the production cinematic view from free cameras given on the
// command line, so in-game shots can match a reference photo's viewpoint.
//   materials_capture OUT_DIR NAME:EX,EY,EZ:TX,TY,TZ:FOV [...] [--width W --height H] [--jpg]
// Engine coordinates (Y up). Actors are parked behind the motel so finishes
// are unobstructed. SWAT_MATERIALS_V3=0 renders the previous material set.
#include "render.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void park(SwatSim* sim,int i,float x,float z) {
    SwatActor* a=&sim->actors[i]; b3Pos f=swat_body_feet_position(&a->controller.body);
    b3Body_SetTransform(a->controller.body.body,(b3Pos){x,f.y+a->controller.body.totalHeight*.5f,z},b3Quat_identity);
    b3Body_SetLinearVelocity(a->controller.body.body,swat_v(0,0,0));
    a->mind.bot=false; a->mind.order=0;
}

int main(int argc,char** argv) {
    if(argc<3) { fprintf(stderr,"usage: %s OUT_DIR NAME:EX,EY,EZ:TX,TY,TZ:FOV [...] [--width W --height H] [--jpg]\n",argv[0]); return 2; }
    const char* out=argv[1]; int width=1280,height=720; bool jpg=false;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--jpg")) jpg=true;
    }
    SetTraceLogLevel(LOG_WARNING);
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    for(int i=0,k=0;i<sim->actor_count;i++) if(sim->actors[i].present) { park(sim,i,-20.f+1.2f*k,-20.f); k++; }
    for(int t=0;t<30;t++) { SwatInput in=swat_neutral_input(); swat_sim_step(sim,&in); }
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT); InitWindow(width,height,"materials capture");
    static SwatView view;
    swat_view_init(&view,true); assert(view.initialized); view.actor=0;
    char path[1024];
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--width") || !strcmp(argv[i],"--height")) { i++; continue; }
        if(argv[i][0]=='-') continue;
        char name[128]; float e[3],t[3],fov;
        if(sscanf(argv[i],"%127[^:]:%f,%f,%f:%f,%f,%f:%f",name,&e[0],&e[1],&e[2],&t[0],&t[1],&t[2],&fov)!=8) {
            fprintf(stderr,"bad view %s\n",argv[i]); continue;
        }
        Camera3D cam={{e[0],e[1],e[2]},{t[0],t[1],t[2]},{0,1,0},fov,CAMERA_PERSPECTIVE};
        for(int f=0;f<6;f++) { BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing(); }
        Image im=LoadImageFromScreen(); snprintf(path,sizeof(path),"%s/%s.%s",out,name,jpg?"jpg":"png");
        ExportImage(im,path); UnloadImage(im); printf("wrote %s\n",path);
    }
    swat_view_close(&view); swat_sim_close(sim); free(sim); return 0;
}
