// Briar Court materials v3 (presentation only). Included once by
// environment_art.c after the texture pool, the interior pass and the
// exterior kit. See ENVIRONMENT_MATERIALS.md, "Materials v3".
//
// Scanned CC0 PBR materials (Poly Haven / ambientCG) baked by
// tools/blender/materials_v3_bake.py into assets/environment/materials_v3/,
// hill-climbed against reference photos of 1960s-70s roadside motels
// (reports/cloud/art-materials-v3/LOG.md). This file only swaps texture maps
// and sets per-material repeat/macro-variation uniforms:
// - exterior kit library (stucco, repair, cream trim, teal fascia, ochre
//   gutter, concrete, dark plinth, roof) and therefore every shell module that
//   borrows it; the walkway deck, its curb face and wheel stops get their own;
// - lot (object 0), road/driveway and highway extension, gravel ground,
//   shoulder and the zoning dirt blend;
// - guest-room carpet, painted drywall (papered faces and framed skins),
//   bathroom floor and wainscot tile, ceilings, reception and laundry floors.
// Geometry, UVs, collision, physics materials, navigation and every
// simulation/protocol value are untouched. SWAT_MATERIALS_V3=0 restores the
// previous maps; SWAT_MATERIALS_V3_MACRO=0 keeps the maps without the
// world-space macro variation / two-scale blend.
#ifndef SWAT_MATERIALS_V3_H
#define SWAT_MATERIALS_V3_H

// Material ids (SwatMaterialV3Id) are declared in environment_art.h.
// tile_m: metres per repeat (materials_v3_recipe.json). uv_per_m: UV density
// of the surfaces it is applied to (motel_v1 modules 1.1, ground asphalt
// 2.1 m, ground gravel 8/3 m, metric box/paper UVs 1). macro: albedo
// amplitude, frequency (1/m), roughness amplitude. detail: second-scale blend
// weight, its UV factor, and (gravel only) the zoning dirt repeat factor.
typedef struct SwatMaterialV3Def { const char* name; bool flip; float tile_m,uv_per_m; float macro[3],detail[3]; } SwatMaterialV3Def;
static const SwatMaterialV3Def mv3_defs[MV3_COUNT]={
    [MV3_STUCCO]={"stucco",false,3.0f,1.1f,{.08f,.35f,.05f},{.5f,.43f,0}},
    [MV3_REPAIR]={"repair",false,1.0f,1.1f,{.05f,.5f,0},{0,0,0}},
    [MV3_TRIM]={"trim",false,2.0f,1.1f,{.05f,.5f,0},{0,0,0}},
    [MV3_FASCIA]={"fascia",false,1.0f,1.1f,{.06f,.5f,.05f},{0,0,0}},
    [MV3_GUTTER]={"gutter",false,1.0f,1.1f,{.06f,.5f,.05f},{0,0,0}},
    [MV3_CONCRETE]={"concrete",false,3.0f,1.1f,{.08f,.3f,.05f},{.4f,.41f,0}},
    [MV3_WALKWAY]={"walkway",false,1.3333f,1.1f,{.08f,.3f,.05f},{.4f,.41f,0}},
    [MV3_PLINTH]={"plinth",false,3.0f,1.1f,{.08f,.4f,0},{0,0,0}},
    [MV3_ROOF]={"roof",false,3.0f,1.1f,{.10f,.25f,0},{.5f,.39f,0}},
    [MV3_LOT]={"lot",true,2.0f,1.0f,{.18f,.08f,.08f},{.5f,.37f,0}},
    [MV3_ROAD]={"road",false,4.0f,1/2.1f,{.08f,.12f,.05f},{.5f,.37f,0}},
    [MV3_GRAVEL]={"gravel",false,3.5f,3/8.f,{.12f,.15f,0},{.5f,.37f,.5f}},
    [MV3_DIRT]={"dirt",false,4.0f,0,{0,0,0},{0,0,0}},
    [MV3_CARPET]={"carpet",false,1.0f,1.1f,{.08f,.6f,0},{0,0,0}},
    [MV3_DRYWALL]={"drywall",true,2.0f,1/.6f,{.04f,.6f,0},{0,0,0}},
    [MV3_CEILING]={"ceiling",false,0.6f,1.1f,{.05f,.6f,0},{0,0,0}},
    [MV3_BATH_FLOOR]={"bath_floor",false,1.0f,1.1f,{.04f,.8f,0},{0,0,0}},
    [MV3_BATH_WALL]={"bath_wall",false,1.0f,1.1f,{.03f,.8f,0},{0,0,0}},
    [MV3_RECEPTION_FLOOR]={"reception_floor",false,1.2f,1.1f,{.05f,.6f,0},{0,0,0}},
    [MV3_LAUNDRY_FLOOR]={"laundry_floor",false,1.5f,1.1f,{.06f,.5f,0},{0,0,0}},
};

