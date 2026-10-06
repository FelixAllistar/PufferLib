// Repeatable offline listening comparisons using the game's propagation/mixer.
#include "mission.h"
#include "audio_dsp.h"
#include "spatial_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

static SwatAudioMixer mixer;
static float pcm[SWAT_AUDIO_RATE*3*2];
static void u16(FILE* f,unsigned int x) { fputc(x&255,f); fputc((x>>8)&255,f); }
static void u32(FILE* f,unsigned int x) { u16(f,x); u16(f,x>>16); }
static bool wav(const char* path,int frames) {
    FILE* f=fopen(path,"wb"); if(!f) return false;
    fwrite("RIFF",1,4,f); u32(f,36+(unsigned)frames*4); fwrite("WAVEfmt ",1,8,f);
    u32(f,16); u16(f,1); u16(f,2); u32(f,SWAT_AUDIO_RATE); u32(f,SWAT_AUDIO_RATE*4);
    u16(f,4); u16(f,16); fwrite("data",1,4,f); u32(f,(unsigned)frames*4);
    for(int i=0;i<frames*2;i++) u16(f,(unsigned)(int)(swat_clamp(pcm[i],-1,1)*32767));
    bool ok=!ferror(f); return fclose(f)==0 && ok;
}
static bool render(const char* directory,FILE* report,const char* name,const SwatWorld* world,
                   SwatSoundEvent event,b3Pos listener,SwatRoomAcoustics room,SwatSpatialAudio* spatial) {
    SwatAcousticPath path=swat_acoustic_path(world,&event,listener);
    swat_audio_init(&mixer,SWAT_AUDIO_RATE); swat_audio_room(&mixer,room);
    if(spatial) { swat_spatial_reset(spatial); mixer.spatial=spatial; mixer.binaural=swat_spatial_process; }
    SwatRoomAcoustics source=swat_acoustic_room(world,event.position);
    SwatAudioVoice* voice=swat_audio_start(&mixer,event,path,swat_v(0,0,1));
    swat_audio_source_room(voice,source,swat_world_room(world,event.position)!=swat_world_room(world,listener),SWAT_AUDIO_RATE);
    swat_audio_mix(&mixer,pcm,SWAT_AUDIO_RATE*3,.85f);
    double energy=0,tail=0;
    for(int i=0;i<SWAT_AUDIO_RATE*3*2;i++) {
        energy+=pcm[i]*pcm[i]; if(i>=SWAT_AUDIO_RATE) tail+=pcm[i]*pcm[i];
    }
    char file[1024]; snprintf(file,sizeof(file),"%s/%s.wav",directory,name);
    fprintf(report,"%s,%.8f,%.8f,%.8f,%.8f,%d,%d,%.4f,%.4f,%.8f,%.8f\n",name,path.bands[0],path.bands[1],path.bands[2],path.gain,
            path.delay_ticks,path.via_doorway,room.rt60[1],source.rt60[1],sqrt(energy/(SWAT_AUDIO_RATE*6)),sqrt(tail/(SWAT_AUDIO_RATE*5)));
    return wav(file,SWAT_AUDIO_RATE*3);
}
int main(int argc,char** argv) {
    const char* directory=argc>1 ? argv[1] : "build/swat/audio-lab";
    bool use_hrtf=argc>2 && !strcmp(argv[2],"--hrtf");
    SwatSpatialAudio* spatial=use_hrtf ? swat_spatial_open(SWAT_AUDIO_RATE,SWAT_AUDIO_VOICES) : NULL;
    if(use_hrtf && !spatial) { fprintf(stderr,"Steam Audio unavailable: run python3 ocean/swat/setup_audio.py\n"); return 1; }
#if defined(_WIN32)
    _mkdir(directory);
#else
    mkdir(directory,0755);
#endif
    char file[1024]; snprintf(file,sizeof(file),"%s/comparison.csv",directory);
    FILE* report=fopen(file,"w"); if(!report) { perror(file); swat_spatial_close(spatial); return 1; }
    fprintf(report,"scene,low,mid,high,gain,delay_ticks,via_doorway,mid_rt60,source_mid_rt60,rms,tail_rms_after_500ms\n");
    SwatSoundEvent event={1,0,0,SWAT_SOUND_SHOT,{-3,1.4f,.17f},3,100,SWAT_CONCRETE};
    b3Pos listener={3,1.4f,.17f}; SwatRoomAcoustics dry={0}; static SwatWorld world;
    bool ok=true; swat_world_init(&world);
    ok=render(directory,report,"01_open",&world,event,listener,dry,spatial)&&ok;
    const char* names[]={"02_wood_44mm","03_brick_200mm","04_glass_6mm","05_steel_8mm"};
    const SwatMaterial materials[]={SWAT_WOOD,SWAT_BRICK,SWAT_GLASS,SWAT_STEEL};
    const float thickness[]={.044f,.2f,.006f,.008f};
    for(int i=0;i<4;i++) {
        int wall=swat_world_box(&world,(b3Pos){0,1.4f,0},swat_v(thickness[i]*.5f,2,3),materials[i],100);
        ok=render(directory,report,names[i],&world,event,listener,dry,spatial)&&ok;
        swat_world_damage(&world,wall,1000);
    }
    swat_build_framed_wall(&world,(b3Pos){0,0,0},0,4,2.7f,0,0,0,0,false);
    const char* boards[]={"06_two_gypsum_faces","07_one_face_broken","08_both_faces_broken"};
    for(int i=0;i<3;i++) {
        ok=render(directory,report,boards[i],&world,event,listener,dry,spatial)&&ok;
        SwatHit hit=swat_world_ray(&world,event.position,swat_v(1,0,0),6,b3_nullBodyId);
        if(hit.hit) swat_world_damage(&world,hit.index,1000);
    }
    swat_world_close(&world); swat_world_init(&world);
    const b3Pos sources[]={{6,1.4f,0},{-6,1.4f,0},{0,1.4f,-6},{0,1.4f,6},{4.24f,5.64f,0}};
    const char* directions[]={"09_front","10_back","11_left","12_right","13_above"};
    listener=(b3Pos){0,1.4f,0};
    for(int i=0;i<5;i++) { event.position=sources[i]; ok=render(directory,report,directions[i],&world,event,listener,dry,spatial)&&ok; }
    swat_world_close(&world); swat_world_init(&world); swat_mission_build_house(&world);
    event.position=(b3Pos){12,1.4f,-2}; listener=(b3Pos){15,1.4f,-1};
    SwatRoomAcoustics furnished=swat_acoustic_room(&world,listener);
    ok=render(directory,report,"14_room_dry",&world,event,listener,dry,spatial)&&ok;
    ok=render(directory,report,"15_room_carpet",&world,event,listener,furnished,spatial)&&ok;
    world.rooms[1].floor=SWAT_TILE; SwatRoomAcoustics hard=swat_acoustic_room(&world,listener);
    ok=render(directory,report,"16_room_tile",&world,event,listener,hard,spatial)&&ok;
    // Compare the same hidden source with a closed/open interior door.
    event.position=(b3Pos){14,1.4f,-2}; listener=(b3Pos){8,1.4f,-2};
    ok=render(directory,report,"17_door_closed",&world,event,listener,swat_acoustic_room(&world,listener),spatial)&&ok;
    for(int i=0;i<world.count;i++) if(world.objects[i].door) world.objects[i].door_open=true;
    for(int i=0;i<60;i++) swat_world_step_doors(&world);
    ok=render(directory,report,"18_doors_open",&world,event,listener,swat_acoustic_room(&world,listener),spatial)&&ok;
    listener=(b3Pos){14,1.4f,-8}; world.rooms[1].floor=SWAT_CARPET;
    ok=render(directory,report,"19_source_room_outside_carpet",&world,event,listener,dry,spatial)&&ok;
    world.rooms[1].floor=SWAT_TILE;
    ok=render(directory,report,"20_source_room_outside_tile",&world,event,listener,dry,spatial)&&ok;
    swat_world_close(&world); swat_world_init(&world);
    event.position=(b3Pos){2,1,0}; listener=(b3Pos){0,1,0}; event.kind=SWAT_SOUND_STEP; event.range=12;
    event.material=SWAT_CARPET; event.strength=.4f*swat_material(event.material)->footstep_gain;
    ok=render(directory,report,"21_step_carpet",&world,event,listener,dry,spatial)&&ok;
    event.material=SWAT_TILE; event.strength=.4f*swat_material(event.material)->footstep_gain;
    ok=render(directory,report,"22_step_tile",&world,event,listener,dry,spatial)&&ok;
    event.kind=SWAT_SOUND_IMPACT; event.strength=.8f;
    const char* impacts[]={"23_impact_wood","24_impact_steel","25_impact_tile","26_impact_carpet"};
    const SwatMaterial surfaces[]={SWAT_WOOD,SWAT_STEEL,SWAT_TILE,SWAT_CARPET};
    for(int i=0;i<4;i++) { event.material=surfaces[i]; ok=render(directory,report,impacts[i],&world,event,listener,dry,spatial)&&ok; }
    swat_world_close(&world); swat_spatial_close(spatial);
    if(fclose(report)) ok=false;
    printf("%s 26 WAV comparisons and metrics: %s (%s)\n",ok ? "Wrote" : "FAILED",directory,use_hrtf ? "Steam Audio HRTF" : "stereo");
    return ok ? 0 : 1;
}
