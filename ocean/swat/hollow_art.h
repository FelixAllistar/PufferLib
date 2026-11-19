// The Hollow (SWAT_HOLLOW): presentation layer. Included once by
// environment_art.c after textured_box/room101_model_load. See HOLLOW.md and
// assets/environment/hollow_v1/README.md.
//
// 1. Material sets (CC0 Poly Haven scans, hollow_v1/materials) replace the
//    default surface maps on the authoritative boxes. The boxed-house boards
//    ARE the collision boxes, so breaks, cuts and fragments stay exact.
// 2. Blender-made GLBs (tools/blender/hollow_*.py): site scenery (terrain,
//    woods, trim, roofs) at identity, and props from hollow_data.h at their
//    placement. A prop's collider box is hidden when its model is present.
// Nothing here touches simulation, navigation, protocol or determinism.
// SWAT_HOLLOW_ART=0 falls back to the default box materials.
#include "hollow_data.h"
#include "hollow_woods.h" // Woods v2 (SWAT_HOLLOW_WOODS).

enum { HOLLOW_MAT_PLANKS, HOLLOW_MAT_DOOR, HOLLOW_MAT_FLOOR, HOLLOW_MAT_ROOF, HOLLOW_MAT_STONE,
       HOLLOW_MAT_GROUND, HOLLOW_MAT_DIRT, HOLLOW_MAT_ROOF_DARK, HOLLOW_MAT_LIMEWASH, HOLLOW_MAT_COUNT };
static const struct { const char* name; float tile; } hollow_mat_defs[HOLLOW_MAT_COUNT]={
    [HOLLOW_MAT_PLANKS]={"planks_grey",1.6f},   // wood_planks_grey: weathered vertical boards
    [HOLLOW_MAT_DOOR]={"planks_old",1.2f},      // old_planks_02: door leaves, interior trim
    [HOLLOW_MAT_FLOOR]={"floor_boards",2.0f},   // old_wood_floor
    [HOLLOW_MAT_ROOF]={"roof_tin",1.8f},        // rusty_corrugated_iron
    [HOLLOW_MAT_STONE]={"field_stone",2.0f},    // old_stone_wall
    [HOLLOW_MAT_GROUND]={"yard_ground",4.0f},   // brown_mud_leaves_01
    [HOLLOW_MAT_DIRT]={"dirt_floor",2.5f},      // dirt_floor: barn, smokehouse, cellar
    [HOLLOW_MAT_ROOF_DARK]={"roof_tin_dark",1.8f}, // rusty_corrugated_iron, graded darker/greyer: house roofs
    [HOLLOW_MAT_LIMEWASH]={"limewash",2.0f},     // old_stone_wall, greyed and lightened: limewashed cellar walls
};
// Scenery GLBs drawn at identity (world space, metres).
static const char* const hollow_scene_files[]={"hollow_terrain","hollow_woods","hollow_house_trim","hollow_outbuildings_trim","hollow_cellar_trim","hollow_barn_interior"};
#define HOLLOW_SCENE_FILES (int)(sizeof(hollow_scene_files)/sizeof(hollow_scene_files[0]))
#define HOLLOW_MODEL_SLOTS 48

typedef struct SwatHollowArt {
    SwatSurfaceMaps maps[HOLLOW_MAT_COUNT];
    Model scene[HOLLOW_SCENE_FILES];
    float scene_scales[HOLLOW_SCENE_FILES][SWAT_ROOM101_MATERIALS];
    BoundingBox scene_bounds[HOLLOW_SCENE_FILES];
    const char* names[HOLLOW_MODEL_SLOTS];
    Model models[HOLLOW_MODEL_SLOTS];
    float scales[HOLLOW_MODEL_SLOTS][SWAT_ROOM101_MATERIALS];
    BoundingBox bounds[HOLLOW_MODEL_SLOTS];
    int model_count;
    signed char prop_model[SWAT_HOLLOW_PROPS]; // model slot per placement row, -1 none
    bool stand_model; // the lane tree stand replaces its overwatch plinth
    SwatHollowWoods* woods; // instanced woods v2; NULL draws the old hollow_woods GLB
} SwatHollowArt;

