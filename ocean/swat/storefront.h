#ifndef SWAT_STOREFRONT_H
#define SWAT_STOREFRONT_H
#include "motel.h"
#define SWAT_STOREFRONT_ASSETS 40
#define SWAT_STOREFRONT_INSTANCES 152
const SwatMotelAsset* swat_storefront_asset(int index);
const SwatMotelInstance* swat_storefront_instance(int index);
void swat_storefront_build(SwatWorld* world);
bool swat_storefront_bind_collision(SwatWorld* world);
#endif
