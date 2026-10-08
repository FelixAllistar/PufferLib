#ifndef SWAT_MATERIALS_H
#define SWAT_MATERIALS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum SwatMaterial {
    SWAT_CONCRETE, SWAT_DRYWALL, SWAT_WOOD, SWAT_GLASS, SWAT_STEEL,
    SWAT_BRICK, SWAT_PLASTER, SWAT_INSULATION, SWAT_TILE, SWAT_CARPET,
    SWAT_SOIL, SWAT_STONE, SWAT_MATERIAL_COUNT
} SwatMaterial;
typedef struct SwatMaterialDef {
    const char* name;
    float resistance;
    float absorption[3]; // diffuse surface energy absorbed at low/mid/high bands
    float transmission_db[3]; // amplitude loss through reference thickness
    float reference_thickness;
    uint8_t color[4];
    float density, friction, restitution, rolling_resistance;
    float impact_pitch, impact_decay, footstep_gain;
    // Game-space structural values, independent of ballistic penetration.
    float fracture_health, impact_threshold, charge_resistance;
} SwatMaterialDef;
const SwatMaterialDef* swat_material(SwatMaterial material);
#ifdef __cplusplus
}
#endif
#endif
