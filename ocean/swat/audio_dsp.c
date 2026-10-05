#include "audio_dsp.h"
#include <string.h>

void swat_audio_init(SwatAudioMixer* mixer,int sample_rate) {
    memset(mixer,0,sizeof(*mixer)); mixer->sample_rate=sample_rate>0 ? sample_rate : SWAT_AUDIO_RATE;
    mixer->forward=swat_v(1,0,0); mixer->right=swat_v(0,0,1); mixer->up=swat_v(0,1,0);
    mixer->output_cursor=SWAT_AUDIO_BLOCK;
    const int primes[4]={1493,1789,2137,2591};
    for(int i=0;i<4;i++) mixer->delays[i]=(int)swat_clamp(primes[i]*mixer->sample_rate/48000.0f,1,SWAT_REVERB_BUFFER-1);
}
void swat_audio_listener(SwatAudioMixer* mixer,b3Vec3 forward,b3Vec3 right,b3Vec3 up) {
    mixer->forward=forward; mixer->right=right; mixer->up=up;
}
void swat_audio_room(SwatAudioMixer* mixer,SwatRoomAcoustics room) {
    mixer->room=room;
    float mid=swat_clamp(room.rt60[1],.1f,3);
    for(int i=0;i<4;i++) {
        mixer->feedback[i]=powf(10,-3.0f*mixer->delays[i]/(mixer->sample_rate*mid));
        mixer->early_delays[i]=(int)swat_clamp(room.early_seconds[i]*mixer->sample_rate,1,SWAT_REVERB_BUFFER-1);
    }
    float cutoff=2000+6000*swat_clamp(room.rt60[2]/mid,0,1);
    mixer->damp_coefficient=1-expf(-2*SWAT_PI*cutoff/mixer->sample_rate);
}
void swat_audio_spatial(SwatAudioVoice* voice,SwatAcousticPath path,b3Vec3 right,int sample_rate) {
    float pan=swat_clamp(b3Dot(path.direction,right),-1,1);
    float gain=swat_clamp(path.gain,0,1)*0.7f;
    voice->left=gain*sqrtf((1-pan)*0.5f); voice->right=gain*sqrtf((1+pan)*0.5f);
    voice->gain=gain; voice->direction=path.direction;
    float clarity=path.bands[0]>1e-6f ? swat_clamp(path.bands[2]/path.bands[0],0,1) : 0;
    float cutoff=350+9000*clarity;
    voice->filter=1-expf(-2*SWAT_PI*cutoff/sample_rate);
}
void swat_audio_source_room(SwatAudioVoice* voice,SwatRoomAcoustics room,bool different_room,int sample_rate) {
    float mid=swat_clamp(room.rt60[1],.1f,3);
    const int delays[2]={1493,1789};
    for(int i=0;i<2;i++) {
        voice->source_delay[i]=(int)swat_clamp(delays[i]*sample_rate/48000.0f,1,SWAT_SOURCE_BUFFER-1);
        voice->source_feedback[i]=powf(10,-3.0f*voice->source_delay[i]/(sample_rate*mid));
    }
    float cutoff=1500+6500*swat_clamp(room.rt60[2]/mid,0,1);
    voice->source_filter=1-expf(-2*SWAT_PI*cutoff/sample_rate);
    voice->source_wet=different_room ? room.wet : 0;
    if(voice->source_wet>0) voice->source_tail=fmaxf(voice->source_tail,mid);
}
SwatAudioVoice* swat_audio_start(SwatAudioMixer* mixer,SwatSoundEvent event,SwatAcousticPath path,b3Vec3 right) {
    int chosen=0; float quietest=1e9f;
    for(int i=0;i<SWAT_AUDIO_VOICES;i++) {
        SwatAudioVoice* voice=&mixer->voices[i];
        if(!voice->active) { chosen=i; break; }
        float gain=voice->left+voice->right;
        if(gain<quietest) { quietest=gain; chosen=i; }
    }
    static const float duration[SWAT_SOUND_KINDS]={.28f,.22f,.3f,.08f,.6f,.5f,.5f,.6f,.65f,.6f,.3f};
    SwatAudioVoice* voice=&mixer->voices[chosen]; memset(voice,0,sizeof(*voice));
    voice->active=true; voice->event=event; voice->noise=event.id*0x9e3779b9u+1;
    voice->token=++mixer->next_token;
    voice->duration=duration[event.kind]; swat_audio_spatial(voice,path,right,mixer->sample_rate);
    voice->smooth_gain=voice->gain; voice->smooth_filter=voice->filter;
    return voice;
}
void swat_audio_recording(SwatAudioVoice* voice,const float* mono,int frames,int rate,float gain) {
    if(!voice || !mono || frames<=0 || rate<8000 || rate>192000 || !isfinite(gain) || gain<=0 || gain>4) return;
    voice->recording=mono; voice->recording_frames=frames; voice->recording_rate=rate; voice->recording_gain=gain;
    voice->duration=(float)frames/rate;
}
static float sample(SwatAudioVoice* voice) {
    float t=voice->time;
    if(voice->recording) {
        float position=t*voice->recording_rate; int at=(int)position;
        if(at>=voice->recording_frames) return 0;
        float a=voice->recording[at],b=at+1<voice->recording_frames ? voice->recording[at+1] : 0;
        return (a+(b-a)*(position-at))*voice->recording_gain;
    }
    float noise=swat_rand01(&voice->noise)*2-1;
    const SwatMaterialDef* surface=swat_material(voice->event.material);
    switch(voice->event.kind) {
        case SWAT_SOUND_SHOT: return noise*expf(-38*t)+.5f*sinf(2*SWAT_PI*85*t)*expf(-25*t);
        case SWAT_SOUND_STEP: return (.6f*noise+.6f*sinf(2*SWAT_PI*surface->impact_pitch*.3f*t))*expf(-surface->impact_decay*1.8f*t);
        case SWAT_SOUND_RELOAD: {
            float pulse=t<.05f ? expf(-90*t) : (t>.12f ? expf(-100*(t-.12f)) : 0);
            return (noise+.2f*sinf(2*SWAT_PI*700*t))*pulse;
        }
        case SWAT_SOUND_HANDLE: return .6f*noise*expf(-80*t);
        case SWAT_SOUND_DOOR: return (.3f*noise+.7f*sinf(2*SWAT_PI*(180*t+18*t*t)))*expf(-6*t);
        case SWAT_SOUND_IMPACT: return (.7f*noise+.4f*sinf(2*SWAT_PI*surface->impact_pitch*t))*expf(-surface->impact_decay*t);
        case SWAT_SOUND_BREAK: return (noise+.25f*sinf(2*SWAT_PI*surface->impact_pitch*t))*expf(-fmaxf(8,surface->impact_decay*.5f)*t);
        case SWAT_SOUND_COMMAND: return .25f*sinf(2*SWAT_PI*180*t)*sinf(SWAT_PI*fminf(t/.6f,1))+.1f*noise*expf(-5*t);
        case SWAT_SOUND_FLASH: return (noise+.5f*sinf(2*SWAT_PI*65*t))*expf(-18*t);
        case SWAT_SOUND_GAS: return noise*.22f*(1-expf(-40*t))*expf(-3*t);
        case SWAT_SOUND_TASER: return .4f*(noise+sinf(2*SWAT_PI*150*t))*expf(-20*t);
        default: return 0;
    }
}
static void mix_block(SwatAudioMixer* mixer) {
    float dt=1.0f/mixer->sample_rate;
    float left[SWAT_AUDIO_BLOCK]={0},right[SWAT_AUDIO_BLOCK]={0},send[SWAT_AUDIO_BLOCK]={0};
    for(int i=0;i<SWAT_AUDIO_VOICES;i++) {
        SwatAudioVoice* voice=&mixer->voices[i]; if(!voice->active) continue;
        float mono[SWAT_AUDIO_BLOCK],l[SWAT_AUDIO_BLOCK],r[SWAT_AUDIO_BLOCK];
        for(int frame=0;frame<SWAT_AUDIO_BLOCK;frame++) {
            float raw=voice->time<voice->duration ? sample(voice) : 0;
            if(voice->source_tail>0) {
                float taps[2];
                for(int j=0;j<2;j++) {
                    float tap=voice->source_echo[j][voice->source_position[j]];
                    voice->source_damping[j]+=voice->source_filter*(tap-voice->source_damping[j]);
                    taps[j]=voice->source_damping[j];
                }
                float scatter[2]={(taps[0]+taps[1])*.70710678f,(taps[0]-taps[1])*.70710678f};
                for(int j=0;j<2;j++) {
                    voice->source_echo[j][voice->source_position[j]]=raw*.5f+scatter[j]*voice->source_feedback[j];
                    voice->source_position[j]=(voice->source_position[j]+1)%voice->source_delay[j];
                }
                voice->source_smooth_wet+=.004f*(voice->source_wet-voice->source_smooth_wet);
                raw+=voice->source_smooth_wet*(taps[0]+taps[1]);
            }
            voice->smooth_gain+=.004f*(voice->gain-voice->smooth_gain);
            voice->smooth_filter+=.004f*(voice->filter-voice->smooth_filter);
            voice->lowpass+=voice->smooth_filter*(raw-voice->lowpass);
            mono[frame]=voice->lowpass*voice->smooth_gain;
            send[frame]+=mono[frame]; voice->time+=dt;
        }
        if(mixer->binaural) {
            b3Vec3 direction=swat_v(b3Dot(voice->direction,mixer->right),b3Dot(voice->direction,mixer->up),
                                   -b3Dot(voice->direction,mixer->forward));
            mixer->binaural(mixer->spatial,i,voice->token,direction,mono,SWAT_AUDIO_BLOCK,l,r);
        } else {
            float pan=swat_clamp(b3Dot(voice->direction,mixer->right),-1,1);
            float gl=sqrtf((1-pan)*.5f),gr=sqrtf((1+pan)*.5f);
            for(int frame=0;frame<SWAT_AUDIO_BLOCK;frame++) { l[frame]=mono[frame]*gl; r[frame]=mono[frame]*gr; }
        }
        for(int frame=0;frame<SWAT_AUDIO_BLOCK;frame++) { left[frame]+=l[frame]; right[frame]+=r[frame]; }
        // Drain convolution and low-pass tails before reusing a voice.
        if(voice->time>voice->duration+voice->source_tail+.04f) voice->active=false;
    }
    for(int frame=0;frame<SWAT_AUDIO_BLOCK;frame++) {
        float taps[4];
        for(int i=0;i<4;i++) {
            float tap=mixer->reverb[i][mixer->positions[i]];
            mixer->damping[i]+=mixer->damp_coefficient*(tap-mixer->damping[i]);
            taps[i]=mixer->damping[i];
        }
        float scatter[4]={taps[0]+taps[1]+taps[2]+taps[3],taps[0]-taps[1]+taps[2]-taps[3],
                          taps[0]+taps[1]-taps[2]-taps[3],taps[0]-taps[1]-taps[2]+taps[3]};
        for(int i=0;i<4;i++) {
            mixer->reverb[i][mixer->positions[i]]=send[frame]*.25f+scatter[i]*.5f*mixer->feedback[i];
            mixer->positions[i]=(mixer->positions[i]+1)%mixer->delays[i];
        }
        mixer->early[mixer->early_position]=send[frame];
        float el=0,er=0;
        for(int i=0;i<4;i++) {
            int p=(mixer->early_position-mixer->early_delays[i]+SWAT_REVERB_BUFFER)%SWAT_REVERB_BUFFER;
            float echo=mixer->early[p]*mixer->room.early_gain[i];
            if(i%2) er+=echo; else el+=echo;
        }
        mixer->early_position=(mixer->early_position+1)%SWAT_REVERB_BUFFER;
        mixer->wet+=.002f*(mixer->room.wet-mixer->wet);
        float l=left[frame]+mixer->wet*(taps[0]+taps[2]+el);
        float r=right[frame]+mixer->wet*(taps[1]+taps[3]+er);
        mixer->output[2*frame]=l/(1+fabsf(l)); mixer->output[2*frame+1]=r/(1+fabsf(r));
    }
    mixer->output_cursor=0;
}
void swat_audio_mix(SwatAudioMixer* mixer,float* stereo,int frames,float volume) {
    volume=isfinite(volume) ? swat_clamp(volume,0,1) : 0;
    for(int frame=0;frame<frames;frame++) {
        if(mixer->output_cursor>=SWAT_AUDIO_BLOCK) mix_block(mixer);
        stereo[2*frame]=volume*mixer->output[2*mixer->output_cursor];
        stereo[2*frame+1]=volume*mixer->output[2*mixer->output_cursor+1];
        mixer->output_cursor++;
    }
}
