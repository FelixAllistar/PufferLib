#ifndef WEBNAV_BROWSER_CONTEXTS_PUBLIC_H
#define WEBNAV_BROWSER_CONTEXTS_PUBLIC_H
#include "browser_contexts.h"
#include "../../families/common/family_api.h"
#include "../../unified/capabilities.h"
#define WBC_REF_STRIDE 32u
#define WBC_MAX_CONTROLS 24u
#define WBC_CONTROL_TEXT_BUDGET 4096u
typedef struct { uint32_t kind, argument, foreground, elapsed_ms; } WBCCommand;
typedef struct { uint32_t ref_base, elapsed_ms; WBCState browser; } WBCSession;

/* Projection/binding depend only on public tab facts. The browser host and the
 * native application use the same controls. Titles are in tab order; address
 * is the active URL. Text clips at UTF-8 boundaries and reports incompleteness.
 * Reserve up to 24 nodes/4 KiB of text and a fresh disjoint 32-ref interval.
 * Both append outputs remain unchanged on failure. */
WB_EXPORT int wbc_controls_append(const WBCView *,const char *const *titles,
    const char *address,uint32_t ref_base,WFView *,WUCapabilities *);
WB_EXPORT int wbc_controls_action(const WBCView *,uint32_t ref_base,
    const WFAction *,WBCCommand *);
WB_EXPORT int wbc_session_reset(WBCSession *,uint32_t seed,uint32_t routes,
    uint32_t capacity,uint32_t first_ref);
WB_EXPORT int wbc_session_validate(const WBCSession *);
WB_EXPORT int wbc_session_observe(const WBCSession *,const char *const *titles,
    const char *address,const char *instruction,WFView *,WUCapabilities *);
WB_EXPORT int wbc_session_action(WBCSession *,const WFAction *);
/* Trusted application routing/timer entry; never expose numeric route IDs as
 * a policy action. Public links resolve through the application's own router. */
WB_EXPORT int wbc_session_command(WBCSession *,const WBCCommand *);
#endif
