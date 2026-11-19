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
// 3. Architecture overlay (architecture_kit.glb, motel_exterior_arch_data.h):
//    parapets and coping, corrugated bull-nose canopy, rafters, beams, post
//    sleeves and braces, plinths, door/window surrounds, AC collars, block
//    coursing, rear plumbing, scuppers/downspouts, roof equipment, office gable
//    and the laundry annex parapet. Wall geometry is pre-clipped into one piece
//    per authoritative skin section and drawn only with that section (matched
//    by its fixed centre), so a breached or removed section takes exactly its
//    share of every overlay with it. Object pieces follow their owner (roof,
//    canopy, post). It replaces a few exterior kit pieces (old gutters, rear
//    downspouts, post bases, canopy membrane) while active. Nothing is read by
//    the simulation. SWAT_MOTEL_ARCHITECTURE=0 restores the previous look; it
//    needs the exterior kit's library (off with SWAT_MOTEL_EXTERIOR=0).
#include "motel_exterior_data.h"
#include "motel_exterior_arch_data.h"

static bool exterior_enabled(void) {
    const char* e=getenv("SWAT_MOTEL_EXTERIOR");
    return !e || strcmp(e,"0");
}

static bool arch_enabled(void) {
    const char* e=getenv("SWAT_MOTEL_ARCHITECTURE");
    return !e || strcmp(e,"0");
}

static void exterior_close(SwatEnvironmentArt* art) {
    swat_art_model_close(art->motel_architecture);art->motel_architecture=(Model){0};
    swat_art_model_close(art->motel_exterior);art->motel_exterior=(Model){0};
}

static void exterior_factors(Model* model,const SwatArtMaterialFactors* factors,int count) {
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
}

static void exterior_borrow(const SwatEnvironmentArt* art,Model* model,const char* path,const char* file);
static bool arch_suppressed[SWAT_EXTERIOR_PIECES];
// Lookup: object placements by owner tag, section placements by centre.
#define ARCH_HASH 1024
static int arch_object_first[SWAT_MOTEL_INSTANCES+1],arch_next[SWAT_ARCH_PLACEMENTS],arch_hash[ARCH_HASH];
// Every placement of an owner, section pieces included: the court layer and
// moved owners repeat a template's whole overlay (swat_motel_art_frame).
static int arch_owner_first[SWAT_MOTEL_INSTANCES+1],arch_owner_next[SWAT_ARCH_PLACEMENTS];
static uint32_t arch_key(float x,float y,float z) {
    int32_t a=(int32_t)lroundf(x*100),b=(int32_t)lroundf(y*100),c=(int32_t)lroundf(z*100);
    return ((uint32_t)a*73856093u^(uint32_t)b*19349663u^(uint32_t)c*83492791u)&(ARCH_HASH-1);
}

static void arch_load(SwatEnvironmentArt* art) {
    if(!arch_enabled() || !art->motel_exterior.meshCount)return;
    char path[4096];if(!asset_path(path,sizeof(path),SWAT_ARCH_KIT_FILE))return;
    Model* model=&art->motel_architecture;*model=LoadModel(path);
    SwatArtMaterialFactors factors[32];
    int count=swat_art_material_factors(path,factors,32);
    if(model->meshCount!=SWAT_ARCH_KIT_MESHES || count!=SWAT_ARCH_KIT_MATERIALS || model->materialCount!=count+1) {
        TraceLog(LOG_WARNING,"SWAT: motel architecture kit missing or mismatched; overlay disabled");
        swat_art_model_close(*model);*model=(Model){0};return;
    }
    exterior_factors(model,factors,count);
    swat_art_model_share_textures(*model,path);
    exterior_borrow(art,model,path,"architecture_kit.glb");
    for(int i=0;i<SWAT_EXTERIOR_PIECES;i++) {
        arch_suppressed[i]=false;
        for(int k=0;k<SWAT_ARCH_SUPPRESS;k++)arch_suppressed[i]|=!strcmp(swat_exterior_pieces[i].name,swat_arch_suppress[k]);
    }
    for(int i=0;i<=SWAT_MOTEL_INSTANCES;i++)arch_object_first[i]=arch_owner_first[i]=-1;
    for(int i=SWAT_ARCH_PLACEMENTS-1;i>=0;i--) {
        int owner=swat_arch_placements[i].owner;arch_owner_next[i]=-1;
        if(owner>0 && owner<=SWAT_MOTEL_INSTANCES){arch_owner_next[i]=arch_owner_first[owner];arch_owner_first[owner]=i;}
    }
    for(int i=0;i<ARCH_HASH;i++)arch_hash[i]=-1;
    for(int i=SWAT_ARCH_PLACEMENTS-1;i>=0;i--) {
        const SwatArchPlacement* a=&swat_arch_placements[i];
        int* head=a->section?&arch_hash[arch_key(a->x,a->y,a->z)]:(a->owner>0 && a->owner<=SWAT_MOTEL_INSTANCES?&arch_object_first[a->owner]:NULL);
        if(!head){arch_next[i]=-1;continue;}
        arch_next[i]=*head;*head=i;
    }
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
    exterior_factors(model,factors,count);
    swat_art_model_share_textures(*model,path);
    arch_load(art);
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
    exterior_borrow(art,model,path,file);
}

