#pragma once
// Client-only presentation. World rendering is full resolution; UI never scales
// with the camera. All gameplay, production, and pet tasks live in ar_sim.h.
#include "raylib.h"
#include "rlgl.h"
#include "ar_sprite_sheet.h"
#include <stdio.h>
#include <stdlib.h>

#define AR_VIEW_SCALE 24.0f
#define AR_FX_COUNT 64
static const Color AR_INK = {20,30,30,255};
static const Color AR_PANEL = {24,37,36,248};
static const Color AR_LINE = {60,79,70,255};
static const Color AR_TEXT = {232,234,213,255};
static const Color AR_MUTED = {144,166,149,255};
static const Color AR_MINT = {123,219,185,255};
static const Color AR_GOLD = {228,185,108,255};
static const Color AR_RED = {224,118,98,255};
static const char* AR_PET_NAMES[] = {"Wisp","Fang","Aegis","Porter","Burrower","Ember"};
static const char* AR_TASK_NAMES[] = {"Assist","Gather","Escort","Hunt","Hold","Home","Terrain"};
static const char* AR_BUILD_NAMES[] = {"Ward tower","Barricade","Extractor","Starfire","Bridge"};
static const char* AR_COMMAND_NAMES[] = {"Auto","Move","Mine seam","Attack","Hold point","Terraform"};
static const char* AR_BIOME_NAMES[] = {"Clover meadows","Pine wilds","Amber woods","Reed marshes","Sunwashed dunes","Slate highlands"};
static inline int ar_pet_sprite(int cls){return cls<4 ? cls+1 : 16+cls-4;}
static inline int ar_build_sprite(int kind){return kind<3 ? 7+kind : 18+kind-3;}
static inline int ar_pet_unlock(ARPG* e,int p) {
    return p==AR_PET_FANG ? e->cfg.unlock_level_fang : p==AR_PET_AEGIS ? e->cfg.unlock_level_aegis :
        p==AR_PET_BURROWER ? 2 : p==AR_PET_EMBER ? 3 : 0;
}

typedef struct {
    float x,y,value,life;
    Color color;
} ARFeedback;
typedef struct {
    float depth,x,y,size;
    int sprite,slot,kind;
} ARDrawable;
typedef struct ARClient {
    float cam_x,cam_y,off_x,off_y,zoom,time;
    Texture2D atlas,expansion;
    ARSpriteSheet companions,keepers,biomes,fauna;
    RenderTexture2D lightmap;
    uint64_t light_signature;
    Texture2D minimap_texture;
    uint32_t minimap_revision;
    int light_mode,light_failed;
    float nature_time;
    Rectangle sprites[20];
    Font font,heading;
    int initialized,selected_pet,build_kind,paused,autoplay,pet_policy;
    int ui_summon,ui_toggle,ui_pause,ui_ability,ui_drive,ui_map;
    int task_override[AR_MAX_PETS];
    int move_target,camera_free;
    uint32_t selected_mask,groups[4];
    int dragging,targeting_nuke,hover_type,hover_id;
    Vector2 drag_start;
    int origin_x,origin_y;
    int map_open,map_ui,animation_tick;
    double map_x,map_y;
    float map_span;
    Texture2D map_texture;
    double map_cache_x,map_cache_y;
    float map_cache_span;
    int map_cache_tick,map_cache_seed,map_cache_version;
    float pet_gait[AR_MAX_PETS],pet_facing[AR_MAX_PETS],keeper_gait,keeper_facing;
    float enemy_gait[AR_MAX_ENEMIES],enemy_facing[AR_MAX_ENEMIES];
    ARDrawable* drawables;
    int drawable_capacity;
    Color* ground_vertices;
    int ground_capacity;
    uint8_t* ground_tiles;
    int tile_capacity,ground_key[8];
    uint32_t ground_revision;
    double render_world_ms,render_ui_ms,render_present_ms;
    float move_x,move_y;
    char notice[160];
    float notice_time,prev_hp,prev_harvest,rate,rate_elapsed,rate_base;
    int prev_tick,prev_builds,prev_nests,prev_camps,prev_pets,prev_level,fx_head;
    float enemy_hp[AR_MAX_ENEMIES];
    ARFeedback fx[AR_FX_COUNT];
} ARClient;

