#ifndef SWAT_ENVIRONMENT_ART_H
#define SWAT_ENVIRONMENT_ART_H
#include "environment_binding.h"
#include "environment_props.h"
#include "raylib.h"

typedef struct SwatEnvironmentArt {
    bool initialized;
    bool lit;
    Texture2D plaster,wood;
    float plaster_tile_metres;
    Model door;
    Model props[SWAT_ENV_PROP_KINDS];
} SwatEnvironmentArt;

// Call only with an active graphics context; headless simulation never loads art.
void swat_environment_art_init(SwatEnvironmentArt* art);
void swat_environment_art_close(SwatEnvironmentArt* art);
// Draws in the caller's object-local transform. False means use graybox fallback.
bool swat_environment_art_draw(const SwatEnvironmentArt* art,const SwatObject* o);
bool swat_environment_art_has_floor(const SwatEnvironmentArt* art,SwatMaterial floor);
// Generated-only decorative tabletop clutter, using current accepted supports.
void swat_environment_art_draw_props(const SwatEnvironmentArt* art,const SwatWorld* world,
                                     const SwatLayout* layout);
#endif
