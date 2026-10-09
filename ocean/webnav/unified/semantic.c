#include "semantic.h"
#include "../text_encoder.h"
#include <math.h>
#include <pthread.h>
#include <string.h>

typedef struct {
    size_t bytes;
    char text[WU_SEMANTIC_CACHE_BYTES];
    float vector[WU_SEMANTIC_DIM];
} WUSemanticEntry;

static pthread_mutex_t semantic_mutex=PTHREAD_MUTEX_INITIALIZER;
static WebTextEncoder *semantic_encoder;
static WUSemanticEntry semantic_cache[WU_SEMANTIC_CACHE_ENTRIES];
static unsigned semantic_next;
static WUSemanticStats semantic_counts;

static void semantic_clear(void) {
    web_text_free(semantic_encoder);
    semantic_encoder=NULL;
    memset(semantic_cache,0,sizeof semantic_cache);
    semantic_next=0;
    memset(&semantic_counts,0,sizeof semantic_counts);
}

int wu_semantic_init(int enabled) {
    pthread_mutex_lock(&semantic_mutex);
    semantic_clear();
    if(enabled)semantic_encoder=web_text_load(
        "build/webnav/reference/potion-tokenizer.json",
        "build/webnav/reference/potion-model.safetensors");
    int result=enabled&&!semantic_encoder?-1:0;
    pthread_mutex_unlock(&semantic_mutex);
    return result;
}

int wu_semantic_encode(const char *text,size_t bytes,float out[WU_SEMANTIC_DIM]) {
    if(!out)return -1;
    memset(out,0,WU_SEMANTIC_DIM*sizeof *out);
    pthread_mutex_lock(&semantic_mutex);
    semantic_counts.calls++;
    if(!semantic_encoder||!bytes) {
        pthread_mutex_unlock(&semantic_mutex);
        return 0;
    }
    if(text&&bytes<=WU_SEMANTIC_CACHE_BYTES) {
        for(unsigned i=0;i<WU_SEMANTIC_CACHE_ENTRIES;i++) {
            const WUSemanticEntry *entry=&semantic_cache[i];
            if(entry->bytes==bytes&&!memcmp(entry->text,text,bytes)) {
                memcpy(out,entry->vector,WU_SEMANTIC_DIM*sizeof *out);
                semantic_counts.hits++;
                pthread_mutex_unlock(&semantic_mutex);
                return 0;
            }
        }
    }
    semantic_counts.misses++;
    float full[WEB_TEXT_DIM];
    if(web_text_encode(semantic_encoder,text,bytes,full,NULL)) {
        pthread_mutex_unlock(&semantic_mutex);
        return -1;
    }
    /* Fixed signed fold: dimension i contributes to i % 32 with sign (-1)^group.
     * Preserve one deterministic accumulation order across cached/uncached paths. */
    for(unsigned i=0;i<WEB_TEXT_DIM;i++)
        out[i%WU_SEMANTIC_DIM]+=((i/WU_SEMANTIC_DIM)&1u)?-full[i]:full[i];
    double norm=0;
    for(unsigned i=0;i<WU_SEMANTIC_DIM;i++)norm+=(double)out[i]*out[i];
    float scale=norm>0?(float)(1.0/sqrt(norm)):0;
    for(unsigned i=0;i<WU_SEMANTIC_DIM;i++)out[i]*=scale;
    if(bytes<=WU_SEMANTIC_CACHE_BYTES) {
        WUSemanticEntry *entry=&semantic_cache[semantic_next];
        entry->bytes=bytes;
        memcpy(entry->text,text,bytes);
        memcpy(entry->vector,out,WU_SEMANTIC_DIM*sizeof *out);
        semantic_next=(semantic_next+1u)%WU_SEMANTIC_CACHE_ENTRIES;
    }
    pthread_mutex_unlock(&semantic_mutex);
    return 0;
}

void wu_semantic_stats(WUSemanticStats *out) {
    if(!out)return;
    pthread_mutex_lock(&semantic_mutex);
    *out=semantic_counts;
    pthread_mutex_unlock(&semantic_mutex);
}

void wu_semantic_close(void) {
    pthread_mutex_lock(&semantic_mutex);
    semantic_clear();
    pthread_mutex_unlock(&semantic_mutex);
}