static inline ARClient* ar_client(ARPG* env) {
    if (!env->client) {
        ARClient* c=(ARClient*)calloc(1,sizeof(ARClient));
        c->cam_x=env->px; c->cam_y=env->py; c->zoom=1.0f;
        c->selected_pet=-1; c->build_kind=-1;c->map_span=256;c->light_mode=1;
        if(getenv("ARPG_LIGHTING"))c->light_mode=(int)ar_clampf((float)atoi(getenv("ARPG_LIGHTING")),0,3);
        if(env->campaign){c->origin_x=((ARWorld*)env->campaign)->origin_x;c->origin_y=((ARWorld*)env->campaign)->origin_y;}
        for(int p=0;p<AR_MAX_PETS;p++) c->task_override[p]=-1;
        env->client=c;
    }
    return (ARClient*)env->client;
}
static inline void ar_notice(ARClient* c,const char* text) {
    snprintf(c->notice,sizeof(c->notice),"%s",text); c->notice_time=4.0f;
}
static inline Vector2 ar_iso(ARClient* c,float x,float y,float z) {
    float scale=AR_VIEW_SCALE*c->zoom;
    return (Vector2){(x-y)*scale+c->off_x,(x+y)*scale*0.5f-z*scale+c->off_y};
}
static inline Vector2 ar_unproject(ARClient* c,Vector2 p) {
    float sx=(p.x-c->off_x)/(AR_VIEW_SCALE*c->zoom);
    float sy=(p.y-c->off_y)/(AR_VIEW_SCALE*c->zoom*0.5f);
    return (Vector2){(sx+sy)*0.5f,(sy-sx)*0.5f};
}
static inline void ar_camera_project(ARClient* c,int width,int height) {
    float scale=AR_VIEW_SCALE*c->zoom;
    c->off_x=width*0.5f-(c->cam_x-c->cam_y)*scale;
    c->off_y=(height-150+76)*0.5f-(c->cam_x+c->cam_y)*scale*0.5f;
}
static inline int ar_world_pointer(ARClient* c,Vector2 mouse,int width,int height) {
    return !c->map_open && mouse.y>76 && mouse.y<height-150 &&
        !CheckCollisionPointRec(mouse,(Rectangle){23,97,298,197}) &&
        !(c->selected_pet>=0 && CheckCollisionPointRec(mouse,(Rectangle){23,305,298,90})) &&
        !CheckCollisionPointRec(mouse,(Rectangle){width-181.0f,88,162,244});
}
static inline void ar_pet_badges(ARClient* c,ARPG* e,Vector2* points) {
    for(int p=0;p<AR_MAX_PETS;p++) {
        points[p]=ar_iso(c,e->pets.x[p],e->pets.y[p],0);points[p].y-=64*c->zoom;
        if(!e->pets.active[p] || e->pets.dormant[p])continue;
        // Resolve labels only, never move units for presentation. Rendering and
        // picking share this exact layout so overlap cannot hide a pick target.
        for(int pass=0;pass<AR_MAX_PETS;pass++) {
            int overlap=0;
            for(int q=0;q<p;q++)if(e->pets.active[q] && !e->pets.dormant[q] &&
                    fabsf(points[p].x-points[q].x)<23 && fabsf(points[p].y-points[q].y)<23){overlap=1;break;}
            if(!overlap)break;points[p].x+=(p&1) ? 24 : -24;
        }
    }
}
static inline void ar_text(ARClient* c,const char* text,float x,float y,int size,Color color) {
    DrawTextEx(c->font,text,(Vector2){x,y},(float)size,0.3f,color);
}
static inline void ar_box(Rectangle r,Color fill) {
    DrawRectangleRounded(r,0.16f,5,fill);
    DrawRectangleRoundedLinesEx(r,0.16f,5,1,AR_LINE);
}
static inline int ar_button(ARClient* c,Rectangle r,const char* label,int active,int enabled) {
    enabled=enabled && (!c->map_open || c->map_ui);
    int hover=enabled && CheckCollisionPointRec(GetMousePosition(),r);
    ar_box(r,active ? (Color){47,76,63,255} : hover ? (Color){42,57,49,255} : AR_PANEL);
    Vector2 t=MeasureTextEx(c->font,label,16,0.3f);
    ar_text(c,label,r.x+(r.width-t.x)*0.5f,r.y+(r.height-t.y)*0.5f,16,
        !enabled ? (Color){87,108,96,255} : active ? AR_MINT : AR_TEXT);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static inline void ar_bar(float x,float y,float w,float h,float ratio,Color color) {
    DrawRectangleRounded((Rectangle){x,y,w,h},0.5f,4,(Color){13,24,25,220});
    DrawRectangleRounded((Rectangle){x,y,w*ar_clampf(ratio,0,1),h},0.5f,4,color);
}
static inline void ar_feedback(ARClient* c,float x,float y,float value,Color color) {
    c->fx[c->fx_head++ % AR_FX_COUNT]=(ARFeedback){x,y,value,1.1f,color};
}
static inline Texture2D ar_load_atlas(ARClient* c,const char* path,int base,int cols) {
    Texture2D texture={0};
    Image atlas=LoadImage(path);
    if (atlas.data) {
        texture=LoadTextureFromImage(atlas);
        SetTextureFilter(texture,TEXTURE_FILTER_BILINEAR);
        Color* pixels=LoadImageColors(atlas);
        // Alpha bounds stay inside each atlas cell; the source image is untouched.
        for(int n=0;n<cols*cols;n++) {
            int x0=(n%cols)*atlas.width/cols,x1=(n%cols+1)*atlas.width/cols;
            int y0=(n/cols)*atlas.height/cols,y1=(n/cols+1)*atlas.height/cols;
            int lx=x1,rx=x0,ty=y1,by=y0;
            for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) if(pixels[y*atlas.width+x].a>40) {
                if(x<lx)lx=x; if(x>rx)rx=x; if(y<ty)ty=y; if(y>by)by=y;
            }
            c->sprites[base+n]=(Rectangle){(float)lx,(float)ty,(float)(rx-lx+1),(float)(by-ty+1)};
        }
        UnloadImageColors(pixels); UnloadImage(atlas);
    }
    return texture;
}
static inline void ar_assets(ARClient* c) {
    SetShapesTexture((Texture2D){rlGetTextureIdDefault(),1,1,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8},(Rectangle){0,0,1,1});
    c->atlas=ar_load_atlas(c,"ocean/arpg/assets/hearthwild-atlas.png",0,4);
    c->expansion=ar_load_atlas(c,"ocean/arpg/assets/hearthwild-frontier-atlas.png",16,2);
    c->companions=ar_sheet_load("ocean/arpg/assets/reach-companions-v4.png",8,4,1);
    c->keepers=ar_sheet_load("ocean/arpg/assets/reach-keepers-v4.png",8,4,1);
    c->biomes=ar_sheet_load("ocean/arpg/assets/reach-biomes-v4.png",4,4,0);
    c->fauna=ar_sheet_load("ocean/arpg/assets/reach-fauna-v5.png",8,4,1);
    c->font=LoadFontEx("resources/shared/Roboto-Regular.ttf",32,NULL,0);
    c->heading=LoadFontEx("resources/shared/Montserrat-Regular.ttf",40,NULL,0);
    SetTextureFilter(c->font.texture,TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(c->heading.texture,TEXTURE_FILTER_BILINEAR);
    c->initialized=1;
    ar_notice(c,"1-8 select a companion. F6 drives it. 0 returns to keeper. M opens the Reach.");
}
static inline void ar_sprite(ARClient* c,int sprite,Vector2 feet,float height,float flip,Color tint) {
    Texture2D texture=sprite>=16 ? c->expansion : c->atlas;
    if(!texture.id) { DrawCircleV(feet,height*0.2f,tint); return; }
    Rectangle src=c->sprites[sprite];
    float width=height*src.width/src.height;
    if(flip<0)src.width=-src.width;
    DrawTexturePro(texture,src,(Rectangle){feet.x-width*0.5f,feet.y-height,width,height},
        (Vector2){0,0},0,tint);
}
// actor 0 = keeper, 1..6 = companions, 7 = thornling.
static inline void ar_actor(ARClient* c,int actor,int frame,Vector2 feet,float height,float flip,Color tint) {
    ARSpriteSheet* sheet=actor>=1 && actor<=4 ? &c->companions : &c->keepers;
    int row=actor>=1 && actor<=4 ? actor-1 : actor==0 ? 0 : actor==7 ? 3 : actor-4;
    if(sheet->texture.id)ar_sheet_draw(sheet,row*8+frame,feet,height,flip,tint);
    else ar_sprite(c,actor==0 ? 0 : actor==7 ? 5 : ar_pet_sprite(actor-1),feet,height,flip,tint);
}
static inline void ar_ring(ARClient* c,float x,float y,float r,Color color) {
    Vector2 p=ar_iso(c,x,y,0);
    if(p.x<-2000 || p.x>GetScreenWidth()+2000 || p.y<-2000 || p.y>GetScreenHeight()+2000)return;
    float radius=r*AR_VIEW_SCALE*c->zoom*1.41421356f;
    DrawEllipseLines((int)p.x,(int)p.y,radius,radius*0.5f,color);
}
static inline void ar_tile(ARClient* c,float x,float y,float half,Color color) {
    Vector2 a=ar_iso(c,x-half,y-half,0),b=ar_iso(c,x+half,y-half,0);
    Vector2 d=ar_iso(c,x-half,y+half,0),e=ar_iso(c,x+half,y+half,0);
    DrawTriangle(a,e,b,color); DrawTriangle(a,d,e,color);
}
static inline Color ar_ground(int tile,int noise) {
    const Color palette[]={{66,78,65,255},{94,119,77,255},{64,91,64,255},
        {146,139,101,255},{62,112,115,255},{41,75,88,255},{119,91,56,255}};
    Color c=palette[tile]; int d=noise%7-3;
    c.r=(unsigned char)(c.r+d);c.g=(unsigned char)(c.g+d);c.b=(unsigned char)(c.b+d);
    return c;
}
static inline Color ar_mix(Color a,Color b,float t) {
    return (Color){(unsigned char)(a.r+(b.r-a.r)*t),(unsigned char)(a.g+(b.g-a.g)*t),(unsigned char)(a.b+(b.b-a.b)*t),255};
}
static inline Color ar_meadow_color(uint32_t seed,float x,float y) {
    const Color greens[]={{108,130,86,255},{89,112,94,255},{140,128,79,255},
        {90,122,107,255},{169,151,109,255},{123,135,120,255}};
    int gx=(int)floorf(x/16),gy=(int)floorf(y/16);
    float u=x/16-gx,v=y/16-gy;u=u*u*(3-2*u);v=v*v*(3-2*v);
    Color a=ar_mix(greens[ar_biome(seed,gx*16,gy*16)],greens[ar_biome(seed,(gx+1)*16,gy*16)],u);
    Color b=ar_mix(greens[ar_biome(seed,gx*16,(gy+1)*16)],greens[ar_biome(seed,(gx+1)*16,(gy+1)*16)],u);
    return ar_mix(a,b,v);
}
static inline Color ar_surface_color(Color base,int tile) {
    if(tile==AR_TILE_FOREST)base=ar_mix(base,(Color){54,78,61,255},0.20f);
    if(tile==AR_TILE_ROCK)base=(Color){110,121,111,255};
    if(tile==AR_TILE_SAND)base=ar_mix(base,(Color){188,175,130,255},0.77f);
    if(tile==AR_TILE_SHALLOW)base=(Color){91,144,143,255};
    if(tile==AR_TILE_DEEP)base=(Color){52,99,120,255};
    if(tile==AR_TILE_BRIDGE)base=(Color){125,100,64,255};
    return base;
}
static inline Color ar_land_color(uint32_t seed,float x,float y,int tile) {
    return ar_surface_color(ar_meadow_color(seed,x,y),tile);
}
static inline Color ar_ground_corner(ARClient* c,ARPG* e,int x,int y,const uint8_t* tiles,int stride) {
    int gx=x-AR_DUN_W/2+c->origin_x,gy=y-AR_DUN_H/2+c->origin_y;
    Color grass=ar_meadow_color(e->dungeon_seed,gx,gy);
    unsigned red=0,green=0,blue=0;
    for(int dy=-1;dy<=0;dy++)for(int dx=-1;dx<=0;dx++) {
        Color a=ar_surface_color(grass,tiles[dy*stride+dx]);
        red+=a.r;green+=a.g;blue+=a.b;
    }
    Color base={(unsigned char)(red/4),(unsigned char)(green/4),(unsigned char)(blue/4),255};
    float tone=ar_fbm(e->dungeon_seed^0x8a71u,gx*.07f,gy*.07f)-.5f;
    return ar_mix(base,tone>0 ? (Color){212,206,160,255} : (Color){33,63,57,255},fabsf(tone)*.7f);
}
static inline void ar_ground_tile(ARClient* c,float x,float y,float cell,const Color* vertices,int stride) {
    const int dx[]={0,0,1,1},dy[]={0,1,1,0};
    rlSetTexture(rlGetTextureIdDefault());rlBegin(RL_QUADS);rlTexCoord2f(0,0);
    for(int i=0;i<4;i++) {
        Vector2 p=ar_iso(c,x+(dx[i]-.5f)*cell,y+(dy[i]-.5f)*cell,0);
        Color color=vertices[dy[i]*stride+dx[i]];
        rlColor4ub(color.r,color.g,color.b,255);rlVertex2f(p.x,p.y);
    }
    rlEnd();
}
static inline void ar_quad(Vector2 a,Vector2 b,Vector2 d,Vector2 e,Color color) {
    DrawTriangle(a,d,b,color);DrawTriangle(a,e,d,color);
}
static inline float ar_cliff_height(ARClient* c,ARPG* env,float x,float y,float cell) {
    return cell*(.7f+.7f*ar_vnoise(env->dungeon_seed^0x8971u,(x/cell+c->origin_x)*.18f,(y/cell+c->origin_y)*.18f));
}
static inline void ar_cliff(ARClient* c,ARPG* env,float x,float y,float cell,int east,int south) {
    float h=cell*0.5f;
    Vector2 a=ar_iso(c,x-h,y-h,ar_cliff_height(c,env,x-h,y-h,cell));
    Vector2 b=ar_iso(c,x+h,y-h,ar_cliff_height(c,env,x+h,y-h,cell));
    Vector2 d=ar_iso(c,x+h,y+h,ar_cliff_height(c,env,x+h,y+h,cell));
    Vector2 e=ar_iso(c,x-h,y+h,ar_cliff_height(c,env,x-h,y+h,cell));
    if(east)ar_quad(b,ar_iso(c,x+h,y-h,0),ar_iso(c,x+h,y+h,0),d,(Color){64,73,68,255});
    if(south)ar_quad(e,d,ar_iso(c,x+h,y+h,0),ar_iso(c,x-h,y+h,0),(Color){49,62,59,255});
    const Vector2 points[]={a,e,d,b};const int dx[]={-1,-1,1,1},dy[]={-1,1,1,-1};
    rlSetTexture(rlGetTextureIdDefault());rlBegin(RL_QUADS);rlTexCoord2f(0,0);
    for(int i=0;i<4;i++) {
        float gx=(x+dx[i]*h)/cell+c->origin_x,gy=(y+dy[i]*h)/cell+c->origin_y;
        float tone=ar_fbm(env->dungeon_seed^881u,gx*.18f,gy*.18f);
        Color color=ar_mix((Color){83,98,91,255},(Color){150,156,131,255},tone);
        rlColor4ub(color.r,color.g,color.b,255);rlVertex2f(points[i].x,points[i].y);
    }
    rlEnd();
    if(east)DrawLineEx(b,d,1,Fade(AR_TEXT,0.24f));
    if(south)DrawLineEx(e,d,1,Fade(AR_TEXT,0.15f));
}
static inline int ar_drawable_compare(const void* a,const void* b) {
    float d=((const ARDrawable*)a)->depth-((const ARDrawable*)b)->depth;
    return d<0 ? -1 : d>0;
}
#include "ar_atmosphere.h"
static inline void ar_world(ARClient* c,ARPG* e) {
    const float half=e->cfg.arena_size*0.5f,cell=e->cfg.arena_size/AR_DUN_W;
    int width=GetScreenWidth(),height=GetScreenHeight();
    int minx=0,miny=0,maxx=AR_DUN_W,maxy=AR_DUN_H;
    if(e->campaign) {
        float lx=1e6f,ly=1e6f,hx=-1e6f,hy=-1e6f;
        for(int corner=0;corner<4;corner++) {
            Vector2 q=ar_unproject(c,(Vector2){corner&1 ? width+180.0f : -180.0f,corner&2 ? height+150.0f : -80.0f});
            lx=fminf(lx,q.x);ly=fminf(ly,q.y);hx=fmaxf(hx,q.x);hy=fmaxf(hy,q.y);
        }
        minx=(int)floorf((lx+half)/cell)-1;miny=(int)floorf((ly+half)/cell)-1;
        maxx=(int)ceilf((hx+half)/cell)+1;maxy=(int)ceilf((hy+half)/cell)+1;
    }
    int needed=4*(maxx-minx)*(maxy-miny)+AR_MAX_ENEMIES+AR_MAX_PETS+AR_MAX_OBSTACLES+AR_MAX_SHARDS+AR_MAX_BUILDINGS+AR_MAX_NESTS+2;
    if(c->drawable_capacity<needed) {
        ARDrawable* next=(ARDrawable*)realloc(c->drawables,(size_t)needed*sizeof(*next));
        if(!next)return;c->drawables=next;c->drawable_capacity=needed;
    }
    ARDrawable* objects=c->drawables;
    int vertex_width=maxx-minx+1,vertex_count=vertex_width*(maxy-miny+1);
    if(c->ground_capacity<vertex_count) {
        Color* next=(Color*)realloc(c->ground_vertices,(size_t)vertex_count*sizeof(Color));
        if(!next)return;c->ground_vertices=next;c->ground_capacity=vertex_count;
    }
    int tile_width=vertex_width+1,tile_count=tile_width*(maxy-miny+2);
    if(c->tile_capacity<tile_count) {
        uint8_t* next=(uint8_t*)realloc(c->ground_tiles,(size_t)tile_count);
        if(!next)return;c->ground_tiles=next;c->tile_capacity=tile_count;
    }
    uint32_t revision=2166136261u;
    for(int i=0;i<AR_DUN_CELLS;i++){revision^=e->dungeon[i];revision*=16777619u;}
    const int key[]={minx,miny,maxx,maxy,c->origin_x,c->origin_y,(int)e->dungeon_seed,e->terrain_version};
    if(revision!=c->ground_revision || memcmp(key,c->ground_key,sizeof(key))) {
        // World-space terrain is stable between edits. Cache samples as well as
        // shared vertex colors; camera follow should not regenerate a whole
        // landscape sixty times a second (especially on software-rendered GL).
        for(int y=miny-1;y<=maxy;y++)for(int x=minx-1;x<=maxx;x++)
            c->ground_tiles[(y-miny+1)*tile_width+x-minx+1]=ar_world_sample(e,x,y);
        for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++)
            c->ground_vertices[(y-miny)*vertex_width+x-minx]=ar_ground_corner(c,e,x,y,
                &c->ground_tiles[(y-miny+1)*tile_width+x-minx+1],tile_width);
        memcpy(c->ground_key,key,sizeof(key));c->ground_revision=revision;
    }
    int count=0;
    // Ground is one continuous batch. Interleaving little grass strokes with
    // every tile changes primitive modes hundreds of times on software GL.
    for(int y=miny;y<maxy;y++)for(int x=minx;x<maxx;x++) {
        float wx=-half+(x+.5f)*cell,wy=-half+(y+.5f)*cell;
        Vector2 p=ar_iso(c,wx,wy,0);
        if(p.x < -180 || p.x > width+180 || p.y < -30 || p.y > height+150)continue;
        ar_ground_tile(c,wx,wy,cell,&c->ground_vertices[(y-miny)*vertex_width+x-minx],vertex_width);
    }
    for(int y=miny;y<maxy;y++)for(int x=minx;x<maxx;x++) {
        float wx=-half+(x+0.5f)*cell,wy=-half+(y+0.5f)*cell;
        Vector2 p=ar_iso(c,wx,wy,0);
        if(p.x < -180 || p.x > width+180 || p.y < -30 || p.y > height+150) continue;
        int tile_index=(y-miny+1)*tile_width+x-minx+1,tile=c->ground_tiles[tile_index];
        int gx=x-AR_DUN_W/2+c->origin_x,gy=y-AR_DUN_H/2+c->origin_y;
        uint32_t hash=ar_hash_xy(e->dungeon_seed,gx,gy);
        int biome=ar_biome(e->dungeon_seed,gx,gy);
        if(tile==AR_TILE_GRASS && hash%13==0) {
            DrawLineEx((Vector2){p.x-3*c->zoom,p.y},(Vector2){p.x,p.y-3*c->zoom},c->zoom,(Color){123,144,91,125});
            DrawLineEx((Vector2){p.x,p.y},(Vector2){p.x+3*c->zoom,p.y-2*c->zoom},c->zoom,(Color){73,104,65,110});
        }
        if((tile==AR_TILE_SHALLOW || tile==AR_TILE_DEEP) && hash%11==0) {
            float sway=sinf(c->time*1.2f+(float)(hash%40))*3;
            DrawLineEx((Vector2){p.x-5+sway,p.y},(Vector2){p.x+7+sway,p.y},1,(Color){141,194,179,75});
        }
        int sprite=-1;float size=0;
        // A jittered coarse grid keeps trees separated. Small understory and
        // climate-specific silhouettes give each region a different rhythm.
        int patch_x=(int)floorf(gx/4.0f),patch_y=(int)floorf(gy/4.0f);
        uint32_t grove=ar_hash_xy(e->dungeon_seed^971u,patch_x,patch_y);
        int tree_slot=gx-patch_x*4==(int)(grove%4) && gy-patch_y*4==(int)((grove/4)%4);
        if((tile==AR_TILE_FOREST || (tile==AR_TILE_GRASS && grove%7==0)) && tree_slot) {
            sprite=biome==AR_BIOME_PINE || biome==AR_BIOME_HIGHLAND ? 2 : biome==AR_BIOME_AUTUMN ? 1 : biome==AR_BIOME_DUNES ? 3 : 0;
            size=92+(grove%27);objects[count++]=(ARDrawable){wx+wy,wx,wy,size,sprite,0,8};
            sprite=-1;
        }
        // Connected cliff caps replace scattered boulder sprites as mountains.
        if(tile==AR_TILE_ROCK) {
            int east=c->ground_tiles[tile_index+1]!=AR_TILE_ROCK;
            int south=c->ground_tiles[tile_index+tile_width]!=AR_TILE_ROCK;
            objects[count++]=(ARDrawable){wx+wy,wx,wy,cell,-1,east|(south<<1),7};
            if(hash%17==0)objects[count++]=(ARDrawable){wx+wy+.05f,wx,wy,28.0f+hash%13,biome==AR_BIOME_HIGHLAND ? 5 : 4,0,9};
        }
        if(tile==AR_TILE_SHALLOW || tile==AR_TILE_DEEP) {
            if(c->ground_tiles[tile_index+1]==AR_TILE_SAND && hash%3==0)
                DrawLineEx(ar_iso(c,wx+cell*.5f,wy-cell*.5f,0),ar_iso(c,wx+cell*.5f,wy+cell*.5f,0),1.5f*c->zoom,(Color){174,205,167,90});
            if(c->ground_tiles[tile_index+tile_width]==AR_TILE_SAND && hash%3==0)
                DrawLineEx(ar_iso(c,wx-cell*.5f,wy+cell*.5f,0),ar_iso(c,wx+cell*.5f,wy+cell*.5f,0),1.5f*c->zoom,(Color){174,205,167,90});
        }
        if(tile==AR_TILE_GRASS && hash%43==0){sprite=biome==AR_BIOME_DUNES ? 9 : biome==AR_BIOME_PINE ? 10 : 11;size=14+hash%8;}
        if(tile==AR_TILE_FOREST && hash%37==0){sprite=hash%2 ? 12 : 13;size=22+hash%9;}
        if(tile==AR_TILE_SAND && hash%53==0){sprite=biome==AR_BIOME_DUNES ? 6 : 15;size=24+hash%13;}
        if(tile==AR_TILE_SHALLOW && hash%23==0){sprite=8;size=25+hash%8;}
        if(sprite>=0)objects[count++]=(ARDrawable){wx+wy,wx,wy,size,sprite,0,8};
        if(tile==AR_TILE_GRASS && hash%1201==0 && (abs(gx)>24 || abs(gy)>24))
            objects[count++]=(ARDrawable){wx+wy,wx,wy,49,7,0,8}; // Weathered waystones, purely scenic.
        if(c->fauna.texture.id && tile==AR_TILE_GRASS && hash%211==0 && biome!=AR_BIOME_DUNES && biome!=AR_BIOME_HIGHLAND) {
            // Ambient wildlife lives in stable world cells, never in Env or
            // its random stream. Short wandering loops remain on this tile.
            float phase=c->nature_time*.7f+(hash%100)*.1f;
            float ax=wx+.23f*cell*sinf(phase),ay=wy+.23f*cell*cosf(phase);
            int species=(hash/211)%2;
            objects[count++]=(ARDrawable){ax+ay,ax,ay,species ? 35.0f : 17.0f,species,(int)(hash%100),10};
        }
    }
    ar_ring(c,e->home_x,e->home_y,e->cfg.home_radius,(Color){184,210,139,85});
    objects[count++]=(ARDrawable){e->home_x+e->home_y-4,e->home_x-2,e->home_y-2,158,14,0,0};
    for(int o=0;o<AR_MAX_OBSTACLES;o++)if(e->obstacle_active[o])
        objects[count++]=(ARDrawable){e->obstacle_x[o]+e->obstacle_y[o],e->obstacle_x[o],e->obstacle_y[o],
            45+e->obstacle_radius[o]*15,12,o,0};
    for(int n=0;n<AR_MAX_SHARDS;n++)if(e->shard_active[n])
        objects[count++]=(ARDrawable){e->shard_x[n]+e->shard_y[n],e->shard_x[n],e->shard_y[n],36,13,n,1};
    for(int b=0;b<AR_MAX_BUILDINGS;b++)if(e->build_active[b])
        objects[count++]=(ARDrawable){e->build_x[b]+e->build_y[b],e->build_x[b],e->build_y[b],
            e->build_kind[b]==AR_BUILD_WALL ? 44.0f : e->build_kind[b]==AR_BUILD_ARTILLERY ? 115.0f : 70.0f,ar_build_sprite(e->build_kind[b]),b,2};
    for(int n=0;n<AR_MAX_NESTS;n++)if(e->nest_active[n]) {
        Vector2 hover=ar_unproject(c,GetMousePosition());
        if(e->show_hitboxes || ar_geometry_dist2(hover.x,hover.y,e->nest_x[n],e->nest_y[n])<9)
            ar_ring(c,e->nest_x[n],e->nest_y[n],AR_CAMP_WAKE_RADIUS,(Color){202,95,70,90});
        objects[count++]=(ARDrawable){e->nest_x[n]+e->nest_y[n],e->nest_x[n],e->nest_y[n],104,10,n,3};
    }
    for(int i=0;i<e->cfg.enemy_cap;i++)if(e->enemies.active[i])
        objects[count++]=(ARDrawable){e->enemies.x[i]+e->enemies.y[i],e->enemies.x[i],e->enemies.y[i],
            e->enemies.type[i] ? 70.0f : 43.0f,e->enemies.type[i] ? 6 : 5,i,4};
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p])
        objects[count++]=(ARDrawable){e->pets.x[p]+e->pets.y[p],e->pets.x[p],e->pets.y[p],
            e->pets.kind[p]==AR_PET_AEGIS || e->pets.kind[p]>=4 ? 53.0f : 44.0f,ar_pet_sprite(e->pets.kind[p]),p,5};
    if(!e->keeper_dormant)objects[count++]=(ARDrawable){e->px+e->py,e->px,e->py,65,0,0,6};
    qsort(objects,(size_t)count,sizeof(objects[0]),ar_drawable_compare);
    // One shadow pass, underneath the depth-sorted objects. This avoids a white
    // texture <-> atlas switch per tree, with no render-resolution reduction.
    for(int n=0;n<count;n++) {
        ARDrawable d=objects[n];if(d.kind==7 || d.kind==9)continue;
        Vector2 feet=ar_iso(c,d.x,d.y,0);
        if(feet.x < -160 || feet.x>width+160 || feet.y<60 || feet.y>height+100)continue;
        float shadow=d.size*.26f*c->zoom;
        DrawEllipse((int)feet.x,(int)feet.y,shadow,shadow*.35f,(Color){15,31,29,65});
    }
    for(int n=0;n<count;n++) {
        ARDrawable d=objects[n];Vector2 feet=ar_iso(c,d.x,d.y,0);
        if(feet.x>=-160 && feet.x<=width+160 && feet.y>=60 && feet.y<=height+100)ar_cast_shadow(c,e,d,feet);
    }
    if(e->rally_active) {
        Vector2 flag=ar_iso(c,e->rally_x,e->rally_y,0);
        ar_ring(c,e->rally_x,e->rally_y,1.0f,AR_MINT);
        DrawLineEx(flag,(Vector2){flag.x,flag.y-32*c->zoom},2,AR_TEXT);
        DrawTriangle((Vector2){flag.x,flag.y-32*c->zoom},(Vector2){flag.x,flag.y-19*c->zoom},
            (Vector2){flag.x+15*c->zoom,flag.y-28*c->zoom},AR_MINT);
    }
    for(int n=0;n<count;n++) {
        ARDrawable d=objects[n]; Vector2 feet=ar_iso(c,d.x,d.y,0);
        if(feet.x < -160 || feet.x>width+160 || feet.y<60 || feet.y>height+100)continue;
        if(d.kind==7){ar_cliff(c,e,d.x,d.y,d.size,d.slot&1,d.slot&2);continue;}
        if(d.kind==9)feet=ar_iso(c,d.x,d.y,ar_cliff_height(c,e,d.x,d.y,cell));
        float flip=1;Color tint=WHITE;int frame=0;
        if(d.kind==5) {
            int p=d.slot;
            float speed=hypotf(e->pets.vx[p],e->pets.vy[p]);
            float age=e->pets.cd[p]>0 ? e->cfg.pet_attack_cooldown-e->pets.cd[p] : -1;
            if(e->pets.command[p]==AR_CMD_WORK && e->pets.work_cd[p]>.7f)age=1-e->pets.work_cd[p];
            if(e->pets.kind[p]==AR_PET_MULE && speed<.08f)
                for(int n=0;n<AR_MAX_SHARDS;n++)if(e->shard_active[n] &&
                        ar_geometry_dist2(e->pets.x[p],e->pets.y[p],e->shard_x[n],e->shard_y[n])<2.3f) {
                    age=e->shard_cd[n]>0 ? e->cfg.gather_period-e->shard_cd[n] : -1;break;
                }
            frame=ar_animation_frame(c->pet_gait[p],speed,age);flip=c->pet_facing[p]<0 ? -1 : 1;
            if(e->pets.invuln[p]>0 && (e->tick/4)%2)tint=(Color){255,205,174,255};
            ar_ring(c,d.x,d.y,0.9f,e->direct_pet==p ? AR_MINT : c->selected_mask&(1u<<p) ? AR_GOLD : (Color){130,219,187,60});
            if(e->direct_pet==p)ar_ring(c,d.x,d.y,1.05f,AR_MINT);
        }
        if(d.kind==6) {
            frame=ar_animation_frame(c->keeper_gait,hypotf(e->pvx,e->pvy),e->fx_nova>0 ? .4f-e->fx_nova : -1);
            flip=c->keeper_facing<0 ? -1 : 1;
            ar_ring(c,d.x,d.y,1,e->direct_pet<0 ? AR_GOLD : Fade(AR_GOLD,.35f));
            if(e->invuln_timer>0 && (e->tick/4)%2)tint=(Color){255,191,172,255};
        }
        if(d.kind==8 && d.sprite<4) {
            if(!e->keeper_dormant && ar_geometry_dist2(d.x,d.y,e->px,e->py)<10)tint.a=80;
            for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p] && ar_geometry_dist2(d.x,d.y,e->pets.x[p],e->pets.y[p])<10)tint.a=80;
        }
        if(d.kind==1 && e->shard_value[d.slot]<1)tint=(Color){120,145,135,170};
        if(d.kind==5)ar_actor(c,e->pets.kind[d.slot]+1,frame,feet,d.size*c->zoom,flip,tint);
        else if(d.kind==6)ar_actor(c,0,frame,feet,d.size*c->zoom,flip,tint);
        else if(d.kind==10) {
            float phase=c->nature_time*.7f+d.slot*.1f;
            int pose=1+((int)(c->nature_time*.52f+d.slot)%4);
            ar_sheet_draw(&c->fauna,d.sprite*8+pose,feet,d.size*c->zoom,cosf(phase)+sinf(phase)<0 ? -1 : 1,WHITE);
        }
        else if(d.kind==4) {
            int i=d.slot;
            frame=ar_animation_frame(c->enemy_gait[i],hypotf(e->enemies.vx[i],e->enemies.vy[i]),-1);
            int skin=ar_enemy_skin(c,e,i);
            if(skin>=0 && c->fauna.texture.id)ar_sheet_draw(&c->fauna,skin*8+frame,feet,d.size*c->zoom,c->enemy_facing[i]<0 ? -1 : 1,tint);
            else ar_actor(c,7,frame,feet,d.size*c->zoom,c->enemy_facing[i]<0 ? -1 : 1,tint);
        } else if(d.kind==8 && d.sprite<4 && c->biomes.texture.id) {
            float wind=sinf(c->nature_time*1.3f+d.x*.7f+d.y*.4f)*1.5f*c->zoom;
            ar_sheet_sway(&c->biomes,d.sprite,feet,d.size*c->zoom,wind,tint);
        } else if((d.kind==8 || d.kind==9) && c->biomes.texture.id)ar_sheet_draw(&c->biomes,d.sprite,feet,d.size*c->zoom,1,tint);
        else ar_sprite(c,d.sprite,feet,d.size*c->zoom,flip,tint);
        if(d.kind==1 && e->shard_cd[d.slot]>0) {
            ar_bar(feet.x-15*c->zoom,feet.y+5*c->zoom,30*c->zoom,3*c->zoom,
                1-e->shard_cd[d.slot]/e->cfg.gather_period,AR_MINT);
        }
        if(d.kind==2 && e->build_kind[d.slot]==AR_BUILD_HARVESTER) {
            float pulse=0.5f+0.5f*sinf(c->time*3);
            DrawCircleV((Vector2){feet.x+12*c->zoom,feet.y-28*c->zoom},2.5f*c->zoom,Fade(AR_MINT,pulse));
        }
        if(d.kind==2 && e->build_kind[d.slot]==AR_BUILD_TOTEM && e->build_flash[d.slot]>0)
            ar_ring(c,d.x,d.y,e->cfg.totem_radius*(1-e->build_flash[d.slot]/0.4f),Fade(AR_MINT,e->build_flash[d.slot]/0.4f));
        if(e->show_hitboxes && d.kind>=4) ar_ring(c,d.x,d.y,d.kind==6 ? e->cfg.player_radius : 0.5f,AR_RED);
    }
    if(e->fx_nova>0)ar_ring(c,e->px,e->py,e->cfg.nova_radius*(1-e->fx_nova/0.4f),Fade(AR_GOLD,e->fx_nova/0.4f));
    if(e->fx_frost>0)ar_ring(c,e->px,e->py,e->cfg.frost_range*(1-e->fx_frost/0.5f),Fade(AR_MINT,e->fx_frost/0.5f));
    if(e->fx_dash>0)ar_ring(c,e->px,e->py,1+(1-e->fx_dash/0.35f)*2,Fade(AR_TEXT,e->fx_dash/0.35f));
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p] && (c->selected_mask&(1u<<p)) && e->pets.command[p]!=AR_CMD_AUTO && e->pets.command[p]!=AR_CMD_HOLD) {
        Vector2 from=ar_iso(c,e->pets.x[p],e->pets.y[p],0),to=ar_iso(c,e->pets.goal_x[p],e->pets.goal_y[p],0);
        DrawLineEx(from,to,1,Fade(AR_GOLD,0.4f));
        ar_ring(c,e->pets.goal_x[p],e->pets.goal_y[p],0.65f,AR_GOLD);
        ar_text(c,TextFormat("#%d",p+1),to.x+8,to.y-12,12,AR_GOLD);
    }
    if(e->fx_blast>0) {
        float age=3-e->fx_blast,fade=e->fx_blast/3;
        Vector2 at=ar_iso(c,e->blast_x,e->blast_y,0);
        ar_ring(c,e->blast_x,e->blast_y,fminf(8,age*9),Fade(AR_GOLD,fade));
        ar_ring(c,e->blast_x,e->blast_y,fminf(10,age*5),Fade(AR_MINT,fade*0.8f));
        // A rising magical eruption with a broad shock front, without shaking
        // the follow camera or flashing the entire screen.
        float rise=(65+age*105)*c->zoom,radius=(85+age*65)*c->zoom;
        Vector2 crown={at.x,at.y-rise};
        DrawCircleGradient((int)at.x,(int)at.y,radius*1.5f,Fade(AR_GOLD,fade*0.7f),Fade(AR_GOLD,0));
        for(int i=0;i<7;i++) {
            float a=i*2*PI/7;
            Vector2 cloud={crown.x+cosf(a)*radius*0.53f,crown.y+sinf(a)*radius*0.3f};
            DrawCircleGradient((int)cloud.x,(int)cloud.y,radius*0.66f,
                Fade((Color){214,151,77,255},fade*0.52f),Fade(AR_INK,0));
        }
        DrawLineEx(at,crown,(36+age*15)*c->zoom,Fade(AR_GOLD,fade*0.35f));
        DrawLineEx(at,crown,(12+age*8)*c->zoom,Fade(AR_TEXT,fade*0.65f));
        DrawCircleGradient((int)crown.x,(int)crown.y,radius,
            Fade((Color){255,246,203,255},fade),Fade(AR_GOLD,0));
        DrawCircleGradient((int)crown.x,(int)crown.y,radius*0.5f,
            Fade(AR_TEXT,fade),Fade(AR_MINT,0));
        for(int i=0;i<30;i++) {
            float a=i*2*PI/30,r=(age*150+15)*c->zoom;
            float height=age*(90+(i%5)*19)-age*age*32;
            Vector2 tip={at.x+cosf(a)*r,at.y+sinf(a)*r*0.5f-height*c->zoom};
            DrawLineEx(tip,(Vector2){tip.x-cosf(a)*19*c->zoom,tip.y-sinf(a)*10*c->zoom+9*c->zoom},
                (2+i%3)*c->zoom,Fade(i%3 ? AR_GOLD : AR_MINT,fade));
        }
        ar_text(c,"STARFIRE",at.x-45,at.y+22*c->zoom,21,Fade(AR_GOLD,fade));
    }
    if(c->targeting_nuke) {
        Vector2 target=ar_unproject(c,GetMousePosition());
        int unloaded=e->campaign && (fabsf(target.x)>e->cfg.arena_size*0.5f-8 || fabsf(target.y)>e->cfg.arena_size*0.5f-8);
        Color color=unloaded ? AR_RED : AR_GOLD;ar_ring(c,target.x,target.y,8,color);
        ar_text(c,unloaded ? "MOVE CLOSER TO LOAD STRIKE AREA" : "STARFIRE / 8 CORES + 20 AETHER",
            GetMousePosition().x+14,GetMousePosition().y,14,color);
    }
    if(c->dragging) {
        Vector2 mouse=GetMousePosition();Rectangle box={fminf(mouse.x,c->drag_start.x),fminf(mouse.y,c->drag_start.y),fabsf(mouse.x-c->drag_start.x),fabsf(mouse.y-c->drag_start.y)};
        DrawRectangleRec(box,Fade(AR_MINT,0.08f));DrawRectangleLinesEx(box,1,AR_MINT);
    }
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && e->pets.cd[p]>e->cfg.pet_attack_cooldown-0.14f) {
        int target=e->pets.target[p];
        if(target>=0 && e->enemies.active[target]) {
            Vector2 from=ar_iso(c,e->pets.x[p],e->pets.y[p],0.65f);
            Vector2 to=ar_iso(c,e->enemies.x[target],e->enemies.y[target],0.6f);
            DrawLineEx(from,to,3*c->zoom,Fade(AR_GOLD,0.8f));
        }
    }
    if(c->build_kind>=0) {
        Vector2 world=ar_unproject(c,GetMousePosition());
        int valid=ar_build_location_valid(e,0,c->build_kind,world.x,world.y)
            && ar_geometry_dist2(e->px,e->py,world.x,world.y)<=144;
        ar_ring(c,world.x,world.y,e->cfg.build_radius[c->build_kind]+0.5f,valid ? AR_MINT : AR_RED);
        ar_sprite(c,ar_build_sprite(c->build_kind),GetMousePosition(),70*c->zoom,1,Fade(valid ? WHITE : AR_RED,0.55f));
        if(c->build_kind==AR_BUILD_HARVESTER)ar_ring(c,world.x,world.y,e->cfg.harvest_radius,Fade(AR_MINT,0.4f));
    }
    for(int i=0;i<AR_FX_COUNT;i++)if(c->fx[i].life>0) {
        ARFeedback* f=&c->fx[i];
        Vector2 p=ar_iso(c,f->x,f->y,0.9f+(1.1f-f->life));
        ar_text(c,TextFormat("%+.0f",f->value),p.x-8,p.y,19,Fade(f->color,fminf(1,f->life*2)));
    }
}
static inline void ar_entity_markers(ARClient* c,ARPG* e) {
    // Gameplay information is not scenery: compose it after the night layer.
    Vector2 badges[AR_MAX_PETS];ar_pet_badges(c,e,badges);
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p]) {
        Vector2 at=badges[p],head=ar_iso(c,e->pets.x[p],e->pets.y[p],0);head.y-=52*c->zoom;
        Color color=e->direct_pet==p ? AR_MINT : c->selected_mask&(1u<<p) ? AR_GOLD : AR_TEXT;
        if(fabsf(at.x-head.x)>2)DrawLineEx((Vector2){at.x,at.y+9},head,1,Fade(color,.5f));
        DrawCircleV(at,10,AR_PANEL);DrawCircleLinesV(at,10,Fade(color,.8f));
        ar_text(c,TextFormat("%d",p+1),at.x-4,at.y-8,14,color);
        if((c->selected_mask&(1u<<p)) || e->pets.hp[p]<e->pets.max_hp[p]*.9f)
            ar_bar(at.x-17,at.y+14,34,4,e->pets.hp[p]/e->pets.max_hp[p],AR_MINT);
    }
    for(int n=0;n<AR_MAX_NESTS;n++)if(e->nest_active[n]) {
        Vector2 at=ar_iso(c,e->nest_x[n],e->nest_y[n],0);at.y-=104*c->zoom;
        ar_bar(at.x-32*c->zoom,at.y-12,64*c->zoom,4,e->nest_hp[n]/e->nest_max_hp[n],AR_RED);
        ar_text(c,"THORN CAMP",at.x-43,at.y-34,13,AR_RED);
    }
    for(int i=0;i<e->cfg.enemy_cap;i++)if(e->enemies.active[i] && e->enemies.hp[i]<e->enemies.max_hp[i]) {
        Vector2 at=ar_iso(c,e->enemies.x[i],e->enemies.y[i],0);at.y-=(e->enemies.type[i] ? 70 : 43)*c->zoom;
        ar_bar(at.x-20*c->zoom,at.y-8,40*c->zoom,4,e->enemies.hp[i]/e->enemies.max_hp[i],AR_RED);
    }
    for(int b=0;b<AR_MAX_BUILDINGS;b++)if(e->build_active[b] && e->build_hp[b]<e->build_max_hp[b]) {
        Vector2 at=ar_iso(c,e->build_x[b],e->build_y[b],0);
        at.y-=(e->build_kind[b]==AR_BUILD_WALL ? 44 : e->build_kind[b]==AR_BUILD_ARTILLERY ? 115 : 70)*c->zoom;
        ar_bar(at.x-23*c->zoom,at.y-8,46*c->zoom,4,e->build_hp[b]/e->build_max_hp[b],AR_GOLD);
    }
}
static inline void ar_minimap(ARClient* c,ARPG* e,float x,float y) {
    float size=144;
    ar_box((Rectangle){x-9,y-9,size+18,size+42},AR_PANEL);
    if(!c->minimap_texture.id || c->minimap_revision!=c->ground_revision) {
        Color pixels[AR_DUN_CELLS];
        for(int i=0;i<AR_DUN_CELLS;i++)pixels[i]=ar_ground(e->dungeon[i],3);
        if(c->minimap_texture.id)UpdateTexture(c->minimap_texture,pixels);
        else {Image im={pixels,AR_DUN_W,AR_DUN_H,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};c->minimap_texture=LoadTextureFromImage(im);}
        c->minimap_revision=c->ground_revision;
    }
    DrawTexturePro(c->minimap_texture,(Rectangle){0,0,AR_DUN_W,AR_DUN_H},(Rectangle){x,y,size,size},(Vector2){0,0},0,WHITE);
    for(int n=0;n<AR_MAX_NESTS;n++)if(e->nest_active[n])
        DrawCircleV((Vector2){x+(e->nest_x[n]/e->cfg.arena_size+0.5f)*size,y+(e->nest_y[n]/e->cfg.arena_size+0.5f)*size},3,AR_RED);
    if(fabsf(e->home_x)<e->cfg.arena_size*0.5f && fabsf(e->home_y)<e->cfg.arena_size*0.5f)
        DrawCircleV((Vector2){x+(e->home_x/e->cfg.arena_size+0.5f)*size,y+(e->home_y/e->cfg.arena_size+0.5f)*size},4,AR_GOLD);
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p])
        DrawCircleV((Vector2){x+(e->pets.x[p]/e->cfg.arena_size+0.5f)*size,y+(e->pets.y[p]/e->cfg.arena_size+0.5f)*size},2,AR_MINT);
    if(!e->keeper_dormant)DrawCircleV((Vector2){x+(e->px/e->cfg.arena_size+0.5f)*size,y+(e->py/e->cfg.arena_size+0.5f)*size},3,WHITE);
    ar_text(c,e->campaign ? "M  OPEN WORLD MAP" : "TRAINING ARENA",x+4,y+size+8,11,AR_GOLD);
    if(e->campaign && !c->map_open && CheckCollisionPointRec(GetMousePosition(),(Rectangle){x-9,y-9,size+18,size+42}) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))c->ui_map=1;
}
static inline void ar_sync_selection(ARClient* c,ARPG* e) {
    c->selected_pet=-1;
    for(int p=0;p<AR_MAX_PETS;p++) {
        if(!e->pets.active[p])c->selected_mask&=~(1u<<p);
        if(c->selected_pet<0 && (c->selected_mask&(1u<<p)))c->selected_pet=p;
    }
}
static inline void ar_select_pet(ARClient* c,ARPG* e,int p,int additive) {
    if(p<0 || p>=AR_MAX_PETS || !e->pets.active[p])return;
    if(additive)c->selected_mask^=1u<<p;
    else c->selected_mask=1u<<p;
    ar_sync_selection(c,e);
}
static inline void ar_drive_selected(ARClient* c,ARPG* e,int return_keeper) {
    ar_sync_selection(c,e);
    int p=return_keeper ? -1 : c->selected_pet;
    if(!return_keeper && (p<0 || c->selected_mask!=(1u<<p))) {
        ar_notice(c,"Choose ONE companion (1-8 or its card), then press F6 to drive it.");return;
    }
    if(!return_keeper && p==e->direct_pet)p=-1;
    ar_world_drive(e,p);c->camera_free=0;c->move_target=0;c->dragging=0;
    c->build_kind=-1;c->targeting_nuke=0;
    // Account for any streaming rebase before snapping the follow camera.
    if(e->campaign){c->origin_x=((ARWorld*)e->campaign)->origin_x;c->origin_y=((ARWorld*)e->campaign)->origin_y;}
    c->cam_x=p<0 ? e->px : e->pets.x[p];c->cam_y=p<0 ? e->py : e->pets.y[p];
    memset(c->fx,0,sizeof(c->fx));
    if(p>=0){c->selected_mask=1u<<p;ar_notice(c,TextFormat("DRIVING #%d %s / WASD move / RMB order / 0 keeper",p+1,AR_PET_NAMES[e->pets.kind[p]]));}
    else ar_notice(c,"KEEPER control restored. Your companions keep their individual assignments.");
}
static inline void ar_hover(ARClient* c,Rectangle r,int type,int id) {
    if(c->map_open)return;
    // Disabled/locked buttons deliberately still expose their stats.
    if(CheckCollisionPointRec(GetMousePosition(),r)){c->hover_type=type;c->hover_id=id;}
}
static inline void ar_tooltip(ARClient* c,ARPG* e) {
    if(!c->hover_type)return;
    Vector2 mouse=GetMousePosition();
    float width=378,height=190,x=ar_clampf(mouse.x-width*0.5f,12,GetScreenWidth()-width-12);
    float y=ar_clampf(mouse.y-height-18,82,GetScreenHeight()-height-12);
    ar_box((Rectangle){x,y,width,height},(Color){17,28,28,253});
    int p=c->hover_id,cls=c->hover_type==2 ? e->pets.kind[p] : p;
    if(c->hover_type<=2) {
        const char* roles[]={"Reliable escort. Attacks nearby enemies and camps.",
            "Fast striker. High damage, low staying power.",
            "Armored guardian. Holds the front line.",
            "Autonomous worker. Gathers at home; avoids combat.",
            "Living excavator. Right-click rock or forest to dig.",
            "Mobile furnace. Right-click terrain to melt it."};
        const char* notes[]={"Auto assist returns to shelter when badly hurt.",
            "Select several fighters, then right-click a camp.",
            "Hold an approach while your other pets work.",
            "Right-click a seam to keep this worker assigned.",
            "Cleared rock becomes usable land or shallow shore.",
            "Near extractor: 2 aether -> 1 core every 8 seconds."};
        ar_actor(c,cls+1,0,(Vector2){x+40,y+68},51,1,WHITE);
        ar_text(c,AR_PET_NAMES[cls],x+78,y+15,22,AR_GOLD);
        ar_text(c,TextFormat("%.0f aether  |  Homestead %d",e->cfg.summon_cost[cls],ar_pet_unlock(e,cls)+1),x+78,y+46,14,AR_MUTED);
        ar_text(c,TextFormat("Health %.0f    Damage %.1f / %.1fs    Speed %.1f",e->cfg.pet_health[cls],
            e->cfg.pet_damage[cls],e->cfg.pet_attack_cooldown,e->cfg.pet_speed[cls]),x+15,y+82,14,AR_TEXT);
        ar_text(c,roles[cls],x+15,y+108,14,AR_TEXT);
        ar_text(c,notes[cls],x+15,y+132,13,AR_MUTED);
        const char* status=e->keeper_dormant ? "Press 0 to return to the keeper and summon" : ar_tech_level(e,0)<ar_pet_unlock(e,cls) ? "LOCKED - grow production to unlock" :
            e->pets_alive>=e->cfg.pet_cap ? "All eight companion slots are occupied" :
            e->shards<e->cfg.summon_cost[cls] ? "Need more aether" :
            e->summon_cd>0 ? TextFormat("Summoning ready in %.1fs",e->summon_cd) : "Ready to summon";
        if(c->hover_type==2)status=TextFormat("#%d  %.0f / %.0f HP  |  %s",p+1,e->pets.hp[p],e->pets.max_hp[p],AR_COMMAND_NAMES[e->pets.command[p]]);
        ar_text(c,status,x+15,y+162,14,AR_MINT);
    } else {
        const char* descriptions[]={"Automatic area defense; attacks camp cores too.",
            "A durable obstacle enemies can chew through.",
            "Extracts renewable seams inside its range.",
            "Aether-fed siege weapon. N, then click a target.",
            "Build connected segments across deep water."};
        const char* details[]={"1.5 damage each second in a 3.5-unit radius.",
            "Use chokepoints; leave your workers a route.",
            "Bring an Ember here to produce Starfire cores.",
            "Shot: 8 cores + 20 aether. Reload: 30 seconds.",
            "A 3.6-unit-wide crossing. Costs 5 aether."};
        ar_sprite(c,ar_build_sprite(p),(Vector2){x+40,y+70},52,1,WHITE);
        ar_text(c,AR_BUILD_NAMES[p],x+78,y+16,22,AR_GOLD);
        ar_text(c,TextFormat("%.0f aether  |  %.0f structure health",e->cfg.build_cost[p],e->cfg.build_hp[p]),x+78,y+46,14,AR_MUTED);
        ar_text(c,descriptions[p],x+15,y+84,14,AR_TEXT);
        ar_text(c,details[p],x+15,y+110,14,AR_MUTED);
        ar_text(c,p==AR_BUILD_ARTILLERY ? "48-unit range / 8-unit blast / destroys terrain." :
            "Preview the footprint, then left-click to place.",x+15,y+135,14,AR_MUTED);
        ar_text(c,p==AR_BUILD_ARTILLERY && ar_tech_level(e,0)<4 ? "Unlocks at Homestead 5" : "Shift-click repeats / Backspace cancels",x+15,y+162,14,AR_MINT);
    }
}
static inline void ar_hud(ARClient* c,ARPG* e) {
    int w=GetScreenWidth(),h=GetScreenHeight();float bottom=h-150;
    c->hover_type=0;ar_sync_selection(c,e);
    DrawRectangle(0,0,w,76,AR_INK);DrawLine(0,75,w,75,AR_LINE);
    DrawTextEx(c->heading,"HEARTHWILD",(Vector2){24,14},27,1.5f,AR_TEXT);
    ar_text(c,"THE REACH  /  LANTERNLIGHT",25,47,11,AR_MUTED);
    ar_text(c,"KEEPER",292,16,12,AR_MUTED);
    ar_bar(292,39,128,8,e->hp/e->max_hp,AR_MINT);
    ar_text(c,TextFormat("%.0f / %.0f",e->hp,e->max_hp),430,31,16,AR_TEXT);
    ar_text(c,"AETHER",527,14,12,AR_MUTED);
    ar_text(c,TextFormat("%.0f",e->shards),527,34,25,AR_GOLD);
    ar_text(c,TextFormat("+%.1f / min",c->rate),588,39,14,AR_MINT);
    ar_text(c,"CORES",699,14,12,AR_MUTED);
    ar_text(c,TextFormat("%.0f",e->cores),699,34,25,AR_MINT);
    ar_text(c,TextFormat("HOMESTEAD %d",ar_tech_level(e,0)+1),790,14,12,AR_MUTED);
    ar_text(c,TextFormat("%d camps cleared",e->camps_cleared),790,39,16,AR_TEXT);
    ar_bar(790,63,155,3,fmodf(e->harvested,20)/20,AR_GOLD);
    if(ar_button(c,(Rectangle){w-212.0f,17,119,40},c->autoplay ? "T  RL autoplay" : "T  Manual",c->autoplay,1))c->ui_toggle=1;
    if(ar_button(c,(Rectangle){w-81.0f,17,58,40},c->paused ? "Play" : "Pause",c->paused,1))c->ui_pause=1;
    ar_minimap(c,e,w-172,97);
    if(ar_button(c,(Rectangle){w-181.0f,282,162,30},TextFormat("F7  %s",AR_LIGHT_NAMES[c->light_mode]),c->light_mode>=2,1))c->light_mode=(c->light_mode+1)%4;
    ar_text(c,c->light_failed ? "Lighting unavailable" : "VIEWER-ONLY LIGHTING",w-174,318,10,AR_MUTED);
    ar_box((Rectangle){23,97,298,103},Fade(AR_PANEL,0.94f));
    ar_text(c,"YOUR NEXT LITTLE PROJECT",38,109,11,AR_GOLD);
    int extractors=0,embers=0,launchers=0;
    for(int b=0;b<AR_MAX_BUILDINGS;b++)if(e->build_active[b]) {
        extractors+=e->build_kind[b]==AR_BUILD_HARVESTER;launchers+=e->build_kind[b]==AR_BUILD_ARTILLERY;
    }
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && e->pets.kind[p]==AR_PET_EMBER)embers++;
    ar_text(c,!extractors ? "Make your first extractor." : !embers ? "A living workshop." : !launchers ? "Build something excessive." : "Bring down the stars.",38,132,19,AR_TEXT);
    ar_text(c,!extractors ? "B, then click beside a crystal seam." : !embers ? "Ember refines aether into Starfire cores." :
        !launchers ? "J builds Starfire. Each shot needs 8 cores." : "N, then click. The frontier keeps going.",38,160,13,AR_MUTED);
    ar_text(c,"Pets do the work. Buildings anchor your outposts.",38,181,12,AR_MUTED);
    ar_box((Rectangle){23,210,298,84},Fade(AR_PANEL,.96f));
    int focus=e->direct_pet;float fx=focus>=0 ? e->pets.x[focus] : e->px,fy=focus>=0 ? e->pets.y[focus] : e->py;
    int biome=ar_biome(e->dungeon_seed,c->origin_x+fx/ar_world_cell(e),c->origin_y+fy/ar_world_cell(e));
    ar_text(c,AR_BIOME_NAMES[biome],38,220,15,AR_GOLD);
    ar_text(c,focus>=0 ? TextFormat("DRIVING #%d  %s",focus+1,AR_PET_NAMES[e->pets.kind[focus]]) : "CONTROLLING KEEPER",38,243,13,focus>=0 ? AR_MINT : AR_TEXT);
    const char* drive_label=c->selected_pet>=0 ? (focus==c->selected_pet ? "F6  Keeper" : TextFormat("F6  Drive #%d",c->selected_pet+1)) : "Select 1-8";
    if(ar_button(c,(Rectangle){186,250,124,32},drive_label,focus>=0,!c->autoplay && c->selected_pet>=0))c->ui_drive=1;
    ar_text(c,TextFormat("%.0f, %.0f",c->origin_x*ar_world_cell(e)+fx,c->origin_y*ar_world_cell(e)+fy),38,268,13,AR_MUTED);
    if(c->selected_pet>=0) {
        int p=c->selected_pet;
        ar_box((Rectangle){23,305,298,90},Fade(AR_PANEL,.96f));
        ar_actor(c,e->pets.kind[p]+1,0,(Vector2){57,370},43,1,WHITE);
        ar_text(c,TextFormat("#%d  %s",p+1,AR_PET_NAMES[e->pets.kind[p]]),88,316,18,AR_TEXT);
        ar_text(c,e->pets.dormant[p] ? "Working beyond this region" : e->direct_pet==p ? "DIRECT CONTROL / WASD" : AR_COMMAND_NAMES[e->pets.command[p]],88,343,13,AR_MINT);
        ar_bar(88,368,178,4,e->pets.hp[p]/e->pets.max_hp[p],AR_MINT);
        ar_text(c,"RMB orders / P auto / 0 keeper",38,379,11,AR_MUTED);
    }
    if(c->notice_time>0) {
        Vector2 m=MeasureTextEx(c->font,c->notice,16,0.3f);float nx=(w-m.x-36)*0.5f;
        ar_box((Rectangle){nx,bottom-59,m.x+36,40},AR_PANEL);
        ar_text(c,c->notice,nx+18,bottom-49,16,AR_GOLD);
    }
    DrawRectangle(0,(int)bottom,w,150,AR_INK);DrawLine(0,(int)bottom,w,(int)bottom,AR_LINE);
    ar_text(c,c->pet_policy ? "COMPANIONS / RL + YOUR ORDERS" : "COMPANIONS / AUTOMATIC ASSIST",24,bottom+12,12,AR_MUTED);
    float pet_width=w*0.39f,card_width=(pet_width-24)/4;
    for(int p=0;p<AR_MAX_PETS;p++) {
        Rectangle card={24+(p%4)*(card_width+6),bottom+33+(p/4)*45,card_width,41};
        int alive=e->pets.active[p],selected=(c->selected_mask&(1u<<p))!=0;
        if(ar_button(c,card,"",selected,alive))
            ar_select_pet(c,e,p,IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT));
        if(!alive){ar_text(c,TextFormat("%d  Empty",p+1),card.x+12,card.y+11,13,AR_MUTED);continue;}
        ar_hover(c,card,2,p);
        ar_actor(c,e->pets.kind[p]+1,0,(Vector2){card.x+19,card.y+32},28,1,WHITE);
        ar_text(c,TextFormat("%d %s",p+1,AR_PET_NAMES[e->pets.kind[p]]),card.x+37,card.y+4,14,selected ? AR_GOLD : AR_TEXT);
        int task=e->pets.task[p],command=e->pets.command[p];
        const char* job=e->direct_pet==p ? "WASD / Driving" : e->pets.dormant[p] ? "Assigned afar" : command!=AR_CMD_AUTO ? AR_COMMAND_NAMES[command] :
            task!=AR_TASK_AUTO ? AR_TASK_NAMES[task] : e->pets.kind[p]==AR_PET_MULE ? "Auto gather" :
            e->pets.kind[p]==AR_PET_EMBER ? "Auto refine" : "Auto escort";
        ar_text(c,job,card.x+39,card.y+20,11,AR_MUTED);
        ar_bar(card.x+6,card.y+37,card.width-12,2,e->pets.hp[p]/e->pets.max_hp[p],AR_MINT);
    }
    float bx=pet_width+42,bwidth=w*0.20f;
    if(c->selected_mask) {
        ar_text(c,"TASK FOR SELECTED / RMB PRECISE ORDER",bx,bottom+12,11,AR_MUTED);
        for(int t=0;t<AR_PET_TASK_COUNT;t++) {
            Rectangle r={bx+(t%3)*(bwidth/3),bottom+33+(t/3)*28,bwidth/3-6,25};
            if(ar_button(c,r,AR_TASK_NAMES[t],c->selected_pet>=0 && c->task_override[c->selected_pet]==t,1)) {
                for(int p=0;p<AR_MAX_PETS;p++)if(c->selected_mask&(1u<<p)) {
                    c->task_override[p]=t;ar_command_pet(e,0,p,AR_CMD_AUTO,e->pets.x[p],e->pets.y[p]);
                }
                ar_notice(c,"Task assigned to selected companions. P restores automatic assist.");
            }
        }
        if(ar_button(c,(Rectangle){bx+bwidth/3,bottom+89,bwidth*2/3-6,25},"F6 Drive selected",e->direct_pet>=0,!c->autoplay))c->ui_drive=1;
    } else {
        ar_text(c,"BUILD YOUR NETWORK",bx,bottom+12,12,AR_MUTED);
        const char* keys[]={"G","V","B","J","O"};
        for(int b=0;b<AR_BUILD_KIND_COUNT;b++) {
            Rectangle r={bx+(b%2)*(bwidth/2),bottom+33+(b/2)*28,bwidth/2-6,25};
            ar_hover(c,r,3,b);
            int unlocked=b==AR_BUILD_ARTILLERY ? ar_tech_level(e,0)>=4 :
                b!=AR_BUILD_HARVESTER || ar_tech_level(e,0)>=e->cfg.unlock_level_harvester;
            if(ar_button(c,r,TextFormat("%s %s %.0f",keys[b],AR_BUILD_NAMES[b],e->cfg.build_cost[b]),c->build_kind==b,
                    !c->autoplay && unlocked && e->builds_alive<AR_MAX_BUILDINGS && e->shards>=e->cfg.build_cost[b]))
                c->build_kind=c->build_kind==b ? -1 : b;
        }
    }
    float ax=bx+bwidth+12,sx=w-211,aw=fminf(137,sx-ax-17);
    ar_text(c,e->direct_pet>=0 ? "DIRECT CONTROL" : "KEEPER",ax,bottom+12,12,AR_MUTED);
    const char* names[]={"Q Dash","E Nova","F Frost","N Starfire"};
    float cds[]={e->dash_cd,e->nova_cd,e->frost_cd,0};
    for(int a=0;a<4;a++) {
        Rectangle r={ax,bottom+33+a*21,aw,19};
        if(e->direct_pet>=0 && a<3) {
            int cls=e->pets.kind[e->direct_pet];
            const char* special=cls==AR_PET_BURROWER ? "Hold E  Dig" : cls==AR_PET_EMBER ? "Hold E  Melt" : cls==AR_PET_MULE ? "Auto-gather" : "Auto-attack";
            ar_text(c,a==0 ? "WASD  Move" : a==1 ? special : "0  Keeper",r.x+3,r.y+2,13,AR_MINT);continue;
        }
        if(ar_button(c,r,cds[a]>0 ? TextFormat("%s %.1f",names[a],cds[a]) : names[a],a==3 && c->targeting_nuke,!c->autoplay && cds[a]<=0)) {
            if(a==3){c->targeting_nuke=!c->targeting_nuke;c->build_kind=-1;}
            else c->ui_ability=a+1;
        }
    }
    ar_text(c,"SUMMON / HOVER FOR STATS",sx,bottom+12,11,AR_MUTED);
    for(int p=0;p<AR_PET_CLASS_COUNT;p++) {
        Rectangle r={sx+(p%2)*97,bottom+33+(p/2)*28,91,25};
        ar_hover(c,r,1,p);
        if(ar_button(c,r,TextFormat("%s %.0f",AR_PET_NAMES[p],e->cfg.summon_cost[p]),0,!c->autoplay && !e->keeper_dormant &&
                ar_tech_level(e,0)>=ar_pet_unlock(e,p) && e->summon_cd<=0 && e->pets_alive<e->cfg.pet_cap &&
                e->shards>=e->cfg.summon_cost[p]))c->ui_summon=p+1;
    }
    ar_text(c,"1-8 select   F6 drive selected   0 keeper   WASD move   LMB/drag select   RMB order   Shift add   M map   Wheel zoom   Home camera   Tab pause",24,h-24,12,AR_MUTED);
    if(c->paused) {
        ar_box((Rectangle){w*0.5f-125,95,250,49},AR_PANEL);
        ar_text(c,e->hp<=0 ? "RUN ENDED / R TO RESTART" : "PAUSED / TAB TO RESUME",w*0.5f-109,112,14,AR_GOLD);
    }
    ar_tooltip(c,e);
}
static inline Vector2 ar_map_point(ARClient* c,Rectangle r,double x,double y) {
    return (Vector2){r.x+r.width*.5f+(float)(x-c->map_x)*r.width/c->map_span,
        r.y+r.height*.5f+(float)(y-c->map_y)*r.width/c->map_span};
}
static inline void ar_map_marker(ARClient* c,Rectangle r,double x,double y,Color color,const char* label) {
    Vector2 p=ar_map_point(c,r,x,y);
    if(!CheckCollisionPointRec(p,r))return;
    DrawCircleV(p,5,AR_INK);DrawCircleV(p,3,color);
    if(label)ar_text(c,label,p.x+7,p.y-7,12,color);
}
static inline void ar_overview(ARClient* c,ARPG* e) {
    if(!c->map_open || !e->campaign)return;
    ARWorld* world=(ARWorld*)e->campaign;
    float width=fminf(920,GetScreenWidth()-80.0f),height=GetScreenHeight()-246;
    Rectangle r={(GetScreenWidth()-width)*.5f,122,width,height};
    Vector2 mouse=GetMousePosition();
    if(CheckCollisionPointRec(mouse,r)) {
        float wheel=GetMouseWheelMove();
        if(wheel) {
            float old=c->map_span;c->map_span=ar_clampf(old*powf(.8f,wheel),64,4096);
            c->map_x+=(mouse.x-r.x-r.width*.5f)*(old-c->map_span)/r.width;
            c->map_y+=(mouse.y-r.y-r.height*.5f)*(old-c->map_span)/r.width;
        }
        if(IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 delta=GetMouseDelta();c->map_x-=delta.x*c->map_span/r.width;c->map_y-=delta.y*c->map_span/r.width;
        }
    }
    if(IsKeyPressed(KEY_HOME)) {
        int p=e->direct_pet;c->map_x=world->origin_x+(p>=0 ? e->pets.x[p] : e->px)/ar_world_cell(e);
        c->map_y=world->origin_y+(p>=0 ? e->pets.y[p] : e->py)/ar_world_cell(e);
    }
    int pw=256,ph=(int)(pw*r.height/r.width);
    if(!c->map_texture.id || c->map_texture.height!=ph || c->map_cache_x!=c->map_x || c->map_cache_y!=c->map_y ||
            c->map_cache_span!=c->map_span || c->map_cache_tick/120!=e->tick/120 ||
            c->map_cache_seed!=(int)world->seed || c->map_cache_version!=world->terrain_version) {
        Color* pixels=(Color*)MemAlloc((unsigned)(pw*ph*sizeof(Color)));
        if(pixels) {
            for(int y=0;y<ph;y++)for(int x=0;x<pw;x++) {
                double gx=c->map_x+(x-pw*.5)*c->map_span/pw,gy=c->map_y+(y-ph*.5)*c->map_span/pw;
                int tile=ar_world_global_tile(e,world,(int)floor(gx),(int)floor(gy));
                Color color=ar_land_color(world->seed,(float)gx,(float)gy,tile);
                int known=ar_world_chunk_id(world,(int)floor(gx/AR_CHUNK_SIZE),(int)floor(gy/AR_CHUNK_SIZE))>=0;
                if(!known)color=ar_mix(color,AR_INK,.57f);
                pixels[y*pw+x]=color;
            }
            Image image={pixels,pw,ph,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            if(c->map_texture.id && c->map_texture.height==ph)UpdateTexture(c->map_texture,pixels);
            else {if(c->map_texture.id)UnloadTexture(c->map_texture);c->map_texture=LoadTextureFromImage(image);SetTextureFilter(c->map_texture,TEXTURE_FILTER_BILINEAR);}
            UnloadImage(image);
            c->map_cache_x=c->map_x;c->map_cache_y=c->map_y;c->map_cache_span=c->map_span;
            c->map_cache_tick=e->tick;c->map_cache_seed=(int)world->seed;c->map_cache_version=world->terrain_version;
        }
    }
    DrawRectangle(0,76,GetScreenWidth(),GetScreenHeight()-226,Fade(AR_INK,.83f));
    ar_box((Rectangle){r.x-16,r.y-38,r.width+32,r.height+83},AR_PANEL);
    ar_text(c,"THE REACH / WORLD ATLAS",r.x,r.y-28,18,AR_GOLD);
    c->map_ui=1;
    if(ar_button(c,(Rectangle){r.x+r.width-109,r.y-31,109,25},"M  Close",0,1))c->ui_map=1;
    c->map_ui=0;
    if(c->map_texture.id)DrawTexturePro(c->map_texture,(Rectangle){0,0,(float)pw,(float)ph},r,(Vector2){0,0},0,WHITE);
    BeginScissorMode((int)r.x,(int)r.y,(int)r.width,(int)r.height);
    int grid=c->map_span>800 ? 128 : c->map_span>320 ? 64 : 32;
    for(int x=(int)floor((c->map_x-c->map_span*.5)/grid)*grid;x<c->map_x+c->map_span*.5;x+=grid) {
        Vector2 p=ar_map_point(c,r,x,c->map_y);DrawLine((int)p.x,(int)r.y,(int)p.x,(int)(r.y+r.height),Fade(AR_TEXT,.075f));
    }
    for(int y=(int)floor((c->map_y-c->map_span*.5)/grid)*grid;y<c->map_y+c->map_span*.5;y+=grid) {
        Vector2 p=ar_map_point(c,r,c->map_x,y);DrawLine((int)r.x,(int)p.y,(int)(r.x+r.width),(int)p.y,Fade(AR_TEXT,.075f));
    }
    float cell=ar_world_cell(e);
    for(int i=0;i<world->building_count;i++)if(world->buildings[i].active)
        ar_map_marker(c,r,world->buildings[i].x/cell,world->buildings[i].y/cell,AR_GOLD,NULL);
    for(int i=0;i<world->nest_count;i++)if(world->nests[i].active)
        ar_map_marker(c,r,world->nests[i].x/cell,world->nests[i].y/cell,AR_RED,NULL);
    ar_map_marker(c,r,world->home_x/cell,world->home_y/cell,AR_GOLD,"Lodge");
    ar_map_marker(c,r,world->origin_x+e->px/cell,world->origin_y+e->py/cell,WHITE,"Keeper");
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p])
        ar_map_marker(c,r,world->origin_x+e->pets.x[p]/cell,world->origin_y+e->pets.y[p]/cell,AR_MINT,TextFormat("#%d",p+1));
    Vector2 a=ar_map_point(c,r,world->origin_x-AR_DUN_W/2,world->origin_y-AR_DUN_H/2);
    float active=AR_DUN_W*r.width/c->map_span;
    DrawRectangleLinesEx((Rectangle){a.x,a.y,active,active},1,Fade(AR_MINT,.35f));
    EndScissorMode();
    ar_text(c,"Wheel zoom / RMB or middle-drag pan / Home center / dim terrain is uncharted",r.x,r.y+r.height+10,13,AR_MUTED);
    ar_text(c,TextFormat("%.0f tiles across  /  %d persistent chunks  /  center %.0f, %.0f",c->map_span,world->chunk_count,c->map_x,c->map_y),r.x,r.y+r.height+29,12,AR_MINT);
}
static inline void c_render(ARPG* e) {
    if(!IsWindowReady()) {
        // Sprite edges already carry alpha. MSAA is optional because software
        // renderers can spend most of a frame resolving a 4x framebuffer.
        SetConfigFlags(FLAG_WINDOW_RESIZABLE|(getenv("ARPG_MSAA") && atoi(getenv("ARPG_MSAA")) ? FLAG_MSAA_4X_HINT : 0));
        InitWindow(1440,900,"Hearthwild / ARPG");
        SetExitKey(KEY_NULL);
        SetWindowMinSize(1280,720);SetTargetFPS(60);
    }
    ARClient* c=ar_client(e);
    if(e->campaign) {
        ARWorld* world=(ARWorld*)e->campaign;
        float dx=(world->origin_x-c->origin_x)*ar_world_cell(e),dy=(world->origin_y-c->origin_y)*ar_world_cell(e);
        if(dx || dy) {
            c->cam_x-=dx;c->cam_y-=dy;c->move_x-=dx;c->move_y-=dy;
            for(int i=0;i<AR_FX_COUNT;i++){c->fx[i].x-=dx;c->fx[i].y-=dy;}
            c->origin_x=world->origin_x;c->origin_y=world->origin_y;
            c->prev_nests=e->nests_alive;c->prev_builds=e->builds_alive;
            memset(c->enemy_hp,0,sizeof(c->enemy_hp));
        }
    }
    if(!c->initialized)ar_assets(c);
    float dt=fminf(GetFrameTime(),0.1f); c->time+=dt;
    if(IsKeyPressed(KEY_F7))c->light_mode=(c->light_mode+1)%4;
    float anim_dt=ar_clampf((e->tick-c->animation_tick)*AR_DT,0,.15f);c->animation_tick=e->tick;
    c->nature_time+=anim_dt;
    c->keeper_gait+=hypotf(e->pvx,e->pvy)*anim_dt;
    if(fabsf(e->pvx-e->pvy)>.05f)c->keeper_facing=e->pvx-e->pvy;
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p]) {
        c->pet_gait[p]+=hypotf(e->pets.vx[p],e->pets.vy[p])*anim_dt;
        if(fabsf(e->pets.vx[p]-e->pets.vy[p])>.05f)c->pet_facing[p]=e->pets.vx[p]-e->pets.vy[p];
    }
    for(int k=0;k<e->enemy_count;k++) {
        int i=e->enemies.dense[k];c->enemy_gait[i]+=hypotf(e->enemies.vx[i],e->enemies.vy[i])*anim_dt;
        if(fabsf(e->enemies.vx[i]-e->enemies.vy[i])>.05f)c->enemy_facing[i]=e->enemies.vx[i]-e->enemies.vy[i];
    }
    c->notice_time=fmaxf(0,c->notice_time-dt);
    for(int i=0;i<AR_FX_COUNT;i++)c->fx[i].life=fmaxf(0,c->fx[i].life-dt);
    if(e->tick<c->prev_tick || !c->prev_tick) {
        memset(c->fx,0,sizeof(c->fx)); memset(c->enemy_hp,0,sizeof(c->enemy_hp));
        c->prev_hp=e->hp; c->prev_harvest=e->harvested; c->rate_base=e->harvested;
        c->prev_builds=e->builds_alive;c->prev_nests=e->nests_alive;c->rate_elapsed=0;c->rate=0;
        c->prev_tick=e->tick;c->prev_pets=e->pets_alive;
        c->prev_level=ar_tech_level(e,0);
    }
    if(e->hp<c->prev_hp)ar_feedback(c,e->px,e->py,e->hp-c->prev_hp,AR_RED);
    if(e->harvested>c->prev_harvest)ar_feedback(c,e->px,e->py,e->harvested-c->prev_harvest,AR_MINT);
    if(!e->campaign && e->builds_alive>c->prev_builds)ar_notice(c,"Outpost established. Your network is growing.");
    if(e->camps_cleared>c->prev_camps)ar_notice(c,"Camp cleared. A little more of the Reach is yours.");
    c->prev_camps=e->camps_cleared;
    if(e->pets_alive<c->prev_pets)ar_notice(c,"A companion fell. Return to shelter and summon a replacement.");
    c->prev_pets=e->pets_alive;
    int level=ar_tech_level(e,0);
    if(level>c->prev_level) {
        const char* unlock=level==e->cfg.unlock_level_aegis ? "Aegis companion unlocked." :
            level==e->cfg.unlock_level_fang ? "Fang companion unlocked." : "Your homestead is thriving.";
        ar_notice(c,TextFormat("Homestead %d - %s",level+1,unlock));
    }
    c->prev_level=level;
    for(int i=0;i<e->cfg.enemy_cap;i++) {
        if(e->enemies.active[i] && c->enemy_hp[i]>e->enemies.hp[i])
            ar_feedback(c,e->enemies.x[i],e->enemies.y[i],e->enemies.hp[i]-c->enemy_hp[i],AR_GOLD);
        if(!e->enemies.active[i] && c->enemy_hp[i]>0)
            ar_feedback(c,e->enemies.x[i],e->enemies.y[i],-c->enemy_hp[i],AR_GOLD);
        c->enemy_hp[i]=e->enemies.active[i] ? e->enemies.hp[i] : 0;
    }
    c->prev_hp=e->hp;c->prev_harvest=e->harvested;c->prev_builds=e->builds_alive;c->prev_nests=e->nests_alive;
    c->rate_elapsed+=(e->tick-c->prev_tick)*AR_DT;c->prev_tick=e->tick;
    if(c->rate_elapsed>=5) {c->rate=(e->harvested-c->rate_base)*60/c->rate_elapsed;c->rate_base=e->harvested;c->rate_elapsed=0;}
    if(ar_world_pointer(c,GetMousePosition(),GetScreenWidth(),GetScreenHeight()))
        c->zoom=ar_clampf(c->zoom+GetMouseWheelMove()*0.09f,0.55f,1.65f);
    if(ar_world_pointer(c,GetMousePosition(),GetScreenWidth(),GetScreenHeight()) && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta=GetMouseDelta();float scale=AR_VIEW_SCALE*c->zoom;
        c->cam_x-=(delta.x+2*delta.y)/(2*scale);
        c->cam_y-=(2*delta.y-delta.x)/(2*scale);
        c->cam_x=ar_clampf(c->cam_x,-e->cfg.arena_size*0.5f,e->cfg.arena_size*0.5f);
        c->cam_y=ar_clampf(c->cam_y,-e->cfg.arena_size*0.5f,e->cfg.arena_size*0.5f);
        c->camera_free=1;
    }
    if(IsKeyPressed(KEY_HOME))c->camera_free=0;
    float blend=1-expf(-7*dt);
    int pilot=e->direct_pet;
    float target_x=pilot>=0 ? e->pets.x[pilot] : e->px,target_y=pilot>=0 ? e->pets.y[pilot] : e->py;
    if(!c->camera_free){c->cam_x+=(target_x-c->cam_x)*blend;c->cam_y+=(target_y-c->cam_y)*blend;}
    ar_camera_project(c,GetScreenWidth(),GetScreenHeight());
    double render_start=GetTime();
    ar_lighting_prepare(c,e);
    BeginDrawing(); ClearBackground((Color){40,60,52,255});
    BeginScissorMode(0,76,GetScreenWidth(),GetScreenHeight()-226);
    ar_world(c,e);
    ar_lighting_draw(c);
    ar_air(c,e);
    ar_entity_markers(c,e);
    double world_done=GetTime();
    EndScissorMode();
    ar_hud(c,e);
    ar_overview(c,e);
    double ui_done=GetTime();
    EndDrawing();
    c->render_world_ms=(world_done-render_start)*1000;
    c->render_ui_ms=(ui_done-world_done)*1000;
    c->render_present_ms=(GetTime()-ui_done)*1000;
}
static inline void c_close(ARPG* e) {
    if(!e->client)return;
    ARClient* c=(ARClient*)e->client;
    if(c->initialized) {
        if(c->atlas.id)UnloadTexture(c->atlas);
        if(c->expansion.id)UnloadTexture(c->expansion);
        if(c->companions.texture.id)UnloadTexture(c->companions.texture);
        if(c->keepers.texture.id)UnloadTexture(c->keepers.texture);
        if(c->biomes.texture.id)UnloadTexture(c->biomes.texture);
        if(c->fauna.texture.id)UnloadTexture(c->fauna.texture);
        if(c->lightmap.id)UnloadRenderTexture(c->lightmap);
        if(c->minimap_texture.id)UnloadTexture(c->minimap_texture);
        if(c->map_texture.id)UnloadTexture(c->map_texture);
        UnloadFont(c->font);UnloadFont(c->heading);
    }
    free(c->ground_vertices);free(c->ground_tiles);free(c->drawables);free(c);e->client=NULL;
    if(IsWindowReady())CloseWindow();
}