static void exterior_borrow(const SwatEnvironmentArt* art,Model* model,const char* path,const char* file) {
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

static void arch_piece_draw(const SwatEnvironmentArt* art,const SwatObject* o,const SwatArchPlacement* a,bool shadow,bool cutaway) {
    Model view=art->motel_architecture;
    view.meshes+=a->first;view.meshMaterial+=a->first;view.meshCount=a->count;
    SwatMotelInstance p={.origin={a->x,a->y,a->z},.scale={1,1,1},.yaw=0,.roof=a->roof};
    location_mesh_draw(art,o,&p,&view,&art->motel_architecture,shadow,cutaway,false,NULL);
}

// A section opened by a breach (or shot through over a large share of its face)
// drops its overlay chunk, so trim, joints or fixtures never bridge the hole.
// Small bullet holes keep it.
static bool arch_section_open(const SwatObject* o) {
    if(!o->cuts)return false;
    float area=0,face=4*o->half.y*o->half.z;
    for(int i=0;i<o->cuts->count;i++) {
        const SwatCut* c=&o->cuts->cuts[i];
        if(c->kind==SWAT_CUT_BREACH)return true;
        area+=4*c->height*c->width;
    }
    return area>.15f*face;
}

static void arch_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(o->tag.index>0 && o->tag.index<=SWAT_MOTEL_INSTANCES) {
        for(int i=arch_object_first[o->tag.index];i>=0;i=arch_next[i])arch_piece_draw(art,o,&swat_arch_placements[i],shadow,cutaway);
        return;
    }
    if(o->part!=SWAT_PART_SKIN || !o->wall_group || o->motion || arch_section_open(o))return;
    for(int i=arch_hash[arch_key(o->center.x,o->center.y,o->center.z)];i>=0;i=arch_next[i]) {
        const SwatArchPlacement* a=&swat_arch_placements[i];
        if(fabsf(a->x-o->center.x)>.006f || fabsf(a->y-o->center.y)>.006f || fabsf(a->z-o->center.z)>.006f)continue;
        if(a->owner>=world->count || world->objects[a->owner].wall_group!=o->wall_group)continue;
        arch_piece_draw(art,o,a,shadow,cutaway);
    }
}

// Map a piece authored against `from` onto `to` (MOTEL_LAYOUT.md court layer).
static SwatMotelInstance exterior_remap(const SwatMotelInstance* from,const SwatMotelInstance* to,b3Pos origin,float yaw,b3Vec3 scale,bool roof) {
    b3Vec3 d=b3SubPos(origin,from->origin);float y=-from->yaw,c=cosf(y),s=sinf(y);
    b3Vec3 local=swat_v(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
    c=cosf(to->yaw);s=sinf(to->yaw);
    b3Pos at=b3OffsetPos(to->origin,swat_v(c*local.x+s*local.z,local.y,-s*local.x+c*local.z));
    return (SwatMotelInstance){.origin=at,.scale=scale,.yaw=yaw+to->yaw-from->yaw,.roof=roof};
}
static void exterior_clone_draw(const SwatEnvironmentArt* art,int source,const SwatMotelInstance* from,const SwatMotelInstance* to,
                                const SwatObject* o,bool shadow,bool cutaway) {
    bool arch=art->motel_architecture.meshCount>0;
    if(arch)for(int i=arch_owner_first[source];i>=0;i=arch_owner_next[i]) {
        const SwatArchPlacement* a=&swat_arch_placements[i];
        Model view=art->motel_architecture;
        view.meshes+=a->first;view.meshMaterial+=a->first;view.meshCount=a->count;
        SwatMotelInstance p=exterior_remap(from,to,(b3Pos){a->x,a->y,a->z},0,swat_v(1,1,1),a->roof);
        location_mesh_draw(art,o,&p,&view,&art->motel_architecture,shadow,cutaway,false,NULL);
    }
    for(int i=0;i<SWAT_EXTERIOR_PLACEMENTS;i++) {
        const SwatExteriorPlacement* e=&swat_exterior_placements[i];
        if(e->owner!=source || (arch && arch_suppressed[e->piece]))continue;
        const SwatExteriorPiece* piece=&swat_exterior_pieces[e->piece];
        Model view=art->motel_exterior;
        view.meshes+=piece->first;view.meshMaterial+=piece->first;view.meshCount=piece->count;
        SwatMotelInstance p=exterior_remap(from,to,(b3Pos){e->x,e->y,e->z},e->yaw,swat_v(e->sx,e->sy,e->sz),e->roof);
        location_mesh_draw(art,o,&p,&view,&art->motel_exterior,shadow,cutaway,false,NULL);
    }
}

static void exterior_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(!art->motel_exterior.meshCount || !o->active)return;
    int source;SwatMotelInstance from,to;
    if(swat_motel_art_frame(world,o->tag.index,&source,&from,&to)){exterior_clone_draw(art,source,&from,&to,o,shadow,cutaway);return;}
    bool arch=art->motel_architecture.meshCount>0;
    if(arch)arch_draw(art,world,o,shadow,cutaway);
    for(int i=0;i<SWAT_EXTERIOR_PLACEMENTS;i++) {
        const SwatExteriorPlacement* e=&swat_exterior_placements[i];
        if(arch && arch_suppressed[e->piece])continue;
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
