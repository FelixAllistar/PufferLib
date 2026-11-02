// Replay an FCEUX movie (.fm2) in our NES core from power-on (2026-11-01): checks that a published SMB1 TAS stays in
// sync here, and writes one line per frame (frame mask world stage area x routine dead) for building practice starts
// along the TAS route and behavior-cloning data. FM2 pad columns are RLDUTSBA; our mask is A=1 B=2 Select=4
// Start=8 Up=16 Down=32 Left=64 Right=128.
// Usage: tas_replay ROM MOVIE.fm2 OUT.tsv
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include <fstream>

int main(int argc,char** argv) {
    if(argc<4) { fprintf(stderr,"usage: tas_replay ROM MOVIE.fm2 OUT.tsv\n"); return 1; }
    try {
        RetroRom& rom=retro_rom();
        { std::lock_guard<std::mutex> lock(rom.mutex); retro_load_rom_locked(rom,argv[1]); }
        Nes_Emu emu; retro_check(emu.set_cart(&rom.cart)); retro_bind_pixels(&emu);
        std::ifstream in(argv[2]); std::string line; std::vector<int> masks;
        const int bit[8]={128,64,32,16,8,4,2,1};  // R L D U T S B A
        while(std::getline(in,line)) {
            if(line.size()<12||line[0]!='|') continue;
            size_t p=line.find('|',1); if(p==std::string::npos) continue;
            int mask=0; for(int i=0;i<8;i++) if(line[p+1+i]!='.') mask|=bit[i];
            masks.push_back(mask);
        }
        // TAS_OFFSET: >0 runs that many blank frames first, <0 drops that many movie frames (power-on alignment)
        int offset=getenv("TAS_OFFSET")?atoi(getenv("TAS_OFFSET")):0;
        for(int i=0;i<offset;i++) retro_check(emu.emulate_frame(0,0));
        if(offset<0) masks.erase(masks.begin(),masks.begin()+std::min((size_t)-offset,masks.size()));
        FILE* out=fopen(argv[3],"w"); if(!out) throw std::runtime_error("cannot write output");
        int ow=-1,ol=-1,start=0,deaths=0; bool was_dead=false;
        for(size_t f=0;f<masks.size();f++) {
            retro_check(emu.emulate_frame(masks[f],0));
            const unsigned char* m=emu.low_mem();
            int w=robs_world(m),l=robs_stage(m),x=m[0x6d]*256+m[0x86];
            bool dead=m[0xe]==0x0b||m[0xb5]>1;
            if(dead&&!was_dead) deaths++;
            was_dead=dead;
            fprintf(out,"%zu %d %d %d %d %d %d %d\n",f,masks[f],w,l,robs_area(m),x,m[0xe],dead);
            if(m[0x770]==1&&(w!=ow||l!=ol)) {
                if(ow>0) printf("%d-%d: %zu frames\n",ow,ol,f-start);
                ow=w; ol=l; start=f;
            }
        }
        const unsigned char* m=emu.low_mem();
        printf("end after %zu frames: %d-%d x=%d mode=%d lives=%d deaths=%d errors=%ld\n",masks.size(),robs_world(m),
            robs_stage(m),m[0x6d]*256+m[0x86],m[0x770],robs_life(m),deaths,(long)emu.error_count());
        fclose(out);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"tas_replay: %s\n",e.what()); return 1; }
}
