#include "spatial_audio.h"
#include "../../vendor/steam_audio/phonon.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#if defined(__linux__)
#include <unistd.h>
#endif
#endif

struct SwatSpatialAudio {
    void* library;
    IPLContext context;
    IPLHRTF hrtf;
    IPLBinauralEffect effects[32];
    uint32_t ids[32];
    int voices;
    IPLerror (IPLCALL *context_create)(IPLContextSettings*,IPLContext*);
    void (IPLCALL *context_release)(IPLContext*);
    IPLerror (IPLCALL *hrtf_create)(IPLContext,IPLAudioSettings*,IPLHRTFSettings*,IPLHRTF*);
    void (IPLCALL *hrtf_release)(IPLHRTF*);
    IPLerror (IPLCALL *effect_create)(IPLContext,IPLAudioSettings*,IPLBinauralEffectSettings*,IPLBinauralEffect*);
    void (IPLCALL *effect_release)(IPLBinauralEffect*);
    void (IPLCALL *effect_reset)(IPLBinauralEffect);
    IPLAudioEffectState (IPLCALL *effect_apply)(IPLBinauralEffect,IPLBinauralEffectParams*,IPLAudioBuffer*,IPLAudioBuffer*);
};
static bool symbol(void* library,const char* name,void* destination,size_t size) {
#if defined(_WIN32)
    FARPROC pointer=GetProcAddress((HMODULE)library,name);
#else
    void* pointer=dlsym(library,name);
#endif
    if(!pointer || size!=sizeof(pointer)) return false;
    memcpy(destination,&pointer,size); return true;
}
void swat_spatial_close(SwatSpatialAudio* audio) {
    if(!audio) return;
    for(int i=0;i<audio->voices;i++) if(audio->effects[i] && audio->effect_release) audio->effect_release(&audio->effects[i]);
    if(audio->hrtf && audio->hrtf_release) audio->hrtf_release(&audio->hrtf);
    if(audio->context && audio->context_release) audio->context_release(&audio->context);
    if(audio->library) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)audio->library);
#else
        dlclose(audio->library);
#endif
    }
    free(audio);
}
SwatSpatialAudio* swat_spatial_open(int rate,int voices) {
    if(voices<1 || voices>32) return NULL;
    SwatSpatialAudio* audio=calloc(1,sizeof(*audio)); if(!audio) return NULL;
    audio->voices=voices;
    const char* path=getenv("SWAT_STEAM_AUDIO_LIBRARY");
#if defined(_WIN32)
    audio->library=(void*)LoadLibraryA(path ? path : "phonon.dll");
#else
    audio->library=dlopen(path ? path : "libphonon.so",RTLD_NOW|RTLD_LOCAL);
#if defined(__linux__)
    if(!audio->library && !path) {
        char local[4096]; ssize_t n=readlink("/proc/self/exe",local,sizeof(local)-32);
        if(n>0) {
            local[n]='\0'; char* slash=strrchr(local,'/');
            if(slash) { strcpy(slash+1,"libphonon.so"); audio->library=dlopen(local,RTLD_NOW|RTLD_LOCAL); }
        }
    }
#endif
    if(!audio->library && !path)
        audio->library=dlopen("build/swat/deps/steam-audio/steamaudio/lib/linux-x64/libphonon.so",RTLD_NOW|RTLD_LOCAL);
#endif
    if(!audio->library) { swat_spatial_close(audio); return NULL; }
#define BIND(field,name) if(!symbol(audio->library,name,&audio->field,sizeof(audio->field))) { swat_spatial_close(audio); return NULL; }
    BIND(context_create,"iplContextCreate"); BIND(context_release,"iplContextRelease");
    BIND(hrtf_create,"iplHRTFCreate"); BIND(hrtf_release,"iplHRTFRelease");
    BIND(effect_create,"iplBinauralEffectCreate"); BIND(effect_release,"iplBinauralEffectRelease");
    BIND(effect_reset,"iplBinauralEffectReset"); BIND(effect_apply,"iplBinauralEffectApply");
#undef BIND
    IPLContextSettings settings={0}; settings.version=STEAMAUDIO_VERSION; settings.simdLevel=IPL_SIMDLEVEL_AVX2;
    if(audio->context_create(&settings,&audio->context)!=IPL_STATUS_SUCCESS) { swat_spatial_close(audio); return NULL; }
    IPLAudioSettings format={rate,SWAT_SPATIAL_FRAMES};
    IPLHRTFSettings hrtf={0}; hrtf.type=IPL_HRTFTYPE_DEFAULT; hrtf.volume=1; hrtf.normType=IPL_HRTFNORMTYPE_RMS;
    const char* sofa=getenv("SWAT_HRTF_SOFA");
    if(sofa && *sofa) { hrtf.type=IPL_HRTFTYPE_SOFA; hrtf.sofaFileName=sofa; }
    if(audio->hrtf_create(audio->context,&format,&hrtf,&audio->hrtf)!=IPL_STATUS_SUCCESS) { swat_spatial_close(audio); return NULL; }
    IPLBinauralEffectSettings effect={audio->hrtf};
    for(int i=0;i<voices;i++) if(audio->effect_create(audio->context,&format,&effect,&audio->effects[i])!=IPL_STATUS_SUCCESS) {
        swat_spatial_close(audio); return NULL;
    }
    return audio;
}
void swat_spatial_process(void* data,int voice,uint32_t event,b3Vec3 direction,
                          const float* mono,int frames,float* left,float* right) {
    SwatSpatialAudio* audio=data;
    if(!audio || voice<0 || voice>=audio->voices || frames<1 || frames>SWAT_SPATIAL_FRAMES) return;
    if(b3Length(direction)<.001f) direction=swat_v(0,0,-1);
    if(audio->ids[voice]!=event) { audio->effect_reset(audio->effects[voice]); audio->ids[voice]=event; }
    float input[SWAT_SPATIAL_FRAMES]={0},l[SWAT_SPATIAL_FRAMES],r[SWAT_SPATIAL_FRAMES];
    memcpy(input,mono,(size_t)frames*sizeof(float)); float* channels[2]={l,r}; float* source[1]={input};
    IPLAudioBuffer in={1,SWAT_SPATIAL_FRAMES,source},out={2,SWAT_SPATIAL_FRAMES,channels};
    IPLBinauralEffectParams params={0}; params.direction=(IPLVector3){direction.x,direction.y,direction.z};
    params.interpolation=IPL_HRTFINTERPOLATION_BILINEAR; params.spatialBlend=1; params.hrtf=audio->hrtf;
    audio->effect_apply(audio->effects[voice],&params,&in,&out);
    memcpy(left,l,(size_t)frames*sizeof(float)); memcpy(right,r,(size_t)frames*sizeof(float));
}
void swat_spatial_reset(SwatSpatialAudio* audio) {
    if(!audio) return;
    for(int i=0;i<audio->voices;i++) { audio->effect_reset(audio->effects[i]); audio->ids[i]=0; }
}
