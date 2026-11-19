// Briar Court interior props, layer v2 (swat/art-props-interior). Render-only,
// compiled into environment_art.c after motel_interior_draw.
//
// Assets: assets/environment/motel_props_int_v2/*.glb, built by the
// deterministic Blender scripts tools/blender/props_int_v2_*.py (see the
// folder README for each prop's reference photograph, sources and licenses).
//
// Contract (same as the interior pass, see ENVIRONMENT_ART.md):
// - Every piece is presentation-only. It inherits an existing support (a
//   floor, desk, counter, roof or wall section) and disappears with it.
//   Wall pieces follow the surviving skin section of their wall group.
// - No colliders, navigation obstacles, AI occlusion, cover, objects, wire
//   fields or replay state are added. Simulation, protocol and determinism are
//   untouched, so the layer can be switched off per client.
// - Floor pieces stand against walls in dead corners, outside every doorway,
//   door swing and the mission/NPC routes (checked by the navigation, motel
//   and interior capture tests).
// SWAT_PROPS_INTERIOR_V2=0 disables the whole layer; a missing GLB drops
// only that piece.
#ifndef SWAT_MOTEL_PROPS_INTERIOR_V2_DATA_H
#define SWAT_MOTEL_PROPS_INTERIOR_V2_DATA_H

enum {
    PIV2_DRAPES,PIV2_DRESSER,PIV2_FRIDGE,PIV2_COFFEE,PIV2_ASHTRAY,PIV2_HEATER,
    PIV2_CLOSET,PIV2_SMOKE,PIV2_WAITING,PIV2_PLANT,PIV2_COOLER,PIV2_BROCHURE,
    PIV2_CART,PIV2_SHELF,SWAT_PROPS_V2_ASSETS
};
static const char* const props_v2_files[SWAT_PROPS_V2_ASSETS]={
    "drapes.glb","dresser.glb","mini_fridge.glb","coffee_maker.glb","ashtray.glb","wall_heater.glb",
    "closet_rod.glb","smoke_detector.glb","waiting_chairs.glb","potted_plant.glb","water_cooler.glb",
    "brochure_rack.glb","laundry_cart.glb","linen_shelf.glb"};

// World-space mount (metres, engine yaw; model front faces local +Z).
// `owner` is the supporting motel object; `wall` mounts follow the surviving
// section of that wall group.
typedef struct SwatPropV2Piece {int asset,owner;float x,y,z,yaw;bool wall;} SwatPropV2Piece;
#define PIV2_ROOM(r) (-6.f+4.f*(r))
static const SwatPropV2Piece props_v2_pieces[]={
    {PIV2_SMOKE,10,-6.0f,2.8f,-1.2f,0,false},
};
#define SWAT_PROPS_V2_PIECES ((int)(sizeof(props_v2_pieces)/sizeof(props_v2_pieces[0])))

typedef struct SwatPropsV2Cache {
    const SwatEnvironmentArt* art;
    Model models[SWAT_PROPS_V2_ASSETS];
    float normal_scale[SWAT_PROPS_V2_ASSETS][SWAT_ROOM101_MATERIALS];
} SwatPropsV2Cache;
// One cache per live view (main, planning, sniper, device); loaded lazily on
// the first motel draw of that view and released with it.
#define SWAT_PROPS_V2_VIEWS 8
static SwatPropsV2Cache props_v2_cache[SWAT_PROPS_V2_VIEWS];

static bool props_v2_enabled(void) {
    const char* enabled=getenv("SWAT_PROPS_INTERIOR_V2");
    return !(enabled && !strcmp(enabled,"0"));
}
static SwatPropsV2Cache* props_v2_get(const SwatEnvironmentArt* art) {
    SwatPropsV2Cache* free_slot=NULL;
    for(int i=0;i<SWAT_PROPS_V2_VIEWS;i++) {
        if(props_v2_cache[i].art==art)return &props_v2_cache[i];
        if(!props_v2_cache[i].art && !free_slot)free_slot=&props_v2_cache[i];
    }
    if(!free_slot)return NULL;
    free_slot->art=art;
    for(int i=0;i<SWAT_PROPS_V2_ASSETS;i++) {
        char file[256];snprintf(file,sizeof(file),"motel_props_int_v2/%s",props_v2_files[i]);
        if(!room101_model_load(&free_slot->models[i],free_slot->normal_scale[i],file)) {
            swat_art_model_close(free_slot->models[i]);free_slot->models[i]=(Model){0};
        }
    }
    return free_slot;
}
static void motel_props_v2_close(const SwatEnvironmentArt* art) {
    for(int i=0;i<SWAT_PROPS_V2_VIEWS;i++)if(props_v2_cache[i].art==art) {
        for(int k=0;k<SWAT_PROPS_V2_ASSETS;k++)swat_art_model_close(props_v2_cache[i].models[k]);
        memset(&props_v2_cache[i],0,sizeof(props_v2_cache[i]));
    }
}
// Pieces supported by this object.
static void motel_props_v2_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(art->location!=1 || !world->motel || !o->active || !props_v2_enabled())return;
    const SwatPropsV2Cache* c=NULL;
    for(int i=0;i<SWAT_PROPS_V2_PIECES;i++) {
        const SwatPropV2Piece* q=&props_v2_pieces[i];
        if(q->owner>=world->count)continue;
        if(q->wall) {
            if(q->owner!=o->tag.index && (o->tag.index<=SWAT_MOTEL_INSTANCES || o->part!=SWAT_PART_SKIN ||
               !o->wall_group || o->wall_group!=world->objects[q->owner].wall_group))continue;
        } else if(q->owner!=o->tag.index)continue;
        b3Pos origin={q->x,q->y,q->z};
        if(q->wall && swat_motel_wall_mount(world,q->owner,&origin)!=o->tag.index)continue;
        if(!c && !(c=props_v2_get(art)))return;
        const Model* model=&c->models[q->asset];if(!model->meshCount)continue;
        SwatMotelInstance mount={.origin=origin,.scale={1,1,1},.yaw=q->yaw};
        location_draw(art,o,&mount,model,shadow,cutaway,false);
    }
}

#endif
