// Briar Court exterior art pass (presentation only). Included by
// environment_art.c after location_mesh_draw. See ENVIRONMENT_ART.md.
//
// 1. Material library: the Blender-built exterior kit carries PBR stucco,
//    concrete, painted plaster/metal and roof membrane materials (CC0 Poly
//    Haven sources, see assets/environment/motel_exterior/README.md). Exterior
//    shell modules whose glTF materials are named MC15_<name> borrow the
//    matching library maps at load time instead of the flat 192 px
//    placeholders. Geometry, UVs, collision and the motel_v1 files are untouched.
// 2. Dressing: gutters, downspouts, lamps, sills, wall packs, meters, vents,
//    post bases, rooftop units and sign floodlights are drawn as children of
//    an existing authoritative owner (wall skin section, roof, post, sign).
//    They have no collider, no navigation/cover effect and vanish with their
//    support. SWAT_MOTEL_EXTERIOR=0 disables both parts for comparison.
#include "motel_exterior_data.h"

static bool exterior_enabled(void) {
    const char* e=getenv("SWAT_MOTEL_EXTERIOR");
    return !e || strcmp(e,"0");
}

static void exterior_close(SwatEnvironmentArt* art) {
    swat_art_model_close(art->motel_exterior);art->motel_exterior=(Model){0};
}

static void exterior_load(SwatEnvironmentArt* art) {
    exterior_close(art);
    if(!exterior_enabled())return;
    char path[4096];if(!asset_path(path,sizeof(path),SWAT_EXTERIOR_KIT_FILE))return;
    Model* model=&art->motel_exterior;*model=LoadModel(path);
    SwatArtMaterialFactors factors[SWAT_LOCATION_MATERIALS-1];
    int count=swat_art_material_factors(path,factors,SWAT_LOCATION_MATERIALS-1);
    if(model->meshCount!=SWAT_EXTERIOR_KIT_MESHES || count!=SWAT_EXTERIOR_KIT_MATERIALS || model->materialCount!=count+1) {
        TraceLog(LOG_WARNING,"SWAT: motel exterior kit missing or mismatched; original shell maps retained");
        exterior_close(art);return;
    }
    for(int m=0;m<count;m++) {
        Material* material=&model->materials[m+1];const SwatArtMaterialFactors* f=&factors[m];
        // Raylib 5.5 omits scalar factors without an ORM map; the library
        // maps multiply these factors, so textured materials keep 1/0 values.
        material->maps[MATERIAL_MAP_ROUGHNESS].value=f->roughness;
        material->maps[MATERIAL_MAP_METALNESS].value=f->metalness;
        material->maps[MATERIAL_MAP_OCCLUSION].value=f->occlusion_strength;
        material->maps[MATERIAL_MAP_NORMAL].value=2; // Signed UV derivatives.
        material->maps[MATERIAL_MAP_ALBEDO].color=(Color){
            (unsigned char)lroundf(255*powf(swat_clamp(f->base_color[0],0,1),1/2.2f)),
            (unsigned char)lroundf(255*powf(swat_clamp(f->base_color[1],0,1),1/2.2f)),
            (unsigned char)lroundf(255*powf(swat_clamp(f->base_color[2],0,1),1/2.2f)),255};
        for(int k=0;k<=MATERIAL_MAP_BRDF;k++) {
            Texture2D* t=&material->maps[k].texture;
            if(t->id && t->id!=rlGetTextureIdDefault()) {GenTextureMipmaps(t);SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR);SetTextureWrap(*t,TEXTURE_WRAP_REPEAT);}
        }
    }
    swat_art_model_share_textures(*model,path);
}

// Exterior shell modules only: interior furniture keeps its own MC15 maps.
static const char* const exterior_shell_files[]={
    "room_facade_4m.glb","solid_wall_4m.glb","end_wall_6m.glb","flat_roof_4x6m.glb",
    "gallery_canopy_4m.glb","gallery_walkway_4m.glb","canopy_post.glb","corner_pier.glb",
    "lobby_glazed_facade.glb","service_doorway_4m.glb","gallery_access_ramp.glb",
    "roadside_sign.glb","end_wall_6m_104_v3.glb","end_wall_6m_105_v3.glb",
    "flat_roof_4x6m_009_v3.glb","solid_wall_4m_013_v4.glb"};

static int exterior_library(const char* file,const char* material) {
    if(strncmp(material,"MC15_",5))return -1;
    const char* name=material+5;
    // The flat roof's top deck and the canopy's sheet deck read as roofing.
    if(!strncmp(file,"flat_roof_4x6m",14) && !strcmp(name,"concrete_dark"))name="roof";
    if(!strncmp(file,"gallery_canopy_4m",17) && !strcmp(name,"steel"))name="roof";
    for(int i=0;i<SWAT_EXTERIOR_LIBRARY;i++)if(!strcmp(swat_exterior_library[i].name,name))return swat_exterior_library[i].material;
    return -1;
}

