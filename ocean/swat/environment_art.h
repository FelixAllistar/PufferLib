#ifndef SWAT_ENVIRONMENT_ART_H
#define SWAT_ENVIRONMENT_ART_H
#include "environment_binding.h"
#include "environment_props.h"
#include "motel.h"
#include "storefront.h"
#include "raylib.h"

typedef enum SwatSurfaceKind {
    SWAT_SURFACE_PLASTER, SWAT_SURFACE_FLOOR, SWAT_SURFACE_FRAME,
    SWAT_SURFACE_DOOR_PAINT, SWAT_SURFACE_DOOR_WOOD, SWAT_SURFACE_WORN, SWAT_SURFACE_COUNT
} SwatSurfaceKind;
typedef struct SwatSurfaceMaps {
    Texture2D color,normal,roughness;
    Vector2 tile;
} SwatSurfaceMaps;
struct SwatLighting;
struct SwatMotelWallArt;
#define SWAT_ROOM101_ASSETS 8
#define SWAT_ROOM101_V3_ASSETS 12
#define SWAT_ROOM101_V4_ASSETS 8
#define SWAT_ROOM101_MATERIALS 8
#define SWAT_LOCATION_MATERIALS 16

typedef struct SwatEnvironmentArt {
    bool initialized;
    int location; // Only the current mission's module bank resides on the GPU.
    bool lit;
    int shadow_room; // Set by depth callbacks; -1 for sun/contact passes.
    Texture2D plaster,wood;
    float plaster_tile_metres;
    bool legacy_plaster;
    SwatSurfaceMaps surfaces[SWAT_SURFACE_COUNT];
    SwatSurfaceMaps motel_asphalt;
    struct SwatLighting* lighting;
    Model door;
    Model motel[SWAT_MOTEL_ASSETS];
    struct SwatMotelWallArt* motel_wall_art;
    Model motel_dressing[SWAT_MOTEL_DRESSING_ASSETS];
    float motel_dressing_normal_scale[SWAT_MOTEL_DRESSING_ASSETS][SWAT_ROOM101_MATERIALS];
    int motel_dressing_occlusion_uv[SWAT_MOTEL_DRESSING_ASSETS][SWAT_ROOM101_MATERIALS];
    int motel_occlusion_uv[SWAT_MOTEL_ASSETS][SWAT_LOCATION_MATERIALS];
    Model motel_numbers[3]; // Rooms 102–104; 101 keeps its accepted v3 asset.
    float motel_number_normal_scale[3][SWAT_ROOM101_MATERIALS];
    Model motel_reception;
    Model motel_roadside;
    float motel_roadside_normal_scale[SWAT_ROOM101_MATERIALS];
    Model motel_personal[2]; // Original Room 103 wallet/glasses, owned by desk 72.
    float motel_personal_normal_scale[2][SWAT_ROOM101_MATERIALS];
    float motel_reception_normal_scale[SWAT_ROOM101_MATERIALS];
    Model room101[SWAT_ROOM101_ASSETS];
    float room101_normal_scale[SWAT_ROOM101_ASSETS][SWAT_ROOM101_MATERIALS];
    bool room101_ready;
    Model room101_v3[SWAT_ROOM101_V3_ASSETS];
    float room101_v3_normal_scale[SWAT_ROOM101_V3_ASSETS][SWAT_ROOM101_MATERIALS];
    bool room101_v3_ready;
    Model room101_v4[SWAT_ROOM101_V4_ASSETS];
    float room101_v4_normal_scale[SWAT_ROOM101_V4_ASSETS][SWAT_ROOM101_MATERIALS];
    bool room101_v4_ready;
    Model masonry_edge;
    float masonry_edge_normal_scale[SWAT_ROOM101_MATERIALS];
    Model room101_desk;
    float room101_desk_normal_scale[SWAT_ROOM101_MATERIALS];
    Model storefront[SWAT_STOREFRONT_ASSETS];
    Model props[SWAT_ENV_PROP_KINDS];
} SwatEnvironmentArt;

// Call only with an active graphics context; headless simulation never loads art.
void swat_environment_art_init(SwatEnvironmentArt* art);
void swat_environment_art_prepare_location(SwatEnvironmentArt* art,const SwatWorld* world);
void swat_environment_art_close(SwatEnvironmentArt* art);
// Releases unique shared material textures once, then the model allocations.
void swat_art_model_close(Model model);
// Draws in the caller's object-local transform. False means use graybox fallback.
void swat_environment_fragment_draw(const SwatObject* object);
bool swat_environment_art_draw(const SwatEnvironmentArt* art,const SwatObject* o);
bool swat_environment_motel_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* object,bool shadow,bool cutaway);
bool swat_environment_storefront_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* object,bool shadow,bool cutaway);
// Blended GLB primitives follow all opaque geometry, sorted per camera.
void swat_environment_art_transparent(const SwatEnvironmentArt* art,const SwatWorld* world,Vector3 eye,bool cutaway);
bool swat_environment_art_has_floor(const SwatEnvironmentArt* art,SwatMaterial floor);
// Generated-only decorative tabletop clutter, using current accepted supports.
void swat_environment_art_draw_props(const SwatEnvironmentArt* art,const SwatWorld* world,
                                     const SwatLayout* layout);
#endif
