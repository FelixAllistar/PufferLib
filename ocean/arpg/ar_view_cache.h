#pragma once
// A bounded world-coordinate ring: camera movement only exposes new strips.
static inline ARGroundSample* ar_ground_cached(ARClient* c,int gx,int gy) {
    unsigned mask=(unsigned)c->ground_side-1;
    return c->ground_samples+(((unsigned)gy&mask)*c->ground_side+((unsigned)gx&mask));
}
static inline void ar_ground_invalidate_vertex(ARClient* c,int gx,int gy) {
    ARGroundSample* s=ar_ground_cached(c,gx,gy);
    if(s->x==gx && s->y==gy)s->vertex_valid=0;
}
static inline int ar_ground_sync_tile(ARClient* c,int gx,int gy,uint8_t tile,int changed) {
    ARGroundSample* s=ar_ground_cached(c,gx,gy);
    if(s->tile_valid && s->x==gx && s->y==gy && s->tile!=tile){s->tile=tile;changed=1;}
    if(changed)for(int dy=0;dy<=1;dy++)for(int dx=0;dx<=1;dx++)ar_ground_invalidate_vertex(c,gx+dx,gy+dy);
    return changed;
}
static inline int ar_ground_prepare(ARClient* c,ARPG* e,int minx,int miny,int maxx,int maxy) {
    double start=GetTime();c->ground_generated=c->ground_colored=0;
    int vw=maxx-minx+1,tw=vw+1,side=1;
    while(side<tw || side<maxy-miny+3)side*=2;
    if(side>c->ground_side) {
        ARGroundSample* samples=(ARGroundSample*)calloc((size_t)side*side,sizeof(*samples));
        if(!samples)return 0;
        free(c->ground_samples);c->ground_samples=samples;c->ground_side=side;c->ground_ready=0;
    }
    int ox=c->origin_x-AR_DUN_W/2,oy=c->origin_y-AR_DUN_H/2;
    int reset=!c->ground_ready || c->ground_key[6]!=(int)e->dungeon_seed ||
        c->ground_key[7]!=e->terrain_version || c->ground_campaign!=(e->campaign!=NULL) || e->tick<c->ground_tick;
    if(reset) {
        memset(c->ground_samples,0,(size_t)c->ground_side*c->ground_side*sizeof(*c->ground_samples));
        c->ground_edit_revision++;
    }
    uint32_t revision=2166136261u;int edited=0;
    int shifted=!reset && (c->origin_x!=c->ground_key[4] || c->origin_y!=c->ground_key[5]);
    if(shifted) {
        // A tick can edit terrain and then stream it out before rendering.
        // Those old live tiles are now in persistent chunks, not e->dungeon.
        int oldx=c->ground_key[4]-AR_DUN_W/2,oldy=c->ground_key[5]-AR_DUN_H/2;
        for(int y=0;y<AR_DUN_H;y++)for(int x=0;x<AR_DUN_W;x++) {
            int gx=oldx+x,gy=oldy+y;uint8_t tile=ar_world_sample(e,gx-ox,gy-oy);
            edited|=ar_ground_sync_tile(c,gx,gy,tile,tile!=c->ground_snapshot[y*AR_DUN_W+x]);
        }
    }
    for(int y=0;y<AR_DUN_H;y++)for(int x=0;x<AR_DUN_W;x++) {
        int i=y*AR_DUN_W+x;uint8_t tile=e->dungeon[i];revision=(revision^tile)*16777619u;
        edited|=ar_ground_sync_tile(c,ox+x,oy+y,tile,!reset && !shifted && tile!=c->ground_snapshot[i]);
    }
    if(edited)c->ground_edit_revision++;
    memcpy(c->ground_snapshot,e->dungeon,sizeof(c->ground_snapshot));
    for(int y=miny-1;y<=maxy;y++)for(int x=minx-1;x<=maxx;x++) {
        int gx=ox+x,gy=oy+y;ARGroundSample* s=ar_ground_cached(c,gx,gy);
        if(!s->tile_valid || s->x!=gx || s->y!=gy) {
            s->x=gx;s->y=gy;s->tile=ar_world_sample(e,x,y);s->biome=(uint8_t)ar_biome(e->dungeon_seed,gx,gy);
            s->tile_valid=1;s->vertex_valid=0;c->ground_generated++;
        }
        c->ground_tiles[(y-miny+1)*tw+x-minx+1]=s->tile;
    }
    for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++) {
        ARGroundSample* s=ar_ground_cached(c,ox+x,oy+y);
        if(!s->vertex_valid) {
            s->vertex=ar_ground_corner(c,e,x,y,&c->ground_tiles[(y-miny+1)*tw+x-minx+1],tw);
            s->vertex_valid=1;c->ground_colored++;
        }
        c->ground_vertices[(y-miny)*vw+x-minx]=s->vertex;
    }
    const int key[]={minx,miny,maxx,maxy,c->origin_x,c->origin_y,(int)e->dungeon_seed,e->terrain_version};
    memcpy(c->ground_key,key,sizeof(key));c->ground_revision=revision;
    c->ground_campaign=e->campaign!=NULL;c->ground_ready=1;c->ground_tick=e->tick;
    c->ground_prepare_ms=(GetTime()-start)*1000;return 1;
}

