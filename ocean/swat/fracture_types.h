#ifndef SWAT_FRACTURE_TYPES_H
#define SWAT_FRACTURE_TYPES_H
#include "swat_math.h"
#define SWAT_IMPACT_ROWS 6
#define SWAT_IMPACT_COLS 5
#define SWAT_IMPACT_PATCHES (SWAT_IMPACT_ROWS*SWAT_IMPACT_COLS)
#define SWAT_MAX_CUTS (SWAT_IMPACT_PATCHES+2)
#define SWAT_CUT_SIDES 16
#define SWAT_CUT_SECTIONS 256
#define SWAT_CUT_VERTICES 32
// Patch kinds identify stable local regions. Repeated impacts grow the same
// convex opening instead of exhausting a lifetime count of bullet holes.
typedef enum SwatCutKind {SWAT_CUT_BREACH,SWAT_CUT_IMPACT,SWAT_CUT_PATCH,
    SWAT_CUT_KINDS=SWAT_CUT_PATCH+SWAT_IMPACT_PATCHES} SwatCutKind;
typedef struct SwatCut {
    float y,z,height,width; // Object-local centre and positive half extents.
    unsigned int seed; // Patch: four packed corner bevels; otherwise roughness seed.
    SwatCutKind kind;
} SwatCut;
typedef struct SwatFracturePolygon {int count;float vertices[SWAT_CUT_VERTICES][2];uint32_t exposed_edges;} SwatFracturePolygon;
typedef struct SwatFracture {
    int count,section_count;
    SwatCut cuts[SWAT_MAX_CUTS];
    SwatFracturePolygon* sections;
    b3HullData** pending_hulls; // Prepared transaction; NULL after physics commit.
} SwatFracture;
#endif
