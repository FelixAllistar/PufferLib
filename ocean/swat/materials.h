#ifndef SWAT_MATERIALS_H
#define SWAT_MATERIALS_H
#include <stdint.h>
typedef enum SwatMaterial {
    SWAT_CONCRETE, SWAT_DRYWALL, SWAT_WOOD, SWAT_GLASS, SWAT_STEEL,
    SWAT_BRICK, SWAT_PLASTER, SWAT_INSULATION, SWAT_TILE, SWAT_CARPET,
    SWAT_SOIL, SWAT_MATERIAL_COUNT
} SwatMaterial;
typedef struct SwatMaterialDef {
    const char* name;
    float resistance;
    float absorption[3]; // diffuse surface energy absorbed at low/mid/high bands
    float transmission_db[3]; // amplitude loss through reference thickness
    float reference_thickness;
    uint8_t color[4];
} SwatMaterialDef;
const SwatMaterialDef* swat_material(SwatMaterial material);
#endif
