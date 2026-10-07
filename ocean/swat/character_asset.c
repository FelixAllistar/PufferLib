#include "character_asset.h"
#include "vendor/cgltf_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ART_NODES 4096
#define ART_VERTICES 1000000
#define ART_TRIANGLES 2000000
#define ART_INFLUENCES 32
#define ART_BYTES (256u*1024u*1024u)
typedef struct ArtNode {
    int parent;
    float translation[3],rotation[4],scale[3],world[16];
} ArtNode;
typedef struct ArtSkin { int count; int* nodes; float *inverse,*palette; } ArtSkin;
typedef struct ArtMesh {
    SwatArtMesh view;
    int skin,sets;
    uint16_t* joints;
    float* weights;
} ArtMesh;
struct SwatCharacterAsset {
    cgltf_data* source;
    void* bytes;
    SwatArtInfo info;
    ArtNode* nodes;
    int* order;
    ArtSkin* skins;
    ArtMesh* meshes;
    double* durations;
    bool pose_dirty;
};
static bool fail(char* error,size_t capacity,const char* reason) { if(error && capacity) snprintf(error,capacity,"%s",reason); return false; }
static void identity(float* m) { memset(m,0,16*sizeof(float)); m[0]=m[5]=m[10]=m[15]=1; }
static void multiply(float* out,const float* a,const float* b) {
    float result[16];
    for(int c=0;c<4;c++) for(int r=0;r<4;r++) {
        double value=0; for(int k=0;k<4;k++) value+=(double)a[k*4+r]*b[c*4+k]; result[c*4+r]=(float)value;
    }
    memcpy(out,result,sizeof(result));
}
static bool normalize4(float* q) {
    double n=0; for(int i=0;i<4;i++) n+=(double)q[i]*q[i];
    if(!isfinite(n) || n<1e-16) return false;
    float inverse=(float)(1/sqrt(n)); for(int i=0;i<4;i++) q[i]*=inverse; return true;
}
static void trs(float* m,const ArtNode* n) {
    const float *q=n->rotation,*s=n->scale; float x=q[0],y=q[1],z=q[2],w=q[3]; identity(m);
    m[0]=(1-2*(y*y+z*z))*s[0]; m[1]=2*(x*y+z*w)*s[0]; m[2]=2*(x*z-y*w)*s[0];
    m[4]=2*(x*y-z*w)*s[1]; m[5]=(1-2*(x*x+z*z))*s[1]; m[6]=2*(y*z+x*w)*s[1];
    m[8]=2*(x*z+y*w)*s[2]; m[9]=2*(y*z-x*w)*s[2]; m[10]=(1-2*(x*x+y*y))*s[2];
    memcpy(m+12,n->translation,3*sizeof(float));
}
static bool finite_values(const float* values,size_t count) { for(size_t i=0;i<count;i++) if(!isfinite(values[i])) return false; return true; }
static bool affine(const float* m) { return finite_values(m,16) && fabsf(m[3])<1e-6f && fabsf(m[7])<1e-6f && fabsf(m[11])<1e-6f && fabsf(m[15]-1)<1e-6f; }
static bool accessor_bounds(const cgltf_accessor* a) {
    if(!a->count || a->is_sparse || !a->buffer_view || !a->buffer_view->buffer->data) return false;
    const cgltf_buffer_view* v=a->buffer_view; size_t packed=cgltf_calc_size(a->type,a->component_type);
    // Checked subtraction/multiplication before calling cgltf's read helpers.
    if(!packed || a->offset>v->size || packed>v->size-a->offset || a->stride<packed) return false;
    return a->count-1<=(v->size-a->offset-packed)/a->stride;
}
static bool read_float(const cgltf_accessor* a,size_t index,float* output,int width) {
    return index<a->count && cgltf_accessor_read_float(a,index,output,(size_t)width) && finite_values(output,(size_t)width);
}
static bool vector_accessor(const cgltf_accessor* a,int width,size_t count) {
    return a && a->count==count && cgltf_num_components(a->type)==(size_t)width;
}
static bool hierarchy(SwatCharacterAsset* asset) {
    int count=asset->info.nodes,used=0; unsigned char* visited=calloc((size_t)count,1); int* chain=malloc((size_t)count*sizeof(int));
    if(!visited || !chain) { free(visited); free(chain); return false; }
    for(int i=0;i<count;i++) {
        int at=i,depth=0;
        while(at>=0 && !visited[at]) { visited[at]=1; chain[depth++]=at; at=asset->nodes[at].parent; }
        if(at>=0 && visited[at]==1) { free(visited); free(chain); return false; }
        while(depth) { int n=chain[--depth]; visited[n]=2; asset->order[used++]=n; }
    }
    free(visited); free(chain); return used==count;
}
static bool validate_source(SwatCharacterAsset* asset,char* error,size_t capacity) {
    cgltf_data* d=asset->source;
    if(d->file_type!=cgltf_file_type_glb || d->buffers_count!=1 || d->buffers[0].uri || d->buffers[0].size>d->bin_size)
        return fail(error,capacity,"Require a self-contained GLB with one embedded buffer.");
    if(d->nodes_count==0 || d->nodes_count>ART_NODES || d->skins_count>64 || d->animations_count>256 || d->meshes_count>256)
        return fail(error,capacity,"Character exceeds supported node/skin/clip/mesh limits.");
    if(d->extensions_required_count) return fail(error,capacity,"Required extensions need a separately implemented consumer.");
    for(size_t i=0;i<d->buffer_views_count;i++) {
        cgltf_buffer_view* v=&d->buffer_views[i];
        if(v->has_meshopt_compression || v->offset>v->buffer->size || v->size>v->buffer->size-v->offset)
            return fail(error,capacity,"Invalid or compressed buffer view.");
    }
    cgltf_options options={0};
    if(cgltf_load_buffers(&options,d,NULL)!=cgltf_result_success) return fail(error,capacity,"Cannot load embedded geometry buffer.");
    for(size_t i=0;i<d->accessors_count;i++) if(!accessor_bounds(&d->accessors[i]))
        return fail(error,capacity,"Empty, sparse or out-of-bounds accessor is unsupported.");
    if(cgltf_validate(d)!=cgltf_result_success) return fail(error,capacity,"GLB structural validation failed.");
    for(size_t i=0;i<d->nodes_count;i++) for(size_t j=0;j<d->nodes[i].children_count;j++) {
        if(d->nodes[i].children[j]->parent!=&d->nodes[i]) return fail(error,capacity,"A node has multiple parents.");
        for(size_t k=0;k<j;k++) if(d->nodes[i].children[k]==d->nodes[i].children[j]) return fail(error,capacity,"Duplicate node child.");
    }
    for(size_t i=0;i<d->images_count;i++) if(d->images[i].uri) return fail(error,capacity,"External image dependencies are unsupported.");
    return true;
}
static bool load_skin(SwatCharacterAsset* asset,int index) {
    cgltf_skin* source=&asset->source->skins[index]; ArtSkin* skin=&asset->skins[index]; skin->count=(int)source->joints_count;
    if(!skin->count || skin->count>ART_NODES) return false;
    skin->nodes=malloc((size_t)skin->count*sizeof(int)); skin->inverse=malloc((size_t)skin->count*16*sizeof(float)); skin->palette=malloc((size_t)skin->count*16*sizeof(float));
    if(!skin->nodes || !skin->inverse || !skin->palette) return false;
    if(source->inverse_bind_matrices && (!vector_accessor(source->inverse_bind_matrices,16,(size_t)skin->count) || source->inverse_bind_matrices->component_type!=cgltf_component_type_r_32f)) return false;
    for(int i=0;i<skin->count;i++) {
        skin->nodes[i]=(int)(source->joints[i]-asset->source->nodes);
        if(source->inverse_bind_matrices) { if(!read_float(source->inverse_bind_matrices,(size_t)i,skin->inverse+i*16,16)) return false; }
        else identity(skin->inverse+i*16);
        if(!affine(skin->inverse+i*16)) return false;
        for(int j=0;j<i;j++) if(skin->nodes[j]==skin->nodes[i]) return false;
    }
    asset->info.joints+=skin->count; return true;
}
static bool load_mesh(SwatCharacterAsset* asset,ArtMesh* mesh,cgltf_node* node,int primitive,int limit,char* error,size_t capacity) {
    cgltf_primitive* p=&node->mesh->primitives[primitive]; SwatArtMesh* v=&mesh->view;
    const cgltf_accessor *position=cgltf_find_accessor(p,cgltf_attribute_type_position,0),*normal=cgltf_find_accessor(p,cgltf_attribute_type_normal,0),*uv=cgltf_find_accessor(p,cgltf_attribute_type_texcoord,0);
    if(p->type!=cgltf_primitive_type_triangles || p->targets_count || !position || position->count>ART_VERTICES || position->type!=cgltf_type_vec3 || position->component_type!=cgltf_component_type_r_32f)
        return fail(error,capacity,"Require triangle primitives with float positions and no morph targets.");
    size_t n=position->count; v->vertices=(int)n; v->node=(int)(node-asset->source->nodes); v->node_name=node->name ? node->name : ""; v->primitive=primitive;
    if(!vector_accessor(normal,3,n) || (uv && !vector_accessor(uv,2,n))) return fail(error,capacity,"Missing or inconsistent normal/UV attributes.");
    if((p->indices ? p->indices->count : n)/3>ART_TRIANGLES) return fail(error,capacity,"Triangle count exceeds preview limits.");
    v->triangles=(int)((p->indices ? p->indices->count : n)/3);
    if((p->indices ? p->indices->count : n)%3 || !v->triangles || (p->indices && (p->indices->type!=cgltf_type_scalar || (p->indices->component_type!=cgltf_component_type_r_8u && p->indices->component_type!=cgltf_component_type_r_16u && p->indices->component_type!=cgltf_component_type_r_32u) || p->indices->normalized)))
        return fail(error,capacity,"Invalid triangle index buffer.");
    v->bind_positions=malloc(n*3*sizeof(float)); v->bind_normals=malloc(n*3*sizeof(float)); v->texcoords=calloc(n*2,sizeof(float));
    v->positions=malloc(n*3*sizeof(float)); v->normals=malloc(n*3*sizeof(float)); v->indices=malloc((size_t)v->triangles*3*sizeof(uint32_t));
    if(!v->bind_positions || !v->bind_normals || !v->texcoords || !v->positions || !v->normals || !v->indices) return fail(error,capacity,"Character geometry allocation failed.");
    for(size_t i=0;i<n;i++) if(!read_float(position,i,(float*)v->bind_positions+i*3,3) || !read_float(normal,i,(float*)v->bind_normals+i*3,3) || (uv && !read_float(uv,i,(float*)v->texcoords+i*2,2)))
        return fail(error,capacity,"Nonfinite vertex attributes.");
    for(size_t i=0;i<(size_t)v->triangles*3;i++) { size_t index=p->indices ? cgltf_accessor_read_index(p->indices,i) : i; if(index>=n) return fail(error,capacity,"Vertex index outside primitive."); ((uint32_t*)v->indices)[i]=(uint32_t)index; }
    mesh->skin=node->skin ? (int)(node->skin-asset->source->skins) : -1;
    for(size_t i=0;i<p->attributes_count;i++) if(p->attributes[i].type==cgltf_attribute_type_joints || p->attributes[i].type==cgltf_attribute_type_weights) {
        if(p->attributes[i].index<0 || p->attributes[i].index>=ART_INFLUENCES/4 || mesh->skin<0) return fail(error,capacity,"Unsupported or unbound skin attribute set.");
        if(p->attributes[i].index+1>mesh->sets) mesh->sets=p->attributes[i].index+1;
        for(size_t j=0;j<i;j++) if(p->attributes[j].type==p->attributes[i].type && p->attributes[j].index==p->attributes[i].index) return fail(error,capacity,"Duplicate skin attribute set.");
    }
    if(mesh->skin>=0) {
        if(!mesh->sets) return fail(error,capacity,"Skinned mesh has no influence sets.");
        v->influences=mesh->sets*4; mesh->joints=calloc(n*(size_t)v->influences,sizeof(uint16_t)); mesh->weights=calloc(n*(size_t)v->influences,sizeof(float));
        if(!mesh->joints || !mesh->weights) return fail(error,capacity,"Skin influence allocation failed.");
        for(int set=0;set<mesh->sets;set++) {
            const cgltf_accessor* joints=cgltf_find_accessor(p,cgltf_attribute_type_joints,set),*weights=cgltf_find_accessor(p,cgltf_attribute_type_weights,set);
            if(!vector_accessor(joints,4,n) || !vector_accessor(weights,4,n) || joints->normalized ||
                (joints->component_type!=cgltf_component_type_r_8u && joints->component_type!=cgltf_component_type_r_16u) ||
                !((weights->component_type==cgltf_component_type_r_32f && !weights->normalized) || ((weights->component_type==cgltf_component_type_r_8u || weights->component_type==cgltf_component_type_r_16u) && weights->normalized))) return fail(error,capacity,"Missing or invalid paired joint/weight sets.");
            for(size_t i=0;i<n;i++) {
                unsigned int ids[4]; float w[4];
                if(!cgltf_accessor_read_uint(joints,i,ids,4) || !read_float(weights,i,w,4)) return fail(error,capacity,"Cannot read skin influences.");
                for(int k=0;k<4;k++) {
                    if(ids[k]>=(unsigned)asset->skins[mesh->skin].count || w[k]<0 || w[k]>1.00001f) return fail(error,capacity,"Joint reference or influence weight outside bounds.");
                    size_t at=i*(size_t)v->influences+set*4+k; mesh->joints[at]=(uint16_t)ids[k]; mesh->weights[at]=w[k];
                }
            }
        }
        for(size_t i=0;i<n;i++) {
            double sum=0; int active=0;
            for(int k=0;k<v->influences;k++) { float w=mesh->weights[i*(size_t)v->influences+k]; sum+=w; active+=w>0; }
            if(fabs(sum-1)>0.0001 || !active) return fail(error,capacity,"Skin weights must already be normalized; no reduction is performed.");
            if(active>limit) return fail(error,capacity,"Consumer influence limit would truncate this mesh.");
            if(active>asset->info.max_influences) asset->info.max_influences=active;
            asset->info.vertices_over_four+=active>4;
        }
    }
    v->joint_ids=mesh->joints; v->joint_weights=mesh->weights;
    v->joint_nodes=mesh->skin>=0 ? asset->skins[mesh->skin].nodes : NULL;
    v->palette=mesh->skin>=0 ? asset->skins[mesh->skin].palette : asset->nodes[v->node].world;
    v->palette_count=mesh->skin>=0 ? asset->skins[mesh->skin].count : 1;
    v->base_color[0]=v->base_color[1]=v->base_color[2]=v->base_color[3]=1; v->roughness=1; v->metalness=1;
    if(p->material) {
        const cgltf_material* material=p->material;
        if(material->alpha_mode!=cgltf_alpha_mode_opaque || material->unlit || material->has_pbr_specular_glossiness ||
            material->has_clearcoat || material->has_transmission || material->has_volume || material->has_ior || material->has_specular ||
            material->has_sheen || material->has_emissive_strength || material->has_iridescence || material->has_diffuse_transmission ||
            material->has_anisotropy || material->has_dispersion || material->extensions_count ||
            material->emissive_factor[0]!=0 || material->emissive_factor[1]!=0 || material->emissive_factor[2]!=0)
            return fail(error,capacity,"Extended, emissive or transparent character materials need an explicit material adapter.");
        v->double_sided=p->material->double_sided;
        if(p->material->has_pbr_metallic_roughness) {
            memcpy(v->base_color,p->material->pbr_metallic_roughness.base_color_factor,sizeof(v->base_color));
            v->roughness=p->material->pbr_metallic_roughness.roughness_factor; v->metalness=p->material->pbr_metallic_roughness.metallic_factor;
        }
        if(p->material->pbr_metallic_roughness.base_color_texture.texture || p->material->normal_texture.texture || p->material->pbr_metallic_roughness.metallic_roughness_texture.texture || p->material->occlusion_texture.texture || p->material->emissive_texture.texture)
            return fail(error,capacity,"Textured character materials need an explicit material adapter; this consumer preserves neutral fixture materials only.");
    }
    if(!finite_values(v->base_color,4) || !isfinite(v->roughness) || !isfinite(v->metalness)) return fail(error,capacity,"Invalid material factors.");
    for(int i=0;i<4;i++) if(v->base_color[i]<0 || v->base_color[i]>1) return fail(error,capacity,"Material color outside unit range.");
    if(v->roughness<0 || v->roughness>1 || v->metalness<0 || v->metalness>1) return fail(error,capacity,"Material factors outside unit range.");
    asset->info.vertices+=v->vertices; asset->info.triangles+=v->triangles;
    return (asset->info.vertices<=ART_VERTICES && asset->info.triangles<=ART_TRIANGLES) || fail(error,capacity,"Combined geometry exceeds preview limits.");
}
static bool load_animations(SwatCharacterAsset* asset,char* error,size_t capacity) {
    cgltf_data* d=asset->source;
    for(size_t c=0;c<d->animations_count;c++) {
        cgltf_animation* clip=&d->animations[c];
        if(!clip->name || !clip->name[0] || clip->channels_count>ART_NODES*3) return fail(error,capacity,"Require named clips and bounded channel counts.");
        for(size_t other=0;other<c;other++) if(!strcmp(clip->name,d->animations[other].name)) return fail(error,capacity,"Ambiguous duplicate clip name.");
        for(size_t k=0;k<clip->channels_count;k++) {
            cgltf_animation_channel* channel=&clip->channels[k]; cgltf_animation_sampler* sampler=channel->sampler;
            int width=channel->target_path==cgltf_animation_path_type_rotation ? 4 : 3;
            if(!channel->target_node || channel->target_node->has_matrix ||
                (channel->target_path!=cgltf_animation_path_type_rotation && channel->target_path!=cgltf_animation_path_type_translation && channel->target_path!=cgltf_animation_path_type_scale)) return fail(error,capacity,"Unsupported animation target (matrix/morph/pointer).");
            for(size_t old=0;old<k;old++) if(clip->channels[old].target_node==channel->target_node && clip->channels[old].target_path==channel->target_path) return fail(error,capacity,"Duplicate animation channel target.");
            if(sampler->interpolation!=cgltf_interpolation_type_linear && sampler->interpolation!=cgltf_interpolation_type_step && sampler->interpolation!=cgltf_interpolation_type_cubic_spline) return fail(error,capacity,"Unsupported interpolation mode.");
            size_t multiple=sampler->interpolation==cgltf_interpolation_type_cubic_spline ? 3 : 1;
            if(!sampler->input || sampler->input->type!=cgltf_type_scalar || sampler->input->component_type!=cgltf_component_type_r_32f || sampler->input->count>360000 ||
                !vector_accessor(sampler->output,width,sampler->input->count*multiple) || sampler->output->component_type!=cgltf_component_type_r_32f) return fail(error,capacity,"Animation sampler layout mismatch.");
            float previous=-1;
            for(size_t key=0;key<sampler->input->count;key++) {
                float time,values[4]; if(!read_float(sampler->input,key,&time,1) || time<=previous || time>3600) return fail(error,capacity,"Animation times must be finite, nonnegative and strictly increasing.");
                if(time<0) return fail(error,capacity,"Negative animation timestamp.");
                previous=time;
                for(size_t part=0;part<multiple;part++) if(!read_float(sampler->output,key*multiple+part,values,width) ||
                    (width==4 && part==(multiple==3 ? 1u : 0u) && !normalize4(values))) return fail(error,capacity,"Invalid animation transform value.");
            }
            if(previous>asset->durations[c]) asset->durations[c]=previous;
        }
    }
    return true;
}
SwatCharacterAsset* swat_character_load_bytes(const void* bytes,size_t size,int limit,char* error,size_t capacity) {
    if(error && capacity) error[0]=0;
    if(!bytes || size<28 || size>ART_BYTES || limit<1 || limit>ART_INFLUENCES) { fail(error,capacity,"Invalid character input or influence limit."); return NULL; }
    SwatCharacterAsset* asset=calloc(1,sizeof(*asset)); if(!asset) return NULL;
    asset->bytes=malloc(size); if(!asset->bytes) goto failed;
    memcpy(asset->bytes,bytes,size); cgltf_options options={0};
    if(cgltf_parse(&options,asset->bytes,size,&asset->source)!=cgltf_result_success || !validate_source(asset,error,capacity)) goto failed;
    cgltf_data* d=asset->source; asset->info.nodes=(int)d->nodes_count; asset->info.skins=(int)d->skins_count; asset->info.clips=(int)d->animations_count;
    asset->nodes=calloc(d->nodes_count,sizeof(ArtNode)); asset->order=calloc(d->nodes_count,sizeof(int)); asset->skins=calloc(d->skins_count ? d->skins_count : 1,sizeof(ArtSkin));
    asset->durations=calloc(d->animations_count ? d->animations_count : 1,sizeof(double));
    if(!asset->nodes || !asset->order || !asset->skins || !asset->durations) goto failed;
    for(size_t i=0;i<d->nodes_count;i++) {
        cgltf_node* node=&d->nodes[i]; asset->nodes[i].parent=node->parent ? (int)(node->parent-d->nodes) : -1;
        float local[16]; cgltf_node_transform_local(node,local); if(!affine(local)) { fail(error,capacity,"Nonfinite or non-affine rest node transform."); goto failed; }
        if(node->mesh) {
            if(node->mesh->primitives_count>512 || node->mesh->primitives_count>(size_t)(512-asset->info.meshes)) { fail(error,capacity,"Too many instantiated mesh primitives."); goto failed; }
            asset->info.meshes+=(int)node->mesh->primitives_count;
        }
    }
    if(asset->info.meshes<1 || asset->info.meshes>512 || !hierarchy(asset)) { fail(error,capacity,"Invalid mesh count or cyclic node hierarchy."); goto failed; }
    asset->meshes=calloc((size_t)asset->info.meshes,sizeof(ArtMesh)); if(!asset->meshes) goto failed;
    for(int i=0;i<asset->info.skins;i++) if(!load_skin(asset,i)) { fail(error,capacity,"Invalid skin joint/inverse-bind mapping."); goto failed; }
    int at=0;
    for(size_t i=0;i<d->nodes_count;i++) if(d->nodes[i].mesh) for(size_t p=0;p<d->nodes[i].mesh->primitives_count;p++)
        if(!load_mesh(asset,&asset->meshes[at++],&d->nodes[i],(int)p,limit,error,capacity)) goto failed;
    if(!load_animations(asset,error,capacity) || !swat_character_sample(asset,NULL,0)) goto failed;
    if(error && capacity) error[0]=0;
    return asset;
failed:
    if(error && capacity && !error[0]) fail(error,capacity,"Character parse/allocation failed.");
    swat_character_free(asset); return NULL;
}
SwatCharacterAsset* swat_character_load(const char* path,int limit,char* error,size_t capacity) {
    if(error && capacity) error[0]=0;
    FILE* file=path ? fopen(path,"rb") : NULL; if(!file) { fail(error,capacity,"Character GLB is missing."); return NULL; }
    if(fseek(file,0,SEEK_END)!=0) { fclose(file); return NULL; }
    long count=ftell(file); if(count<28 || (unsigned long)count>ART_BYTES || fseek(file,0,SEEK_SET)!=0) { fclose(file); fail(error,capacity,"Invalid character file size."); return NULL; }
    void* bytes=malloc((size_t)count); if(!bytes) { fclose(file); return NULL; }
    bool ok=fread(bytes,1,(size_t)count,file)==(size_t)count; fclose(file);
    SwatCharacterAsset* asset=ok ? swat_character_load_bytes(bytes,(size_t)count,limit,error,capacity) : NULL; free(bytes); return asset;
}
void swat_character_free(SwatCharacterAsset* asset) {
    if(!asset) return;
    if(asset->meshes) for(int i=0;i<asset->info.meshes;i++) {
        ArtMesh* mesh=&asset->meshes[i]; SwatArtMesh* v=&mesh->view;
        free((void*)v->bind_positions); free((void*)v->bind_normals); free((void*)v->texcoords); free((void*)v->indices); free(v->positions); free(v->normals); free(mesh->joints); free(mesh->weights);
    }
    if(asset->skins) for(int i=0;i<asset->info.skins;i++) { free(asset->skins[i].nodes); free(asset->skins[i].inverse); free(asset->skins[i].palette); }
    free(asset->meshes); free(asset->skins); free(asset->nodes); free(asset->order); free(asset->durations); cgltf_free(asset->source); free(asset->bytes); free(asset);
}
static bool interpolate(cgltf_animation_channel* channel,double time,float* result) {
    cgltf_animation_sampler* s=channel->sampler; size_t count=s->input->count,left=0,right=count-1; float t0,t1;
    read_float(s->input,0,&t0,1); read_float(s->input,right,&t1,1);
    if(time<=t0) right=left=0;
    else if(time>=t1) left=right;
    else {
        while(right-left>1) { size_t mid=(left+right)/2; float at; read_float(s->input,mid,&at,1); if(time<at) right=mid; else left=mid; }
    }
    int width=channel->target_path==cgltf_animation_path_type_rotation ? 4 : 3;
    bool cubic=s->interpolation==cgltf_interpolation_type_cubic_spline;
    size_t multiple=cubic ? 3 : 1; float a[4],b[4];
    read_float(s->output,left*multiple+(cubic ? 1 : 0),a,width);
    if(left==right || s->interpolation==cgltf_interpolation_type_step) memcpy(result,a,(size_t)width*sizeof(float));
    else {
        read_float(s->output,right*multiple+(cubic ? 1 : 0),b,width);
        read_float(s->input,left,&t0,1); read_float(s->input,right,&t1,1); double fraction=(time-t0)/(t1-t0);
        if(cubic) {
            float outgoing[4],incoming[4]; read_float(s->output,left*3+2,outgoing,width); read_float(s->output,right*3,incoming,width);
            double u=fraction,u2=u*u,u3=u2*u,dt=t1-t0;
            for(int i=0;i<width;i++) result[i]=(float)((2*u3-3*u2+1)*a[i]+(u3-2*u2+u)*dt*outgoing[i]+(-2*u3+3*u2)*b[i]+(u3-u2)*dt*incoming[i]);
        } else if(width==4) {
            normalize4(a); normalize4(b); double dot=0; for(int i=0;i<4;i++) dot+=(double)a[i]*b[i];
            if(dot<0) { dot=-dot; for(int i=0;i<4;i++) b[i]=-b[i]; }
            if(dot>1) dot=1;
            double wa=1-fraction,wb=fraction;
            if(dot<.9995) { double angle=acos(dot),inverse=1/sin(angle); wa=sin((1-fraction)*angle)*inverse; wb=sin(fraction*angle)*inverse; }
            for(int i=0;i<4;i++) result[i]=(float)(wa*a[i]+wb*b[i]);
        } else for(int i=0;i<width;i++) result[i]=(float)(a[i]+fraction*(b[i]-a[i]));
    }
    return finite_values(result,(size_t)width) && (width!=4 || normalize4(result));
}
static bool normal_transform(float* out,const float* m,const float* input) {
    double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(fabs(determinant)<1e-14) { out[0]=out[1]=out[2]=0; return false; }
    double x=((e*i-f*h)*input[0]+(f*g-d*i)*input[1]+(d*h-e*g)*input[2])/determinant;
    double y=((c*h-b*i)*input[0]+(a*i-c*g)*input[1]+(b*g-a*h)*input[2])/determinant;
    double z=((b*f-c*e)*input[0]+(c*d-a*f)*input[1]+(a*e-b*d)*input[2])/determinant;
    double length=sqrt(x*x+y*y+z*z); if(!isfinite(length) || length<1e-14) { out[0]=out[1]=out[2]=0; return false; }
    out[0]=(float)(x/length); out[1]=(float)(y/length); out[2]=(float)(z/length); return true;
}
static void refresh_pose(SwatCharacterAsset* asset) {
    asset->pose_dirty=false;
    for(int i=0;i<asset->info.skins;i++) {
        ArtSkin* skin=&asset->skins[i]; for(int j=0;j<skin->count;j++) multiply(skin->palette+j*16,asset->nodes[skin->nodes[j]].world,skin->inverse+j*16);
    }
    for(int k=0;k<asset->info.meshes;k++) {
        SwatArtMesh* v=&asset->meshes[k].view; v->visible=false;
        // Conservative visibility: zero-scale props stay collapsed. The GPU
        // handles singular blended vertices identically to the CPU consumer.
        for(int j=0;j<(v->influences ? v->vertices*v->influences : 1);j++) {
            if(v->influences && v->joint_weights[j]==0) continue;
            const float* m=v->palette+(v->influences ? v->joint_ids[j]*16 : 0);
            double det=(double)m[0]*(m[5]*m[10]-m[9]*m[6])-(double)m[4]*(m[1]*m[10]-m[9]*m[2])+(double)m[8]*(m[1]*m[6]-m[5]*m[2]);
            if(fabs(det)>1e-14) { v->visible=true; break; }
        }
    }
}
float swat_art_normal_scale(const char* path) {
    cgltf_options options={0}; cgltf_data* source=NULL; float scale=1;
    if(path && cgltf_parse_file(&options,path,&source)==cgltf_result_success) {
        if(source->meshes_count && source->meshes[0].primitives_count) {
            const cgltf_material* material=source->meshes[0].primitives[0].material;
            if(material && material->normal_texture.texture && isfinite(material->normal_texture.scale) &&
               material->normal_texture.scale>=0 && material->normal_texture.scale<=8) scale=material->normal_texture.scale;
        }
        cgltf_free(source);
    }
    return scale;
}
int swat_art_material_factors(const char* path,SwatArtMaterialFactors* out,int capacity) {
    cgltf_options options={0}; cgltf_data* source=NULL;
    if(!path || !out || capacity<0 || cgltf_parse_file(&options,path,&source)!=cgltf_result_success) return -1;
    if(source->materials_count>(cgltf_size)capacity) { cgltf_free(source); return -1; }
    int count=(int)source->materials_count;
    for(int i=0;i<count;i++) {
        const cgltf_material* m=&source->materials[i];
        const cgltf_pbr_metallic_roughness* p=&m->pbr_metallic_roughness;
        out[i]=(SwatArtMaterialFactors){.base_color={1,1,1,1},.roughness=1,.metalness=1,.normal_scale=1,.occlusion_strength=1};
        if(m->has_pbr_metallic_roughness) {
            memcpy(out[i].base_color,p->base_color_factor,sizeof(out[i].base_color));
            out[i].roughness=p->roughness_factor; out[i].metalness=p->metallic_factor;
        }
        if(m->normal_texture.texture) out[i].normal_scale=m->normal_texture.scale;
        if(m->occlusion_texture.texture) out[i].occlusion_strength=m->occlusion_texture.scale;
    }
    cgltf_free(source); return count;
}
bool swat_character_sample_pose(SwatCharacterAsset* asset,const char* clip_name,double time) {
    if(!asset || !isfinite(time)) return false;
    cgltf_animation* clip=NULL;
    if(clip_name) {
        for(int i=0;i<asset->info.clips;i++) if(!strcmp(asset->source->animations[i].name,clip_name)) { clip=&asset->source->animations[i]; break; }
        if(!clip) return false;
    }
    for(int i=0;i<asset->info.nodes;i++) {
        const cgltf_node* source=&asset->source->nodes[i]; ArtNode* n=&asset->nodes[i];
        memcpy(n->translation,source->translation,sizeof(n->translation)); memcpy(n->rotation,source->rotation,sizeof(n->rotation)); memcpy(n->scale,source->scale,sizeof(n->scale));
        if(!normalize4(n->rotation)) return false;
    }
    if(clip) for(size_t i=0;i<clip->channels_count;i++) {
        cgltf_animation_channel* channel=&clip->channels[i]; ArtNode* n=&asset->nodes[channel->target_node-asset->source->nodes];
        float* output=channel->target_path==cgltf_animation_path_type_rotation ? n->rotation : channel->target_path==cgltf_animation_path_type_translation ? n->translation : n->scale;
        if(!interpolate(channel,time,output)) return false;
    }
    for(int k=0;k<asset->info.nodes;k++) {
        int i=asset->order[k]; ArtNode* n=&asset->nodes[i]; float local[16];
        if(asset->source->nodes[i].has_matrix) memcpy(local,asset->source->nodes[i].matrix,sizeof(local)); else trs(local,n);
        if(n->parent>=0) multiply(n->world,asset->nodes[n->parent].world,local); else memcpy(n->world,local,sizeof(local));
        if(!finite_values(n->world,16)) return false;
    }
    refresh_pose(asset); return true;
}
bool swat_character_skin(SwatCharacterAsset* asset) {
    if(!asset) return false;
    if(asset->pose_dirty) refresh_pose(asset);
    for(int k=0;k<asset->info.meshes;k++) {
        ArtMesh* mesh=&asset->meshes[k]; SwatArtMesh* v=&mesh->view; v->visible=false;
        for(int j=0;j<v->vertices;j++) {
            float matrix[16];
            if(mesh->skin>=0) {
                memset(matrix,0,sizeof(matrix));
                for(int w=0;w<v->influences;w++) {
                    size_t at=(size_t)j*v->influences+w; float weight=mesh->weights[at]; if(weight==0) continue;
                    const float* joint=asset->skins[mesh->skin].palette+mesh->joints[at]*16;
                    for(int q=0;q<16;q++) matrix[q]+=weight*joint[q];
                }
            } else memcpy(matrix,asset->nodes[v->node].world,sizeof(matrix));
            const float* p=v->bind_positions+j*3;
            for(int c=0;c<3;c++) v->positions[j*3+c]=matrix[c]*p[0]+matrix[4+c]*p[1]+matrix[8+c]*p[2]+matrix[12+c];
            v->visible|=normal_transform(v->normals+j*3,matrix,v->bind_normals+j*3);
        }
        if(!finite_values(v->positions,(size_t)v->vertices*3) || !finite_values(v->normals,(size_t)v->vertices*3)) return false;
    }
    return true;
}
bool swat_character_sample(SwatCharacterAsset* asset,const char* clip_name,double time) {
    return swat_character_sample_pose(asset,clip_name,time) && swat_character_skin(asset);
}
bool swat_character_transform_node(SwatCharacterAsset* asset,int node,const float delta[16]) {
    if(!asset || node<0 || node>=asset->info.nodes || !affine(delta)) return false;
    for(int i=0;i<asset->info.nodes;i++) {
        int parent=i;
        while(parent>=0 && parent!=node) parent=asset->nodes[parent].parent;
        if(parent==node) { multiply(asset->nodes[i].world,delta,asset->nodes[i].world); if(!affine(asset->nodes[i].world)) return false; }
    }
    asset->pose_dirty=true; return true;
}
bool swat_character_finalize_pose(SwatCharacterAsset* asset) {
    if(!asset) return false;
    if(asset->pose_dirty) refresh_pose(asset);
    return true;
}
bool swat_character_capture_pose(const SwatCharacterAsset* asset,float* matrices,size_t count) {
    if(!asset || !matrices || count!=(size_t)asset->info.nodes*16) return false;
    for(int i=0;i<asset->info.nodes;i++) memcpy(matrices+i*16,asset->nodes[i].world,16*sizeof(float));
    return true;
}
bool swat_character_restore_pose(SwatCharacterAsset* asset,const float* matrices,size_t count) {
    if(!asset || !matrices || count!=(size_t)asset->info.nodes*16) return false;
    for(int i=0;i<asset->info.nodes;i++) if(!affine(matrices+i*16)) return false;
    for(int i=0;i<asset->info.nodes;i++) memcpy(asset->nodes[i].world,matrices+i*16,16*sizeof(float));
    refresh_pose(asset); return true;
}
SwatArtInfo swat_character_info(const SwatCharacterAsset* asset) { return asset ? asset->info : (SwatArtInfo){0}; }
const SwatArtMesh* swat_character_mesh(const SwatCharacterAsset* asset,int index) { return asset && index>=0 && index<asset->info.meshes ? &asset->meshes[index].view : NULL; }
const char* swat_character_clip_name(const SwatCharacterAsset* asset,int clip) { return asset && clip>=0 && clip<asset->info.clips ? asset->source->animations[clip].name : NULL; }
double swat_character_clip_duration(const SwatCharacterAsset* asset,int clip) { return asset && clip>=0 && clip<asset->info.clips ? asset->durations[clip] : 0; }
const float* swat_character_node_matrix(const SwatCharacterAsset* asset,int node) { return asset && node>=0 && node<asset->info.nodes ? asset->nodes[node].world : NULL; }
int swat_character_find_node(const SwatCharacterAsset* asset,const char* name) {
    if(!asset || !name) return -1;
    int found=-1;
    for(int i=0;i<asset->info.nodes;i++) if(asset->source->nodes[i].name && !strcmp(asset->source->nodes[i].name,name)) { if(found>=0) return -1; found=i; }
    return found;
}
const char* swat_character_node_name(const SwatCharacterAsset* asset,int node) {
    return asset && node>=0 && node<asset->info.nodes ? asset->source->nodes[node].name : NULL;
}
