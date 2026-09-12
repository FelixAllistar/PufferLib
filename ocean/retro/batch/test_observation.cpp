#define PUFFERLIB_BUILD_MAIN
#include "../retro.h"
#include "../nes_emu/abstract_file.h"
#include <cassert>

static std::vector<char> state(Nes_Emu& emu) {
    Nes_State s; emu.save_state(&s); Mem_Writer out;
    retro_check(s.write(out)); return std::vector<char>(out.data(),out.data()+out.size());
}
static void verify(Env& e,const float* obs) {
    const auto& fr=e.emu->frame();
    const unsigned char* p=e.reset_image?e.start->pixels:fr.pixels;
    const short* pal=e.reset_image?e.start->palette:fr.palette;
    int pitch=e.reset_image?256:fr.pitch;
    for(int i=0;i<OBS_SIZE;i++) assert(std::isfinite(obs[i]));
    for(int y=0;y<120;y++) for(int x=0;x<128;x++) {
        float sum=0;
        for(int dy=0;dy<2;dy++) for(int dx=0;dx<2;dx++) {
            const auto& c=Nes_Emu::nes_colors[pal[p[(2*y+dy)*pitch+2*x+dx]]&(Nes_Emu::color_table_size-1)];
            sum+=retro_luma(c.red,c.green,c.blue);
        }
        assert(obs[112+y*128+x]==sum/4);
    }
}
static void preview(Env& e,const float* obs,const char* path) {
    const auto& fr=e.emu->frame(); std::vector<Color> rgb(1024*480);
    // Left: original framebuffer at 2x. Right: actual input values at 4x.
    for(int y=0;y<480;y++) for(int x=0;x<512;x++) {
        const auto& c=Nes_Emu::nes_colors[fr.palette[fr.pixels[(y/2)*fr.pitch+x/2]]&(Nes_Emu::color_table_size-1)];
        rgb[y*1024+x]=Color{c.red,c.green,c.blue,255};
        unsigned char v=(unsigned char)std::lround(obs[112+(y/4)*128+x/4]*255);
        rgb[y*1024+512+x]=Color{v,v,v,255};
    }
    Image image={rgb.data(),1024,480,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    assert(ExportImage(image,path));
}
int main(int argc,char** argv) {
    static_assert(OBS_SIZE==15472,"full-screen observation ABI");
    Dict a={}; dict_set_str(&a,"spawn_levels","all"); dict_set_str(&a,"cpu_backend","reference");
    dict_set_str(&a,"render_backend","reference"); dict_set(&a,"max_frames",128);
    Dict b={}; dict_set_str(&b,"spawn_levels","all"); dict_set_str(&b,"cpu_backend","blocks");
    dict_set_str(&b,"render_backend","wide"); dict_set(&b,"max_frames",128);
    Env ref={},fast={}; std::vector<float> oa(OBS_SIZE),ob(OBS_SIZE);
    float action=0,ra=0,rb=0,ta=0,tb=0; ref.rng=fast.rng=73;
    puf_init(&ref,&a); puf_init(&fast,&b);
    ref.agents[0]={oa.data(),&action,&ra,&ta,nullptr,0};
    fast.agents[0]={ob.data(),&action,&rb,&tb,nullptr,0};
    for(int level=0;level<32;level++) {
        ref.spawn_pin=fast.spawn_pin=1; ref.cur_spawn=fast.cur_spawn=level;
        puf_reset(&ref); puf_reset(&fast);
        assert(ref.world==level/4+1&&ref.stage==level%4+1);
        assert(oa==ob); verify(fast,ob.data());
        // Changing camera/crop RAM cannot move/replace the full-screen image.
        Nes_State saved; fast.emu->save_state(&saved);
        fast.emu->low_mem()[0x86]^=255; fast.emu->low_mem()[0x71c]^=127;
        fast.emu->low_mem()[0x3b8]^=239;
        std::vector<float> changed(OBS_SIZE); retro_compute_obs_real(&fast,changed.data());
        assert(!memcmp(changed.data()+112,ob.data()+112,RETRO_TILES*sizeof(float)));
        fast.emu->load_state(saved); assert(fast.emu->set_rom_blocks(true));
        for(int frame=0;frame<160;frame++) {
            action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|((frame%48<28)?RETRO_BTN_A:0));
            puf_step(&ref); verify(ref,oa.data());
            puf_step(&fast); verify(fast,ob.data());
            assert(oa==ob&&ra==rb&&ta==tb&&state(*ref.emu)==state(*fast.emu));
            if(argc>1&&level==0&&frame==64) preview(fast,ob.data(),argv[1]);
        }
    }
    puf_close(&ref); puf_close(&fast); dict_clear(&a); dict_clear(&b);
    Dict invalid={}; dict_set_str(&invalid,"spawn_levels","1-1");
    dict_set_str(&invalid,"render_backend","crop"); Env rejected={}; bool failed=false;
    try { puf_init(&rejected,&invalid); } catch(const std::exception&) { failed=true; }
    puf_close(&rejected); dict_clear(&invalid); assert(failed);
    puts("PASS: 32 starts, 5120 decisions; every full-screen pixel, reset, reward, terminal and serialized state matched; image independent of crop RAM");
}
