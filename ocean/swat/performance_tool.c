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
static bool replay_step(SwatReplay* replay,SwatSim* sim,SwatInput* input,double* tick,double* verification) {
    double start=seconds();
    uint32_t expected;
    if(swat_replay_next(replay,input,&expected)!=1) {
        fprintf(stderr,"Incomplete replay at frame %u\n",replay->frame);return false;
    }
    double step_start=seconds();swat_sim_step(sim,input);double step_end=seconds();
    if(tick)*tick=step_end-step_start;
    if(swat_replay_digest(sim)!=expected) {
        fprintf(stderr,"Replay digest mismatch at frame %u tick %d\n",replay->frame,sim->tick);return false;
    }
    if(verification)*verification=(step_start-start)+(seconds()-step_end);
    return true;
}
int main(int argc,char** argv) {
    bool headless=false,audio=false,draw_only=false,planning=false,scope=false,expanded=false,indoors=false,fire=false,hidden=false,stereo=false;
    const char* record_path=NULL;
    const char* replay_path=NULL;int seek=0;bool seek_set=false;
    bool wall_faces_reference=false,shadow_events=false;
    int room_index=-1;float camera_yaw=0;bool camera_yaw_set=false;
    const char* capture=NULL;
    int frames=300,mission=SWAT_HOUSE;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--headless")) headless=true;
        else if(!strcmp(argv[i],"--hidden")) hidden=true;
        else if(!strcmp(argv[i],"--audio")) audio=true;
        else if(!strcmp(argv[i],"--audio-stereo")) audio=stereo=true;
        else if(!strcmp(argv[i],"--draw-only")) draw_only=true;
        else if(!strcmp(argv[i],"--plan")) planning=true;
        else if(!strcmp(argv[i],"--camera")) scope=true;
        else if(!strcmp(argv[i],"--expanded-camera")) scope=expanded=true;
        else if(!strcmp(argv[i],"--building")) mission=SWAT_BUILDING;
        else if(!strcmp(argv[i],"--generated")) mission=SWAT_GENERATED;
        else if(!strcmp(argv[i],"--storefront")) mission=SWAT_STOREFRONT;
        else if(!strcmp(argv[i],"--motel")) mission=SWAT_MOTEL;
        else if(!strcmp(argv[i],"--indoors")) indoors=true;
        else if(!strcmp(argv[i],"--room")) {
            if(++i>=argc)return 1;
            char* end;long index=strtol(argv[i],&end,10);
            if(end==argv[i] || *end || index<0 || index>1024)return 1;
            room_index=(int)index;indoors=true;
        }
        else if(!strcmp(argv[i],"--yaw")) {
            if(++i>=argc)return 1;
            char* end;camera_yaw=strtof(argv[i],&end);
            if(end==argv[i] || *end || !isfinite(camera_yaw))return 1;
            camera_yaw_set=true;
        }
        else if(!strcmp(argv[i],"--fire")) fire=true;
        else if(!strcmp(argv[i],"--wall-faces-reference"))wall_faces_reference=true;
        else if(!strcmp(argv[i],"--shadow-events"))shadow_events=true;
        else if(!strcmp(argv[i],"--record") || !strcmp(argv[i],"--capture") || !strcmp(argv[i],"--replay")) {
            const char* option=argv[i];if(++i>=argc)return 1;
            if(!strcmp(option,"--record"))record_path=argv[i];
            else if(!strcmp(option,"--capture"))capture=argv[i];else replay_path=argv[i];
        }
        else if(!strcmp(argv[i],"--frames") || !strcmp(argv[i],"--seek")) {
            bool is_seek=!strcmp(argv[i],"--seek");if(++i>=argc)return 1;
            char* end;long value=strtol(argv[i],&end,10);
            if(end==argv[i] || *end || value<0 || value>INT32_MAX)return 1;
            if(is_seek){seek=(int)value;seek_set=true;}else frames=(int)value;
        }
        else { fprintf(stderr,"usage: performance_tool [--headless|--hidden] [--audio|--audio-stereo] [--draw-only] [--plan] [--camera|--expanded-camera] [--generated|--building|--storefront|--motel] [--indoors|--room INDEX] [--yaw RADIANS] [--fire] [--wall-faces-reference] [--shadow-events] [--record FILE|--replay FILE --seek TICKS] [--capture PNG] [--frames 30..3600]\n"); return 1; }
    }
    if(frames<30 || frames>3600 || (headless && (audio || draw_only || capture)) || (record_path && (draw_only || indoors || fire))) return 1;
    if((seek_set&&!replay_path) || (replay_path&&(record_path || draw_only || indoors || fire || scope || camera_yaw_set || mission!=SWAT_HOUSE)))return 1;
    SwatReplay playback={0};
    if(replay_path && (!swat_replay_open(&playback,replay_path) || (uint64_t)seek+frames+31>playback.count)) {
        fprintf(stderr,"Cannot read requested replay window\n");if(playback.file)swat_replay_close(&playback);return 1;
    }
    SwatSim* sim=calloc(1,sizeof(*sim)); double* measurements=calloc((size_t)frames*11,sizeof(double));
    if(!sim || !measurements) { if(playback.file)swat_replay_close(&playback);free(sim); free(measurements); return 1; }
    SwatConfig config=swat_default_config(); config.mission=mission; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=36000;
    uint32_t seed=42;if(replay_path){config=playback.config;mission=config.mission;seed=playback.seed;}
    double start=seconds(); swat_sim_init(sim,config,seed); double initialization=seconds()-start;
    SwatInput input=swat_neutral_input();
    for(int i=0;i<seek;i++)if(!replay_step(&playback,sim,&input,NULL,NULL)) {
        swat_replay_close(&playback);swat_sim_close(sim);free(sim);free(measurements);return 1;
    }
    if(room_index>=sim->world.room_count){swat_sim_close(sim);free(sim);free(measurements);return 1;}
    if(indoors && sim->world.room_count) {
        int selected=room_index>=0 ? room_index : mission==SWAT_BUILDING ? 1 : 0;
        const SwatRoom* room=&sim->world.rooms[selected];
        b3Pos feet={room->center.x,room->center.y-room->half.y+.02f,room->center.z};
        SwatController* c=&sim->actors[0].controller;
        b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity); b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);
        c->yaw=camera_yaw_set ? camera_yaw : fire ? .55f : 0; c->pitch=c->recoil_pitch=c->recoil_yaw=0;
        printf("indoor_room=%d initial_yaw=%.6f\n",selected,c->yaw);
    }
    if(fire) sim->actors[0].arsenal.slots[0].mode=SWAT_AUTO;
    SwatView view={0}; SwatSoundView sound={0};
    if(!headless) {
        swat_view_init(&view,hidden); if(!view.initialized) { if(playback.file)swat_replay_close(&playback);swat_sim_close(sim); free(sim); free(measurements); return 1; }
        SetTargetFPS(0); view.planning=planning; view.sniper_camera=scope;
        if(wall_faces_reference) {
            swat_environment_art_prepare_location(&view.environment,&sim->world);
            swat_environment_motel_face_batch(&view.environment,false);
        }
        view.scope=expanded; view.camera_expansion=expanded ? 1 : 0;
        if(audio) swat_sound_view_init(&sound);
        if(stereo) { swat_spatial_close(sound.spatial); sound.spatial=NULL; }
    }
    input=swat_neutral_input();
    if(scope) { input.sniper_order=SWAT_SNIPER_ASSIGN; input.sniper_unit=0; input.sniper_post=0; }
    SwatReplay recording={0};
    if(record_path && !swat_replay_record(&recording,record_path,sim)) {
        fprintf(stderr,"Cannot record benchmark to %s\n",record_path);
        swat_sound_view_close(&sound); swat_view_close(&view); swat_sim_close(sim); free(sim); free(measurements); return 1;
    }
    start=seconds();bool valid=true;double first_tick=0;
    if(replay_path)valid=replay_step(&playback,sim,&input,&first_tick,NULL);
    else {swat_sim_step(sim,&input);first_tick=seconds()-start;}
    if(record_path)valid=swat_replay_append(&recording,&input,sim)&&valid;
    input.sniper_order=SWAT_SNIPER_NONE;
    unsigned int sun_start=0,rooms_start=0,actors_start=0,faces_start=0;
    double shadow_max=0;unsigned int max_sun=0,max_rooms=0,max_actors=0,max_faces=0;int max_tick=0;
    int measured_start=0,destroyed_start=0,detonations=0,measured=0;
    for(int i=0;valid && i<frames+30;i++) {
        if(i==30){measured_start=sim->tick;destroyed_start=sim->totals.destroyed;}
        unsigned int sun_before=view.lighting.sun_geometry_updates,rooms_before=view.lighting.room_updates,actors_before=view.lighting.actor_room_updates;
        unsigned int faces_before=view.lighting.actor_face_updates;
        if(!replay_path){input.fire=fire; input.reload=fire && !sim->actors[0].arsenal.slots[0].magazine;}
        // Sweep intact wall areas instead of spending the run firing through
        // the first broken leaf/opening. Keep all impacts on the real weapon path.
        if(indoors && fire) { input.yaw_delta=sinf(i*.065f)*.012f; input.pitch_delta=cosf(i*.043f)*.002f; }
        double replay_tick=0,verification=0;
        double t=seconds();if(replay_path)valid=replay_step(&playback,sim,&input,&replay_tick,&verification);else if(!draw_only) swat_sim_step(sim,&input);
        if(!valid)break;
        if(i>=30 && input.door_tool==SWAT_DETONATE_CHARGE)detonations++;
        if(recording.file && !swat_replay_append(&recording,&input,sim)) valid=false;
        double tick=replay_path?replay_tick:seconds()-t;
        t=seconds(); if(audio) swat_sound_view_update(&sound,sim,0,1,0,0); double mix=seconds()-t;
        t=seconds();
        if(!headless) { BeginDrawing(); swat_view_draw(&view,sim,false,75); }
        double drawing=seconds()-t; t=seconds(); if(!headless) EndDrawing(); double present=seconds()-t;
        if(i>=30) {
            if(i==30){sun_start=sun_before;rooms_start=rooms_before;actors_start=actors_before;faces_start=faces_before;}
            unsigned int sun=view.lighting.sun_geometry_updates-sun_before,rooms=view.lighting.room_updates-rooms_before,actors=view.lighting.actor_room_updates-actors_before;
            unsigned int faces=view.lighting.actor_face_updates-faces_before;
            if(view.shadow_seconds>shadow_max){shadow_max=view.shadow_seconds;max_tick=sim->tick;max_sun=sun;max_rooms=rooms;max_actors=actors;max_faces=faces;}
            if(shadow_events && (sun || rooms || view.shadow_seconds>.030))
                printf("shadow_event tick=%d shadow_ms=%.3f static_sun=%u static_rooms=%u dynamic_rooms=%u dynamic_faces=%u shots=%d destroyed=%d\n",sim->tick,view.shadow_seconds*1000,sun,rooms,actors,faces,sim->actors[0].arsenal.shots,sim->totals.destroyed);
            int n=i-30; measurements[n]=tick; measurements[frames+n]=mix;
            measured++;
            measurements[2*frames+n]=drawing; measurements[3*frames+n]=present;
            measurements[4*frames+n]=tick+mix+drawing+present;
            measurements[5*frames+n]=view.pose_seconds;measurements[6*frames+n]=view.shadow_seconds;
            measurements[7*frames+n]=view.scene_seconds;measurements[8*frames+n]=view.geometry_seconds;
            measurements[9*frames+n]=view.weapon_seconds;
            measurements[10*frames+n]=verification;
        }
    }
    if(measured!=frames) {
        if(playback.file)swat_replay_close(&playback);
        if(recording.file)swat_replay_close(&recording);
        swat_sound_view_close(&sound);swat_view_close(&view);swat_sim_close(sim);free(sim);free(measurements);return 1;
    }
    if(replay_path)printf("replay_verified_frames=%u seek=%d measured_ticks=%d..%d detonation_inputs=%d destroyed_delta=%d\n",playback.frame,seek,measured_start+1,sim->tick,detonations,sim->totals.destroyed-destroyed_start);
    printf("SWAT PERF mission=%s seed=%u objects=%d actors=%d frames=%d draw_only=%d audio=%d planning=%d camera=%d expanded=%d indoors=%d shots=%d destroyed=%d init_ms=%.3f first_tick_ms=%.3f\n",
        swat_mission(mission)->name,seed,sim->world.count,sim->actor_count,frames,draw_only,audio,planning,scope,expanded,indoors,sim->actors[0].arsenal.shots,sim->totals.destroyed,initialization*1000,first_tick*1000);
    report("simulation",measurements,frames); report("audio",measurements+frames,frames);
    if(replay_path)report("replay_verification_excluded_from_total",measurements+10*frames,frames);
    printf("wall_faces_reference=%d\n",wall_faces_reference);
    printf("shadow_cache static_sun=%u static_rooms=%u dynamic_rooms=%u dynamic_faces=%u max_tick=%d max_static_sun=%u max_static_rooms=%u max_dynamic_rooms=%u max_dynamic_faces=%u\n",
        view.lighting.sun_geometry_updates-sun_start,view.lighting.room_updates-rooms_start,view.lighting.actor_room_updates-actors_start,view.lighting.actor_face_updates-faces_start,max_tick,max_sun,max_rooms,max_actors,max_faces);
    SwatWallFaceStats face_stats=swat_environment_motel_face_stats(&view.environment);
    printf("wall_face_cache builds=%u batch_submissions=%u piece_submissions=%u vertices=%zu attribute_bytes=%zu\n",face_stats.builds,face_stats.submissions,face_stats.piece_submissions,face_stats.vertices,face_stats.attribute_bytes);
    report("draw_submit",measurements+2*frames,frames); report("present",measurements+3*frames,frames);
    report("total",measurements+4*frames,frames);
    if(!headless) {
        report("pose",measurements+5*frames,frames);report("shadows",measurements+6*frames,frames);
        report("scene_setup",measurements+7*frames,frames);report("world_geometry",measurements+8*frames,frames);
        report("weapon",measurements+9*frames,frames);
    }
    if(view.characters) printf("character poses=%u draws=%u prepare_mean_ms=%.3f prepare_max_ms=%.3f\n",view.characters->preparations,view.characters->draws,
        view.characters->preparations ? view.characters->prepare_seconds/view.characters->preparations*1000 : 0,view.characters->prepare_max_seconds*1000);
    if(view.characters) {
        unsigned int uploads=0;
        for(int bank=0;bank<SWAT_CHARACTER_BANKS;bank++)uploads+=view.characters->banks[bank].palette_uploads;
        printf("character_palette uploads=%u\n",uploads);
    }
    if(!headless) {
        SwatArtTextureStats textures=swat_art_texture_stats();
        printf("shared textures=%zu model_owners=%zu resident_mib=%.2f duplicate_mib_saved=%.2f\n",
            textures.textures,textures.owners,textures.bytes/1048576.0,textures.saved_bytes/1048576.0);
    }
    if(capture) { Image frame=LoadImageFromScreen(); valid=ExportImage(frame,capture) && valid; UnloadImage(frame); }
    if(recording.file && !swat_replay_close(&recording)) valid=false;
    if(playback.file && !swat_replay_close(&playback))valid=false;
    swat_sound_view_close(&sound); swat_view_close(&view); swat_sim_close(sim); free(sim); free(measurements);
    if(swat_art_texture_stats().textures){fprintf(stderr,"Leaked shared model textures\n");valid=false;}
    return valid ? 0 : 1;
}