typedef struct SwatMaterialsV3 {
    SwatSurfaceMaps maps[MV3_COUNT];
    Material owner;                  // Pool owner key for the set's own reference.
    bool macro;                      // SWAT_MATERIALS_V3_MACRO
    Model floors[2];                 // Reception (owner 2) and laundry (127) floor copies.
    MaterialMap* floor_maps[2][2];
} SwatMaterialsV3;

static bool mv3_enabled(void) {
    const char* e=getenv("SWAT_MATERIALS_V3"),*style=getenv("SWAT_ENVIRONMENT_STYLE");
    return (!e || strcmp(e,"0")) && (!style || strcmp(style,"legacy"));
}

static const SwatSurfaceMaps* mv3_maps(const SwatEnvironmentArt* art,int id) {
    const SwatMaterialsV3* v=art->materials_v3;
    return v && id>=0 && id<MV3_COUNT && v->maps[id].color.id ? &v->maps[id] : NULL;
}

// Every v3 texture is a pool entry; models that borrow it become counted
// owners, so close order between the set and its borrowers is irrelevant.
static bool mv3_register(SwatMaterialsV3* v,Texture2D t) {
    if(!t.id)return true;
    ArtTextureEntry* e=calloc(1,sizeof(*e));if(!e)return false;
    e->texture=t;e->gpu_bytes=texture_bytes(t);
    if(!texture_owner(e,&v->owner)){free(e);return false;}
    e->next=art_textures;art_textures=e;return true;
}

static void mv3_close(SwatEnvironmentArt* art) {
    SwatMaterialsV3* v=art->materials_v3;if(!v)return;
    for(int f=0;f<2;f++){MemFree(v->floors[f].materials);for(int s=0;s<2;s++)MemFree(v->floor_maps[f][s]);}
    for(int i=0;i<MV3_COUNT;i++) {
        Texture2D t[3]={v->maps[i].color,v->maps[i].normal,v->maps[i].roughness};
        for(int k=0;k<3;k++)if(t[k].id && !texture_release(&v->owner,t[k]))UnloadTexture(t[k]);
    }
    free(v);art->materials_v3=NULL;
}

