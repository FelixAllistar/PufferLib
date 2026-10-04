#ifndef SWAT_CHARACTER_VIEW_H
#define SWAT_CHARACTER_VIEW_H
#include "character_asset.h"
#include "lighting.h"
typedef struct SwatCharacterView {
    SwatCharacterAsset* asset;
    Model model;
    unsigned char* visible;
} SwatCharacterView;
bool swat_character_view_init(SwatCharacterView* view,const char* path,char* error,size_t capacity);
bool swat_character_view_sample(SwatCharacterView* view,const char* clip,double time);
void swat_character_view_draw(const SwatCharacterView* view,const SwatLighting* lighting,Matrix root);
void swat_character_view_close(SwatCharacterView* view);
#endif
