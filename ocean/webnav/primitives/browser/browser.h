#ifndef WEBNAV_PRIMITIVE_BROWSER_H
#define WEBNAV_PRIMITIVE_BROWSER_H
#include <stdint.h>
#if defined(__GNUC__)
#define WB_EXPORT __attribute__((visibility("default")))
#else
#define WB_EXPORT
#endif
enum { WB_READY, WB_LOADING, WB_FAILED };
enum { WB_WAIT, WB_NAVIGATE, WB_BACK, WB_FORWARD, WB_RETRY, WB_RELOAD };
/* Opaque private transport; never put these words in learner observations. */
typedef struct { uint32_t words[64]; } WBState;
typedef struct { uint32_t route, phase, can_back, can_forward; } WBView;
WB_EXPORT int wb_validate(const WBState *);
WB_EXPORT int wb_reset(WBState *, uint32_t seed);
/* Invalid input is rejected atomically; time is a caller-provided delta. */
WB_EXPORT int wb_step(WBState *, uint32_t command, uint32_t route, uint32_t elapsed_ms);
WB_EXPORT int wb_observe(const WBState *, WBView *);
#endif
