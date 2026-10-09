#ifndef WEBNAV_RESPONSE_FORM_H
#define WEBNAV_RESPONSE_FORM_H
#include "../../families/common/family_api.h"
#include "../../unified/capabilities.h"
#include "../finish/finish.h"
#define WRF_WORDS 65536u
#define WRF_REF_STRIDE 32u
#define WRF_MAX_NODES 16u
#define WRF_INSERT_BYTES 255u
typedef struct { uint32_t ref_base,words[WRF_WORDS]; } WRFState;

/* Assign a disjoint fresh public-ref interval per episode. Accepted form
 * actions advance by 32 refs. The state contains no goal or instruction. */
WF_EXPORT int wrf_reset(WRFState *,uint32_t first_ref);
WF_EXPORT int wrf_validate(const WRFState *);
WF_EXPORT int wrf_phase(const WRFState *);
WF_EXPORT int wrf_observe(const WRFState *,WFView *,WUCapabilities *);
/* Append host controls to an existing public snapshot. Requires space for up
 * to 16 nodes. Failure leaves both output structures unchanged. Values that
 * exceed the remaining WF text budget are clipped at UTF-8 boundaries and
 * explicitly flagged; the draft itself is never truncated. Selection offsets
 * are UTF-8 bytes, while editing moves/deletes complete Unicode scalars. */
WF_EXPORT int wrf_append(const WRFState *,WFView *,WUCapabilities *);
WF_EXPORT int wrf_step(WRFState *,const WFAction *);
/* Host focus handoff to another composed interface; retains both drafts and
 * selection ranges. A terminal form absorbs the handoff. */
WF_EXPORT int wrf_blur(WRFState *);
WF_EXPORT int wrf_expire(WRFState *);
WF_EXPORT int wrf_json(const WRFState *,char *,size_t,size_t *);
#endif
