#ifndef WEBNAV_BRIDGE_H
#define WEBNAV_BRIDGE_H
#include <stdint.h>
#define WEBNAV_BATCH 32
#define WEBNAV_WORDS 256
#define WEBNAV_OBS 128
#define WEBNAV_ACTIONS 13
#define WEBNAV_OBS_OFFSET 32
#define WEBNAV_MASK_OFFSET 160
#ifdef __cplusplus
extern "C" {
#endif
/* One process-wide CPU Bend runtime, serialized at batched call boundaries.
 * Caller owns persistent state buffers. No GPU or subprocess is used. */
void webnav_batch(uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS]);
#ifdef __cplusplus
}
#endif
#endif
