#ifndef WEBNAV_TEXT_CACHE_H
#define WEBNAV_TEXT_CACHE_H
#include "text_encoder.h"
typedef struct WebTextCache WebTextCache;
/* Immutable caches: strings are exact UTF-8 bytes, IDs are local to this file.
 * Record file SHA-256 in any checkpoint/rollout that stores these IDs. */
int web_text_cache_write(const char *path,WebTextEncoder *encoder,const char *const *texts,uint32_t count);
WebTextCache *web_text_cache_load(const char *path);
void web_text_cache_free(WebTextCache *cache);
const float *web_text_cache_find(const WebTextCache *cache,const char *text,size_t length,uint32_t *id);
#endif
