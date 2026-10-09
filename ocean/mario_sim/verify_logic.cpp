#include "../retro/retro.h"
#include "logic.h"
#include "rom_import.h"
#include "logic_cpu.h"
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

static SmbLogic import_logic(Nes_Emu& e) {
    auto s=smb_import_logic(e);if(getenv("SMB_LOGIC_UNTIMED"))s.timing.enabled=0;return s;
}
int main(int argc,char**argv) {
    try {
        int count=argc>1?atoi(argv[1]):1,limit=argc>2?atoi(argv[2]):600;
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.set_rom_blocks(false);e.set_idle_skip(true);
        long frames=0;int failures=0;int seen[64]={};
        for(int stage=0;stage<count;stage++) {
            retro_prepare_start_locked(rom,stage/4+1,stage%4+1);e.load_state(rom.starts[stage]->state);
            SmbLogic s=import_logic(e);
            for(int t=0;t<limit;t++) {
                int buttons=t<60?0:130|((t%40<24)?1:0);
                if(smb_native_frame(&s,rom.cart.prg(),buttons)) {
                    fprintf(stderr,"native fault stage=%d frame=%d pc=%04x code=%x instructions=%u\n",stage,t,s.pc,s.fault,s.instructions);failures++;break;
                }
                retro_check(e.emulate_skip_frame_fast(buttons,0));frames++;
                const auto* m=e.low_mem();int differences=0;
                if(getenv("SMB_CLOCK_TRACE")) {int q[12];e.frame_clock_debug(q);fprintf(stderr,"t=%d clock=%d/%d pc=%04x/%04lx\n",t,s.timing.timestamp,q[0],s.pc,e.cpu_debug().r.pc);}
                auto ref=import_logic(e);
                if(s.timing.enabled&&(s.timing.timestamp!=ref.timing.timestamp||s.pc!=ref.pc||s.a!=ref.a||s.x!=ref.x||s.y!=ref.y||s.sp!=ref.sp||((s.p^ref.p)&207))) {
                    fprintf(stderr,"state stage=%d t=%d clock=%d/%d pc=%04x/%04x a=%02x/%02x x=%02x/%02x y=%02x/%02x sp=%02x/%02x p=%02x/%02x\n",stage,t,s.timing.timestamp,ref.timing.timestamp,s.pc,ref.pc,s.a,ref.a,s.x,ref.x,s.y,ref.y,s.sp,ref.sp,s.p,ref.p);failures++;break;
                }
                for(int k=0;k<2048;k++)if(m[k]!=s.ram[k]) {
                    if(differences<24)fprintf(stderr," %03x:%02x/%02x",k,s.ram[k],m[k]);differences++;
                }
                if(differences) {
                    fprintf(stderr,"\nstage=%d frame=%d bytes=%d framecount=%d/%d pc=%04x/%04lx sp=%02x/%02x\n",stage,t,differences,s.ram[9],m[9],s.pc,e.cpu_debug().r.pc,s.sp,e.cpu_debug().r.sp);failures++;break;
                }
                for(int k=0;k<6;k++)if(m[15+k])seen[m[22+k]&63]++;
                if(getenv("SMB_LOGIC_GAMEPLAY_ONLY")&&(m[0x770]!=1||m[0x772]!=3))break;
            }
        }
        printf("frames=%ld failures=%d actors:",frames,failures);
        for(int k=0;k<64;k++)if(seen[k])printf(" %d:%d",k,seen[k]);printf("\n");
        return failures?1:0;
    }catch(const std::exception& e){fprintf(stderr,"logic parity: %s\n",e.what());return 2;}
}
