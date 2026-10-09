#ifndef WEBNAV_UNIFIED_CAPABILITIES_H
#define WEBNAV_UNIFIED_CAPABILITIES_H
#include "../families/common/family_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WU_CAP_VERSION 1u
#define WU_MAX_CAPABILITIES 1024u
enum { WU_UNIT_NONE, WU_UNIT_INDEX, WU_UNIT_PIXEL, WU_UNIT_FIXED_PIXEL,
       WU_UNIT_NORMALIZED, WU_UNIT_KEY, WU_UNIT_TEXT_OFFSET };
enum { WU_CAP_TEXT=1u, WU_CAP_ASCII=2u, WU_CAP_RANGE=4u };
/* Additive host contract; ABI v2 family structs remain unchanged. All data is
 * derived from public WFView plus the static task's declared widget protocol.
 * ref is the public target; wire_target may be zero for focused-field APIs.
 * Bounds/units describe transport parameters; policy parameters are normalized
 * to these ranges. A step of zero means one. No private row data is permitted. */
typedef struct {
    uint32_t kind,ref,wire_target,flags,text_capacity;
    uint32_t min0,max0,step0,unit0,min1,max1,step1,unit1;
} WUCapability;
typedef struct {
    uint32_t version,count,incomplete;
    WUCapability items[WU_MAX_CAPABILITIES];
} WUCapabilities;
int wu_cap_add(WUCapabilities *,WUCapability);
const WFNode *wu_node(const WFView *,uint32_t ref);
int wu_available(const WFNode *);
int wu_capabilities(const WFFamily *,uint32_t local_task,const WFView *,WUCapabilities *);
/* Group providers: 1 handled, 0 other family, -1 invalid. */
int wu_caps_basic(const WFFamily *,uint32_t,const WFView *,WUCapabilities *);
int wu_caps_text(const WFFamily *,uint32_t,const WFView *,WUCapabilities *);
int wu_caps_widgets(const WFFamily *,uint32_t,const WFView *,WUCapabilities *);
int wu_caps_pointer(const WFFamily *,uint32_t,const WFView *,WUCapabilities *);
#ifdef __cplusplus
}
#endif
#endif
