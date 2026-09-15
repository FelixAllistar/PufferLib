// The reference suite stays executable in this binary too. Both engines share
// peripheral semantics; this certifies a refactor, not hardware equivalence.
#define main retro_reference_suite_main
#include "../tests/test_retro.cpp"
#undef main

static void instruction_differential() {
    // Exercise every compiled entry under varied flags, registers, stack wrap,
    // and indirect addressing. Exclude peripheral I/O: frame replay covers its
    // scheduler; these tests deliberately execute just one CPU instruction.
    Nes_Emu a,b;
    retro_check(a.set_cart(&retro_rom().cart)); retro_check(b.set_cart(&retro_rom().cart));
    require(b.set_rom_blocks(true),"compiled instruction engine unavailable");
    auto& ca=a.cpu_debug(); auto& cb=b.cpu_debug();
    struct Case { int pc,mode,control; };
    const Case cases[]={
#include "rom_blocks_cases.inc"
    };
    const unsigned char* prg=retro_rom().cart.prg();
    unsigned rng=419; int checks=0;
    for(const auto& test:cases) {
        unsigned operand=test.mode>=2?prg[test.pc-0x8000+1]:0;
        unsigned addr=operand;
        if(test.mode>=10) addr|=prg[test.pc-0x8000+2]<<8;
        // LENGTH modes: imp,acc,imm,zp,zx,zy,ix,iy,rel,abs,ax,ay.
        if(test.mode==9) addr|=prg[test.pc-0x8000+2]<<8;
        if(test.mode>=9&&!test.control&&addr>=0x1f00&&addr<0x8000) continue;
        for(int trial=0;trial<16;trial++) {
            for(int i=0;i<2048;i++) ca.low_mem[i]=retro_random(&rng);
            Nes_Cpu::registers_t regs={}; regs.pc=test.pc;
            regs.a=trial<4?(trial==0?0:trial==1?255:trial==2?127:128):retro_random(&rng);
            regs.x=trial<4?255:retro_random(&rng); regs.y=trial<4?255:retro_random(&rng);
            regs.sp=trial<4?trial:retro_random(&rng); regs.status=retro_random(&rng);
            if(test.mode==6||test.mode==7) {
                unsigned zp=(operand+(test.mode==6?regs.x:0))&255;
                ca.low_mem[zp]=255;
                const int pages[]={0,6,0x80,0xfe,0xff};
                ca.low_mem[(zp+1)&255]=pages[trial%5];
            }
            ca.r=cb.r=regs; memcpy(cb.low_mem,ca.low_mem,2048);
            auto ra=ca.run(1),rb=cb.run(1);
            bool same=ca.r.pc==cb.r.pc&&ca.r.a==cb.r.a&&ca.r.x==cb.r.x&&ca.r.y==cb.r.y
                &&ca.r.sp==cb.r.sp&&ca.r.status==cb.r.status&&ca.time()==cb.time()&&ra==rb
                &&!memcmp(ca.low_mem,cb.low_mem,2048)&&ca.error_count()==cb.error_count();
            if(!same) {
                fprintf(stderr,"instruction divergence pc=%04x opcode=%02x trial=%d ticks=%ld/%ld pc'=%04lx/%04lx flags=%02x/%02x A=%02x/%02x\n",
                    test.pc,prg[test.pc-0x8000],trial,ca.time(),cb.time(),ca.r.pc,cb.r.pc,ca.r.status,cb.r.status,ca.r.a,cb.r.a);
                throw std::runtime_error("compiled instruction semantics differ");
            }
            checks++;
        }
    }
    // Writable RAM code is intentionally interpreted, including modification
    // of its own immediate operand. It must not acquire stale compiled bytes.
    const unsigned char tape[]={0xa9,0xff,0x18,0x69,1,0xee,0x01,0x05,0x4c,0,5};
    memcpy(ca.low_mem+0x500,tape,sizeof(tape)); memcpy(cb.low_mem,ca.low_mem,2048);
    ca.r={}; cb.r={}; ca.r.pc=cb.r.pc=0x500;
    for(int i=0;i<1024;i++) {
        ca.run(1); cb.run(1);
        require(ca.r.pc==cb.r.pc&&ca.r.a==cb.r.a&&ca.r.status==cb.r.status
            &&ca.time()==cb.time()&&!memcmp(ca.low_mem,cb.low_mem,2048),"RAM code fallback diverged");
    }
    printf("PASS: %d instruction edge cases and 1024 self-modifying RAM instructions\n",checks);
    Nes_Cart altered;
    retro_check(altered.resize_prg(32768)); retro_check(altered.resize_chr(8192));
    memcpy(altered.prg(),prg,32768); memcpy(altered.chr(),retro_rom().cart.chr(),8192);
    altered.prg()[0x100]^=1; altered.set_mapper(0,0);
    Nes_Emu incompatible; retro_check(incompatible.set_cart(&altered));
    require(!incompatible.set_rom_blocks(true),"mismatching PRG accepted by compiled engine");
    puts("PASS: altered PRG is rejected before compiled execution");
}

