#ifndef SWAT_CHARACTER_ASSET_H
#define SWAT_CHARACTER_ASSET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Independent presentation-only GLB consumer. No authority, camera, physics,
// gameplay events, Raylib skinning or influence reduction live in this module.
typedef struct SwatCharacterAsset SwatCharacterAsset;
typedef struct SwatArtMesh {
    const char* node_name;
    int node,primitive,vertices,triangles,influences;
    const float *bind_positions,*bind_normals,*texcoords;
    const uint32_t* indices;
    float *positions,*normals; // sampled scene-space geometry, owned by asset
    float base_color[4],roughness,metalness;
    bool visible,double_sided;
} SwatArtMesh;
typedef struct SwatArtInfo {
    int nodes,skins,joints,meshes,clips,max_influences,vertices_over_four;
    int vertices,triangles;
} SwatArtInfo;
SwatCharacterAsset* swat_character_load(const char* path,int influence_limit,char* error,size_t capacity);
SwatCharacterAsset* swat_character_load_bytes(const void* bytes,size_t size,int influence_limit,char* error,size_t capacity);
void swat_character_free(SwatCharacterAsset* asset);
SwatArtInfo swat_character_info(const SwatCharacterAsset* asset);
const SwatArtMesh* swat_character_mesh(const SwatCharacterAsset* asset,int mesh);
const char* swat_character_clip_name(const SwatCharacterAsset* asset,int clip);
double swat_character_clip_duration(const SwatCharacterAsset* asset,int clip);
// Exact per-channel timestamps/interpolation. NULL clip samples the rest pose;
// named clips clamp rather than loop. Seeking emits no events or side effects.
bool swat_character_sample(SwatCharacterAsset* asset,const char* clip,double seconds);
// Column-major scene-space node transform after sampling; pointers stay owned.
const float* swat_character_node_matrix(const SwatCharacterAsset* asset,int node);
int swat_character_find_node(const SwatCharacterAsset* asset,const char* name);
#ifdef __cplusplus
}
#endif
#endif
