#pragma once
// Client-only presentation. World rendering is full resolution; UI never scales
// with the camera. All gameplay, production, and pet tasks live in ar_sim.h.
#include "raylib.h"
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
static const char* AR_PET_NAMES[] = {"Wisp","Fang","Aegis","Porter"};
static const char* AR_TASK_NAMES[] = {"Assist","Gather","Escort","Hunt","Hold","Home"};
static const char* AR_BUILD_NAMES[] = {"Ward tower","Barricade","Extractor"};

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
    Texture2D atlas;
    Rectangle sprites[16];
    Font font,heading;
    int initialized,selected_pet,build_kind,paused,autoplay,pet_policy;
    int ui_summon,ui_toggle,ui_pause,ui_ability;
    int task_override[AR_MAX_PETS];
    int move_target,camera_free;
    float move_x,move_y;
    char notice[160];
    float notice_time,prev_hp,prev_harvest,rate,rate_elapsed,rate_base;
    int prev_tick,prev_builds,prev_nests,prev_pets,prev_level,fx_head;
    float enemy_hp[AR_MAX_ENEMIES];
    ARFeedback fx[AR_FX_COUNT];
} ARClient;

static inline ARClient* ar_client(ARPG* env) {
    if (!env->client) {
        ARClient* c=(ARClient*)calloc(1,sizeof(ARClient));
        c->cam_x=env->px; c->cam_y=env->py; c->zoom=1.0f;
        c->selected_pet=-1; c->build_kind=-1;
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
static inline void ar_text(ARClient* c,const char* text,float x,float y,int size,Color color) {
    DrawTextEx(c->font,text,(Vector2){x,y},(float)size,0.3f,color);
}
static inline void ar_box(Rectangle r,Color fill) {
    DrawRectangleRounded(r,0.16f,5,fill);
    DrawRectangleRoundedLinesEx(r,0.16f,5,1,AR_LINE);
}
static inline int ar_button(ARClient* c,Rectangle r,const char* label,int active,int enabled) {
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
static inline void ar_assets(ARClient* c) {
    Image atlas=LoadImage("ocean/arpg/assets/hearthwild-atlas.png");
    if (atlas.data) {
        c->atlas=LoadTextureFromImage(atlas);
        SetTextureFilter(c->atlas,TEXTURE_FILTER_BILINEAR);
        Color* pixels=LoadImageColors(atlas);
        // Alpha bounds stay inside each atlas cell; the source image is untouched.
        for(int n=0;n<16;n++) {
            int x0=(n%4)*atlas.width/4,x1=(n%4+1)*atlas.width/4;
            int y0=(n/4)*atlas.height/4,y1=(n/4+1)*atlas.height/4;
            int lx=x1,rx=x0,ty=y1,by=y0;
            for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) if(pixels[y*atlas.width+x].a>40) {
                if(x<lx)lx=x; if(x>rx)rx=x; if(y<ty)ty=y; if(y>by)by=y;
            }
            c->sprites[n]=(Rectangle){(float)lx,(float)ty,(float)(rx-lx+1),(float)(by-ty+1)};
        }
        UnloadImageColors(pixels); UnloadImage(atlas);
    }
    c->font=LoadFontEx("resources/shared/Roboto-Regular.ttf",32,NULL,0);
    c->heading=LoadFontEx("resources/shared/Montserrat-Regular.ttf",40,NULL,0);
    SetTextureFilter(c->font.texture,TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(c->heading.texture,TEXTURE_FILTER_BILINEAR);
    c->initialized=1;
    ar_notice(c,"Your porter is already working. Build an extractor beside a crystal seam.");
}
static inline void ar_sprite(ARClient* c,int sprite,Vector2 feet,float height,float flip,Color tint) {
    if(!c->atlas.id) { DrawCircleV(feet,height*0.2f,tint); return; }
    Rectangle src=c->sprites[sprite];
    float width=height*src.width/src.height;
    if(flip<0)src.width=-src.width;
    DrawTexturePro(c->atlas,src,(Rectangle){feet.x-width*0.5f,feet.y-height,width,height},
        (Vector2){0,0},0,tint);
}
static inline void ar_ring(ARClient* c,float x,float y,float r,Color color) {
    Vector2 p=ar_iso(c,x,y,0);
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
        {146,139,101,255},{62,112,115,255},{41,75,88,255}};
    Color c=palette[tile]; int d=noise%7-3;
    c.r=(unsigned char)(c.r+d);c.g=(unsigned char)(c.g+d);c.b=(unsigned char)(c.b+d);
    return c;
}
static inline int ar_drawable_compare(const void* a,const void* b) {
    float d=((const ARDrawable*)a)->depth-((const ARDrawable*)b)->depth;
    return d<0 ? -1 : d>0;
}
static inline void ar_world(ARClient* c,ARPG* e) {
    const float half=e->cfg.arena_size*0.5f,cell=e->cfg.arena_size/AR_DUN_W;
    int width=GetScreenWidth(),height=GetScreenHeight();
    ARDrawable objects[AR_DUN_CELLS+AR_MAX_ENEMIES+AR_MAX_PETS+AR_MAX_OBSTACLES+AR_MAX_SHARDS+AR_MAX_BUILDINGS+AR_MAX_NESTS+2];
    int count=0;
    for(int y=0;y<AR_DUN_H;y++)for(int x=0;x<AR_DUN_W;x++) {
        float wx=-half+(x+0.5f)*cell,wy=-half+(y+0.5f)*cell;
        Vector2 p=ar_iso(c,wx,wy,0);
        if(p.x < -180 || p.x > width+180 || p.y < -30 || p.y > height+150) continue;
        int tile=e->dungeon[y*AR_DUN_W+x];
        uint32_t hash=ar_hash_xy(e->dungeon_seed,x,y);
        ar_tile(c,wx,wy,cell*0.505f,ar_ground(tile,(int)(hash%16)));
        if(tile==AR_TILE_GRASS && hash%6==0) {
            DrawLineEx((Vector2){p.x-3*c->zoom,p.y},(Vector2){p.x,p.y-3*c->zoom},c->zoom,(Color){123,144,91,125});
            DrawLineEx((Vector2){p.x,p.y},(Vector2){p.x+3*c->zoom,p.y-2*c->zoom},c->zoom,(Color){73,104,65,110});
        }
        if((tile==AR_TILE_SHALLOW || tile==AR_TILE_DEEP) && hash%11==0) {
            float sway=sinf(c->time*1.2f+(float)(hash%40))*3;
            DrawLineEx((Vector2){p.x-5+sway,p.y},(Vector2){p.x+7+sway,p.y},1,(Color){141,194,179,75});
        }
        int sprite=-1;float size=0;
        if(tile==AR_TILE_FOREST && hash%5==0){sprite=11;size=100+(hash%25);}
        if(tile==AR_TILE_ROCK && hash%3==0){sprite=12;size=49+(hash%14);}
        if(tile==AR_TILE_GRASS && hash%67==0){sprite=15;size=27;}
        if(sprite>=0)objects[count++]=(ARDrawable){wx+wy,wx,wy,size,sprite,0,0};
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
            e->build_kind[b]==AR_BUILD_WALL ? 44 : 70,7+e->build_kind[b],b,2};
    for(int n=0;n<AR_MAX_NESTS;n++)if(e->nest_active[n]) {
        ar_ring(c,e->nest_x[n],e->nest_y[n],AR_CAMP_WAKE_RADIUS,(Color){202,95,70,90});
        objects[count++]=(ARDrawable){e->nest_x[n]+e->nest_y[n],e->nest_x[n],e->nest_y[n],104,10,n,3};
    }
    for(int i=0;i<e->cfg.enemy_cap;i++)if(e->enemies.active[i])
        objects[count++]=(ARDrawable){e->enemies.x[i]+e->enemies.y[i],e->enemies.x[i],e->enemies.y[i],
            e->enemies.type[i] ? 70 : 43,e->enemies.type[i] ? 6 : 5,i,4};
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p])
        objects[count++]=(ARDrawable){e->pets.x[p]+e->pets.y[p],e->pets.x[p],e->pets.y[p],
            e->pets.kind[p]==AR_PET_AEGIS ? 46 : 37,e->pets.kind[p]+1,p,5};
    objects[count++]=(ARDrawable){e->px+e->py,e->px,e->py,61,0,0,6};
    qsort(objects,(size_t)count,sizeof(objects[0]),ar_drawable_compare);
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
        float flip=1,bob=0;Color tint=WHITE;
        if(d.kind==5) {
            int p=d.slot;
            float speed=fabsf(e->pets.vx[p])+fabsf(e->pets.vy[p]);
            bob=speed>0.2f ? sinf(c->time*11+p)*2*c->zoom : sinf(c->time*2+p)*c->zoom;
            flip=e->pets.vx[p]-e->pets.vy[p] < -0.1f ? -1 : 1;
            if(e->pets.invuln[p]>0 && (e->tick/4)%2)tint=(Color){255,205,174,255};
            ar_ring(c,d.x,d.y,0.9f,c->selected_pet==p ? AR_GOLD : (Color){130,219,187,95});
        }
        if(d.kind==6) {
            bob=(fabsf(e->pvx)+fabsf(e->pvy)>0.2f ? sinf(c->time*11)*1.7f : 0)*c->zoom;
            flip=e->pvx-e->pvy < -0.1f ? -1 : 1;
            ar_ring(c,d.x,d.y,1,AR_GOLD);
            if(e->invuln_timer>0 && (e->tick/4)%2)tint=(Color){255,191,172,255};
        }
        if(d.sprite==11 && ar_geometry_dist2(d.x,d.y,e->px,e->py)<8)tint.a=95;
        if(d.kind==1 && e->shard_value[d.slot]<1)tint=(Color){120,145,135,170};
        float shadow=d.size*0.26f*c->zoom;
        DrawEllipse((int)feet.x,(int)feet.y,shadow,shadow*0.35f,(Color){15,31,29,65});
        ar_sprite(c,d.sprite,(Vector2){feet.x,feet.y-bob},d.size*c->zoom,flip,tint);
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
        if(d.kind==2 && e->build_hp[d.slot]<e->build_max_hp[d.slot])
            ar_bar(feet.x-23*c->zoom,feet.y-d.size*c->zoom-8,46*c->zoom,4,
                e->build_hp[d.slot]/e->build_max_hp[d.slot],AR_GOLD);
        if(d.kind==4 && e->enemies.hp[d.slot]<e->enemies.max_hp[d.slot])
            ar_bar(feet.x-20*c->zoom,feet.y-d.size*c->zoom-8,40*c->zoom,4,
                e->enemies.hp[d.slot]/e->enemies.max_hp[d.slot],AR_RED);
        if(d.kind==5 && (c->selected_pet==d.slot || e->pets.hp[d.slot]<e->pets.max_hp[d.slot]*0.9f))
            ar_bar(feet.x-17*c->zoom,feet.y-d.size*c->zoom-8,34*c->zoom,4,
                e->pets.hp[d.slot]/e->pets.max_hp[d.slot],AR_MINT);
        if(d.kind==3) {
            ar_bar(feet.x-32*c->zoom,feet.y-d.size*c->zoom-12,64*c->zoom,4,
                e->nest_hp[d.slot]/e->nest_max_hp[d.slot],AR_RED);
            ar_text(c,"THORN CAMP",feet.x-43,feet.y-d.size*c->zoom-34,13,AR_RED);
        }
        if(e->show_hitboxes && d.kind>=4) ar_ring(c,d.x,d.y,d.kind==6 ? e->cfg.player_radius : 0.5f,AR_RED);
    }
    if(e->fx_nova>0)ar_ring(c,e->px,e->py,e->cfg.nova_radius*(1-e->fx_nova/0.4f),Fade(AR_GOLD,e->fx_nova/0.4f));
    if(e->fx_frost>0)ar_ring(c,e->px,e->py,e->cfg.frost_range*(1-e->fx_frost/0.5f),Fade(AR_MINT,e->fx_frost/0.5f));
    if(e->fx_dash>0)ar_ring(c,e->px,e->py,1+(1-e->fx_dash/0.35f)*2,Fade(AR_TEXT,e->fx_dash/0.35f));
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
        ar_sprite(c,7+c->build_kind,GetMousePosition(),70*c->zoom,1,Fade(valid ? WHITE : AR_RED,0.55f));
        if(c->build_kind==AR_BUILD_HARVESTER)ar_ring(c,world.x,world.y,e->cfg.harvest_radius,Fade(AR_MINT,0.4f));
    }
    for(int i=0;i<AR_FX_COUNT;i++)if(c->fx[i].life>0) {
        ARFeedback* f=&c->fx[i];
        Vector2 p=ar_iso(c,f->x,f->y,0.9f+(1.1f-f->life));
        ar_text(c,TextFormat("%+.0f",f->value),p.x-8,p.y,19,Fade(f->color,fminf(1,f->life*2)));
    }
}
static inline void ar_minimap(ARClient* c,ARPG* e,float x,float y) {
    float size=144,cell=size/AR_DUN_W;
    ar_box((Rectangle){x-9,y-9,size+18,size+42},AR_PANEL);
    for(int gy=0;gy<AR_DUN_H;gy++)for(int gx=0;gx<AR_DUN_W;gx++)
        DrawRectangleRec((Rectangle){x+gx*cell,y+gy*cell,cell+0.1f,cell+0.1f},ar_ground(e->dungeon[gy*AR_DUN_W+gx],3));
    for(int n=0;n<AR_MAX_NESTS;n++)if(e->nest_active[n])
        DrawCircleV((Vector2){x+(e->nest_x[n]/e->cfg.arena_size+0.5f)*size,y+(e->nest_y[n]/e->cfg.arena_size+0.5f)*size},3,AR_RED);
    DrawCircleV((Vector2){x+(e->home_x/e->cfg.arena_size+0.5f)*size,y+(e->home_y/e->cfg.arena_size+0.5f)*size},4,AR_GOLD);
    DrawCircleV((Vector2){x+(e->px/e->cfg.arena_size+0.5f)*size,y+(e->py/e->cfg.arena_size+0.5f)*size},3,WHITE);
    ar_text(c,"THE GREEN REACH",x+6,y+size+8,12,AR_MUTED);
}
static inline void ar_hud(ARClient* c,ARPG* e) {
    int w=GetScreenWidth(),h=GetScreenHeight(); float bottom=h-150;
    DrawRectangle(0,0,w,76,AR_INK); DrawLine(0,75,w,75,AR_LINE);
    DrawTextEx(c->heading,"HEARTHWILD",(Vector2){24,14},27,1.5f,AR_TEXT);
    ar_text(c,"A LITTLE KINGDOM, AT YOUR PACE",25,47,11,AR_MUTED);
    ar_text(c,"KEEPER",300,16,12,AR_MUTED);
    ar_bar(300,39,146,8,e->hp/e->max_hp,AR_MINT);
    ar_text(c,TextFormat("%.0f / %.0f",e->hp,e->max_hp),456,31,16,AR_TEXT);
    ar_text(c,"AETHER",550,14,12,AR_MUTED);
    ar_text(c,TextFormat("%.0f",e->shards),550,34,25,AR_GOLD);
    ar_text(c,TextFormat("+%.1f / min",c->rate),616,39,14,AR_MINT);
    ar_text(c,TextFormat("HOMESTEAD %d",ar_tech_level(e,0)+1),754,14,12,AR_MUTED);
    ar_text(c,TextFormat("%d / %d camps cleared",e->camps_cleared,e->camps_cleared+e->nests_alive),754,39,16,AR_TEXT);
    ar_bar(754,63,155,3,fmodf(e->harvested,20)/20,AR_GOLD);
    if(ar_button(c,(Rectangle){w-212,17,119,40},c->autoplay ? "T  RL autoplay" : "T  Manual",c->autoplay,1))c->ui_toggle=1;
    if(ar_button(c,(Rectangle){w-81,17,58,40},c->paused ? "Play" : "Pause",c->paused,1))c->ui_pause=1;
    ar_minimap(c,e,w-172,97);
    ar_box((Rectangle){23,97,298,103},Fade(AR_PANEL,0.94f));
    ar_text(c,"YOUR NEXT LITTLE PROJECT",38,109,11,AR_GOLD);
    int extractors=0;
    for(int b=0;b<AR_MAX_BUILDINGS;b++)if(e->build_active[b] && e->build_kind[b]==AR_BUILD_HARVESTER)extractors++;
    ar_text(c,!e->nests_alive ? "The Reach is yours." : extractors ? "Let the workshop do the work." : "Make your first extractor.",38,132,19,AR_TEXT);
    ar_text(c,!e->nests_alive ? "Keep growing your little kingdom." : extractors ? "Explore a red camp when you're ready." : "Press B, then click beside a crystal seam.",38,160,14,AR_MUTED);
    ar_text(c,"Shelter restores you and your companions.",38,181,12,AR_MUTED);
    if(c->notice_time>0) {
        Vector2 m=MeasureTextEx(c->font,c->notice,17,0.3f);
        float nx=(w-m.x-36)*0.5f;
        ar_box((Rectangle){nx,bottom-59,m.x+36,40},AR_PANEL);
        ar_text(c,c->notice,nx+18,bottom-49,17,AR_GOLD);
    }
    DrawRectangle(0,(int)bottom,w,150,AR_INK); DrawLine(0,(int)bottom,w,(int)bottom,AR_LINE);
    ar_text(c,c->pet_policy ? "COMPANIONS  /  RL POLICY" : "COMPANIONS  /  SCRIPTED ASSIST",24,bottom+12,12,AR_MUTED);
    for(int p=0;p<AR_MAX_PETS;p++) {
        Rectangle card={24+p*139.0f,bottom+35,130,72};
        int alive=e->pets.active[p],selected=c->selected_pet==p;
        if(ar_button(c,card,"",selected,alive))c->selected_pet=selected ? -1 : p;
        if(!alive){ar_text(c,"Empty slot",card.x+24,card.y+27,14,AR_MUTED);continue;}
        ar_sprite(c,e->pets.kind[p]+1,(Vector2){card.x+25,card.y+45},34,1,WHITE);
        ar_text(c,AR_PET_NAMES[e->pets.kind[p]],card.x+50,card.y+10,15,AR_TEXT);
        int task=e->pets.task[p];
        const char* job=task==AR_TASK_AUTO ? (e->pets.kind[p]==AR_PET_MULE ? "Auto gather" : "Auto escort") : AR_TASK_NAMES[task];
        ar_text(c,job,card.x+50,card.y+31,12,AR_MUTED);
        ar_bar(card.x+12,card.y+59,106,4,e->pets.hp[p]/e->pets.max_hp[p],AR_MINT);
    }
    float bx=604;
    if(c->selected_pet>=0) {
        ar_text(c,"ASSIGN TASK  /  CLICK A CARD TO DESELECT",bx,bottom+12,12,AR_MUTED);
        for(int t=0;t<AR_PET_TASK_COUNT;t++) {
            if(ar_button(c,(Rectangle){bx+(t%3)*100,bottom+35+(t/3)*37,92,31},AR_TASK_NAMES[t],
                    c->task_override[c->selected_pet]==t,1)) {
                c->task_override[c->selected_pet]=t;
                ar_notice(c,"Task assigned. P clears overrides and restores the pet policy.");
            }
        }
    } else {
        ar_text(c,"BUILD YOUR HOMESTEAD",bx,bottom+12,12,AR_MUTED);
        const char* build_keys[]={"G","V","B"};
        for(int b=0;b<3;b++) {
            char label[64];snprintf(label,sizeof(label),"%s  %s  %.0f",build_keys[b],AR_BUILD_NAMES[b],e->cfg.build_cost[b]);
            int unlocked=b!=AR_BUILD_HARVESTER || ar_tech_level(e,0)>=e->cfg.unlock_level_harvester;
            if(ar_button(c,(Rectangle){bx,bottom+33+b*27,273,24},label,c->build_kind==b,
                    !c->autoplay && unlocked && e->builds_alive<AR_MAX_BUILDINGS && e->shards>=e->cfg.build_cost[b]))
                c->build_kind=c->build_kind==b ? -1 : b;
        }
    }
    ar_text(c,"KEEPER ABILITIES",914,bottom+12,12,AR_MUTED);
    const char* ability_names[]={"Q  Dash","E  Nova","F  Frost"};
    float cds[]={e->dash_cd,e->nova_cd,e->frost_cd};
    float periods[]={e->cfg.dash_cooldown,e->cfg.nova_cooldown,e->cfg.frost_cooldown};
    for(int a=0;a<3;a++) {
        Rectangle r={914,bottom+33+a*27,138,24};
        const char* label=cds[a]>0 ? TextFormat("%s  %.1fs",ability_names[a],cds[a]) : ability_names[a];
        if(ar_button(c,r,label,0,!c->autoplay && cds[a]<=0))c->ui_ability=a+1;
        if(cds[a]>0 && periods[a]>0)ar_bar(r.x+5,r.y+22,r.width-10,2,1-cds[a]/periods[a],AR_MINT);
    }
    float sx=w-210;
    ar_text(c,"SUMMON COMPANION",sx,bottom+12,12,AR_MUTED);
    for(int p=0;p<4;p++) {
        int unlocked=p==0 || p==3 || ar_tech_level(e,0)>=(p==1 ? e->cfg.unlock_level_fang : e->cfg.unlock_level_aegis);
        if(ar_button(c,(Rectangle){sx+(p%2)*94,bottom+35+(p/2)*37,86,31},
                TextFormat("%s %.0f",AR_PET_NAMES[p],e->cfg.summon_cost[p]),0,
                !c->autoplay && unlocked && e->summon_cd<=0 && e->pets_alive<e->cfg.pet_cap && e->shards>=e->cfg.summon_cost[p]))c->ui_summon=p+1;
    }
    ar_text(c,"WASD move   Right-click rally   1 Follow / 2 Advance / 3 Hold / 4 Focus   Wheel zoom   Middle-drag pan   Home recenter   Tab pause   P pet brain   R restart",24,h-26,13,AR_MUTED);
    if(c->paused) {
        ar_box((Rectangle){w*0.5f-125,95,250,49},AR_PANEL);
        ar_text(c,e->hp<=0 ? "RUN ENDED  /  R TO RESTART" : "PAUSED  /  TAB TO RESUME",w*0.5f-109,112,14,AR_GOLD);
    }
}
static inline void c_render(ARPG* e) {
    if(!IsWindowReady()) {
        SetConfigFlags(FLAG_MSAA_4X_HINT|FLAG_WINDOW_RESIZABLE);
        InitWindow(1440,900,"Hearthwild / ARPG");
        SetWindowMinSize(1280,720);SetTargetFPS(60);
    }
    ARClient* c=ar_client(e);
    if(!c->initialized)ar_assets(c);
    float dt=fminf(GetFrameTime(),0.1f); c->time+=dt;
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
    if(e->builds_alive>c->prev_builds)ar_notice(c,"Outpost established. Your network is growing.");
    if(e->nests_alive<c->prev_nests)ar_notice(c,"Camp cleared. A little more of the Reach is yours.");
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
    if(GetMousePosition().y>76 && GetMousePosition().y<GetScreenHeight()-150)
        c->zoom=ar_clampf(c->zoom+GetMouseWheelMove()*0.09f,0.55f,1.65f);
    if(IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta=GetMouseDelta();float scale=AR_VIEW_SCALE*c->zoom;
        c->cam_x-=(delta.x+2*delta.y)/(2*scale);
        c->cam_y-=(2*delta.y-delta.x)/(2*scale);
        c->cam_x=ar_clampf(c->cam_x,-e->cfg.arena_size*0.5f,e->cfg.arena_size*0.5f);
        c->cam_y=ar_clampf(c->cam_y,-e->cfg.arena_size*0.5f,e->cfg.arena_size*0.5f);
        c->camera_free=1;
    }
    if(IsKeyPressed(KEY_HOME))c->camera_free=0;
    float blend=1-expf(-7*dt);
    if(!c->camera_free){c->cam_x+=(e->px-c->cam_x)*blend;c->cam_y+=(e->py-c->cam_y)*blend;}
    c->off_x=GetScreenWidth()*0.5f-(c->cam_x-c->cam_y)*AR_VIEW_SCALE*c->zoom;
    c->off_y=(GetScreenHeight()-150+76)*0.5f-(c->cam_x+c->cam_y)*AR_VIEW_SCALE*c->zoom;
    BeginDrawing(); ClearBackground((Color){40,60,52,255});
    BeginScissorMode(0,76,GetScreenWidth(),GetScreenHeight()-226);
    ar_world(c,e);
    EndScissorMode();
    ar_hud(c,e);
    EndDrawing();
}
static inline void c_close(ARPG* e) {
    if(!e->client)return;
    ARClient* c=(ARClient*)e->client;
    if(c->initialized) {
        if(c->atlas.id)UnloadTexture(c->atlas);
        UnloadFont(c->font);UnloadFont(c->heading);
    }
    free(c);e->client=NULL;
    if(IsWindowReady())CloseWindow();
}