static void mv3_load(SwatEnvironmentArt* art) {
    mv3_close(art);
    if(!mv3_enabled())return;
    SwatMaterialsV3* v=calloc(1,sizeof(*v));if(!v)return;
    const char* macro=getenv("SWAT_MATERIALS_V3_MACRO"),*pbr=getenv("SWAT_ENVIRONMENT_PBR");
    v->macro=!macro || strcmp(macro,"0");
    int loaded=0;
    for(int i=0;i<MV3_COUNT;i++) {
        const SwatMaterialV3Def* d=&mv3_defs[i];SwatSurfaceMaps* m=&v->maps[i];char file[256];
        snprintf(file,sizeof(file),"materials_v3/%s_basecolor.jpg",d->name);m->color=load_surface(file,d->flip);
        if(m->color.id && (!pbr || strcmp(pbr,"0"))) {
            snprintf(file,sizeof(file),"materials_v3/%s_normal.jpg",d->name);m->normal=load_surface(file,d->flip);
            snprintf(file,sizeof(file),"materials_v3/%s_roughness.jpg",d->name);m->roughness=load_surface(file,d->flip);
            // A material is complete or absent: never mix v3 colour with old relief.
            if(!m->normal.id || !m->roughness.id) {
                if(m->normal.id)UnloadTexture(m->normal);
                if(m->roughness.id)UnloadTexture(m->roughness);
                UnloadTexture(m->color);*m=(SwatSurfaceMaps){0};
            }
        }
        if(!m->color.id)continue;
        // Grazing ground/floor views keep their aggregate instead of blurring.
        Texture2D* t[3]={&m->color,&m->normal,&m->roughness};
        for(int k=0;k<3;k++)if(t[k]->id)SetTextureFilter(*t[k],TEXTURE_FILTER_ANISOTROPIC_8X);
        m->tile=(Vector2){d->tile_m,d->tile_m};
        if(!mv3_register(v,m->color) || !mv3_register(v,m->normal) || !mv3_register(v,m->roughness)) {
            TraceLog(LOG_WARNING,"SWAT: materials v3 texture pool allocation failed");
        }
        loaded++;
    }
    art->materials_v3=v;
    if(loaded<MV3_COUNT)TraceLog(LOG_WARNING,"SWAT: materials v3: %d of %d materials missing; those keep the previous maps",MV3_COUNT-loaded,MV3_COUNT);
}

static bool mv3_in_pool(Texture2D t) {
    for(ArtTextureEntry* e=art_textures;e;e=e->next)if(e->texture.id==t.id)return true;
    return false;
}

// Replace one material's albedo/normal/ORM maps. A displaced texture that is
// pooled stays owned by the model until it closes; an unpooled one that no
// other map of the model references is released here.
static void mv3_assign(const SwatEnvironmentArt* art,Model* model,int index,int id) {
    const SwatSurfaceMaps* m=mv3_maps(art,id);
    if(!m || !model->materials || index<1 || index>=model->materialCount)return;
    Material* to=&model->materials[index];
    static const int slots[3]={MATERIAL_MAP_ALBEDO,MATERIAL_MAP_NORMAL,MATERIAL_MAP_ROUGHNESS};
    Texture2D with[3]={m->color,m->normal,m->roughness};
    for(int s=0;s<3;s++) {
        Texture2D old=to->maps[slots[s]].texture;
        if(with[s].id) {
            for(ArtTextureEntry* e=art_textures;e;e=e->next)if(e->texture.id==with[s].id){texture_owner(e,model->materials);break;}
        }
        to->maps[slots[s]].texture=with[s];
        if(!old.id || old.id==rlGetTextureIdDefault() || old.id==with[s].id || mv3_in_pool(old))continue;
        bool used=false;
        for(int a=0;a<model->materialCount && !used;a++)for(int k=0;k<=MATERIAL_MAP_BRDF;k++)
            if(model->materials[a].maps[k].texture.id==old.id){used=true;break;}
        if(!used)UnloadTexture(old);
    }
    to->maps[MATERIAL_MAP_ALBEDO].color=WHITE;
    to->maps[MATERIAL_MAP_ROUGHNESS].value=m->roughness.id?1:.85f;
    to->maps[MATERIAL_MAP_METALNESS].value=0;
    to->maps[MATERIAL_MAP_NORMAL].value=2; // Signed UV derivatives.
}

// glTF material name -> Raylib material index (+1), or -1.
static int mv3_material_index(const char* path,const char* name) {
    cgltf_options options={0};cgltf_data* source=NULL;int found=-1;
    if(cgltf_parse_file(&options,path,&source)!=cgltf_result_success)return -1;
    for(size_t m=0;m<source->materials_count && found<0;m++)
        if(source->materials[m].name && !strcmp(source->materials[m].name,name))found=(int)m+1;
    cgltf_free(source);return found;
}
static void mv3_assign_named(const SwatEnvironmentArt* art,Model* model,const char* file,const char* name,int id) {
    char path[4096];if(!model->meshCount || !asset_path(path,sizeof(path),file))return;
    int index=mv3_material_index(path,name);
    if(index>0)mv3_assign(art,model,index,id);
}

