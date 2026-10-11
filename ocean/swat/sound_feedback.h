#ifndef SWAT_SOUND_FEEDBACK_H
#define SWAT_SOUND_FEEDBACK_H
#include "acoustics.h"

// Client presentation only. Accepted events and NPC hearing remain unchanged.
static inline bool swat_sound_same_contact(const SwatSoundEvent* a,const SwatSoundEvent* b) {
    return a->tick==b->tick && a->source_actor==b->source_actor &&
        b3Distance(a->position,b->position)<.08f;
}
static inline bool swat_sound_contact_replaced(const SwatSoundLog* log,const SwatSoundEvent* event) {
    if(event->kind!=SWAT_SOUND_IMPACT)return false;
    for(int i=log->count-1;i>=0;i--) {
        const SwatSoundEvent* other=swat_sound_at(log,i);
        if(other->kind==SWAT_SOUND_BREAK && other->material==event->material &&
           swat_sound_same_contact(event,other))return true;
    }
    return false;
}
static inline SwatMaterial swat_sound_recorded_material(SwatSoundKind kind,SwatMaterial material) {
    // Foley families, not measured bullet/charge recordings. Exact authored
    // material entries override these fallback families in the bank selector.
    static const SwatMaterial impacts[SWAT_MATERIAL_COUNT]={
        SWAT_CONCRETE,SWAT_DRYWALL,SWAT_WOOD,SWAT_GLASS,SWAT_STEEL,
        SWAT_CONCRETE,SWAT_DRYWALL,SWAT_CLOTH,SWAT_TILE,SWAT_CLOTH,
        SWAT_CLOTH,SWAT_CONCRETE,SWAT_GLASS,SWAT_ALUMINUM,SWAT_CLOTH,SWAT_CLOTH};
    static const SwatMaterial breaks[SWAT_MATERIAL_COUNT]={
        SWAT_BRICK,SWAT_DRYWALL,SWAT_WOOD,SWAT_GLASS,SWAT_STEEL,
        SWAT_BRICK,SWAT_PLASTER,SWAT_CLOTH,SWAT_GLASS,SWAT_CLOTH,
        SWAT_CLOTH,SWAT_BRICK,SWAT_GLASS,SWAT_STEEL,SWAT_CLOTH,SWAT_CLOTH};
    if(material<0 || material>=SWAT_MATERIAL_COUNT)return material;
    if(kind==SWAT_SOUND_IMPACT)return impacts[material];
    if(kind==SWAT_SOUND_BREAK)return breaks[material];
    return material==SWAT_OPAQUE_GLASS?SWAT_GLASS:material;
}
#endif
