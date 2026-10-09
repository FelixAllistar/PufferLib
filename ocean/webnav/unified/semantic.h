#ifndef WEBNAV_UNIFIED_SEMANTIC_H
#define WEBNAV_UNIFIED_SEMANTIC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define WU_SEMANTIC_DIM 32
#define WU_SEMANTIC_CACHE_ENTRIES 1024u
#define WU_SEMANTIC_CACHE_BYTES 1024u

/* Initialize before starting environment workers. Enabled initialization loads
 * the existing fingerprint-pinned Potion assets, returning -1 on load failure.
 * Reinitialization clears the cache and counters. Close only after workers stop. */
int wu_semantic_init(int enabled);
void wu_semantic_close(void);

/* Exact UTF-8 byte strings are cached; strings over CACHE_BYTES encode uncached.
 * The pinned 256-vector is folded into 32 slots with alternating signs for each
 * group of 32, then L2 normalized. This is a lossy projection of the existing
 * token-vector sum, not a contextual encoder upgrade. Empty text or a disabled /
 * uninitialized encoder yields zeros and success. Invalid enabled input yields
 * zeros and -1. The output pointer must be non-null. */
int wu_semantic_encode(const char *text,size_t bytes,float out[WU_SEMANTIC_DIM]);

/* Calls includes disabled, empty and failed attempts with non-null output.
 * Misses includes uncached and failed enabled nonempty requests; hits counts
 * successful exact cache matches. Snapshots are protected by the cache mutex. */
typedef struct { uint64_t calls,hits,misses; } WUSemanticStats;
void wu_semantic_stats(WUSemanticStats *out);

#ifdef __cplusplus
}
#endif
#endif
