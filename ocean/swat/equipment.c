#include "equipment.h"
#include <string.h>
// Ordinary C data is the mod surface. Add slots/profiles here as the equipment
// ecosystem grows; mass, movement and protection remain authority-owned.
static const SwatKitDef kits[SWAT_KIT_COUNT]={
    {"Recon","Carbine / wand / taser / picks / flash + CS",11,1,0,false,true,false,false,1,1,2,0},
    {"Control","Impact / wand / taser / picks / 1 charge",16,.92f,.12f,true,true,false,true,1,2,3,1},
    {"Entry","Carbine / ram / plates / picks / 2 charges",25,.78f,.38f,false,false,true,true,2,1,0,2},
};
const SwatKitDef* swat_kit(int kit) { return &kits[kit>=0 && kit<SWAT_KIT_COUNT ? kit : 0]; }
void swat_equipment_kit(SwatEquipment* gear,int kit) {
    gear->kit=kit; const SwatKitDef* d=swat_kit(kit);
    gear->flashbangs=d->flashbangs; gear->gas_grenades=d->gas_grenades; gear->taser_charges=d->taser_charges;
    gear->breaching_charges=d->breaching_charges; gear->wedges=2; gear->spray_ticks=360;
}
void swat_equipment_init(SwatEquipment* gear) {
    memset(gear,0,sizeof(*gear)); gear->cuff_target=gear->door_target=-1; swat_equipment_kit(gear,0);
}
float swat_equipment_mobility(const SwatEquipment* gear) {
    return swat_kit(gear->kit)->mobility*(1-.55f*swat_clamp(gear->wounds[SWAT_LEGS]/70,0,1))*(gear->gas_ticks>30 ? .65f : 1);
}
