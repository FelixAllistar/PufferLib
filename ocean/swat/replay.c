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
