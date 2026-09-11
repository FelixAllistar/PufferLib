// Diagnostic only: two independent cores from cold boot, without state/phase
// alignment. A successful process exit is NOT an exactness pass. Keep RNG and
// boot differences visible; do not silently mask them to manufacture parity.
#include "retro.h"
#include "include/libretro.h"
#include <dlfcn.h>
#include <array>
static unsigned buttons;
static bool environment(unsigned cmd,void* data) {
    switch(cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data=true; return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data=false; return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data="."; return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
        default: return false;
    }
}
static void video(const void*,unsigned,unsigned,size_t) {}
static void sound(int16_t,int16_t) {}
static size_t batch(const int16_t*,size_t n) { return n; }
static void poll() {}
static int16_t input(unsigned port,unsigned,unsigned,unsigned id) {
    if(port) return 0;
    unsigned lib=((buttons&1)?1u<<RETRO_DEVICE_ID_JOYPAD_A:0)
        |((buttons&2)?1u<<RETRO_DEVICE_ID_JOYPAD_B:0)
        |((buttons&8)?1u<<RETRO_DEVICE_ID_JOYPAD_START:0)
        |((buttons&16)?1u<<RETRO_DEVICE_ID_JOYPAD_UP:0)
        |((buttons&32)?1u<<RETRO_DEVICE_ID_JOYPAD_DOWN:0)
        |((buttons&64)?1u<<RETRO_DEVICE_ID_JOYPAD_LEFT:0)
        |((buttons&128)?1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT:0);
    return id==RETRO_DEVICE_ID_JOYPAD_MASK?lib:(id<16?(lib>>id)&1:0);
}
int main() {
    void* core=dlopen("ocean/retro/cores/fceumm_libretro.so",RTLD_NOW|RTLD_LOCAL);
    if(!core) { fprintf(stderr,"%s\n",dlerror()); return 1; }
#define API(name) auto name=(decltype(&retro_##name))dlsym(core,"retro_" #name)
    API(set_environment); API(set_video_refresh); API(set_audio_sample); API(set_audio_sample_batch);
    API(set_input_poll); API(set_input_state); API(init); API(load_game); API(run);
    API(get_memory_data); API(get_memory_size); API(unload_game); API(deinit);
    set_environment(environment); set_video_refresh(video); set_audio_sample(sound);
    set_audio_sample_batch(batch); set_input_poll(poll); set_input_state(input); init();
    std::vector<unsigned char> bytes(40976); FILE* f=fopen("ocean/retro/roms/smb1.nes","rb");
    if(!f||fread(bytes.data(),1,bytes.size(),f)!=bytes.size()) return 2;
    fclose(f);
    retro_game_info game={"ocean/retro/roms/smb1.nes",bytes.data(),bytes.size(),nullptr};
    if(!load_game(&game)) return 3;
    auto m=(unsigned char*)get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    printf("reference RAM size=%zu\n",get_memory_size(RETRO_MEMORY_SYSTEM_RAM));
    Nes_Cart cart; Mem_File_Reader reader(bytes.data(),bytes.size()); retro_check(cart.load_ines(reader));
    Nes_Emu q; retro_check(q.set_cart(&cart)); retro_bind_pixels(&q);
    int mismatch=0;
    for(int frame=0;frame<600;frame++) {
        buttons=frame==100?8:frame>300?(128|2|((frame%48<28)?1:0)):0;
        run(); retro_check(q.emulate_frame(buttons,0)); const unsigned char* n=q.low_mem();
        int diff=0; for(int j=0;j<2048;j++) diff+=m[j]!=n[j];
        if(frame>=300&&diff) mismatch++;
        if(frame%30==0||frame==599) printf("frame=%d diff=%d mode=%d/%d task=%d/%d fc=%d/%d x=%d/%d sub=%d/%d y=%d/%d rng=%d/%d\n",
            frame,diff,m[0x770],n[0x770],m[0x772],n[0x772],m[9],n[9],robs_x(m),robs_x(n),m[0x400],n[0x400],m[0xce],n[0xce],m[0x7a7],n[0x7a7]);
    }
    printf("Unaligned full-RAM mismatches after frame 300: %d/300 (diagnostic, not a parity certificate)\n",mismatch);
    unload_game(); deinit(); dlclose(core);
}
