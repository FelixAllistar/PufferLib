#include "audio_dsp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static SwatAudioMixer a,b;
    swat_audio_init(&a,SWAT_AUDIO_RATE); swat_audio_init(&b,SWAT_AUDIO_RATE);
    SwatSoundEvent event={1,0,0,SWAT_SOUND_SHOT,{0,1,3},3,100,SWAT_CONCRETE};
    SwatAcousticPath right={{1,1,1},1,{0,0,1},0,false};
    b3Vec3 listener_right={0,0,1};
    swat_audio_start(&a,event,right,listener_right); swat_audio_start(&b,event,right,listener_right);
    float first[4096],second[4096];
    swat_audio_mix(&a,first,2048,1); swat_audio_mix(&b,second,2048,1);
    assert(!memcmp(first,second,sizeof(first)));
    float left_energy=0,right_energy=0;
    for(int i=0;i<2048;i++) {
        assert(isfinite(first[2*i]) && isfinite(first[2*i+1]));
        left_energy+=first[2*i]*first[2*i]; right_energy+=first[2*i+1]*first[2*i+1];
    }
    assert(right_energy>1 && left_energy==0);
    swat_audio_mix(&a,first,2048,0);
    for(int i=0;i<4096;i++) assert(first[i]==0);
    for(int i=0;i<100;i++) { event.id=(uint32_t)(i+2); swat_audio_start(&a,event,right,listener_right); }
    swat_audio_mix(&a,first,2048,1);
    for(int i=0;i<4096;i++) assert(isfinite(first[i]) && fabsf(first[i])<1);
    for(int i=0;i<16;i++) swat_audio_mix(&a,first,2048,1);
    for(int i=0;i<SWAT_AUDIO_VOICES;i++) assert(!a.voices[i].active);
    SwatAcousticPath muffled=right; muffled.bands[2]=.001f;
    swat_audio_init(&a,SWAT_AUDIO_RATE); swat_audio_init(&b,SWAT_AUDIO_RATE);
    swat_audio_start(&a,event,muffled,listener_right); swat_audio_start(&b,event,right,listener_right);
    swat_audio_mix(&a,first,2048,1); swat_audio_mix(&b,second,2048,1);
    float muffled_changes=0,clear_changes=0;
    for(int i=1;i<2048;i++) {
        float m=first[2*i+1]-first[2*i-1],c=second[2*i+1]-second[2*i-1];
        muffled_changes+=m*m; clear_changes+=c*c;
    }
    assert(muffled_changes<clear_changes*.5f);
    // A room tail outlives the source; longer RT60 retains more late energy.
    SwatRoomAcoustics short_room={{.35f,.35f,.25f},.5f,{.01f,.02f,.015f,.025f},{.1f,.1f,.1f,.1f}};
    SwatRoomAcoustics long_room=short_room;
    for(int i=0;i<3;i++) long_room.rt60[i]*=4;
    swat_audio_init(&a,SWAT_AUDIO_RATE); swat_audio_init(&b,SWAT_AUDIO_RATE);
    swat_audio_room(&a,short_room); swat_audio_room(&b,long_room);
    swat_audio_start(&a,event,right,listener_right); swat_audio_start(&b,event,right,listener_right);
    double short_tail=0,long_tail=0;
    for(int block=0;block<48;block++) {
        swat_audio_mix(&a,first,2048,1); swat_audio_mix(&b,second,2048,1);
        for(int i=0;i<4096;i++) {
            assert(isfinite(first[i]) && isfinite(second[i]) && fabsf(first[i])<1 && fabsf(second[i])<1);
            if(block>12) { short_tail+=first[i]*first[i]; long_tail+=second[i]*second[i]; }
        }
    }
    assert(long_tail>short_tail*10 && long_tail>1e-6);
    // The listener is outdoors (zero room bus), but indoor source decay still
    // travels through the same directional/filter path as its direct sound.
    swat_audio_init(&a,SWAT_AUDIO_RATE); swat_audio_init(&b,SWAT_AUDIO_RATE);
    SwatAudioVoice* indoor=swat_audio_start(&a,event,right,listener_right);
    swat_audio_source_room(indoor,long_room,true,SWAT_AUDIO_RATE);
    swat_audio_start(&b,event,right,listener_right);
    double source_tail=0,dry_tail=0;
    for(int block=0;block<48;block++) {
        swat_audio_mix(&a,first,2048,1); swat_audio_mix(&b,second,2048,1);
        for(int i=0;i<4096;i++) {
            assert(isfinite(first[i]) && fabsf(first[i])<1);
            if(block>12) { source_tail+=first[i]*first[i]; dry_tail+=second[i]*second[i]; }
        }
    }
    assert(source_tail>1e-6 && source_tail>dry_tail*100);
    assert(!indoor->active);
    puts("PASS source-room DSP: room decay reaches an outdoor listener through the source path, stays bounded and expires");
    puts("PASS room DSP: bounded stereo reflections, persistent decay after source expiry and RT60-dependent late energy");
    puts("PASS audio DSP: deterministic stereo PCM, right/left orientation, mute, bounded overlapping voices, voice expiry and material low-pass");
    return 0;
}
