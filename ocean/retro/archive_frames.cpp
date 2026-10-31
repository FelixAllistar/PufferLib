// Debug view (2026-10-31): write the saved screen of each state of one key point in a keypoint archive as a PPM
// (OUT_DIR/kpPOINT_xX.ppm), to check by eye where the states are.
// Usage: archive_frames ARCHIVE.gz POINT OUT_DIR
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN

int main(int argc,char** argv) {
    try {
        if(argc<4) throw std::runtime_error("usage: archive_frames ARCHIVE.gz POINT OUT_DIR");
        int point=atoi(argv[2]);
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.archive_enabled","1"); puf_ini_put(&ini,"env.archive_practice","1");
        puf_ini_put(&ini,"env.archive_capacity_mb","1024"); puf_ini_put(&ini,"env.terminate_on_clear","1");
        puf_ini_set(puf_ini_section(&ini,"env",0),"archive_init_path",argv[1]);
        Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        int n=0;
        for(auto& c:retro_archive_bank().cells) {
            if((int)((c.key>>52)&255)!=point) continue;
            const auto& s=c.state->start;
            char path[512]; snprintf(path,sizeof(path),"%s/kp%d_x%04d.ppm",argv[3],point,s.x);
            FILE* f=fopen(path,"wb"); if(!f) throw std::runtime_error("cannot write ppm");
            fprintf(f,"P6 256 240 255\n");
            for(int i=0;i<256*240;i++) {
                const auto& col=Nes_Emu::nes_colors[s.palette[s.pixels[i]]&(Nes_Emu::color_table_size-1)];
                unsigned char rgb[3]={(unsigned char)col.red,(unsigned char)col.green,(unsigned char)col.blue};
                fwrite(rgb,1,3,f);
            }
            fclose(f); n++;
        }
        printf("%d frames\n",n);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"archive_frames: %s\n",ex.what());return 1;}
}
