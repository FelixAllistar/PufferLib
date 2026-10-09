#include "observation.h"
#include "bank_io.h"
#include "trace.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static void position(SmbLogic& s,int obj,int x,int y) {
    s.ram[0x6d+obj]=x>>8;s.ram[0x86+obj]=x&255;
    s.ram[0xb5+obj]=(y+256)>>8;s.ram[0xce + obj]=y&255;
}
static SmbLogic empty_scene() {
    SmbLogic s={};s.timing.video_frame=100;s.ram[0x74e]=1;s.ram[0x754]=1;s.ram[0xe]=8;
    position(s,0,120,160);s.ram[0x725]=1;s.ram[0x726]=0;s.ram[0x71f]=0;
    s.ram[0x499]=1;return s;
}
static void semantic_controls(const uint8_t* world) {
    SmbLogic s=empty_scene();float o[FPT_OBS],other[FPT_OBS];FptObservationHistory h={};
    // Both horizontal edges and both vertical gameplay edges are represented.
    s.ram[0x500]=0x61;s.ram[0x50f]=0xc0;s.ram[0x5c0]=0x24;s.ram[0x5cf]=0xc2;
    fpt_observe(&s,world,&h,o);
    require(o[FPT_TERRAIN_OFFSET]==0x61&&o[FPT_TERRAIN_OFFSET+15]==0xc0,"top screen corners lost");
    require(o[FPT_TERRAIN_OFFSET+12*17]==0x24&&o[FPT_TERRAIN_OFFSET+12*17+15]==0xc2,"bottom screen corners lost");
    require(o[FPT_TERRAIN_OFFSET+16]==-2,"aligned camera has phantom seventeenth column");
    s.ram[0x71c]=1;s.ram[0x71f]=1;s.ram[0x5d0]=0xc4;
    fpt_observe(&s,world,&h,o);require(o[FPT_TERRAIN_OFFSET+16]==0xc4,"partial right edge lost while scrolling");
    require(fpt_terrain_feature(0xc4,9,16,0,0,1.0f/16)==1.0f/16,"partial tile coverage wrong");
    require(fpt_terrain_feature(0x23,1,0,0,0,0)==1&&fpt_terrain_feature(0x23,2,0,0,0,0)==1,
        "visually blank bouncing-block tile lost its collision");
    require(fpt_terrain_feature(0x5f,1,0,0,0,0)==0&&fpt_terrain_feature(0x5f,3,0,0,0,0)==1,
        "hidden block must be bumpable without blocking lateral motion");
    require(fpt_terrain_feature(0x6d,5,0,0,0,0)==1,"residual flag ball must use climb semantics");
    // A parser cursor can point at an unwritten cell with stale ring-buffer data.
    s.ram[0x71f]=0;fpt_observe(&s,world,&h,o);
    require(o[FPT_TERRAIN_OFFSET+16]==-1,"stale parser column leaked as known terrain");
    require(o[FPT_TERRAIN_OFFSET+1]==0,"known empty confused with unknown");
    for(int phase:{0,4}){s.ram[0x71f]=phase;require(fpt_terrain_tile(&s,16)==-1,"parser phase residency");}
    for(int phase:{1,2,3,5,6,7}){s.ram[0x71f]=phase;require(fpt_terrain_tile(&s,16)==0xc4,"written parser column hidden");}

    s=empty_scene();s.ram[0x57]=0xff;s.ram[0x9f]=0xfe;s.ram[0x433]=128;s.ram[0x400]=254;
    h={};fpt_observe(&s,world,&h,o);
    require(o[0]==-1.0f/64&&o[1]==-1.5f/8,"signed velocity or vertical fractional units wrong");
    s.ram[0x400]=255;fpt_observe(&s,world,&h,other);require(other[2]-o[2]==1.0f/256,"subpixel precision lost");

    // Score, lives, level labels, coins, sound queues and sprite graphics must
    // not influence this policy. Mutate them without touching physics state.
    h={};fpt_observe(&s,world,&h,o);SmbLogic changed=s;
    for(int i=0x7d7;i<=0x7fa;i++)changed.ram[i]^=255;
    for(int i:{0x75a,0x75c,0x75e,0x75f,0x760,0x7fc,0xfa,0xfb,0xfc,0xfd,0xfe,0xff})changed.ram[i]^=255;
    for(int i=0x200;i<0x400;i++)changed.ram[i]^=255;
    FptObservationHistory h2={};fpt_observe(&changed,world,&h2,other);
    require(!memcmp(o,other,sizeof(o)),"HUD/audio/sprite bookkeeping leaked into observation");

    s=empty_scene();h={};s.ram[0xf]=1;s.ram[0x16]=6;s.ram[0x49a]=9;position(s,1,150,160);
    s.ram[0x2a]=0x81;s.ram[0x4a2]=7;position(s,13,180,100);
    s.ram[0x24]=1;s.ram[0x4a0]=7;position(s,7,90,120);
    fpt_observe(&s,world,&h,o);
    require(o[FPT_ENTITY_OFFSET]==7,"Goomba missing");
    require(o[FPT_ENTITY_OFFSET+FPT_ENTITY_MISC_OFFSET*24]==FPT_ENTITY_HAMMER,"separate hammer pool missing");
    require(o[FPT_ENTITY_OFFSET+FPT_ENTITY_FIREBALL_OFFSET*24]==FPT_ENTITY_FIREBALL,"separate player fireball pool missing");
    require(o[FPT_ENTITY_OFFSET+FPT_ENTITY_MISC_OFFSET*24+FPT_E_MOTION_KNOWN]==0,"reset invents object velocity");
    s.timing.video_frame++;position(s,13,178,99);s.ram[0x64]=0x70;
    fpt_observe(&s,world,&h,other);
    require(other[FPT_ENTITY_OFFSET+FPT_ENTITY_MISC_OFFSET*24+FPT_E_VX]==-2.0f/8&&other[FPT_ENTITY_OFFSET+FPT_ENTITY_MISC_OFFSET*24+FPT_E_VY]==-1.0f/8,
        "hammer velocity did not use observed motion");
    require(other[FPT_ENTITY_OFFSET+FPT_ENTITY_MISC_OFFSET*24+FPT_E_MOTION_KNOWN]==1,"object motion validity missing");
    auto same=h;fpt_observe(&s,world,&same,o);require(!memcmp(o,other,sizeof(o)),"observing same frame twice changes velocity");
    s.ram[0x24]=0x80;s.ram[0x2a]=1;fpt_observe(&s,world,&h,o);
    require(o[FPT_ENTITY_OFFSET+FPT_ENTITY_FIREBALL_OFFSET*24]==0&&o[FPT_ENTITY_OFFSET+FPT_ENTITY_MISC_OFFSET*24]==0,"cosmetic explosion or score coin exposed as projectile");

    // Long firebar geometry uses the ROM's own quantized trigonometric table.
    s=empty_scene();h={};s.ram[0xf]=1;s.ram[0x10]=0x80;s.ram[0x16]=0x1f;s.ram[0xa0]=0;position(s,1,120,160);
    fpt_observe(&s,world,&h,o);
    for(int k=0;k<12;k++) {
        const float* e=o+FPT_ENTITY_OFFSET+k*24;
        require(e[0]==0x20,"long firebar segment missing");
        require(e[FPT_E_DX]==0&&e[FPT_E_DY]==(-8*k)/256.0f,"firebar table geometry wrong");
    }
    s.ram[0x16]=0x1b;fpt_observe(&s,world,&h,o);
    require(o[FPT_ENTITY_OFFSET+5*24]!=0&&o[FPT_ENTITY_OFFSET+6*24]==0,"short firebar length wrong");
    position(s,1,0,160);fpt_observe(&s,world,&h,o);
    for(int k=0;k<6;k++)require(o[FPT_ENTITY_OFFSET+k*24]==0,"ROM-suppressed edge firebar exposed");
    position(s,1,120,0xf8);fpt_observe(&s,world,&h,o);
    for(int k=0;k<6;k++)require(o[FPT_ENTITY_OFFSET+k*24]==0,"ROM-hidden firebar exposed");
    position(s,1,120,160);
    s.ram[0x16]=0x24;s.ram[0x1e]=0xff;s.ram[0x49a]=5;fpt_observe(&s,world,&h,o);
    require(o[FPT_ENTITY_OFFSET+FPT_E_ACTIVE]==1,"platform partner state misread as enemy defeat");
    s.ram[0x16]=6;s.ram[0x1e]=2;fpt_observe(&s,world,&h,o);
    require(o[FPT_ENTITY_OFFSET+FPT_E_ACTIVE]==0,"stomped Goomba marked active");
    s.ram[0x16]=0x2e;s.ram[0x39]=2;s.ram[0x49a]=3;fpt_observe(&s,world,&h,o);
    require(o[FPT_ENTITY_OFFSET]==FPT_ENTITY_STAR,"powerup type lost");
    puts("PASS full screen/partial edges, parser residency, precision, HUD independence, all projectile pools, firebar geometry, object history");
}

