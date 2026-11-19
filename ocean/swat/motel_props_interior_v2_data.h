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
    PIV2_CART,PIV2_SHELF,PIV2_DRAPES_B,SWAT_PROPS_V2_ASSETS
};
static const char* const props_v2_files[SWAT_PROPS_V2_ASSETS]={
    "drapes.glb","dresser.glb","mini_fridge.glb","coffee_maker.glb","ashtray.glb","wall_heater.glb",
    "closet_rod.glb","smoke_detector.glb","waiting_chairs.glb","potted_plant.glb","water_cooler.glb",
    "brochure_rack.glb","laundry_cart.glb","linen_shelf.glb","drapes_avocado.glb"};

// World-space mount (metres, engine yaw; model front faces local +Z).
// `owner` is the supporting motel object; `wall` mounts follow the surviving
// section of that wall group.
typedef struct SwatPropV2Piece {int asset,owner;float x,y,z,yaw;bool wall;} SwatPropV2Piece;
#define PIV2_ROOM(r) (-6.f+4.f*(r))
static const SwatPropV2Piece props_v2_pieces[]={
    // Guest rooms 101-104: drapes on the facade above the window (wall
    // mount, origin on the interior plane, local +x = west).
    {PIV2_DRAPES,13,PIV2_ROOM(0)+.8f,2.5f,-.1f,SWAT_PI,true},
    {PIV2_DRAPES_B,37,PIV2_ROOM(1)+.8f,2.5f,-.1f,SWAT_PI,true},
    {PIV2_DRAPES,61,PIV2_ROOM(2)+.8f,2.5f,-.1f,SWAT_PI,true},
    {PIV2_DRAPES_B,85,PIV2_ROOM(3)+.8f,2.5f,-.1f,SWAT_PI,true},
    // Baseboard heater under each window, between the drape stacks
    // (floor piece, back on the facade's interior plane).
    {PIV2_HEATER,9,PIV2_ROOM(0)+.75f,.0f,-.095f,SWAT_PI,false},
    {PIV2_HEATER,33,PIV2_ROOM(1)+.75f,.0f,-.095f,SWAT_PI,false},
    {PIV2_HEATER,57,PIV2_ROOM(2)+.75f,.0f,-.095f,SWAT_PI,false},
    {PIV2_HEATER,81,PIV2_ROOM(3)+.75f,.0f,-.095f,SWAT_PI,false},
    // Guest-room furniture, varied per room. East wall (party wall, face
    // x = X+1.905): the luggage rack stands at z -0.47..-0.83, the bed edge at
    // X+1.35 leaves a 0.53 m dead strip beside it. West wall (face X-1.905):
    // the entrance door opens flat against it up to its bumper at z -0.95;
    // the desk starts at z -1.74, so the nook between holds a 0.5 m fridge.
    // Open closet (shelf, rod, hangers) over the luggage rack: 101, 102.
    {PIV2_CLOSET,106,PIV2_ROOM(0)+1.95f,.0f,-.82f,-SWAT_PI*.5f,true},
    {PIV2_CLOSET,107,PIV2_ROOM(1)+1.95f,.0f,-.82f,-SWAT_PI*.5f,true},
    // Low dresser with its tall mirror beside the bed: 101, 103, 104.
    {PIV2_DRESSER,9,PIV2_ROOM(0)+1.905f,.0f,-2.02f,-SWAT_PI*.5f,false},
    {PIV2_DRESSER,57,PIV2_ROOM(2)+1.905f,.0f,-1.95f,-SWAT_PI*.5f,false},
    {PIV2_DRESSER,81,PIV2_ROOM(3)+1.905f,.0f,-1.95f,-SWAT_PI*.5f,false},
    // Mini fridge in the west nook, coffee maker on top: 102, 103, 104.
    {PIV2_FRIDGE,33,PIV2_ROOM(1)-1.905f,.0f,-1.33f,SWAT_PI*.5f,false},
    {PIV2_FRIDGE,57,PIV2_ROOM(2)-1.905f,.0f,-1.33f,SWAT_PI*.5f,false},
    {PIV2_FRIDGE,81,PIV2_ROOM(3)-1.905f,.0f,-1.33f,SWAT_PI*.5f,false},
    {PIV2_COFFEE,33,PIV2_ROOM(1)-1.86f,.85f,-1.27f,SWAT_PI*.5f+.15f,false},
    {PIV2_COFFEE,81,PIV2_ROOM(3)-1.86f,.85f,-1.40f,SWAT_PI*.5f-.2f,false},
    // Ashtrays (tabletop): free corner of the desk in 101/103 (102/104
    // carry the beer bottles there), the reception counter beside the phone
    // and the laundry desk. Owners must be motel instances (this hook runs
    // in the instance draw path; separately built props such as the Room 104
    // nightstand are drawn elsewhere).
    {PIV2_ASHTRAY,24,PIV2_ROOM(0)-1.22f,.7583f,-2.96f,.4f,false},
    {PIV2_ASHTRAY,72,PIV2_ROOM(2)-1.22f,.7606f,-2.96f,-.9f,false},
    {PIV2_ASHTRAY,121,-10.45f,1.091f,-3.47f,.2f,false},
    {PIV2_ASHTRAY,135,-11.55f,.752f,-7.22f,2.0f,false},
    // Reception lobby (customer side of the counter, x -11.88..-8.12,
    // z -0.12..-3.2). The east wall is a dead end: water cooler in the front
    // corner, tandem waiting chairs under the clock, a snake plant by the
    // counter end. Brochure rack on the west wall between the entrance
    // door's swing (z > -0.94) and the noticeboard (z < -2.24).
    {PIV2_COOLER,2,-8.10f,.0f,-.48f,-SWAT_PI*.5f,false},
    {PIV2_WAITING,2,-8.10f,.0f,-1.55f,-SWAT_PI*.5f,false},
    {PIV2_PLANT,2,-8.10f,.0f,-2.72f,-SWAT_PI*.5f,false},
    {PIV2_BROCHURE,104,-11.95f,.75f,-1.55f,SWAT_PI*.5f,true},
    // Laundry: linen shelving in the nook between the north wall and the
    // desk (west wall), canvas cart against the east wall north of the
    // service opening (z -7.37..-8.63 stays clear).
    {PIV2_SHELF,127,-11.905f,.0f,-6.55f,SWAT_PI*.5f,false},
    {PIV2_CART,127,-8.12f,.0f,-6.76f,-SWAT_PI*.5f,false},
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