// Exterior kit library -> v3 (after exterior_load, before the shells borrow it).
static void mv3_exterior_kit(SwatEnvironmentArt* art) {
    if(!art->materials_v3 || !art->motel_exterior.meshCount)return;
    static const struct {const char* name;int id;} map[]={
        {"stucco",MV3_STUCCO},{"repair",MV3_REPAIR},{"concrete",MV3_CONCRETE},{"concrete_dark",MV3_PLINTH},
        {"roof",MV3_ROOF},{"cream",MV3_TRIM},{"teal",MV3_FASCIA},{"ochre",MV3_GUTTER}};
    for(int i=0;i<SWAT_EXTERIOR_LIBRARY;i++)for(size_t k=0;k<sizeof(map)/sizeof(map[0]);k++)
        if(!strcmp(swat_exterior_library[i].name,map[k].name))
            mv3_assign(art,&art->motel_exterior,swat_exterior_library[i].material,map[k].id);
}

// Module-specific surfaces the kit library does not distinguish.
static void mv3_module(const SwatEnvironmentArt* art,Model* model,const char* path) {
    if(!art->materials_v3 || !model->meshCount)return;
    const char* file=strrchr(path,'/');file=file?file+1:path;
    static const struct {const char* file,*material;int id;} map[]={
        {"gallery_walkway_4m.glb","MC15_concrete",MV3_WALKWAY},   // deck: jointed, stained
        {"gallery_walkway_4m.glb","MC15_cream",MV3_CONCRETE},     // its curb face to the lot
        {"gallery_access_ramp.glb","MC15_concrete",MV3_WALKWAY},
        {"parking_wheel_stop.glb","MC15_concrete",MV3_CONCRETE},
        {"bathroom_partition_4m.glb","MC15_tile",MV3_BATH_WALL},
    };
    for(size_t k=0;k<sizeof(map)/sizeof(map[0]);k++)if(!strcmp(file,map[k].file)) {
        int index=mv3_material_index(path,map[k].material);
        if(index>0)mv3_assign(art,model,index,map[k].id);
    }
}

// Ground support meshes, the shoulder and the highway extension.
static void mv3_ground(SwatEnvironmentArt* art) {
    if(!art->materials_v3)return;
    mv3_assign_named(art,&art->motel_ground,"motel_ground/connected_ground_render.glb","CC0_Gravel_ground_01_8over3m",MV3_GRAVEL);
    mv3_assign_named(art,&art->motel_ground,"motel_ground/connected_ground_render.glb","CC0_Clean_Asphalt_2p1m",MV3_ROAD);
    mv3_assign_named(art,&art->motel_surroundings[0],"motel_surroundings/shoulder_render.glb","gravel_soil",MV3_GRAVEL);
    mv3_assign_named(art,&art->motel_backdrop[2],"motel_backdrop/road_extension.glb","road_extension_asphalt",MV3_ROAD);
}

static MaterialMap* mv3_floor_maps(const SwatEnvironmentArt* art,const Material* source,int id) {
    const SwatSurfaceMaps* m=mv3_maps(art,id);
    MaterialMap* maps=MemAlloc(16*sizeof(MaterialMap));memset(maps,0,16*sizeof(MaterialMap));
    memcpy(maps,source->maps,(MATERIAL_MAP_BRDF+1)*sizeof(MaterialMap));
    maps[MATERIAL_MAP_ALBEDO].texture=m->color;maps[MATERIAL_MAP_ALBEDO].color=WHITE;
    maps[MATERIAL_MAP_NORMAL].texture=m->normal;maps[MATERIAL_MAP_NORMAL].value=2;
    maps[MATERIAL_MAP_ROUGHNESS].texture=m->roughness;maps[MATERIAL_MAP_ROUGHNESS].value=m->roughness.id?1:.85f;
    maps[MATERIAL_MAP_METALNESS].value=0;maps[MATERIAL_MAP_OCCLUSION].texture=(Texture2D){0};
    return maps;
}
static void mv3_set_maps(MaterialMap* maps,const SwatSurfaceMaps* m) {
    if(!maps || !m)return;
    maps[MATERIAL_MAP_ALBEDO].texture=m->color;maps[MATERIAL_MAP_ALBEDO].color=WHITE;
    maps[MATERIAL_MAP_NORMAL].texture=m->normal;maps[MATERIAL_MAP_NORMAL].value=2;
    maps[MATERIAL_MAP_ROUGHNESS].texture=m->roughness;maps[MATERIAL_MAP_ROUGHNESS].value=m->roughness.id?1:.85f;
    maps[MATERIAL_MAP_METALNESS].value=0;
}

