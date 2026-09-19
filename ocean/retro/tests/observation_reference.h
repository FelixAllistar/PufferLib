#pragma once

// Pre-optimization observation builder: retain the complete float staging
// image as an independent regression oracle for both float32 and bf16 hosts.
// Tests only; production never materializes this intermediate image.
static void retro_observation_reference(const Env* e,obs_t* obs) {
    float values[OBS_SIZE];
    RetroScalars sc={e->x_pos,e->x_pos_max,e->coins,e->score,e->tick,e->world,e->stage,e->area,e->time,e->has_flag,e->is_dead,0,0};
    retro_ego_ent(values,e->emu->low_mem(),&sc);
    const auto& fr=e->emu->frame();
    const unsigned char* pixels=e->reset_image?e->start->pixels:fr.pixels;
    const short* palette=e->reset_image?e->start->palette:fr.palette;
    int pitch=e->reset_image?256:(int)fr.pitch;
    struct PaletteCache { short palette[256]; float lut[256]; bool valid; };
    static thread_local PaletteCache cache={};
    if(!cache.valid||memcmp(cache.palette,palette,sizeof(cache.palette))) {
        memcpy(cache.palette,palette,sizeof(cache.palette));
        for(int i=0;i<256;i++) {
            const auto& c=Nes_Emu::nes_colors[palette[i]&(Nes_Emu::color_table_size-1)];
            cache.lut[i]=retro_luma(c.red,c.green,c.blue);
        }
        cache.valid=true;
    }
    const float* lut=cache.lut;
    int idx=RETRO_EGO_SIZE+RETRO_ENT_SIZE;
    for(int y=0;y<240;y+=RETRO_OBS_SCALE) for(int x=0;x<256;x+=RETRO_OBS_SCALE) {
        float sum=0;
        if(pixels) {
            const unsigned char* row=pixels+y*pitch+x;
            for(int dy=0;dy<RETRO_OBS_SCALE;dy++) for(int dx=0;dx<RETRO_OBS_SCALE;dx++)
                sum+=lut[row[dy*pitch+dx]];
        }
        values[idx++]=sum/(RETRO_OBS_SCALE*RETRO_OBS_SCALE);
    }
    for(int i=0;i<OBS_SIZE;i++) {
#if defined(from_float) && !defined(PRECISION_FLOAT)
        obs[i]=from_float(values[i]);
#else
        obs[i]=values[i];
#endif
    }
}

static bool retro_observation_matches_reference(const Env* e,const obs_t* obs) {
    obs_t expected[OBS_SIZE];
    retro_observation_reference(e,expected);
    return !memcmp(obs,expected,sizeof(expected));
}

// Exercise every palette index and changes to the emphasis/color table,
// independent of which colors happen to appear in the chosen ROM trajectory.
static bool retro_observation_synthetic_parity(const Env* source) {
    RetroStart image={}; Env env=*source; env.start=&image; env.reset_image=true;
    obs_t actual[OBS_SIZE];
    for(int pass=0;pass<16;pass++) {
        for(int i=0;i<256;i++) image.palette[i]=(i*73+pass*53)%Nes_Emu::color_table_size;
        for(int i=0;i<256*240;i++) image.pixels[i]=(i*17+(i/256)*31+pass*53)&255;
        retro_compute_obs_real(&env,actual);
        if(!retro_observation_matches_reference(&env,actual)) return false;
    }
    return true;
}