static void compare(Nes_Emu& a,Nes_Emu& b,int level,int frame) {
    bool ram=!memcmp(a.low_mem(),b.low_mem(),2048);
    auto sa=saved(a),sb=saved(b);
    if(!ram||sa!=sb||!equal_image(a,b)) {
        fprintf(stderr,"FAIL: blocks divergence level=%d-%d frame=%d RAM=%s state=%s image=%s\n",
            level/4+1,level%4+1,frame,ram?"same":"DIFF",sa==sb?"same":"DIFF",equal_image(a,b)?"same":"DIFF");
        int count=0;
        for(int i=0;i<2048&&count<12;i++) if(a.low_mem()[i]!=b.low_mem()[i]) {
            fprintf(stderr,"  RAM[%04x] ref=%02x blocks=%02x\n",i,a.low_mem()[i],b.low_mem()[i]); count++;
        }
        for(size_t i=0;i<std::min(sa.size(),sb.size());i++) if(sa[i]!=sb[i]) {
            fprintf(stderr,"  state first difference offset=%zu ref=%02x blocks=%02x\n",i,(unsigned char)sa[i],(unsigned char)sb[i]); break;
        }
        throw std::runtime_error("compiled CPU parity failure");
    }
}

static void polling_differential() {
    Nes_Emu a,b;
    retro_check(a.set_cart(&retro_rom().cart)); retro_check(b.set_cart(&retro_rom().cart));
    a.set_idle_skip(true); b.set_idle_skip(true);
    require(b.set_rom_blocks(true),"polling test requires blocks");
    auto& ca=a.cpu_debug(); auto& cb=b.cpu_debug();
    unsigned rng=73; int checks=0;
    // Enter at the branch itself, including flags that did NOT come from the
    // preceding load/AND and an initially set write latch. This catches
    // shortcuts that only work along the game's usual entry path.
    for(int pc : {0x800d,0x8012,0x8142,0x8155}) for(int trial=0;trial<16;trial++)
        for(int limit=1;limit<=128;limit++) {
            Nes_Cpu::registers_t r={}; r.pc=pc;
            r.a=trial%4==0?0:trial%4==1?2:trial%4==2?64:retro_random(&rng);
            r.x=retro_random(&rng); r.y=retro_random(&rng); r.sp=retro_random(&rng);
            r.status=retro_random(&rng);
            int value=2;
            if(pc==0x8142) { r.status&=~2; value=(trial&1)?0xc0:0x40; }
            else if(pc==0x8155) { r.status|=2; value=(trial&1)?0x80:0; }
            else r.status&=~0x80;
            ca.r=cb.r=r;
            a.cached_status_debug(10000,value,true); b.cached_status_debug(10000,value,true);
            auto ra=ca.run(limit),rb=cb.run(limit);
            if(ra!=rb||ca.time()!=cb.time()||saved(a)!=saved(b)) {
                fprintf(stderr,"poll fold divergence pc=%04x trial=%d limit=%d time=%ld/%ld\n",pc,trial,limit,ca.time(),cb.time());
                throw std::runtime_error("poll folding changed branch-entry or clock-boundary semantics");
            }
            checks++;
        }
    printf("PASS: %d status-poll branch-entry/latch/clock-boundary cases\n",checks);
}

