#pragma once
// Presentation only: never consume simulation RNG or add state to observations.
static const char* AR_LIGHT_NAMES[]={"Unlit","Daylight","Dusk","Moonlight"};

static inline void ar_lamp(ARClient* c,float x,float y,float z,float radius,Color color) {
    Vector2 p=ar_iso(c,x,y,z);float scale=.5f;
    p.x*=scale;p.y*=scale;radius*=c->zoom*scale;
    if(p.x+radius<0 || p.y+radius<0 || p.x-radius>c->lightmap.texture.width || p.y-radius>c->lightmap.texture.height)return;
    DrawCircleGradient((int)p.x,(int)p.y,radius,color,(Color){0,0,0,0});
}
static inline void ar_lighting_prepare(ARClient* c,ARPG* e) {
    if(!c->light_mode || c->light_failed)return;
    int w=(GetScreenWidth()+1)/2,h=(GetScreenHeight()+1)/2;
    if(c->lightmap.id && (c->lightmap.texture.width!=w || c->lightmap.texture.height!=h)) {
        UnloadRenderTexture(c->lightmap);memset(&c->lightmap,0,sizeof(c->lightmap));
        c->light_signature=0;
    }
    if(!c->lightmap.id) {
        c->lightmap=LoadRenderTexture(w,h);
        if(!IsRenderTextureValid(c->lightmap)){c->light_failed=1;return;}
        SetTextureFilter(c->lightmap.texture,TEXTURE_FILTER_BILINEAR);
    }
    uint64_t signature=UINT64_C(14695981039346656037);
    const float camera[]={c->off_x,c->off_y,c->zoom,(float)c->light_mode,e->home_x,e->home_y,e->px,e->py,(float)e->keeper_dormant,e->fx_blast,e->blast_x,e->blast_y};
    signature=ar_world_hash(signature,camera,sizeof(camera));
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p]) {
        float lamp[]={e->pets.x[p],e->pets.y[p],(float)e->pets.kind[p]};signature=ar_world_hash(signature,lamp,sizeof(lamp));
    }
    for(int b=0;b<AR_MAX_BUILDINGS;b++)if(e->build_active[b]) {
        float lamp[]={e->build_x[b],e->build_y[b],(float)e->build_kind[b]};signature=ar_world_hash(signature,lamp,sizeof(lamp));
    }
    for(int n=0;n<AR_MAX_SHARDS;n++)if(e->shard_active[n] && e->shard_value[n]>0) {
        float lamp[]={e->shard_x[n],e->shard_y[n]};signature=ar_world_hash(signature,lamp,sizeof(lamp));
    }
    if(signature==c->light_signature)return;
    c->light_signature=signature;
    const Color ambient[]={{255,255,255,255},{238,240,224,255},{112,133,164,255},{70,94,139,255}};
    BeginTextureMode(c->lightmap);ClearBackground(ambient[c->light_mode]);
    BeginBlendMode(BLEND_ADDITIVE);
    float strength=c->light_mode==1 ? .18f : 1;
    Color warm=Fade((Color){255,179,92,255},strength),cool=Fade((Color){93,199,209,255},strength*.55f);
    ar_lamp(c,e->home_x-2,e->home_y-2,1,255,warm);
    if(!e->keeper_dormant)ar_lamp(c,e->px,e->py,.9f,155,warm);
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p]) {
        int kind=e->pets.kind[p];
        ar_lamp(c,e->pets.x[p],e->pets.y[p],.5f,kind==AR_PET_EMBER ? 135 : 65,kind==AR_PET_EMBER || kind==AR_PET_BURROWER ? warm : cool);
    }
    for(int b=0;b<AR_MAX_BUILDINGS;b++)if(e->build_active[b] && e->build_kind[b]!=AR_BUILD_WALL && e->build_kind[b]!=AR_BUILD_BRIDGE)
        ar_lamp(c,e->build_x[b],e->build_y[b],1,125,e->build_kind[b]==AR_BUILD_HARVESTER ? warm : cool);
    for(int n=0;n<AR_MAX_SHARDS;n++)if(e->shard_active[n] && e->shard_value[n]>0)
        ar_lamp(c,e->shard_x[n],e->shard_y[n],.3f,47,Fade(cool,.6f));
    if(e->fx_blast>0)ar_lamp(c,e->blast_x,e->blast_y,1,400,Fade(warm,fminf(1,e->fx_blast)));
    EndBlendMode();EndTextureMode();
}
static inline void ar_lighting_draw(ARClient* c) {
    if(!c->light_mode || c->light_failed || !c->lightmap.id)return;
    BeginBlendMode(BLEND_MULTIPLIED);
    DrawTexturePro(c->lightmap.texture,(Rectangle){0,0,(float)c->lightmap.texture.width,-(float)c->lightmap.texture.height},
        (Rectangle){0,0,(float)GetScreenWidth(),(float)GetScreenHeight()},(Vector2){0,0},0,WHITE);
    EndBlendMode();
}