// Library maps must live in the shared texture pool, so each borrowing model
// becomes a counted owner and model close order does not matter.
static bool exterior_adopt(Material* materials,Texture2D texture) {
    if(!texture.id || texture.id==rlGetTextureIdDefault())return true;
    for(ArtTextureEntry* e=art_textures;e;e=e->next)if(e->texture.id==texture.id)return texture_owner(e,materials);
    return false;
}

static void exterior_override(const SwatEnvironmentArt* art,Model* model,const char* path) {
    if(!art->motel_exterior.meshCount || !model->meshCount)return;
    const char* file=strrchr(path,'/');file=file?file+1:path;
    bool shell=false;
    for(size_t i=0;i<sizeof(exterior_shell_files)/sizeof(exterior_shell_files[0]);i++)shell|=!strcmp(file,exterior_shell_files[i]);
    if(!shell)return;
    cgltf_options options={0};cgltf_data* source=NULL;
    if(cgltf_parse_file(&options,path,&source)!=cgltf_result_success)return;
    for(size_t m=0;m<source->materials_count && (int)m+1<model->materialCount;m++) {
        const char* name=source->materials[m].name;
        int lib=name?exterior_library(file,name):-1;
        if(lib<1 || lib>=art->motel_exterior.materialCount)continue;
        const Material* from=&art->motel_exterior.materials[lib];
        static const int slots[3]={MATERIAL_MAP_ALBEDO,MATERIAL_MAP_NORMAL,MATERIAL_MAP_ROUGHNESS};
        bool pooled=true;
        for(int s=0;s<3;s++)pooled&=from->maps[slots[s]].texture.id!=0;
        for(int s=0;pooled && s<3;s++)pooled&=exterior_adopt(model->materials,from->maps[slots[s]].texture);
        if(!pooled)continue;
        Material* to=&model->materials[m+1];
        for(int s=0;s<3;s++)to->maps[slots[s]].texture=from->maps[slots[s]].texture;
        to->maps[MATERIAL_MAP_ALBEDO].color=WHITE;
        to->maps[MATERIAL_MAP_ROUGHNESS].value=from->maps[MATERIAL_MAP_ROUGHNESS].value;
        to->maps[MATERIAL_MAP_METALNESS].value=from->maps[MATERIAL_MAP_METALNESS].value;
        to->maps[MATERIAL_MAP_NORMAL].value=2;
    }
    cgltf_free(source);
}

// Current support for a placement: the owner itself, or for wall mounts the
// surviving skin section of the owner's wall group under the origin.
static int exterior_support(const SwatWorld* w,const SwatExteriorPlacement* e) {
    if(e->owner<=0 || e->owner>=w->count)return -1;
    const SwatObject* owner=&w->objects[e->owner];
    if(owner->active)return e->owner;
    if(!e->wall || !owner->wall_group)return -1;
    for(int i=owner->wall_group-1;i<w->count && w->objects[i].wall_group==owner->wall_group;i++) {
        const SwatObject* s=&w->objects[i];if(s->part!=SWAT_PART_SKIN)continue;
        b3Vec3 d=b3SubPos((b3Pos){e->x,e->y,e->z},s->center);
        float c=cosf(s->yaw),sn=sinf(s->yaw),x=c*d.x-sn*d.z,y=d.y,z=sn*d.x+c*d.z;
        if(fabsf(y)>s->half.y || fabsf(z)>s->half.z || fabsf(x)>s->half.x+.25f)continue;
        bool inside=true;
        if(s->fractured)for(int j=0;j<4;j++) {
            int k=(j+1)%4;float ay=s->corners[j][0],az=s->corners[j][1];
            if((s->corners[k][0]-ay)*(z-az)-(s->corners[k][1]-az)*(y-ay)<-1e-5f)inside=false;
        }
        if(inside)return s->active?i:-1;
    }
    return -1;
}

static void exterior_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(!art->motel_exterior.meshCount || !o->active)return;
    for(int i=0;i<SWAT_EXTERIOR_PLACEMENTS;i++) {
        const SwatExteriorPlacement* e=&swat_exterior_placements[i];
        if(e->owner!=o->tag.index && (!e->wall || o->tag.index<=SWAT_MOTEL_INSTANCES || o->part!=SWAT_PART_SKIN ||
           !o->wall_group || e->owner>=world->count || o->wall_group!=world->objects[e->owner].wall_group))continue;
        if(exterior_support(world,e)!=o->tag.index)continue;
        const SwatExteriorPiece* piece=&swat_exterior_pieces[e->piece];
        Model view=art->motel_exterior;
        view.meshes+=piece->first;view.meshMaterial+=piece->first;view.meshCount=piece->count;
        SwatMotelInstance p={.origin={e->x,e->y,e->z},.scale={e->sx,e->sy,e->sz},.yaw=e->yaw,.roof=e->roof};
        location_mesh_draw(art,o,&p,&view,&art->motel_exterior,shadow,cutaway,false,NULL);
    }
}
