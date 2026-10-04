#ifndef SWAT_ENVIRONMENT_ART_H
#define SWAT_ENVIRONMENT_ART_H
#include "environment_binding.h"
#include "raylib.h"

typedef struct SwatEnvironmentArt {
    bool initialized;
    Texture2D plaster,wood;
    Model door;
} SwatEnvironmentArt;

// Call only with an active graphics context; headless simulation never loads art.
void swat_environment_art_init(SwatEnvironmentArt* art);
void swat_environment_art_close(SwatEnvironmentArt* art);
// Draws in the caller's object-local transform. False means use graybox fallback.
bool swat_environment_art_draw(const SwatEnvironmentArt* art,const SwatObject* o);
bool swat_environment_art_has_floor(const SwatEnvironmentArt* art,SwatMaterial floor);
#endif
