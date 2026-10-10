#include "materials.h"

// Authored game approximations. Surface absorption and transmission are
// different quantities; these are intentionally separate, editable tables.
// Transmission is per layer at reference thickness, not a building STC rating.
static const SwatMaterialDef definitions[SWAT_MATERIAL_COUNT]={
    {"Concrete",10000,{.02f,.04f,.06f},{40,55,70},.20f,{83,91,91,255},2400,.65f,.35f,.02f,900,45,1,900,1000,2},
    {"Gypsum board",1.5f,{.29f,.10f,.05f},{8,14,22},.0125f,{190,188,172,255},800,.6f,.08f,.06f,340,55,.8f,38,0,0.025},
    {"Timber",3,{.15f,.10f,.07f},{9,18,30},.038f,{139,99,56,255},600,.5f,.22f,.035f,220,24,.85f,150,0,0.1},
    {"Window glass",.5f,{.18f,.06f,.03f},{12,20,30},.006f,{114,177,187,110},2500,.25f,.45f,.01f,2700,12,1.1f,8,0,0.005},
    {"Steel",80,{.03f,.04f,.05f},{25,40,55},.008f,{91,104,114,255},7800,.35f,.55f,.015f,1650,10,1.2f,600,200,2},
    {"Brick masonry",400,{.03f,.04f,.07f},{30,45,58},.10f,{133,92,76,255},1900,.75f,.28f,.04f,760,38,1,280,250,0.45},
    {"Exterior plaster",2,{.04f,.05f,.07f},{10,16,24},.016f,{162,164,150,255},1000,.6f,.10f,.05f,410,48,.85f,55,0,0.03},
    {"Fibrous insulation",.1f,{.35f,.80f,.95f},{2,6,12},.09f,{165,151,104,255},35,.9f,.005f,.3f,85,100,.2f,10,0,0.005},
    {"Ceramic tile",25,{.02f,.03f,.04f},{18,28,38},.012f,{169,183,180,255},2200,.25f,.50f,.01f,2100,18,1.1f,100,60,0.15},
    {"Carpet",.1f,{.08f,.35f,.65f},{1,3,8},.012f,{108,108,94,255},200,.85f,.008f,.25f,100,90,.3f,15,0,0.005},
    {"Earth",10000,{.25f,.50f,.75f},{40,55,70},.20f,{67,83,58,255},1600,.8f,.02f,.15f,120,65,.5f,900,1000,2},
    {"Solid rock",10000,{.02f,.03f,.05f},{40,55,70},.20f,{112,111,103,255},2700,.75f,.30f,.03f,1100,38,1,1000,1000,2},
    // Same glass response; the frozen motel art has opaque dusty glazing.
    {"Opaque window glass",.5f,{.18f,.06f,.03f},{12,20,30},.006f,{114,145,151,255},2500,.25f,.45f,.01f,2700,12,1.1f,8,0,0.005},
};
const SwatMaterialDef* swat_material(SwatMaterial material) {
    return &definitions[material>=0 && material<SWAT_MATERIAL_COUNT ? material : SWAT_CONCRETE];
}
