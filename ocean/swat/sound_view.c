#include "sound_view.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define SWAT_AUDIO_BUFFER 2048
static void recordings_open(SwatSoundView* view) {
    const char* root=getenv("SWAT_SOUND_ASSETS"); char directory[1024],path[1280];
    if(root && *root) snprintf(directory,sizeof(directory),"%s",root);
    else if(FileExists("build/swat/assets/audio/bank.txt")) snprintf(directory,sizeof(directory),"build/swat/assets/audio");
    else snprintf(directory,sizeof(directory),"%sassets/audio",GetApplicationDirectory());
    snprintf(path,sizeof(path),"%s/bank.txt",directory); FILE* file=fopen(path,"r"); if(!file) return;
    char line[1024],name[256]; int kind,material,profile; float gain;
    while(view->clip_count<SWAT_SOUND_CLIPS && fgets(line,sizeof(line),file)) {
        if(line[0]=='#' || sscanf(line,"%d %d %d %f %255s",&kind,&material,&profile,&gain,name)!=5) continue;
        if(kind<0 || kind>=SWAT_SOUND_KINDS || material< -1 || material>=SWAT_MATERIAL_COUNT || profile< -1 || profile>1 ||
           !isfinite(gain) || gain<=0 || gain>4 || strstr(name,"..") || name[0]=='/' || strchr(name,':') || strchr(name,'\\')) continue;
        snprintf(path,sizeof(path),"%s/%s",directory,name); Wave wave=LoadWave(path); if(!IsWaveValid(wave)) continue;
        if(wave.frameCount>wave.sampleRate*4u) { UnloadWave(wave); continue; }
        WaveFormat(&wave,SWAT_AUDIO_RATE,32,1); float* pcm=LoadWaveSamples(wave); bool valid=pcm!=NULL;
        for(unsigned int i=0;valid && i<wave.frameCount;i++) if(!isfinite(pcm[i]) || fabsf(pcm[i])>1.01f) valid=false;
        if(valid) view->clips[view->clip_count++]=(SwatSoundClip){pcm,(int)wave.frameCount,(int)wave.sampleRate,kind,material,profile,gain};
        else if(pcm) UnloadWaveSamples(pcm);
        UnloadWave(wave);
    }
    fclose(file); TraceLog(LOG_INFO,"SWAT AUDIO: %d recorded variants loaded",view->clip_count);
}
static void recording_start(const SwatSoundView* view,const SwatSim* sim,SwatAudioVoice* voice) {
    int choices[SWAT_SOUND_CLIPS],count=0; bool sidearm=false;
    int actor=voice->event.source_actor;
    if(actor>=0 && actor<sim->actor_count) sidearm=sim->actors[actor].arsenal.active==1;
    for(int i=0;i<view->clip_count;i++) {
        const SwatSoundClip* clip=&view->clips[i];
        if(clip->kind==(int)voice->event.kind && (clip->material<0 || clip->material==(int)voice->event.material) &&
           (clip->profile<0 || clip->profile==(int)sidearm)) choices[count++]=i;
    }
    if(!count) return;
    uint32_t hash=voice->event.id*2654435761u; hash^=hash>>16;
    const SwatSoundClip* clip=&view->clips[choices[hash%(unsigned int)count]];
    swat_audio_recording(voice,clip->pcm,clip->frames,clip->rate,clip->gain);
}
void swat_sound_view_init(SwatSoundView* view) {
    memset(view,0,sizeof(*view)); view->episode=view->actor=-1;
    InitAudioDevice(); if(!IsAudioDeviceReady()) return;
    SetAudioStreamBufferSizeDefault(SWAT_AUDIO_BUFFER);
    view->stream=LoadAudioStream(SWAT_AUDIO_RATE,32,2);
    if(!IsAudioStreamValid(view->stream)) { CloseAudioDevice(); return; }
    swat_audio_init(&view->mixer,SWAT_AUDIO_RATE);
    view->spatial=swat_spatial_open(SWAT_AUDIO_RATE,SWAT_AUDIO_VOICES);
    TraceLog(LOG_INFO,"SWAT AUDIO: %s",view->spatial ? "Steam Audio binaural HRTF" : "stereo fallback (Steam Audio initialization unavailable)");
    recordings_open(view);
    PlayAudioStream(view->stream); view->initialized=true;
}
void swat_sound_view_update(SwatSoundView* view,const SwatSim* sim,int actor,float volume,uint32_t floor,float yaw_offset) {
    if(!view->initialized) return;
    if(view->episode!=sim->episode || view->actor!=actor) {
        swat_audio_init(&view->mixer,SWAT_AUDIO_RATE); memset(&view->memory,0,sizeof(view->memory));
        swat_spatial_reset(view->spatial);
        view->mixer.spatial=view->spatial;
        view->mixer.binaural=view->spatial ? swat_spatial_process : NULL;
        view->next_acoustic_tick=0;
        memset(view->path_ids,0,sizeof(view->path_ids)); view->path_generation=-1;
        view->memory.minimum_id=floor; view->episode=sim->episode; view->actor=actor;
    }
    bool remote=actor>=SWAT_MAX_ACTORS && actor<SWAT_MAX_ACTORS+SWAT_MAX_DEVICES && sim->devices[actor-SWAT_MAX_ACTORS].active;
    if(remote || (actor>=0 && actor<sim->actor_count && sim->actors[actor].present)) {
        SwatController displayed=remote ? (SwatController){.yaw=sim->devices[actor-SWAT_MAX_ACTORS].yaw,.pitch=sim->devices[actor-SWAT_MAX_ACTORS].pitch} : sim->actors[actor].controller;
        displayed.yaw=swat_angle(displayed.yaw+yaw_offset);
        b3Pos eye=remote ? swat_device_eye(&sim->devices[actor-SWAT_MAX_ACTORS]) : swat_controller_eye(&displayed); b3Vec3 forward,right,up;
        swat_controller_view(&displayed,&forward,&right,&up);
        swat_audio_listener(&view->mixer,forward,right,up);
        for(int i=0;i<sim->sounds.count;i++) {
            const SwatSoundEvent* event=swat_sound_at(&sim->sounds,i);
            if(swat_hearing_consumed(&view->memory,event->id)) continue;
            if(sim->tick-event->tick>SWAT_SOUND_LIFETIME) { swat_hearing_consume(&view->memory,event->id); continue; }
            int slot=(int)(event->id%SWAT_SOUND_CAPACITY);
            if(view->path_ids[slot]!=event->id || sim->tick-view->path_ticks[slot]>=6 ||
               view->path_generation!=sim->world.generation) {
                view->paths[slot]=swat_acoustic_path(&sim->world,event,eye);
                view->path_ids[slot]=event->id; view->path_ticks[slot]=sim->tick;
            }
            SwatAcousticPath path=view->paths[slot];
            if(sim->tick<event->tick+path.delay_ticks || path.gain<.008f) continue;
            swat_hearing_consume(&view->memory,event->id);
            SwatAudioVoice* voice=swat_audio_start(&view->mixer,*event,path,right);
            recording_start(view,sim,voice);
            swat_audio_source_room(voice,swat_acoustic_room(&sim->world,event->position),
                swat_world_room(&sim->world,event->position)!=swat_world_room(&sim->world,eye),SWAT_AUDIO_RATE);
        }
        if(sim->tick>=view->next_acoustic_tick || view->path_generation!=sim->world.generation) {
            view->next_acoustic_tick=sim->tick+6;
            swat_audio_room(&view->mixer,swat_acoustic_room(&sim->world,eye));
            for(int i=0;i<SWAT_AUDIO_VOICES;i++) {
                SwatAudioVoice* voice=&view->mixer.voices[i];
                if(voice->active) {
                    swat_audio_spatial(voice,swat_acoustic_path(&sim->world,&voice->event,eye),right,SWAT_AUDIO_RATE);
                    swat_audio_source_room(voice,swat_acoustic_room(&sim->world,voice->event.position),
                        swat_world_room(&sim->world,voice->event.position)!=swat_world_room(&sim->world,eye),SWAT_AUDIO_RATE);
                }
            }
        }
        view->path_generation=sim->world.generation;
    }
    float buffer[SWAT_AUDIO_BUFFER*2];
    for(int blocks=0;blocks<2 && IsAudioStreamProcessed(view->stream);blocks++) {
        swat_audio_mix(&view->mixer,buffer,SWAT_AUDIO_BUFFER,volume);
        UpdateAudioStream(view->stream,buffer,SWAT_AUDIO_BUFFER);
    }
}
void swat_sound_view_close(SwatSoundView* view) {
    if(view->initialized) { StopAudioStream(view->stream); UnloadAudioStream(view->stream); CloseAudioDevice(); }
    for(int i=0;i<view->clip_count;i++) UnloadWaveSamples(view->clips[i].pcm);
    swat_spatial_close(view->spatial);
    memset(view,0,sizeof(*view));
}