// Interior pass finishes (after motel_interior_load): the derived copies own
// only their MaterialMap arrays, so swapping textures leaves ownership as is.
static void mv3_interior(SwatEnvironmentArt* art) {
    SwatMaterialsV3* v=art->materials_v3;SwatMotelInteriorArt* a=art->motel_interior;
    if(!v)return;
    if(a) {
        for(int i=0;i<SWAT_MOTEL_INSTANCES;i++)mv3_set_maps(a->papered_maps[i],mv3_maps(art,MV3_DRYWALL));
        mv3_set_maps(a->floor_maps[0],mv3_maps(art,MV3_CARPET));
        mv3_set_maps(a->floor_maps[1],mv3_maps(art,MV3_BATH_FLOOR));
        mv3_set_maps(a->roof_maps,mv3_maps(art,MV3_CEILING));
    }
    // Reception (vinyl tile) and laundry (terrazzo) floors: both finish slots.
    const Model* floor=&art->motel[4];
    for(int f=0;f<2;f++) {
        int id=f?MV3_LAUNDRY_FLOOR:MV3_RECEPTION_FLOOR;
        if(!floor->meshCount || floor->materialCount!=4 || !mv3_maps(art,id))continue;
        Model* m=&v->floors[f];*m=*floor;
        m->materials=MemAlloc(floor->materialCount*sizeof(Material));
        memcpy(m->materials,floor->materials,floor->materialCount*sizeof(Material));
        for(int s=0;s<2;s++){v->floor_maps[f][s]=mv3_floor_maps(art,&floor->materials[2+s],id);m->materials[2+s].maps=v->floor_maps[f][s];}
    }
}
static const Model* mv3_floor(const SwatEnvironmentArt* art,int owner) {
    const SwatMaterialsV3* v=art->materials_v3;
    if(!v)return NULL;
    const Model* m=owner==2?&v->floors[0]:owner==127?&v->floors[1]:NULL;
    return m && m->meshCount?m:NULL;
}

// Per-draw uniforms for a material that carries a v3 albedo (call after the
// ordinary material binding, which resets them).
static int mv3_lookup(const SwatEnvironmentArt* art,unsigned int albedo) {
    const SwatMaterialsV3* v=art->materials_v3;
    if(!v || !albedo)return -1;
    for(int i=0;i<MV3_COUNT;i++)if(v->maps[i].color.id==albedo)return i;
    return -1;
}
static void mv3_variation(const SwatEnvironmentArt* art,int id,bool mesh) {
    const SwatMaterialsV3* v=art->materials_v3;const SwatMaterialV3Def* d=&mv3_defs[id];
    Vector2 scale={0};
    if(mesh && d->uv_per_m>0){float s=1/(d->uv_per_m*d->tile_m);scale=(Vector2){s,s};}
    Vector4 macro={0},detail={0};
    if(v->macro){macro=(Vector4){d->macro[0],d->macro[1],d->macro[2],0};detail=(Vector4){d->detail[0],d->detail[1],0,0};}
    // Gravel carries the zoning dirt blend: its repeat follows the v3 dirt tile.
    if(id==MV3_GRAVEL && mv3_maps(art,MV3_DIRT)){detail.z=2/mv3_defs[MV3_DIRT].tile_m;if(!v->macro)macro.y=0;}
    swat_lighting_surface_variation(art->lighting,mesh,scale,macro,detail);
}
static void mv3_bind(const SwatEnvironmentArt* art,const Material* material) {
    if(!art->materials_v3 || !material->maps)return;
    int id=mv3_lookup(art,material->maps[MATERIAL_MAP_ALBEDO].texture.id);
    if(id>=0)mv3_variation(art,id,true);
}
#endif
