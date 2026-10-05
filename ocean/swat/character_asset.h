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
    const uint16_t* joint_ids;
    const int* joint_nodes;
    const float *joint_weights,*palette; // all sets; palette is column-major
    int palette_count;
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
// Skeleton-only evaluation for full-influence GPU deformation. Source curves
// stay exact; presentation corrections affect this sampled pose only.
bool swat_character_sample_pose(SwatCharacterAsset* asset,const char* clip,double seconds);
bool swat_character_skin(SwatCharacterAsset* asset);
bool swat_character_transform_node(SwatCharacterAsset* asset,int node,const float delta[16]);
bool swat_character_finalize_pose(SwatCharacterAsset* asset);
// Snapshot node-world matrices for reuse across camera/shadow passes.
bool swat_character_capture_pose(const SwatCharacterAsset* asset,float* matrices,size_t count);
bool swat_character_restore_pose(SwatCharacterAsset* asset,const float* matrices,size_t count);
// Column-major scene-space node transform after sampling; pointers stay owned.
const float* swat_character_node_matrix(const SwatCharacterAsset* asset,int node);
int swat_character_find_node(const SwatCharacterAsset* asset,const char* name);
const char* swat_character_node_name(const SwatCharacterAsset* asset,int node);
#ifdef __cplusplus
}
#endif
#endif