static void differential() {
    Dict cfg={}; dict_set_str(&cfg,"spawn_levels","all");
    Env seed={}; puf_init(&seed,&cfg);
    Nes_Emu a,b;
    retro_check(a.set_cart(&retro_rom().cart)); retro_check(b.set_cart(&retro_rom().cart));
    a.set_idle_skip(true); b.set_idle_skip(true);
    std::vector<unsigned char> pa(Nes_Emu::buffer_width*256),pb(pa.size());
    a.set_pixels(pa.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    b.set_pixels(pb.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    require(b.set_rom_blocks(true),"compiled engine not available for matching PRG");
    require(b.set_wide_background(true),"wide background unavailable");
    for(int f=0;f<400;f++) {
        unsigned char mask=f==100?RETRO_BTN_START:f>300?RETRO_BTN_RIGHT|RETRO_BTN_A:0;
        retro_check(a.emulate_frame(mask)); retro_check(b.emulate_frame(mask)); compare(a,b,0,f);
    }
    unsigned rng=98765; int frames=400;
    for(int level=0;level<32;level++) {
        a.load_state(retro_rom().starts[level]->state); b.load_state(retro_rom().starts[level]->state);
        require(b.set_rom_blocks(true),"restore failed to re-enable compiled CPU");
        for(int f=0;f<2048;f++) {
            int mask=retro_action_mask(retro_random(&rng)%64);
            if(f<768) mask=RETRO_BTN_RIGHT|RETRO_BTN_B|((f%48<28)?RETRO_BTN_A:0);
            else if(f<1024) mask=RETRO_BTN_LEFT|RETRO_BTN_B|((f%37<19)?RETRO_BTN_A:0);
            else if(f>=1280&&f<1536) mask=0;
            if(f==1600||f==1620) mask=RETRO_BTN_START;
            retro_check(a.emulate_frame(mask)); retro_check(b.emulate_frame(mask));
            require(!a.error_count()&&!b.error_count(),"unsupported opcode in differential trajectory");
            compare(a,b,level,f); frames++;
            if(f%128==127) {
                Nes_State sa,sb; a.save_state(&sa); b.save_state(&sb);
                a.load_state(sa); b.load_state(sb); require(b.set_rom_blocks(true),"roundtrip enable failed");
            }
        }
    }
    puf_close(&seed); dict_clear(&cfg);
    printf("PASS: %d compiled/reference ROM frames; full RAM, CPU/PPU/APU/joypad serialized state and images\n",frames);
}

static void check_reference_visual_means(const Env& e,const float* obs) {
    // Independent full-screen means, including reset images and all edges.
    const auto& frame=e.emu->frame();
    const auto* pixels=e.reset_image?e.start->pixels:frame.pixels;
    const auto* palette=e.reset_image?e.start->palette:frame.palette;
    int pitch=e.reset_image?256:frame.pitch;
    float lut[256];
    for(int i=0;i<256;i++) {
        const auto& c=Nes_Emu::nes_colors[palette[i]&(Nes_Emu::color_table_size-1)];
        lut[i]=retro_luma(c.red,c.green,c.blue);
    }
    for(int ty=0;ty<120;ty++) for(int tx=0;tx<128;tx++) {
        float sum=0;
        if(pixels) for(int y=0;y<2;y++) for(int x=0;x<2;x++)
            sum+=lut[pixels[(ty*2+y)*pitch+tx*2+x]];
        float mean=sum*0.25f;
        require(!memcmp(&mean,obs+RETRO_EGO_SIZE+RETRO_ENT_SIZE+ty*128+tx,sizeof(float)),
            "parallel observation mean differs from original arithmetic");
    }
}

static void wrapper_differential() {
    Dict cfg={}; dict_set_str(&cfg,"spawn_levels","all"); dict_set(&cfg,"max_frames",257);
    Dict vec={}; dict_set(&vec,"total_agents",32); dict_set(&vec,"num_buffers",1);
    int n,starts[1],counts[1];
    Env* a=my_vec_init(&n,starts,counts,&vec,&cfg);
    dict_set_str(&cfg,"cpu_backend","blocks");
    dict_set_str(&cfg,"render_backend","wide");
    Env* b=my_vec_init(&n,starts,counts,&vec,&cfg);
    std::vector<float> oa(n*OBS_SIZE),ob(oa.size()),act(n),ra(n),rb(n),ta(n),tb(n);
    for(int i=0;i<n;i++) {
        a[i].agents[0]={oa.data()+i*OBS_SIZE,&act[i],&ra[i],&ta[i],nullptr,0};
        b[i].agents[0]={ob.data()+i*OBS_SIZE,&act[i],&rb[i],&tb[i],nullptr,0};
        puf_reset(&a[i]); puf_reset(&b[i]);
    }
    unsigned rng=73;
    for(int f=0;f<600;f++) {
        for(int i=0;i<n;i++) {
            act[i]=retro_random(&rng)%64; puf_step(&a[i]);
            check_reference_visual_means(a[i],oa.data()+i*OBS_SIZE);
        }
        #pragma omp parallel for num_threads(4) schedule(static)
        for(int i=0;i<n;i++) puf_step(&b[i]);
        require(oa==ob&&ra==rb&&ta==tb,"compiled wrapper observations/rewards/terminals differ");
        for(int i=0;i<n;i++) require(!memcmp(&a[i].log,&b[i].log,sizeof(Log)),"compiled episode accounting differs");
    }
    my_vec_close(a); my_vec_close(b); dict_clear(&cfg); dict_clear(&vec);
    puts("PASS: 19,200 batched wrapper decisions, 1/4 workers, exact observations/rewards/terminals/logs through resets");
}

static void full_screen_differential() {
    Dict cfg={}; dict_set_str(&cfg,"spawn_levels","all");
    Env a={},b={}; puf_init(&a,&cfg);
    dict_set_str(&cfg,"cpu_backend","blocks");
    dict_set_str(&cfg,"render_backend","wide"); puf_init(&b,&cfg);
    a.spawn_pin=b.spawn_pin=1;
    unsigned rng=98765; int frames=0;
    std::vector<short> pixels(256*240);
    float oa[OBS_SIZE],ob[OBS_SIZE];
    for(int level=0;level<32;level++) {
        a.cur_spawn=b.cur_spawn=level; puf_reset(&a); puf_reset(&b);
        for(int f=0;f<1024;f++) {
            unsigned char mask=retro_action_mask(retro_random(&rng)%64);
            if(f<512) mask=RETRO_BTN_RIGHT|RETRO_BTN_B|((f%48<28)?RETRO_BTN_A:0);
            retro_frame(&a,mask,true); retro_sync_from_emu(&a);
            retro_compute_obs_real(&a,oa);
            const auto& fr=a.emu->frame();
            for(int y=0;y<240;y++) for(int x=0;x<256;x++)
                pixels[y*256+x]=fr.palette[fr.pixels[y*fr.pitch+x]];
            retro_frame(&b,mask,true); retro_sync_from_emu(&b);
            retro_compute_obs_real(&b,ob);
            if(saved(*a.emu)!=saved(*b.emu)||memcmp(oa,ob,sizeof(oa))) {
                fprintf(stderr,"full-screen divergence level=%d-%d frame=%d\n",level/4+1,level%4+1,f);
                throw std::runtime_error("full-screen machine state or observation mismatch");
            }
            const auto& fb=b.emu->frame();
            for(int y=0;y<240;y++) for(int x=0;x<256;x++)
                require(pixels[y*256+x]==fb.palette[fb.pixels[y*fb.pitch+x]],
                    "full-screen rendering changed a visible pixel");
            frames++;
        }
    }
    printf("PASS: %d full-screen/reference frames, exact state/observations/pixels\n",frames);
    puf_close(&a); puf_close(&b); dict_clear(&cfg);
}

int main() {
    try { differential(); instruction_differential(); polling_differential(); full_screen_differential(); wrapper_differential(); return 0; }
    catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
