#include "replay.h"
#include <string.h>

static bool write32(FILE* file,uint32_t value) {
    unsigned char b[]={value>>24,value>>16,value>>8,value};
    return fwrite(b,1,4,file)==4;
}
static bool read32(FILE* file,uint32_t* value) {
    unsigned char b[4]; if(fread(b,1,4,file)!=4) return false;
    *value=((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3]; return true;
}
uint32_t swat_replay_digest(const SwatSim* sim) {
    SwatSnapshot snapshot; unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_snapshot(sim,1,&snapshot);
    size_t count=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    uint32_t hash=2166136261u;
    for(size_t i=0;i<count;i++) { hash^=bytes[i]; hash*=16777619u; }
    return count ? hash : 0;
}
bool swat_replay_record(SwatReplay* r,const char* path,const SwatSim* sim) {
    memset(r,0,sizeof(*r)); if(sim->tick) return false;
    unsigned char config[128]; size_t size=swat_encode_scenario(config,sizeof(config),1,&sim->config);
    if(!size || !path || !path[0]) return false;
    r->file=fopen(path,"wb+"); if(!r->file) return false;
    r->config=sim->config; r->seed=sim->reset_seed; r->writing=true;
    r->failed=!(write32(r->file,0x53475250u) && write32(r->file,1) && write32(r->file,SWAT_NET_VERSION) &&
        write32(r->file,0) && write32(r->file,r->seed) && write32(r->file,(uint32_t)size) &&
        write32(r->file,(uint32_t)sim->config.max_ticks) && write32(r->file,sim->config.randomize | (sim->config.hostile_fire<<1)) &&
        fwrite(config,1,size,r->file)==size);
    if(r->failed) { swat_replay_close(r); return false; } return true;
}
bool swat_replay_append(SwatReplay* r,const SwatInput* input,const SwatSim* after) {
    if(!r->file || !r->writing || r->failed || after->tick!=(int)r->count+1) return false;
    unsigned char bytes[256]; SwatCommand command={1,r->count+1,*input};
    size_t size=swat_encode_command(bytes,sizeof(bytes),&command);
    r->failed=!size || !write32(r->file,(uint32_t)size) || fwrite(bytes,1,size,r->file)!=size || !write32(r->file,swat_replay_digest(after));
    if(!r->failed) r->count++;
    return !r->failed;
}
bool swat_replay_open(SwatReplay* r,const char* path) {
    memset(r,0,sizeof(*r)); r->file=fopen(path,"rb"); if(!r->file) return false;
    uint32_t magic,version,wire,size,epoch,max_ticks,flags; unsigned char config[128];
    bool ok=read32(r->file,&magic) && read32(r->file,&version) && read32(r->file,&wire) &&
        read32(r->file,&r->count) && read32(r->file,&r->seed) && read32(r->file,&size) &&
        read32(r->file,&max_ticks) && read32(r->file,&flags);
    ok=ok && magic==0x53475250u && version==1 && wire==SWAT_NET_VERSION && r->count<=3600000 && max_ticks>0 && max_ticks<=3600000 && flags<=3 && size<=sizeof(config) &&
        fread(config,1,size,r->file)==size && swat_decode_scenario(config,size,&epoch,&r->config);
    if(!ok) { r->failed=true; swat_replay_close(r); return false; }
    r->config.max_ticks=(int)max_ticks; r->config.randomize=flags&1; r->config.hostile_fire=flags&2;
    return true;
}
int swat_replay_next(SwatReplay* r,SwatInput* input,uint32_t* expected) {
    if(!r->file || r->writing || r->failed) return -1;
    if(r->frame==r->count) {
        if(fgetc(r->file)==EOF && !ferror(r->file)) return 0;
        r->failed=true; return -1;
    }
    uint32_t size,hash; unsigned char bytes[256]; SwatCommand command;
    bool ok=read32(r->file,&size) && size<=sizeof(bytes) && fread(bytes,1,size,r->file)==size &&
        read32(r->file,&hash) && swat_decode_command(&command,bytes,size) && command.sequence==r->frame+1;
    if(!ok) { r->failed=true; return -1; }
    *input=command.input; *expected=hash; r->frame++; return 1;
}
bool swat_replay_close(SwatReplay* r) {
    bool ok=!r->failed;
    if(r->file) {
        if(r->writing && (fseek(r->file,12,SEEK_SET)!=0 || !write32(r->file,r->count) || fflush(r->file)!=0)) ok=false;
        if(fclose(r->file)!=0) ok=false;
    }
    r->file=NULL; r->failed=!ok; return ok;
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>

bool swat_replay_checkpoint(SwatReplay* r,const char* path) {
    if(!r->file || !r->writing || r->failed || !path || !path[0]) return false;
    char temporary[2048];
#if defined(_WIN32)
    long pid=(long)_getpid();
#else
    long pid=(long)getpid();
#endif
    int length=snprintf(temporary,sizeof(temporary),"%s.tmp.%ld",path,pid);
    if(length<0 || (size_t)length>=sizeof(temporary)) return false;
    if(fseek(r->file,12,SEEK_SET)!=0 || !write32(r->file,r->count) || fflush(r->file)!=0 || fseek(r->file,0,SEEK_SET)!=0) { r->failed=true; return false; }
    FILE* output=fopen(temporary,"wb"); if(!output) { fseek(r->file,0,SEEK_END); return false; }
    unsigned char buffer[8192]; size_t count; bool ok=true;
    while((count=fread(buffer,1,sizeof(buffer),r->file))!=0) if(fwrite(buffer,1,count,output)!=count) { ok=false; break; }
    if(ferror(r->file) || fflush(output)!=0) ok=false;
    if(fclose(output)!=0) ok=false;
    clearerr(r->file); if(fseek(r->file,0,SEEK_END)!=0) { r->failed=true; ok=false; }
    if(ok) {
#if defined(_WIN32)
        ok=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        ok=rename(temporary,path)==0;
#endif
    }
    if(!ok) remove(temporary);
    return ok;
}
bool swat_replay_restore(SwatSim* destination,const char* path,char* error,size_t capacity) {
    SwatReplay reader;
    if(!swat_replay_open(&reader,path)) { snprintf(error,capacity,"Save is missing, damaged or from an incompatible build."); return false; }
    SwatSim* restored=calloc(1,sizeof(*restored));
    if(!restored) { swat_replay_close(&reader); snprintf(error,capacity,"Not enough memory to restore the mission."); return false; }
    swat_sim_init(restored,reader.config,reader.seed);
    SwatInput input; uint32_t expected; int status; bool valid=true;
    while((status=swat_replay_next(&reader,&input,&expected))==1) {
        swat_sim_step(restored,&input);
        if(swat_replay_digest(restored)!=expected) { snprintf(error,capacity,"Save diverges at tick %u; check the game and layout model version.",reader.frame); valid=false; break; }
    }
    bool closed=swat_replay_close(&reader);
    if(status<0 || !closed) { snprintf(error,capacity,"Save contains truncated or invalid frame data."); valid=false; }
    if(!valid) { swat_sim_close(restored); free(restored); return false; }
    // Transfer only after complete verification. Retarget physics user data:
    // every body tag lived inside the temporary simulation being transferred.
    swat_sim_close(destination); *destination=*restored;
    for(int i=0;i<destination->world.count;i++) if(destination->world.objects[i].active)
        b3Body_SetUserData(destination->world.objects[i].body,&destination->world.objects[i].tag);
    for(int i=0;i<destination->actor_count;i++) if(destination->actors[i].present)
        b3Body_SetUserData(destination->actors[i].controller.body.body,&destination->actors[i].tag);
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) if(B3_IS_NON_NULL(destination->projectiles[i].body))
        b3Body_SetUserData(destination->projectiles[i].body,&destination->projectiles[i].tag);
    for(int i=0;i<SWAT_MAX_DEVICES;i++) if(B3_IS_NON_NULL(destination->devices[i].body))
        b3Body_SetUserData(destination->devices[i].body,&destination->devices[i].tag);
    free(restored); if(capacity) error[0]=0; return true;
}

bool swat_replay_continue(SwatReplay* r,const char* path,const char* checkpoint,const SwatSim* sim) {
    if(!path || !path[0] || !checkpoint || r->file) return false;
    SwatReplay source;
    if(!swat_replay_open(&source,checkpoint)) return false;
    SwatInput input; uint32_t hash=0; int status;
    while((status=swat_replay_next(&source,&input,&hash))==1) {}
    bool valid=status==0 && source.count==(uint32_t)sim->tick && source.seed==sim->reset_seed &&
        (!source.count || hash==swat_replay_digest(sim));
    if(!valid || fseek(source.file,0,SEEK_SET)!=0) { swat_replay_close(&source); return false; }
    char temporary[2048];
#ifdef _WIN32
    long pid=(long)_getpid();
#else
    long pid=(long)getpid();
#endif
    int length=snprintf(temporary,sizeof(temporary),"%s.continue.tmp.%ld",path,pid);
    if(length<0 || (size_t)length>=sizeof(temporary)) { swat_replay_close(&source); return false; }
    FILE* output=fopen(temporary,"wb");
    if(!output) { swat_replay_close(&source); return false; }
    unsigned char bytes[8192]; size_t n; bool ok=true;
    while((n=fread(bytes,1,sizeof(bytes),source.file))>0) if(fwrite(bytes,1,n,output)!=n) { ok=false; break; }
    if(ferror(source.file) || fflush(output)!=0) ok=false;
    if(fclose(output)!=0) ok=false;
    if(!swat_replay_close(&source)) ok=false;
    if(ok) {
#ifdef _WIN32
        ok=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        ok=rename(temporary,path)==0;
#endif
    }
    if(!ok) { remove(temporary); return false; }
    memset(r,0,sizeof(*r)); r->file=fopen(path,"rb+");
    if(!r->file) return false;
    r->config=source.config; r->seed=source.seed; r->count=source.count; r->writing=true;
    if(fseek(r->file,0,SEEK_END)!=0) { r->failed=true; swat_replay_close(r); return false; }
    return true;
}