static inline int ar_map_index(ARClient* c,int x,int y) {
    return (int)(((unsigned)y&(unsigned)(c->map_height-1))*c->map_width+((unsigned)x&(unsigned)(c->map_width-1)));
}
static inline int ar_map_prepare(ARClient* c,ARPG* e,int pw,int ph) {
    double start=GetTime();ARWorld* w=(ARWorld*)e->campaign;
    c->map_generated=c->map_pending=0;
    int mw=1,mh=1;while(mw<pw+64)mw*=2;while(mh<ph+64)mh*=2;
    int resized=mw!=c->map_width || mh!=c->map_height;
    if(resized) {
        Color* pixels=(Color*)calloc((size_t)mw*mh,sizeof(*pixels));
        ARMapSample* samples=(ARMapSample*)calloc((size_t)mw*mh,sizeof(*samples));
        if(!pixels || !samples){free(pixels);free(samples);return 0;}
        free(c->map_pixels);free(c->map_samples);c->map_pixels=pixels;c->map_samples=samples;
        c->map_width=mw;c->map_height=mh;
        if(c->map_texture.id)UnloadTexture(c->map_texture);c->map_texture=(Texture2D){0};
    }
    int reset=resized || c->map_cache_span!=c->map_span || c->map_cache_seed!=(int)w->seed ||
        c->map_cache_version!=w->terrain_version || c->map_chunks!=w->chunk_count ||
        c->map_edit_revision!=c->ground_edit_revision;
    int changed=reset;
    if(reset) {
        memset(c->map_samples,0,(size_t)mw*mh*sizeof(*c->map_samples));
        for(int i=0;i<mw*mh;i++)c->map_pixels[i]=AR_INK;
        c->map_cursor=0;
    }
    double step=c->map_span/pw;
    int x0=(int)floor(c->map_x/step-pw*.5)-1,y0=(int)floor(c->map_y/step-ph*.5)-1;
    int sw=pw+3,sh=ph+3,total=sw*sh;
    // Shift immediately using the retained image. Only pixels newly exposed by
    // panning become pending; their procedural work has a two-ms frame budget.
    for(int y=y0;y<y0+sh;y++)for(int x=x0;x<x0+sw;x++) {
        int i=ar_map_index(c,x,y);ARMapSample* s=c->map_samples+i;
        if(s->x!=x || s->y!=y) {
            s->x=x;s->y=y;s->valid=0;c->map_pixels[i]=AR_INK;changed=1;
        }
        c->map_pending+=!s->valid;
    }
    // Round robin prevents continuous dragging from starving the far rows.
    int scanned=0;
    while(scanned<total && c->map_pending) {
        int cursor=c->map_cursor++%total;scanned++;
        int x=x0+cursor%sw,y=y0+cursor/sw,i=ar_map_index(c,x,y);
        ARMapSample* s=c->map_samples+i;
        if(s->valid)continue;
        double gx=(x+.5)*step,gy=(y+.5)*step;
        int tile=ar_world_global_tile(e,w,(int)floor(gx),(int)floor(gy));
        Color color=ar_land_color(w->seed,(float)gx,(float)gy,tile);
        int known=ar_world_chunk_id(w,(int)floor(gx/AR_CHUNK_SIZE),(int)floor(gy/AR_CHUNK_SIZE))>=0;
        c->map_pixels[i]=known ? color : ar_mix(color,AR_INK,.57f);
        s->valid=1;c->map_pending--;c->map_generated++;changed=1;
        if((c->map_generated&31)==0 && GetTime()-start>=.002)break;
    }
    c->map_cursor%=total;
    if(!c->map_texture.id) {
        Image image={c->map_pixels,mw,mh,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        c->map_texture=LoadTextureFromImage(image);
        SetTextureFilter(c->map_texture,TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(c->map_texture,TEXTURE_WRAP_REPEAT);
    } else if(changed)UpdateTexture(c->map_texture,c->map_pixels);
    c->map_cache_span=c->map_span;
    c->map_cache_seed=(int)w->seed;c->map_cache_version=w->terrain_version;
    c->map_chunks=w->chunk_count;c->map_edit_revision=c->ground_edit_revision;
    c->map_prepare_ms=(GetTime()-start)*1000;return 1;
}
static inline Rectangle ar_map_source(ARClient* c,int pw,int ph) {
    double step=c->map_span/pw,x=c->map_x/step-pw*.5,y=c->map_y/step-ph*.5;
    return (Rectangle){(float)(((unsigned)(int)floor(x)&(unsigned)(c->map_width-1))+x-floor(x)),
        (float)(((unsigned)(int)floor(y)&(unsigned)(c->map_height-1))+y-floor(y)),(float)pw,(float)ph};
}
