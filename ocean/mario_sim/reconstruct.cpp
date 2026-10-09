#include "reference_sources.h"

static SmbScene randomized(const SmbScene& original,uint32_t seed,int mode) {
    SmbScene s=original;uint32_t rng=seed;const auto* m=s.initial.ram;
    s.mask=SMB_SCENE_CLOCK;s.frame=random_u32(rng)&255;s.interval=random_u32(rng)%21;
    for(auto& b:s.random)b=(uint8_t)random_u32(rng);
    if(mode&1) {
        s.mask|=SMB_SCENE_FORM;s.player_form=random_u32(rng)%3;s.player_size=s.player_form?0:1;
    }
    if(mode&2)for(int k=0;k<5;k++)if(m[15+k]&&m[15+k]<128) {
        s.mask|=SMB_SCENE_ACTOR;s.actor_slot=k;
        s.actor_x=m[0x6e + k]*256+m[0x87+k]+(int)(random_u32(rng)%17)-8;
        s.actor_y=((int)m[0xb6+k]-1)*256+m[0xcf+k];
        s.actor_frame_timer=random_u32(rng)%65;s.actor_interval_timer=random_u32(rng)%33;break;
    }
    if(mode&4) {
        s.mask|=SMB_SCENE_PLAYER;s.player_x=m[0x6d]*256+m[0x86];
        s.player_y=((int)m[0xb5]-1)*256+m[0xce];s.player_xsub=random_u32(rng)&255;s.player_ysub=random_u32(rng)&255;
        s.player_vx=(int8_t)m[0x57];s.player_vy=(int8_t)m[0x9f];
    }
    if(mode&8) {
        s.mask|=SMB_SCENE_TILE;s.tile_column=(m[0x6d]*256+m[0x86])/16+3;
        int last=m[0x725]*16+m[0x726]-((m[0x71f]&3)==0);
        if(s.tile_column>last)s.tile_column=last;
        if(s.tile_column<last-31)s.tile_column=last-31;
        s.tile_row=12;s.tile_value=0x61;
    }
    return s;
}
static int construct_world(uint8_t* data) {
    int changes=0;const int counts[]={3,22,3,6};
    for(int type=0;type<4;type++)for(int index=0;index<counts[type];index++) {
        int p=smb_world_area_pointer(data,type,index,1),entry=0;
        for(int off=0;off<256;) {
            int a=smb_world_byte(data,p+off),b=smb_world_byte(data,p+off+1),row=a&15;
            if(a==255)break;
            if(row==14){off+=3;continue;}
            if(row!=15) {
                int id=b&63,replacement=id;
                switch(id){case 0:replacement=3;break;case 3:replacement=16;break;
                    case 6:replacement=2;break;case 10:replacement=11;break;
                    case 27:replacement=28;break;case 29:replacement=30;break;
                    case 37:replacement=38;break;}
                if(replacement!=id) {
                    if(smb_world_enemy_type(data,type,index,entry,replacement))throw std::runtime_error("invalid generated enemy parameter");
                    changes++;
                }
                entry++;
            }
            off+=2;
        }
        p=smb_world_area_pointer(data,type,index,0);entry=0;
        for(int off=2;off<258;off+=2) {
            int a=smb_world_byte(data,p+off),b=smb_world_byte(data,p+off+1);
            if(a==253)break;
            if((a&15)<12&&!(b&112)&&(b&15)<=8) {
                if(smb_world_block_contents(data,type,index,entry++,((b&15)+3)%9))throw std::runtime_error("invalid generated block parameter");
                changes++;
            }
        }
    }
    return changes;
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3||(argc==3&&std::string(argv[2])!="supplement"&&std::string(argv[2])!="world"&&std::string(argv[2])!="boundaries"))
            throw std::runtime_error("usage: reconstruct OUTPUT_DIRECTORY [supplement|world|boundaries]");
        bool supplement=argc==3&&std::string(argv[2])=="supplement",world=argc==3&&std::string(argv[2])=="world";
        bool boundaries=argc==3&&std::string(argv[2])=="boundaries";
        std::filesystem::create_directories(argv[1]);std::string dir=argv[1];
        std::ofstream trace(dir+"/clips.bin",std::ios::binary),cases(dir+"/cases.jsonl");
        if(!trace||!cases)throw std::runtime_error("cannot open outputs");
        SmbTraceHeader header={0,SMB_TRACE_VERSION,sizeof(SmbScene),sizeof(SmbTraceCase),sizeof(SmbTraceFrame),0,0,1,0x6e01246e5d215cb3ull};
        trace.write((char*)&header,sizeof(header));
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        int world_changes=world?construct_world(rom.cart.prg()):0;
        trace.write((char*)rom.cart.prg(),SMB_PRG);
        Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.set_rom_blocks(false);e.set_idle_skip(true);
        auto sources=collect(rom,e,boundaries);printf("Collected %zu %s starts across 32 stages.\n",sources.size(),world?"constructed":"reference");fflush(stdout);
        const int actions[]={0,128,130,129,131,64,66,65,67,1,16,32,160,144,2,3};
        std::array<long,64> actor_frames={};std::array<long,4> powerups={};
        long hammer_frames=0,fireball_frames=0,water_frames=0,injury_frames=0,star_frames=0;
        int failures=0,terminated=0,constructed=0,used_sources=0;long one_frame=0,persistent=0;
        const char* debug_case=getenv("SMB_RECONSTRUCT_CASE");
        for(size_t si=0;si<sources.size();si++)for(int kind=0;kind<28;kind++) {
            const auto* original=sources[si].scene.initial.ram;
            bool powerup=original[0x14]&&original[0x1b]==46,bowser=false;
            for(int k=0;k<5;k++)bowser|=original[15+k]&&original[15+k]<128&&original[22+k]==45;
            if(supplement&&(kind<20||(!powerup&&!bowser)))continue;
            if(world&&(sources[si].name!="jump_cycle_0"||sources[si].frame))continue;
            if(boundaries&&sources[si].name.rfind("boundary_",0)!=0)continue;
            if(debug_case&&(int)(si*28+kind)!=atoi(debug_case))continue;
            if((!supplement&&kind==0)||(supplement&&kind==20))used_sources++;
            const auto& source=sources[si];e.load_state(*source.snapshot);
            uint32_t seed=0x672931u+(uint32_t)si*977u+(uint32_t)kind*71u,rng=seed;
            SmbScene scene=kind<20?source.scene:randomized(source.scene,seed,kind-19);
            if(supplement) {
                int mode=kind-20;
                if(powerup&&smb_scene_powerup(&scene,mode&3,mode>=4))
                    throw std::runtime_error("invalid powerup template");
                smb_scene_player_effects(&scene,mode&1?96:0,0);
                if(bowser)smb_scene_difficulty(&scene,mode,1,1);
            }
            SmbLogic s={};if(smb_scene_reset(&s,&scene))throw std::runtime_error("invalid scene configuration case="+std::to_string(si*28+kind));
            if(scene.mask)configure_reference(e,s);
            if(scene.mask||world)constructed++;
            SmbTraceCase c={scene,0,(uint32_t)source.stage,(uint32_t)source.frame,(uint32_t)kind,seed};
            std::vector<SmbTraceFrame> frames;int mismatch=-1,bad_frame=-1;std::string reason;
            int limit=kind<16?1:240;
            for(int t=0;t<limit;t++) {
                int buttons=kind<16?actions[kind]:kind==16?(t<(int)source.teacher.size()?source.teacher[t]:explorer(t+source.frame,0))
                    :kind==17?130:kind==18?(t<100?64:131):kind==19?(t%48<32?131:130)
                    :(t%8?frames.back().buttons:actions[random_u32(rng)%16]);
                int fault=smb_native_frame(&s,rom.cart.prg(),buttons);
                retro_check(e.emulate_skip_frame_fast(buttons,0));const auto* m=e.low_mem();
                auto reference=import_logic(e);
                SmbTraceFrame f={};f.buttons=buttons;memcpy(f.ram,m,SMB_RAM);
                f.a=reference.a;f.x=reference.x;f.y=reference.y;f.p=reference.p&207;f.sp=reference.sp;f.pc=reference.pc;
                f.timestamp=reference.timing.timestamp;f.video_frame=reference.timing.video_frame;
                f.control=reference.timing.control;f.mask=reference.timing.mask;frames.push_back(f);
                if(fault){reason="native_fault";mismatch=fault;bad_frame=t;break;}
                for(int k=0;k<SMB_RAM;k++)if(s.ram[k]!=m[k]){mismatch=k;break;}
                if(mismatch>=0){reason=boundary(e)?"ram":"video_frame_boundary";bad_frame=t;break;}
                if(s.pc!=f.pc||s.a!=f.a||s.x!=f.x||s.y!=f.y||(s.p&207)!=f.p||s.sp!=f.sp
                    ||s.timing.timestamp!=f.timestamp||s.timing.video_frame!=f.video_frame
                    ||s.timing.control!=f.control||s.timing.mask!=f.mask) {
                    mismatch=SMB_RAM;reason="cpu_or_clock";bad_frame=t;break;
                }
                float obs[SMB_DEBUG_OBS];smb_debug_observe(&s,obs);
                for(int k=0;k<SMB_DEBUG_OBS;k++)if(obs[k]!=(float)m[k]/256.0f){mismatch=k;reason="observation";bad_frame=t;break;}
                if(mismatch>=0)break;
                for(int k=0;k<6;k++)if(m[15+k]&&m[15+k]<128)actor_frames[m[22+k]&63]++;
                if(m[0x14]&&m[0x1b]==46&&m[0x39]<4)powerups[m[0x39]]++;
                for(int k=0;k<9;k++)if(m[0x2a+k]&0x80){hammer_frames++;break;}
                if(m[0x24]||m[0x25])fireball_frames++;
                water_frames+=m[0x74e]==0;injury_frames+=m[0x79e]!=0;star_frames+=m[0x79f]!=0;
                if(t==0&&!gameplay(m))terminated++;
            }
            c.frames=frames.size();header.cases++;header.frames+=c.frames;
            if(kind<16)one_frame++;else persistent++;
            trace.write((char*)&c,sizeof(c));trace.write((char*)frames.data(),frames.size()*sizeof(SmbTraceFrame));
            cases<<"{\"case\":"<<si*28+kind<<",\"source\":\""<<source.name<<"\",\"stage\":\""<<source.stage/4+1<<'-'<<source.stage%4+1
                <<"\",\"source_frame\":"<<source.frame<<",\"kind\":"<<kind<<",\"seed\":"<<seed<<",\"constructed\":"<<(scene.mask||world?"true":"false")
                <<",\"frames\":"<<c.frames<<",\"passed\":"<<(mismatch<0?"true":"false")<<",\"reason\":\""<<reason<<"\",\"field\":"<<mismatch<<",\"bad_frame\":"<<bad_frame<<"}\n";
            if(mismatch>=0){failures++;if(failures<=16)fprintf(stderr,"case %u stage=%d source=%d kind=%d frame=%d %s %x\n",header.cases-1,source.stage,source.frame,kind,bad_frame,reason.c_str(),mismatch);}
        }
        if(!header.cases)throw std::runtime_error("empty reconstruction panel");
        if(!failures&&!debug_case)header.magic=SMB_TRACE_MAGIC;
        trace.seekp(0);trace.write((char*)&header,sizeof(header));
        std::ofstream summary(dir+"/summary.json");
        summary<<"{\"scope\":\"video_frame_clips\",\"panel\":\""<<(supplement?"supplement":world?"world":boundaries?"boundaries":"main")
            <<"\",\"complete_game_qualified\":false,\"sources\":"<<used_sources<<",\"available_sources\":"<<sources.size()
            <<",\"world_data_edits\":"<<world_changes
            <<",\"cases\":"<<header.cases<<",\"frames\":"<<header.frames<<",\"failures\":"<<failures
            <<",\"one_frame_cases\":"<<one_frame<<",\"persistent_cases\":"<<persistent<<",\"constructed_cases\":"<<constructed
            <<",\"first_frame_outside_gameplay\":"<<terminated<<",\"actor_frames\":{";
        bool first=true;for(int k=0;k<64;k++)if(actor_frames[k]){summary<<(first?"":",")<<'"'<<k<<"\":"<<actor_frames[k];first=false;}
        summary<<"},\"powerup_frames\":[";for(int k=0;k<4;k++)summary<<(k?",":"")<<powerups[k];
        summary<<"],\"hammer_frames\":"<<hammer_frames<<",\"player_fireball_frames\":"<<fireball_frames
            <<",\"water_frames\":"<<water_frames<<",\"injury_frames\":"<<injury_frames<<",\"star_frames\":"<<star_frames<<"}\n";
        printf("Reconstruction: cases=%u frames=%u failures=%d constructed=%d\n",header.cases,header.frames,failures,constructed);
        return failures?1:0;
    }catch(const std::exception& e){fprintf(stderr,"reconstruct: %s\n",e.what());return 2;}
}
