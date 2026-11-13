// Ecosystem and animal AI (docs/ecosystem.md): the activities the scripted
// animals choose between (bots.c). Tools and the debug overlay read an
// animal's activity from CretAgent.bot_state.
#ifndef CRET_ECO_H
#define CRET_ECO_H
#include "sim.h"
#ifdef __cplusplus
extern "C" {
#endif

// What a scripted animal is doing (CretAgent.bot_state). Wander, graze and
// rest are the calm activities the population director may move.
enum { BOT_WANDER, BOT_GRAZE, BOT_FLEE, BOT_HUNT, BOT_REST, BOT_DRINK, BOT_SEEK_WATER, BOT_FEED, BOT_DEFEND, BOT_NEST, BOT_RAID, BOT_CARRY,
       BOT_STATES };
extern const char* const CRET_BOT_STATE_NAMES[BOT_STATES];

#ifdef __cplusplus
}
#endif
#endif
