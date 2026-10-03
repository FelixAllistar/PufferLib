#include "audio_dsp.h"
#include <string.h>

void swat_audio_init(SwatAudioMixer* mixer,int sample_rate) {
    memset(mixer,0,sizeof(*mixer)); mixer->sample_rate=sample_rate>0 ? sample_rate : SWAT_AUDIO_RATE;
}
void swat_audio_spatial(SwatAudioVoice* voice,SwatAcousticPath path,b3Vec3 right,int sample_rate) {
    float pan=swat_clamp(b3Dot(path.direction,right),-1,1);
    float gain=swat_clamp(path.gain,0,1)*0.7f;
    voice->left=gain*sqrtf((1-pan)*0.5f); voice->right=gain*sqrtf((1+pan)*0.5f);
    float clarity=path.bands[0]>1e-6f ? swat_clamp(path.bands[2]/path.bands[0],0,1) : 0;
    float cutoff=350+9000*clarity;
    voice->filter=1-expf(-2*SWAT_PI*cutoff/sample_rate);
}
void swat_audio_start(SwatAudioMixer* mixer,SwatSoundEvent event,SwatAcousticPath path,b3Vec3 right) {
    int chosen=0; float quietest=1e9f;
    for(int i=0;i<SWAT_AUDIO_VOICES;i++) {
        SwatAudioVoice* voice=&mixer->voices[i];
        if(!voice->active) { chosen=i; break; }
        float gain=voice->left+voice->right;
        if(gain<quietest) { quietest=gain; chosen=i; }
    }
    static const float duration[SWAT_SOUND_KINDS]={.28f,.14f,.3f,.08f,.6f,.12f,.5f};
    SwatAudioVoice* voice=&mixer->voices[chosen]; memset(voice,0,sizeof(*voice));
    voice->active=true; voice->event=event; voice->noise=event.id*0x9e3779b9u+1;
    voice->duration=duration[event.kind]; swat_audio_spatial(voice,path,right,mixer->sample_rate);
}
static float sample(SwatAudioVoice* voice) {
    float t=voice->time;
    float noise=swat_rand01(&voice->noise)*2-1;
    switch(voice->event.kind) {
        case SWAT_SOUND_SHOT: return noise*expf(-38*t)+.5f*sinf(2*SWAT_PI*85*t)*expf(-25*t);
        case SWAT_SOUND_STEP: return (.5f*noise+sinf(2*SWAT_PI*95*t))*expf(-45*t);
        case SWAT_SOUND_RELOAD: {
            float pulse=t<.05f ? expf(-90*t) : (t>.12f ? expf(-100*(t-.12f)) : 0);
            return (noise+.2f*sinf(2*SWAT_PI*700*t))*pulse;
        }
        case SWAT_SOUND_HANDLE: return .6f*noise*expf(-80*t);
        case SWAT_SOUND_DOOR: return (.3f*noise+.7f*sinf(2*SWAT_PI*(180*t+18*t*t)))*expf(-6*t);
        case SWAT_SOUND_IMPACT: return noise*expf(-65*t);
        case SWAT_SOUND_BREAK: return (noise+.25f*sinf(2*SWAT_PI*260*t))*expf(-12*t);
        default: return 0;
    }
}
void swat_audio_mix(SwatAudioMixer* mixer,float* stereo,int frames,float volume) {
    volume=isfinite(volume) ? swat_clamp(volume,0,1) : 0;
    float dt=1.0f/mixer->sample_rate;
    for(int frame=0;frame<frames;frame++) {
        float left=0,right=0;
        for(int i=0;i<SWAT_AUDIO_VOICES;i++) {
            SwatAudioVoice* voice=&mixer->voices[i]; if(!voice->active) continue;
            float raw=sample(voice); voice->lowpass+=voice->filter*(raw-voice->lowpass);
            left+=voice->lowpass*voice->left; right+=voice->lowpass*voice->right;
            voice->time+=dt; if(voice->time>=voice->duration) voice->active=false;
        }
        // Bounded soft limiting keeps overlapping gunshots from clipping.
        stereo[2*frame]=volume*left/(1+fabsf(left)); stereo[2*frame+1]=volume*right/(1+fabsf(right));
    }
}
