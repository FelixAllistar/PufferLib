#include "materials.h"

// Authored game approximations. Surface absorption and transmission are
// different quantities; these are intentionally separate, editable tables.
// Transmission is per layer at reference thickness, not a building STC rating.
static const SwatMaterialDef definitions[SWAT_MATERIAL_COUNT]={
    {"Concrete",10000,{.02f,.04f,.06f},{40,55,70},.20f,{83,91,91,255}},
    {"Gypsum board",1.5f,{.29f,.10f,.05f},{8,14,22},.0125f,{190,188,172,255}},
    {"Timber",3,{.15f,.10f,.07f},{9,18,30},.038f,{139,99,56,255}},
    {"Window glass",.5f,{.18f,.06f,.03f},{12,20,30},.006f,{114,177,187,110}},
    {"Steel",80,{.03f,.04f,.05f},{25,40,55},.008f,{91,104,114,255}},
    {"Brick masonry",400,{.03f,.04f,.07f},{30,45,58},.10f,{133,92,76,255}},
    {"Exterior plaster",2,{.04f,.05f,.07f},{10,16,24},.016f,{162,164,150,255}},
    {"Fibrous insulation",.1f,{.35f,.80f,.95f},{2,6,12},.09f,{165,151,104,255}},
    {"Ceramic tile",25,{.02f,.03f,.04f},{18,28,38},.012f,{169,183,180,255}},
    {"Carpet",.1f,{.08f,.35f,.65f},{1,3,8},.012f,{108,108,94,255}},
    {"Earth",10000,{.25f,.50f,.75f},{40,55,70},.20f,{67,83,58,255}},
};
const SwatMaterialDef* swat_material(SwatMaterial material) {
    return &definitions[material>=0 && material<SWAT_MATERIAL_COUNT ? material : SWAT_CONCRETE];
}
