// Compact regressions: controller bytes replayed from natural ROM starts.
// No checkpoints, videos or frame-by-frame RAM archives are needed.
#include "reference_sources.h"
#include "rom_check.h"
#include <iterator>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: test_parity STAGE_INDEX ACTION_BYTES");
        int stage=std::stoi(argv[1]);fpt_rom_require(stage>=0&&stage<32,"invalid stage");
        std::ifstream input(argv[2],std::ios::binary);
        fpt_rom_require(bool(input),"missing action tape");
        std::vector<uint8_t> actions((std::istreambuf_iterator<char>(input)),{});
        fpt_rom_require(!actions.empty()&&actions.size()<=108000,"invalid tape length");
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        retro_prepare_start_locked(rom,stage/4+1,stage%4+1);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));
        emu.set_rom_blocks(false);emu.set_idle_skip(true);emu.load_state(rom.starts[stage]->state);
        auto native=smb_import_logic(emu);
        for(size_t t=0;t<actions.size();t++) {
            fpt_rom_require(!smb_native_frame(&native,rom.cart.prg(),actions[t]),"native fault");
            retro_check(emu.emulate_skip_frame_fast(actions[t],0));auto real=smb_import_logic(emu);
            int difference=fpt_rom_difference(native,real);
            if(difference>=0) {
                for(const auto* s:{&native,&real})fprintf(stderr,
                    "frame=%zu field=%d pc=%04x a=%02x p=%02x timestamp=%d video=%d status=%02x control=%02x mask=%02x clock=%d length=%d extra=%d nmi=%d ended=%d\n",
                    t,difference,s->pc,s->a,s->p&207,s->timing.timestamp,s->timing.video_frame,s->timing.status,
                    s->timing.control,s->timing.mask,s->timing.clock,s->timing.length,s->timing.extra,s->timing.nmi,s->timing.frame_ended);
                return 1;
            }
        }
        printf("{\"stage\":%d,\"ROM_frames\":%zu,\"failures\":0}\n",stage,actions.size());return 0;
    }catch(const std::exception& e){fprintf(stderr,"parity: %s\n",e.what());return 2;}
}