template<class T>static void read_exact(std::ifstream& f,T* p,size_t n=1) {
    require(bool(f.read((char*)p,n*sizeof(T))),"truncated recorded ROM trace");
}
static void trace_controls(const char* path) {
    std::ifstream in(path,std::ios::binary);SmbTraceHeader header;read_exact(in,&header);
    require(header.magic==SMB_TRACE_MAGIC&&header.version==SMB_TRACE_VERSION&&header.scene_size==sizeof(SmbScene)
        &&header.case_size==sizeof(SmbTraceCase)&&header.frame_size==sizeof(SmbTraceFrame),"incompatible trace");
    uint8_t world[SMB_PRG];read_exact(in,world,SMB_PRG);
    unsigned frames=0,hammer=0,fireball=0,firebar=0;std::set<int> types;float o[FPT_OBS];
    for(unsigned c=0;c<header.cases;c++) {
        SmbTraceCase item;read_exact(in,&item);SmbLogic s=item.scene.initial;FptObservationHistory history={};
        fpt_observe(&s,world,&history,o);
        for(unsigned t=0;t<item.frames;t++) {
            SmbTraceFrame f;read_exact(in,&f);memcpy(s.ram,f.ram,SMB_RAM);s.timing.video_frame=f.video_frame;
            fpt_observe(&s,world,&history,o);frames++;
            for(float x:o)require(isfinite(x),"non-finite observation on ROM trajectory");
            // Independent coverage requirement: every on-screen positioned
            // hammer / fireball in its dedicated pool has an entity record.
            const auto* m=s.ram;int left=m[0x71a]*256+m[0x71c];
            for(int k=0;k<9;k++)if(m[0x2a+k]&128) {
                int x=m[0x7a+k]*256+m[0x93+k],y=((int)m[0xc2+k]-1)*256+m[0xdb+k];
                if(x>=left&&x+8<=left+256&&y>=32&&y+8<=240) {
                    require(o[FPT_ENTITY_OFFSET+(FPT_ENTITY_MISC_OFFSET+k)*24]==FPT_ENTITY_HAMMER,"ROM hammer omitted");hammer++;
                }
            }
            for(int k=0;k<2;k++)if(m[0x24+k]==1) {
                int x=m[0x74+k]*256+m[0x8d+k],y=((int)m[0xbc+k]-1)*256+m[0xd5+k];
                if(x>=left&&x+8<=left+256&&y>=32&&y+8<=240) {
                    require(o[FPT_ENTITY_OFFSET+(FPT_ENTITY_FIREBALL_OFFSET+k)*24]==FPT_ENTITY_FIREBALL,"ROM player fireball omitted");fireball++;
                }
            }
            for(int k=0;k<FPT_ENTITY_COUNT;k++)if(o[FPT_ENTITY_OFFSET+k*24]) {
                int type=(int)o[FPT_ENTITY_OFFSET+k*24];types.insert(type);firebar+=type>=0x1c&&type<=0x23;
            }
        }
    }
    require(in.peek()==EOF&&frames==header.frames,"trace frame count mismatch");
    printf("ROM trace %s: frames=%u types=%zu hammers=%u player_fireballs=%u firebar_segments=%u\n",path,frames,types.size(),hammer,fireball,firebar);
}
int main(int argc,char** argv) {
    try {
        SmbBank bank("build/mario_sim/fpg/curriculum/bank.bin");semantic_controls(bank.worlds.data());
        for(int i=1;i<argc;i++)trace_controls(argv[i]);return 0;
    }catch(const std::exception& e){fprintf(stderr,"semantic observation test: %s\n",e.what());return 1;}
}