static bool hollow_art_enabled(void) {
    const char* e=getenv("SWAT_HOLLOW_ART");
    return !e || strcmp(e,"0");
}
static void hollow_art_close(SwatEnvironmentArt* art) {
    SwatHollowArt* h=art->hollow; if(!h) return;
    for(int i=0;i<HOLLOW_MAT_COUNT;i++) {
        Texture2D t[3]={h->maps[i].color,h->maps[i].normal,h->maps[i].roughness};
        for(int k=0;k<3;k++) if(t[k].id) UnloadTexture(t[k]);
    }
    for(int i=0;i<HOLLOW_SCENE_FILES;i++) swat_art_model_close(h->scene[i]);
    for(int i=0;i<h->model_count;i++) swat_art_model_close(h->models[i]);
    hollow_woods_close(h->woods);
    free(h); art->hollow=NULL;
}
static void hollow_art_load(SwatEnvironmentArt* art) {
    hollow_art_close(art);
    if(!hollow_art_enabled()) return;
    SwatHollowArt* h=calloc(1,sizeof(*h)); if(!h) return;
    const char* pbr=getenv("SWAT_ENVIRONMENT_PBR");
    for(int i=0;i<HOLLOW_MAT_COUNT;i++) {
        SwatSurfaceMaps* m=&h->maps[i]; char file[256];
        snprintf(file,sizeof(file),"hollow_v1/materials/%s_basecolor.jpg",hollow_mat_defs[i].name); m->color=load_surface(file,true);
        if(m->color.id && (!pbr || strcmp(pbr,"0"))) {
            snprintf(file,sizeof(file),"hollow_v1/materials/%s_normal.jpg",hollow_mat_defs[i].name); m->normal=load_surface(file,true);
            snprintf(file,sizeof(file),"hollow_v1/materials/%s_roughness.jpg",hollow_mat_defs[i].name); m->roughness=load_surface(file,true);
        }
        Texture2D* t[3]={&m->color,&m->normal,&m->roughness};
        for(int k=0;k<3;k++) if(t[k]->id) {
            GenTextureMipmaps(t[k]); SetTextureFilter(*t[k],TEXTURE_FILTER_ANISOTROPIC_8X); SetTextureWrap(*t[k],TEXTURE_WRAP_REPEAT);
        }
        m->tile=(Vector2){hollow_mat_defs[i].tile,hollow_mat_defs[i].tile};
    }
    h->woods=hollow_woods_load();
    for(int i=0;i<HOLLOW_SCENE_FILES;i++) {
        if(h->woods && !strcmp(hollow_scene_files[i],"hollow_woods")) continue; // replaced by woods v2
        char file[160]; snprintf(file,sizeof(file),"hollow_v1/%s.glb",hollow_scene_files[i]);
        if(!room101_model_load(&h->scene[i],h->scene_scales[i],file)) { swat_art_model_close(h->scene[i]); h->scene[i]=(Model){0}; continue; }
        h->scene_bounds[i]=GetModelBoundingBox(h->scene[i]);
    }
    for(int i=0;i<SWAT_HOLLOW_PROPS;i++) {
        const char* name=swat_hollow_props[i].model; h->prop_model[i]=-1;
        if(!name) continue;
        int slot=-1;
        for(int j=0;j<h->model_count;j++) if(!strcmp(h->names[j],name)) slot=j;
        if(slot<0 && h->model_count<HOLLOW_MODEL_SLOTS) {
            slot=h->model_count++; h->names[slot]=name;
            char file[160]; snprintf(file,sizeof(file),"hollow_v1/props/%s.glb",name);
            if(room101_model_load(&h->models[slot],h->scales[slot],file)) h->bounds[slot]=GetModelBoundingBox(h->models[slot]);
            else { swat_art_model_close(h->models[slot]); h->models[slot]=(Model){0}; }
        }
        if(slot>=0 && h->models[slot].meshCount) h->prop_model[i]=(signed char)slot;
        if(h->prop_model[i]>=0 && !strcmp(name,"tree_stand")) h->stand_model=true;
    }
    art->hollow=h;
}
static int hollow_prop_row(const SwatObject* o) {
    if(o->part!=SWAT_PART_OBSTACLE) return -1;
    for(int i=0;i<SWAT_HOLLOW_PROPS;i++) {
        const SwatHollowProp* p=&swat_hollow_props[i];
        if(p->solid && fabsf((float)o->center.x-p->x)<.01f && fabsf((float)o->center.z-p->z)<.01f &&
           fabsf((float)o->center.y-(p->y+p->hy))<.01f) return i;
    }
    return -1;
}
// Box materials. Returns false to leave the object to the default path.
static bool hollow_box_draw(const SwatEnvironmentArt* art,const SwatObject* o) {
    const SwatHollowArt* h=art->hollow;
    if(!h || (o->door && (o->latch_broken || o->cuts))) return false;
    if(o->part==SWAT_PART_LIGHT) {
        // A bare bulb on a cord, not a fixture box: the cord rises to the
        // ceiling, the bulb hangs at the lamp origin (swat_world_room_light).
        swat_lighting_surface(art->lighting,(Texture2D){0},(Texture2D){0},(Vector3){0},(Vector2){0},false);
        DrawCubeV((Vector3){0,o->half.y+.06f,0},(Vector3){.008f,.14f,.008f},(Color){20,18,16,255});
        DrawCubeV((Vector3){0,o->half.y-.005f,0},(Vector3){.03f,.03f,.03f},(Color){60,58,52,255});
        DrawSphereEx((Vector3){0,-.01f,0},.032f,8,8,(Color){255,236,190,255});
        return true;
    }
    if(o->material==SWAT_GLASS) {
        if(o->part==SWAT_PART_LIGHT || o->fractured || o->cuts) return false;
        // Old, filmed-over window glass: dark and nearly opaque at dusk.
        swat_lighting_surface(art->lighting,(Texture2D){0},(Texture2D){0},(Vector3){0},(Vector2){0},false);
        DrawCubeV((Vector3){0},(Vector3){2*o->half.x,2*o->half.y,2*o->half.z},(Color){30,33,31,225});
        return true;
    }
    // Stepped gable fill (collision): the trim GLB draws the true triangle.
    if(o->material==SWAT_WOOD && o->part==SWAT_PART_SKIN && !o->wall_group && o->center.y>2.2f && !o->fractured && !o->cuts) return true;
    // Overwatch plinths (mission.c pattern): the tree stand model replaces the lane one.
    if(o->material==SWAT_STONE && o->part==SWAT_PART_SOLID && fabsf(o->half.x-.85f)<.001f && fabsf(o->half.z-.85f)<.001f &&
       fabsf((float)o->center.x+4)<.01f && fabsf((float)o->center.z+12)<.01f && h->stand_model) return true;
    int row=hollow_prop_row(o);
    if(row>=0 && h->prop_model[row]>=0) return true; // drawn by its model
    if(row>=0 && !swat_hollow_props[row].model && o->material==SWAT_CLOTH) return true; // foliage volume of a modelled berm
    int id=-1,grain=-1;
    bool flat=o->half.y<=.16f && o->half.x>.35f && o->half.z>.35f && o->pitch==0;
    switch(o->material) {
    case SWAT_WOOD:
        if(o->door) { id=HOLLOW_MAT_DOOR; grain=1; }
        else if(flat || (o->part==SWAT_PART_SOLID && o->half.y<.2f)) { id=HOLLOW_MAT_FLOOR; grain=o->half.x>o->half.z ? 0 : 2; }
        else { id=HOLLOW_MAT_PLANKS; grain=o->half.y>=o->half.x && o->half.y>=o->half.z ? 1 : -1; }
        break;
    case SWAT_STEEL:
        // Only the barn's roof keeps red rust.
        if(o->part!=SWAT_PART_LIGHT && o->half.y<.1f) id=o->center.x>31 && o->center.x<42 ? HOLLOW_MAT_ROOF : HOLLOW_MAT_ROOF_DARK;
        break;
    case SWAT_STONE: id=o->center.z>14 && o->center.y<1.2f ? HOLLOW_MAT_LIMEWASH : HOLLOW_MAT_STONE; break; // cellar liners are limewashed
    case SWAT_SOIL:
        // Earth floors inside the barn, smokehouse and cellar read as packed dirt;
        // the hill bank's face is a dry-laid field-stone retaining wall.
        id=(o->center.y<-1 && o->center.z>14 && o->half.x<3) ||
           (fabsf((float)o->center.x-25.5f)<.01f && fabsf((float)o->center.z-4.5f)<.01f) ||   // smokehouse floor
           (fabsf((float)o->center.x-36.5f)<.01f && fabsf((float)o->center.z+2)<.01f) ? HOLLOW_MAT_DIRT : // barn floor
           (o->center.z>12 && o->center.y>-.5f && o->half.y>1) ? HOLLOW_MAT_STONE : HOLLOW_MAT_GROUND; break;
    case SWAT_CLOTH: case SWAT_ALUMINUM: break;
    default: break;
    }
    if(id<0 || !h->maps[id].color.id) return false;
    const SwatSurfaceMaps* m=&h->maps[id];
    swat_lighting_surface(art->lighting,m->normal,m->roughness,(Vector3){0},(Vector2){0},false);
    // Woods v2: the yard's one mud-and-leaves scan gets world-space macro
    // variation (value, roughness) and a second rotated repeat, so it no
    // longer reads as one uniform tile; the decals add paths, ruts and drifts.
    if(id==HOLLOW_MAT_GROUND && h->woods) swat_lighting_surface_variation(art->lighting,false,(Vector2){0},(Vector4){.3f,.11f,.12f,0},(Vector4){.5f,.37f,0,0});
    textured_box(m->color,o,art->lit,m->tile,grain,true);
    if(id==HOLLOW_MAT_GROUND && h->woods) swat_lighting_surface_variation(art->lighting,false,(Vector2){0},(Vector4){0},(Vector4){0});
    return true;
}
static Matrix hollow_prop_transform(const SwatHollowProp* p) {
    return MatrixMultiply(MatrixRotateY(p->yaw),MatrixTranslate(p->x,p->y,p->z));
}
static void hollow_model_draw(const SwatEnvironmentArt* art,const Model* model,const float* scales,BoundingBox bounds,
                              Matrix transform,bool shadow,bool lit,Matrix scene) {
    if(!model->meshCount) return;
    if(art->bounds && bounds_outside(art->bounds,props_ext_v2_world_bounds(bounds,transform),scene)) return;
    for(int m=0;m<model->meshCount;m++) {
        Material material=model->materials[model->meshMaterial[m]];
        MaterialMap maps[SWAT_MATERIAL_MAPS];
        if(shadow) {
            memset(maps,0,sizeof(maps)); maps[MATERIAL_MAP_ALBEDO].texture.id=rlGetTextureIdDefault();
            maps[MATERIAL_MAP_ALBEDO].color=WHITE; material.maps=maps;
            material.shader=(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
        } else {
            material.shader=lit ? art->lighting->mesh.shader : (Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
            if(lit) swat_lighting_material_uv(art->lighting,material,true,scales[model->meshMaterial[m]],0);
        }
        DrawMesh(model->meshes[m],material,transform);
    }
}
static void hollow_scene_draw(const SwatEnvironmentArt* art,bool shadow) {
    const SwatHollowArt* h=art->hollow; if(!h) return;
    bool lit=!shadow && art->lighting && art->lighting->enabled && art->lighting->prepared;
    Matrix scene=shadow && art->lighting && art->lighting->shadow_restricted ?
        art->lighting->shadow_clip : MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection());
    rlDrawRenderBatchActive();
    // Glancing cards (canopy, flyers, wire) need both faces.
    rlDisableBackfaceCulling();
    for(int i=0;i<HOLLOW_SCENE_FILES;i++)
        hollow_model_draw(art,&h->scene[i],h->scene_scales[i],h->scene_bounds[i],MatrixIdentity(),shadow,lit,scene);
    for(int i=0;i<SWAT_HOLLOW_PROPS;i++) {
        int slot=h->prop_model[i]; if(slot<0) continue;
        hollow_model_draw(art,&h->models[slot],h->scales[slot],h->bounds[slot],hollow_prop_transform(&swat_hollow_props[i]),shadow,lit,scene);
    }
    hollow_woods_draw(art,h->woods,shadow,lit,scene);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    if(lit) swat_lighting_material(art->lighting,(Material){0},false);
}
