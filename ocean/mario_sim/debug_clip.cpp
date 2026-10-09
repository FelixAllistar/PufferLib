#include "trace.h"
#include "logic_cpu.h"
#include <cstdio>
#include <fstream>
#include <vector>
int main(int argc,char**argv) {
    if(argc!=3)return 2;int target=atoi(argv[2]);std::ifstream in(argv[1],std::ios::binary);
    SmbTraceHeader h;in.read((char*)&h,sizeof(h));std::vector<uint8_t> data(SMB_PRG);in.read((char*)data.data(),data.size());
    if(h.frame_size!=sizeof(SmbTraceFrame)||h.case_size!=sizeof(SmbTraceCase))return 3;
    for(int i=0;i<=target;i++) {
        SmbTraceCase c;in.read((char*)&c,sizeof(c));if(!in)return 4;
        if(i<target){in.seekg((long)c.frames*sizeof(SmbTraceFrame),std::ios::cur);continue;}
        SmbLogic s;smb_scene_reset(&s,&c.scene);
        for(unsigned t=0;t<c.frames;t++) {
            SmbTraceFrame f;in.read((char*)&f,sizeof(f));if(!in)return 5;
            smb_native_frame(&s,data.data(),f.buttons);
            if(t+5>=c.frames) {
                printf("t=%u buttons=%d fault=%x clock=%d/%d video=%d/%d pc=%04x/%04x a=%02x/%02x x=%02x/%02x y=%02x/%02x p=%02x/%02x sp=%02x/%02x ctrl=%02x/%02x mask=%02x/%02x cpuclock=%d len=%d extra=%d frameextra=%d nmi=%d status=%02x\n",t,f.buttons,s.fault,s.timing.timestamp,f.timestamp,s.timing.video_frame,f.video_frame,s.pc,f.pc,s.a,f.a,s.x,f.x,s.y,f.y,s.p,f.p,s.sp,f.sp,s.timing.control,f.control,s.timing.mask,f.mask,s.timing.clock,s.timing.length,s.timing.extra,s.timing.frame_extra,s.timing.nmi,s.timing.status);
                for(int k=0;k<SMB_RAM;k++)if(s.ram[k]!=f.ram[k])printf(" RAM[%03x]=%02x/%02x",k,s.ram[k],f.ram[k]);puts("");
            }
        }
    }
}
