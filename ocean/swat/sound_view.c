#include "sound_view.h"
#include <string.h>

#define SWAT_AUDIO_BUFFER 2048
void swat_sound_view_init(SwatSoundView* view) {
    memset(view,0,sizeof(*view)); view->episode=view->actor=-1;
    InitAudioDevice(); if(!IsAudioDeviceReady()) return;
    SetAudioStreamBufferSizeDefault(SWAT_AUDIO_BUFFER);
    view->stream=LoadAudioStream(SWAT_AUDIO_RATE,32,2);
    if(!IsAudioStreamValid(view->stream)) { CloseAudioDevice(); return; }
    swat_audio_init(&view->mixer,SWAT_AUDIO_RATE);
    PlayAudioStream(view->stream); view->initialized=true;
}
void swat_sound_view_update(SwatSoundView* view,const SwatSim* sim,int actor,float volume,uint32_t floor,float yaw_offset) {
    if(!view->initialized) return;
    if(view->episode!=sim->episode || view->actor!=actor) {
        swat_audio_init(&view->mixer,SWAT_AUDIO_RATE); memset(&view->memory,0,sizeof(view->memory));
        view->memory.minimum_id=floor; view->episode=sim->episode; view->actor=actor;
    }
    if(actor>=0 && actor<sim->actor_count && sim->actors[actor].present) {
        SwatController displayed=sim->actors[actor].controller;
        displayed.yaw=swat_angle(displayed.yaw+yaw_offset);
        b3Pos eye=swat_controller_eye(&displayed); b3Vec3 forward,right,up;
        swat_controller_view(&displayed,&forward,&right,&up);
        for(int i=0;i<sim->sounds.count;i++) {
            const SwatSoundEvent* event=swat_sound_at(&sim->sounds,i);
            if(swat_hearing_consumed(&view->memory,event->id)) continue;
            if(sim->tick-event->tick>SWAT_SOUND_LIFETIME) { swat_hearing_consume(&view->memory,event->id); continue; }
            SwatAcousticPath path=swat_acoustic_path(&sim->world,event,eye);
            if(sim->tick<event->tick+path.delay_ticks || path.gain<.008f) continue;
            swat_hearing_consume(&view->memory,event->id);
            swat_audio_start(&view->mixer,*event,path,right);
        }
        for(int i=0;i<SWAT_AUDIO_VOICES;i++) {
            SwatAudioVoice* voice=&view->mixer.voices[i];
            if(voice->active) swat_audio_spatial(voice,swat_acoustic_path(&sim->world,&voice->event,eye),right,SWAT_AUDIO_RATE);
        }
    }
    float buffer[SWAT_AUDIO_BUFFER*2];
    for(int blocks=0;blocks<2 && IsAudioStreamProcessed(view->stream);blocks++) {
        swat_audio_mix(&view->mixer,buffer,SWAT_AUDIO_BUFFER,volume);
        UpdateAudioStream(view->stream,buffer,SWAT_AUDIO_BUFFER);
    }
}
void swat_sound_view_close(SwatSoundView* view) {
    if(view->initialized) { StopAudioStream(view->stream); UnloadAudioStream(view->stream); CloseAudioDevice(); }
    memset(view,0,sizeof(*view));
}
