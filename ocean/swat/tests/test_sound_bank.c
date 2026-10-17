// Exercise the production loader/selector without opening an audio device.
// Static implementation access keeps these test hooks out of the player API.
#include "../sound_view.c"
#include <assert.h>

static SwatSoundView view;
static SwatSim sim;
static float buffer[4096];
static SwatAcousticPath clear={.bands={1,1,1},.gain=1,.direction={0,0,1}};
static SwatAudioVoice* start(int id,SwatSoundKind kind,SwatMaterial material) {
    swat_audio_init(&view.mixer,SWAT_AUDIO_RATE);
    SwatSoundEvent event={.id=(uint32_t)id,.source_actor=-1,.kind=kind,.material=material};
    SwatAudioVoice* voice=swat_audio_start(&view.mixer,event,clear,swat_v(0,0,1));
    recording_start(&view,&sim,voice);return voice;
}
int main(int argc,char** argv) {
    assert(argc>=2);
#ifdef _WIN32
    assert(!_putenv_s("SWAT_SOUND_ASSETS",argv[1]));
#else
    assert(!setenv("SWAT_SOUND_ASSETS",argv[1],1));
#endif
    recordings_open(&view);assert(view.clip_count==92 && view.clip_count<SWAT_SOUND_CLIPS);
    for(int c=0;c<view.clip_count;c++) {
        const SwatSoundClip* clip=&view.clips[c];double energy=0;
        assert(clip->frames>0 && clip->frames<=4*clip->rate);
        for(int i=0;i<clip->frames;i++) {
            assert(isfinite(clip->pcm[i]) && fabsf(clip->pcm[i])<=1.01f);
            energy+=clip->pcm[i]*clip->pcm[i];
        }
        assert(energy>1e-6);
    }
    for(int kind=SWAT_SOUND_IMPACT;kind<=SWAT_SOUND_BREAK;kind++)for(int m=0;m<SWAT_MATERIAL_COUNT;m++) {
        const float* seen[3]={0};int count=0;
        for(int id=1;id<=48;id++) {
            SwatAudioVoice* voice=start(id,kind,m);assert(voice->recording);
            bool found=false;for(int i=0;i<count;i++)found|=seen[i]==voice->recording;
            if(!found){assert(count<3);seen[count++]=voice->recording;}
        }
        assert(count==3);
        SwatAudioVoice* voice=start(12,kind,m);double energy=0;
        for(int block=0;block<100;block++) {
            swat_audio_mix(&view.mixer,buffer,2048,1);
            for(int i=0;i<2048;i++) {
                assert(buffer[2*i]==0 && isfinite(buffer[2*i+1]) && fabsf(buffer[2*i+1])<1);
                energy+=buffer[2*i+1]*buffer[2*i+1];
            }
        }
        assert(energy>1e-5 && !voice->active);
    }
    const SwatSoundKind entry_kinds[]={SWAT_SOUND_DOOR,SWAT_SOUND_DOOR_CLOSE,SWAT_SOUND_BREACH};
    for(int k=0;k<3;k++) {
        const float* seen[3]={0};int count=0;
        for(int id=1;id<=48;id++) {
            SwatAudioVoice* voice=start(id,entry_kinds[k],SWAT_WOOD);assert(voice->recording);
            bool found=false;for(int i=0;i<count;i++)found|=seen[i]==voice->recording;
            if(!found){assert(count<3);seen[count++]=voice->recording;}
        }
        assert(count==(k==2?3:2));
    }
    // An exact authored surface beats its family and any wildcard, even when
    // a wildcard has a weapon profile. Missing recordings retain synthesis.
    static float exact[1]={.1f},family[1]={.2f},wildcard[1]={.3f};
    static SwatSoundView custom;
    custom.clip_count=3;
    custom.clips[0]=(SwatSoundClip){wildcard,1,48000,SWAT_SOUND_IMPACT,-1,0,1};
    custom.clips[1]=(SwatSoundClip){family,1,48000,SWAT_SOUND_IMPACT,SWAT_CONCRETE,-1,1};
    custom.clips[2]=(SwatSoundClip){exact,1,48000,SWAT_SOUND_IMPACT,SWAT_STONE,-1,1};
    SwatAudioVoice voice={.event={.id=7,.source_actor=-1,.kind=SWAT_SOUND_IMPACT,.material=SWAT_STONE}};
    recording_start(&custom,&sim,&voice);assert(voice.recording==exact);
    custom.clip_count=2;voice.recording=NULL;recording_start(&custom,&sim,&voice);assert(voice.recording==family);
    custom.clip_count=0;voice.recording=NULL;recording_start(&custom,&sim,&voice);assert(!voice.recording);

    SwatSoundLog log={0};b3Pos point={0,1,0};
    swat_sound_surface(&log,100,0,SWAT_SOUND_IMPACT,point,1,25,SWAT_WOOD);
    SwatSoundEvent hit=*swat_sound_at(&log,0);
    assert(!swat_sound_contact_replaced(&log,&hit));
    swat_sound_surface(&log,100,1,SWAT_SOUND_BREAK,point,1,25,SWAT_WOOD);
    swat_sound_surface(&log,101,0,SWAT_SOUND_BREAK,point,1,25,SWAT_WOOD);
    swat_sound_surface(&log,100,0,SWAT_SOUND_BREAK,point,1,25,SWAT_GLASS);
    swat_sound_surface(&log,100,0,SWAT_SOUND_BREAK,(b3Pos){.1f,1,0},1,25,SWAT_WOOD);
    assert(!swat_sound_contact_replaced(&log,&hit));
    swat_sound_surface(&log,100,0,SWAT_SOUND_BREAK,point,1,25,SWAT_WOOD);
    SwatSoundLog unchanged=log;
    assert(swat_sound_contact_replaced(&log,&hit));
    assert(!swat_sound_contact_replaced(&log,swat_sound_at(&log,log.count-1)));
    assert(!memcmp(&log,&unchanged,sizeof(log)));

    if(argc>2) {
        // One compact listening proof, six cues at 1.5 second intervals:
        // ceramic hit, aluminum hit, soft hit, masonry/glass/wood break.
        static float proof[SWAT_AUDIO_RATE*9*2];
        const SwatMaterial surfaces[]={SWAT_TILE,SWAT_ALUMINUM,SWAT_CLOTH,SWAT_BRICK,SWAT_GLASS,SWAT_WOOD};
        for(int i=0;i<6;i++) {
            start(12+i,i<3?SWAT_SOUND_IMPACT:SWAT_SOUND_BREAK,surfaces[i]);
            swat_audio_mix(&view.mixer,proof+i*SWAT_AUDIO_RATE*3,SWAT_AUDIO_RATE*3/2,.85f);
        }
        Wave wave=WaveCopy((Wave){.frameCount=SWAT_AUDIO_RATE*9,.sampleRate=SWAT_AUDIO_RATE,.sampleSize=32,.channels=2,.data=proof});
        assert(IsWaveValid(wave));
        WaveFormat(&wave,SWAT_AUDIO_RATE,16,2);assert(ExportWave(wave,argv[2]));
        UnloadWave(wave);
    }
    if(argc>3) {
        static float proof[SWAT_AUDIO_RATE*9*2];
        const SwatSoundKind kinds[]={SWAT_SOUND_DOOR,SWAT_SOUND_DOOR_CLOSE,SWAT_SOUND_BREACH,
            SWAT_SOUND_DOOR,SWAT_SOUND_DOOR_CLOSE,SWAT_SOUND_BREACH};
        for(int i=0;i<6;i++) {
            start(12+i,kinds[i],SWAT_WOOD);
            swat_audio_mix(&view.mixer,proof+i*SWAT_AUDIO_RATE*3,SWAT_AUDIO_RATE*3/2,.85f);
        }
        Wave wave=WaveCopy((Wave){.frameCount=SWAT_AUDIO_RATE*9,.sampleRate=SWAT_AUDIO_RATE,.sampleSize=32,.channels=2,.data=proof});
        assert(IsWaveValid(wave));WaveFormat(&wave,SWAT_AUDIO_RATE,16,2);assert(ExportWave(wave,argv[3]));UnloadWave(wave);
    }
    swat_sound_view_close(&view);assert(!view.clip_count);
    puts("PASS production sound bank: 92 decoded entries; all 16 impact/break materials, two open/close and three breach variants, exact/family/wildcard priority, directional finite PCM, expiry, contact deduplication and cleanup");
    return 0;
}
