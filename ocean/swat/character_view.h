#ifndef SWAT_CHARACTER_VIEW_H
#define SWAT_CHARACTER_VIEW_H
#include "character_asset.h"
#include <string.h>
static inline bool swat_character_body_mesh(const char* name) {
    return name && (strstr(name,"full body") || !strcmp(name,"SWAT_Wearer"));
}
#include "lighting.h"
typedef struct SwatCharacterView {
    SwatCharacterAsset* asset;
    Model model;
    unsigned char* visible;
    bool gpu;
    Shader fallback;
    Texture2D *influence_maps,*palette_maps;
    float** palette_pixels;
    Mesh* arms;
    // Borrowed rigid body replacement; source animation and magazine meshes
    // stay intact. Owner outlives this view and releases the model/textures.
    const Model* rigid_rifle;
    Matrix rifle_bridge;
    float weapon_normal_scale;
} SwatCharacterView;
bool swat_character_view_init(SwatCharacterView* view,const char* path,char* error,size_t capacity);
bool swat_character_view_init_gpu(SwatCharacterView* view,const char* path,char* error,size_t capacity);
bool swat_character_view_sample(SwatCharacterView* view,const char* clip,double time);
void swat_character_view_draw(const SwatCharacterView* view,SwatLighting* lighting,Matrix root);
void swat_character_view_draw_first_person(const SwatCharacterView* view,SwatLighting* lighting,Matrix root);
void swat_character_view_close(SwatCharacterView* view);
#endif
