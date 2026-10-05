// Reproducible player cost breakdown; no frame limiter and no policy shortcut.
#include "render.h"
#include "sound_view.h"
#include "replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

static double seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER ticks,frequency; QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&frequency);
    return (double)ticks.QuadPart/frequency.QuadPart;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static int compare(const void* a,const void* b) {
    double x=*(const double*)a,y=*(const double*)b; return (x>y)-(x<y);
}
static void report(const char* name,double* values,int count) {
    double total=0; for(int i=0;i<count;i++) total+=values[i];
    qsort(values,(size_t)count,sizeof(*values),compare);
    printf("%s mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
        name,total/count*1000,values[count/2]*1000,values[count*95/100]*1000,values[count-1]*1000);
}
int main(int argc,char** argv) {
    bool headless=false,audio=false,draw_only=false,planning=false,scope=false,expanded=false;
    const char* record_path=NULL;
    const char* capture=NULL;
    int frames=300,mission=SWAT_HOUSE;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--headless")) headless=true;
        else if(!strcmp(argv[i],"--audio")) audio=true;
        else if(!strcmp(argv[i],"--draw-only")) draw_only=true;
        else if(!strcmp(argv[i],"--plan")) planning=true;
        else if(!strcmp(argv[i],"--camera")) scope=true;
        else if(!strcmp(argv[i],"--expanded-camera")) scope=expanded=true;
        else if(!strcmp(argv[i],"--generated")) mission=SWAT_GENERATED;
        else if(!strcmp(argv[i],"--record") && ++i<argc) record_path=argv[i];
        else if(!strcmp(argv[i],"--capture") && ++i<argc) capture=argv[i];
        else if(!strcmp(argv[i],"--frames") && ++i<argc) frames=atoi(argv[i]);
        else { fprintf(stderr,"usage: performance_tool [--headless] [--audio] [--draw-only] [--plan] [--camera|--expanded-camera] [--generated] [--record FILE] [--capture PNG] [--frames 30..3600]\n"); return 1; }
    }
    if(frames<30 || frames>3600 || (headless && (audio || draw_only || capture)) || (record_path && draw_only)) return 1;
    SwatSim* sim=calloc(1,sizeof(*sim)); double* measurements=calloc((size_t)frames*5,sizeof(double));
    if(!sim || !measurements) { free(sim); free(measurements); return 1; }
    SwatConfig config=swat_default_config(); config.mission=mission; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=36000;
    double start=seconds(); swat_sim_init(sim,config,42); double initialization=seconds()-start;
    SwatView view={0}; SwatSoundView sound={0};
    if(!headless) {
        swat_view_init(&view,false); if(!view.initialized) { swat_sim_close(sim); free(sim); free(measurements); return 1; }
        SetTargetFPS(0); view.planning=planning; view.sniper_camera=scope;
        view.scope=expanded; view.camera_expansion=expanded ? 1 : 0;
        if(audio) swat_sound_view_init(&sound);
    }
    SwatInput input=swat_neutral_input();
    if(scope) { input.sniper_order=SWAT_SNIPER_ASSIGN; input.sniper_unit=0; input.sniper_post=0; }
    SwatReplay recording={0};
    if(record_path && !swat_replay_record(&recording,record_path,sim)) {
        fprintf(stderr,"Cannot record benchmark to %s\n",record_path);
        swat_sound_view_close(&sound); swat_view_close(&view); swat_sim_close(sim); free(sim); free(measurements); return 1;
    }
    start=seconds(); swat_sim_step(sim,&input); double first_tick=seconds()-start;
    bool valid=!record_path || swat_replay_append(&recording,&input,sim);
    input.sniper_order=SWAT_SNIPER_NONE;
    for(int i=0;i<frames+30;i++) {
        double t=seconds(); if(!draw_only) swat_sim_step(sim,&input);
        if(recording.file && !swat_replay_append(&recording,&input,sim)) valid=false;
        double tick=seconds()-t;
        t=seconds(); if(audio) swat_sound_view_update(&sound,sim,0,1,0,0); double mix=seconds()-t;
        t=seconds();
        if(!headless) { BeginDrawing(); swat_view_draw(&view,sim,false,75); }
        double drawing=seconds()-t; t=seconds(); if(!headless) EndDrawing(); double present=seconds()-t;
        if(i>=30) {
            int n=i-30; measurements[n]=tick; measurements[frames+n]=mix;
            measurements[2*frames+n]=drawing; measurements[3*frames+n]=present;
            measurements[4*frames+n]=tick+mix+drawing+present;
        }
    }
    printf("SWAT PERF mission=%s seed=42 objects=%d actors=%d frames=%d draw_only=%d audio=%d planning=%d camera=%d expanded=%d init_ms=%.3f first_tick_ms=%.3f\n",
        mission==SWAT_GENERATED ? "generated" : "house",sim->world.count,sim->actor_count,frames,draw_only,audio,planning,scope,expanded,initialization*1000,first_tick*1000);
    report("simulation",measurements,frames); report("audio",measurements+frames,frames);
    report("draw_submit",measurements+2*frames,frames); report("present",measurements+3*frames,frames);
    report("total",measurements+4*frames,frames);
    if(view.characters) printf("character poses=%u draws=%u prepare_mean_ms=%.3f prepare_max_ms=%.3f\n",view.characters->preparations,view.characters->draws,
        view.characters->preparations ? view.characters->prepare_seconds/view.characters->preparations*1000 : 0,view.characters->prepare_max_seconds*1000);
    if(capture) { Image frame=LoadImageFromScreen(); valid=ExportImage(frame,capture) && valid; UnloadImage(frame); }
    if(recording.file && !swat_replay_close(&recording)) valid=false;
    swat_sound_view_close(&sound); swat_view_close(&view); swat_sim_close(sim); free(sim); free(measurements); return valid ? 0 : 1;
}
