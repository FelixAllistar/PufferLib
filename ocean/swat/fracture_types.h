#ifndef SWAT_FRACTURE_TYPES_H
#define SWAT_FRACTURE_TYPES_H
#include "swat_math.h"
#define SWAT_MAX_CUTS 4
#define SWAT_CUT_SIDES 16
#define SWAT_CUT_SECTIONS 128
#define SWAT_CUT_VERTICES 32
typedef enum SwatCutKind {SWAT_CUT_BREACH,SWAT_CUT_IMPACT,SWAT_CUT_KINDS} SwatCutKind;
typedef struct SwatCut {
    float y,z,height,width; // Object-local centre and positive half extents.
    unsigned int seed;
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
