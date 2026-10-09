#ifndef WEBNAV_RECORD_BROWSER_H
#define WEBNAV_RECORD_BROWSER_H
#include "../../families/common/family_api.h"
#include "../../primitives/page/page.h"
#include "../../unified/capabilities.h"

#define WA_VERSION 1u
#define WA_WORDS 512u
typedef struct {
    uint32_t version,ref_base,elapsed_ms;
    /* Private transport state; never pass this row to a policy. */
    uint32_t words[WA_WORDS];
} WAState;

/* The host assigns a fresh ref interval per episode. Each accepted step uses
 * the next 128 refs; exhaustion rejects atomically. Reset accepts dirty memory.
 * Reusing an interval across live episodes is a host error. */
WF_EXPORT int wa_reset(WAState *,uint32_t seed,uint32_t first_ref);
WF_EXPORT int wa_validate(const WAState *);
/* Instructions are supplied by the host, separately from the synthetic world.
 * No goal, reward or benchmark selector is stored in this application. */
WF_EXPORT int wa_observe(const WAState *,const char *instruction,WFView *,WPPage *,WUCapabilities *);
/* Standard WF actions use current public refs, including focused-field edits.
 * Rejected/obsolete actions leave the state byte-for-byte unchanged. */
WF_EXPORT int wa_step(WAState *,const WFAction *);
#endif