// A sheared alpha silhouette, projected consistently away from the sun. Kept
// below every standing object; mirroring the actor never mirrors the sunlight.
static inline int ar_enemy_skin(ARClient* c,ARPG* e,int i);
static inline void ar_cast_shadow(ARClient* c,ARPG* e,ARDrawable d,Vector2 feet) {
    if(!c->light_mode || d.kind==7 || d.kind==9)return;
    Texture2D tex=d.sprite>=16 ? c->expansion : c->atlas;
    Rectangle src=c->sprites[d.sprite>=0 && d.sprite<20 ? d.sprite : 0];
    ARSpriteSheet* sheet=NULL;int frame=0;float flip=1;
    if(d.kind==8){sheet=&c->biomes;frame=d.sprite;}
    if(d.kind==10){sheet=&c->fauna;frame=d.sprite*8+1+((int)(c->nature_time*.52f+d.slot)%4);flip=cosf(c->nature_time*.7f+d.slot*.1f)+sinf(c->nature_time*.7f+d.slot*.1f)<0 ? -1 : 1;}
    if(d.kind==5) {
        int cls=e->pets.kind[d.slot];sheet=cls<4 ? &c->companions : &c->keepers;
        frame=(cls<4 ? cls : cls-3)*8+ar_animation_frame(c->pet_gait[d.slot],hypotf(e->pets.vx[d.slot],e->pets.vy[d.slot]),-1);
        flip=c->pet_facing[d.slot]<0 ? -1 : 1;
    }
    if(d.kind==6){sheet=&c->keepers;frame=ar_animation_frame(c->keeper_gait,hypotf(e->pvx,e->pvy),-1);flip=c->keeper_facing<0 ? -1 : 1;}
    if(d.kind==4) {
        int skin=ar_enemy_skin(c,e,d.slot);sheet=skin>=0 && c->fauna.texture.id ? &c->fauna : &c->keepers;
        frame=(sheet==&c->fauna ? skin : 3)*8+ar_animation_frame(c->enemy_gait[d.slot],hypotf(e->enemies.vx[d.slot],e->enemies.vy[d.slot]),-1);
        flip=c->enemy_facing[d.slot]<0 ? -1 : 1;
    }
    if(sheet && sheet->texture.id){tex=sheet->texture;src=sheet->frame[frame];}
    else sheet=NULL;
    if(!tex.id || src.height<=0)return;
    float scale=d.size*c->zoom/(sheet && sheet->body_height[frame/sheet->columns]>0 ? sheet->body_height[frame/sheet->columns] : src.height);
    Vector2 pivot=sheet ? sheet->pivot[frame] : (Vector2){src.width*.5f,src.height};
    if(flip<0)pivot.x=src.width-pivot.x;
    float left=-pivot.x*scale,right=(src.width-pivot.x)*scale,top=-pivot.y*scale,bottom=(src.height-pivot.y)*scale;
    float stretch=c->light_mode==2 ? .8f : .43f;
    Color tint={13,24,36,(unsigned char)(c->light_mode==3 ? 30 : 57)};
    float u=src.x/tex.width,v=src.y/tex.height,uw=src.width/tex.width,vh=src.height/tex.height;
    if(flip<0){u+=uw;uw=-uw;}
    rlSetTexture(tex.id);rlBegin(RL_QUADS);rlColor4ub(tint.r,tint.g,tint.b,tint.a);
    // Vertical projection reverses winding: reverse the vertices too, or GL
    // silently culls the complete shadow on drivers with backface culling.
    rlTexCoord2f(u,v);rlVertex2f(feet.x+left-top*stretch,feet.y-top*.23f);
    rlTexCoord2f(u+uw,v);rlVertex2f(feet.x+right-top*stretch,feet.y-top*.23f);
    rlTexCoord2f(u+uw,v+vh);rlVertex2f(feet.x+right-bottom*stretch,feet.y-bottom*.23f);
    rlTexCoord2f(u,v+vh);rlVertex2f(feet.x+left-bottom*stretch,feet.y-bottom*.23f);
    rlEnd();rlSetTexture(0);
}

