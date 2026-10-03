#include "equipment.h"
#include <string.h>
// Ordinary C data is the mod surface. Add slots/profiles here as the equipment
// ecosystem grows; mass, movement and protection remain authority-owned.
static const SwatKitDef kits[SWAT_KIT_COUNT]={
    {"Recon","Carbine / sidearm / optiwand / cuffs",11,1,0,false,true,false},
    {"Control","Impact launcher / sidearm / optiwand / cuffs",15,.92f,.12f,true,true,false},
    {"Entry","Carbine / sidearm / ram / heavy plates / cuffs",24,.78f,.38f,false,false,true},
};
const SwatKitDef* swat_kit(int kit) { return &kits[kit>=0 && kit<SWAT_KIT_COUNT ? kit : 0]; }
void swat_equipment_init(SwatEquipment* gear) { memset(gear,0,sizeof(*gear)); gear->cuff_target=-1; }
float swat_equipment_mobility(const SwatEquipment* gear) {
    return swat_kit(gear->kit)->mobility*(1-.55f*swat_clamp(gear->wounds[SWAT_LEGS]/70,0,1));
}
