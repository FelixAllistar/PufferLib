// Exact embedded-image sharing for live GLB material owners. Texture sampling
// policy must already be repeat/trilinear. AO images are excluded because the
// separate UV1 atlases switch to clamp after loading. Never resize/re-encode.
// Use the character consumer's parser ABI. Raylib exports a different cgltf
// revision; its unprefixed functions cannot consume these structures.
#define cgltf_parse_file swat_cgltf_parse_file
#define cgltf_load_buffers swat_cgltf_load_buffers
#define cgltf_free swat_cgltf_free
#include "vendor/cgltf.h"
typedef struct ArtTextureOwner {
    Material* materials;
    struct ArtTextureOwner* next;
} ArtTextureOwner;
typedef struct ArtTextureEntry {
    Texture2D texture;
    unsigned char* encoded;
    size_t size,gpu_bytes;
    uint64_t hash;
    int sampler[4];
    ArtTextureOwner* owners;
    struct ArtTextureEntry* next;
} ArtTextureEntry;
static ArtTextureEntry* art_textures;
static size_t texture_bytes(Texture2D t) {
    size_t bytes=0;int w=t.width,h=t.height;
    for(int i=0;i<t.mipmaps;i++){bytes+=(size_t)GetPixelDataSize(w,h,t.format);w=w>1?w/2:1;h=h>1?h/2:1;}
    return bytes;
}
static bool texture_owner(ArtTextureEntry* entry,Material* materials) {
    for(ArtTextureOwner* o=entry->owners;o;o=o->next)if(o->materials==materials)return true;
    ArtTextureOwner* owner=malloc(sizeof(*owner));if(!owner)return false;
    *owner=(ArtTextureOwner){materials,entry->owners};entry->owners=owner;return true;
}
static bool texture_has_owner(const ArtTextureEntry* entry,Material* materials) {
    for(const ArtTextureOwner* o=entry->owners;o;o=o->next)if(o->materials==materials)return true;
    return false;
}
static void share_image(Model model,int material,int slot,const cgltf_texture* source) {
    if(!source || !source->image || !source->image->buffer_view || material>=model.materialCount)return;
    Texture2D original=model.materials[material].maps[slot].texture;
    if(!original.id || original.id==rlGetTextureIdDefault())return;
    // An image used in AO may also occupy the packed roughness map. Exclude
    // every reference to that ID so a later clamp change cannot affect a peer.
    for(int m=0;m<model.materialCount;m++)if(model.materials[m].maps[MATERIAL_MAP_OCCLUSION].texture.id==original.id)return;
    for(ArtTextureEntry* e=art_textures;e;e=e->next)
        if(e->texture.id==original.id && texture_has_owner(e,model.materials))return;
    const cgltf_buffer_view* view=source->image->buffer_view;
    if(!view->buffer || !view->buffer->data || view->offset>view->buffer->size || view->size>view->buffer->size-view->offset)return;
    const unsigned char* bytes=(const unsigned char*)view->buffer->data+view->offset;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<view->size;i++)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    int sampler[4]={0};
    if(source->sampler) {
        sampler[0]=source->sampler->wrap_s;sampler[1]=source->sampler->wrap_t;
        sampler[2]=source->sampler->min_filter;sampler[3]=source->sampler->mag_filter;
    }
    ArtTextureEntry* entry=NULL;
    for(ArtTextureEntry* e=art_textures;e;e=e->next) {
        Texture2D t=e->texture;
        if(e->hash==hash && e->size==view->size && t.width==original.width && t.height==original.height &&
           t.format==original.format && t.mipmaps==original.mipmaps && !memcmp(e->sampler,sampler,sizeof(sampler)) &&
           !memcmp(e->encoded,bytes,view->size)){entry=e;break;}
    }
    if(!entry) {
        entry=calloc(1,sizeof(*entry));if(!entry)return;
        entry->encoded=malloc(view->size);
        if(!entry->encoded || !texture_owner(entry,model.materials)){free(entry->encoded);free(entry);return;}
        memcpy(entry->encoded,bytes,view->size);memcpy(entry->sampler,sampler,sizeof(sampler));
        entry->texture=original;entry->hash=hash;entry->size=view->size;entry->gpu_bytes=texture_bytes(original);
        entry->next=art_textures;art_textures=entry;return;
    }
    if(!texture_owner(entry,model.materials))return;
    for(int m=0;m<model.materialCount;m++)for(int k=0;k<=MATERIAL_MAP_BRDF;k++)
        if(model.materials[m].maps[k].texture.id==original.id)model.materials[m].maps[k].texture=entry->texture;
    if(original.id!=entry->texture.id)UnloadTexture(original);
}
void swat_art_model_share_textures(Model model,const char* path) {
    const char* enabled=getenv("SWAT_ART_TEXTURE_SHARING");
    if(!path || !*path || !model.materials || model.materialCount<=0 || (enabled && !strcmp(enabled,"0")))return;
    cgltf_options options={0};cgltf_data* source=NULL;
    if(cgltf_parse_file(&options,path,&source)!=cgltf_result_success)return;
    if(source->materials_count+1!=(size_t)model.materialCount || cgltf_load_buffers(&options,source,path)!=cgltf_result_success) {
        cgltf_free(source);return;
    }
    for(size_t m=0;m<source->materials_count;m++) {
        const cgltf_material* mat=&source->materials[m];
        share_image(model,(int)m+1,MATERIAL_MAP_ALBEDO,mat->pbr_metallic_roughness.base_color_texture.texture);
        share_image(model,(int)m+1,MATERIAL_MAP_ROUGHNESS,mat->pbr_metallic_roughness.metallic_roughness_texture.texture);
        share_image(model,(int)m+1,MATERIAL_MAP_NORMAL,mat->normal_texture.texture);
        share_image(model,(int)m+1,MATERIAL_MAP_EMISSION,mat->emissive_texture.texture);
    }
    cgltf_free(source);
}
static bool texture_release(Material* materials,Texture2D texture) {
    ArtTextureEntry** at=&art_textures;
    while(*at && (*at)->texture.id!=texture.id)at=&(*at)->next;
    if(!*at)return false;
    ArtTextureEntry* entry=*at;ArtTextureOwner** owner=&entry->owners;
    while(*owner && (*owner)->materials!=materials)owner=&(*owner)->next;
    if(*owner){ArtTextureOwner* removed=*owner;*owner=removed->next;free(removed);}
    if(!entry->owners) {
        *at=entry->next;UnloadTexture(entry->texture);free(entry->encoded);free(entry);
    }
    return true;
}
static void texture_release_remaining(Material* materials) {
    // Material overrides may replace/remove a map after registration. Its
    // original shared allocation still belongs to this model until close.
    for(ArtTextureEntry* e=art_textures;e;) {
        ArtTextureEntry* next=e->next;
        if(texture_has_owner(e,materials))texture_release(materials,e->texture);
        e=next;
    }
}
SwatArtTextureStats swat_art_texture_stats(void) {
    SwatArtTextureStats stats={0};
    for(const ArtTextureEntry* e=art_textures;e;e=e->next) {
        size_t owners=0;for(const ArtTextureOwner* o=e->owners;o;o=o->next)owners++;
        stats.textures++;stats.owners+=owners;stats.bytes+=e->gpu_bytes;
        stats.saved_bytes+=e->gpu_bytes*(owners-1);
    }
    return stats;
}