static inline int ar_enemy_skin(ARClient* c,ARPG* e,int i) {
    // Variants preserve the existing light/heavy combat roles and model ABI.
    if(e->enemies.type[i])return 3;
    // Use the spawn home, not the moving position: chasing across a biome
    // boundary must not transform the same living enemy into another animal.
    int biome=ar_biome(e->dungeon_seed,c->origin_x+e->enemies.home_x[i]/ar_world_cell(e),c->origin_y+e->enemies.home_y[i]/ar_world_cell(e));
    return biome==AR_BIOME_MARSH || biome==AR_BIOME_AUTUMN ? 2 : -1;
}

static inline void ar_air(ARClient* c,ARPG* e) {
    // World-anchored motes and windblown seeds; simulation pause freezes them.
    int ox=(int)floorf((c->cam_x/ar_world_cell(e)+c->origin_x)/8),oy=(int)floorf((c->cam_y/ar_world_cell(e)+c->origin_y)/8);
    Vector2 motes[169];float pulses[169];Color colors[169];int count=0;
    for(int y=oy-6;y<=oy+6;y++)for(int x=ox-6;x<=ox+6;x++) {
        uint32_t h=ar_hash_xy(e->dungeon_seed^0xa19u,x,y);if(h%3)continue;
        float phase=(h%1000)*.01f,t=c->nature_time;
        float wx=(x*8+(h%71)*.1f-c->origin_x)*ar_world_cell(e),wy=(y*8+((h/71)%71)*.1f-c->origin_y)*ar_world_cell(e);
        int biome=ar_biome(e->dungeon_seed,x*8,y*8);
        if(biome==AR_BIOME_DUNES || biome==AR_BIOME_HIGHLAND)continue;
        Vector2 p=ar_iso(c,wx+sinf(t*.33f+phase),wy+cosf(t*.27f+phase),.4f+.2f*sinf(t+phase));
        float pulse=.35f+.65f*(.5f+.5f*sinf(t*1.7f+phase));
        if(p.x<0 || p.x>GetScreenWidth() || p.y<76 || p.y>GetScreenHeight()-150)continue;
        motes[count]=p;pulses[count]=pulse;colors[count++]=biome==AR_BIOME_AUTUMN ? AR_GOLD : AR_TEXT;
    }
    if(c->light_mode>=2) {
        // Keep triangles and quads in separate batches, not two flushes/mote.
        for(int i=0;i<count;i++)DrawCircleGradient((int)motes[i].x,(int)motes[i].y,7*c->zoom,Fade(AR_GOLD,.2f*pulses[i]),Fade(AR_GOLD,0));
        for(int i=0;i<count;i++)DrawCircleV(motes[i],1.4f*c->zoom,Fade((Color){246,233,156,255},pulses[i]));
    } else for(int i=0;i<count;i++)DrawLineEx(motes[i],(Vector2){motes[i].x+3*c->zoom,motes[i].y-1},c->zoom,Fade(colors[i],.35f*pulses[i]));
}
