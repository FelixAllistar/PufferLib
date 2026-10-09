#ifndef WEBNAV_PRIMITIVE_BROWSER_CONTEXTS_H
#define WEBNAV_PRIMITIVE_BROWSER_CONTEXTS_H
#include "../browser/browser.h"
enum { WBC_OPEN=6, WBC_SWITCH=7, WBC_CLOSE=8, WBC_MAX_TABS=16, WBC_WORDS=2048 };
/* Private transport. Route zero is the blank document; the host maps remaining
 * route IDs to public URLs. Tab identities persist until close and never reuse. */
typedef struct { uint32_t words[WBC_WORDS]; } WBCState;
typedef struct { uint32_t identity; WBView browser; } WBCTabView;
typedef struct {
    uint32_t count, active, can_open, can_close;
    WBCTabView tabs[WBC_MAX_TABS];
} WBCView;
WB_EXPORT int wbc_validate(const WBCState *);
WB_EXPORT int wbc_reset(WBCState *, uint32_t seed, uint32_t route_count, uint32_t capacity);
/* WB commands 0..5 act on the active tab. OPEN takes route and foreground 0/1;
 * SWITCH/CLOSE take a tab identity. Other arguments must be zero. Invalid or
 * impossible commands reject atomically, including their elapsed-time delta.
 * Every accepted command first advances all existing tabs by elapsed_ms. */
WB_EXPORT int wbc_step(WBCState *, uint32_t command, uint32_t argument,
    uint32_t foreground, uint32_t elapsed_ms);
WB_EXPORT int wbc_observe(const WBCState *, WBCView *);
#endif
