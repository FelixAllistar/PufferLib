#include "semantic.h"
#include "../text_encoder.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

/* Source-only contract checks. Enabled cases require the pinned local assets. */
static void assert_zero(const float v[WU_SEMANTIC_DIM]) {
    for(unsigned i=0;i<WU_SEMANTIC_DIM;i++)assert(v[i]==0);
}

static void assert_fold(WebTextEncoder *encoder,const char *text,size_t bytes,
                        const float actual[WU_SEMANTIC_DIM]) {
    float full[WEB_TEXT_DIM],expected[WU_SEMANTIC_DIM]={0};
    assert(web_text_encode(encoder,text,bytes,full,NULL)==0);
    for(unsigned group=0;group<WEB_TEXT_DIM/WU_SEMANTIC_DIM;group++)
        for(unsigned slot=0;slot<WU_SEMANTIC_DIM;slot++)
            expected[slot]+=(group&1u)?-full[group*WU_SEMANTIC_DIM+slot]:
                                      full[group*WU_SEMANTIC_DIM+slot];
    double norm=0;
    for(unsigned slot=0;slot<WU_SEMANTIC_DIM;slot++)
        norm+=(double)expected[slot]*expected[slot];
    float scale=norm>0?(float)(1.0/sqrt(norm)):0;
    double actual_norm=0;
    for(unsigned slot=0;slot<WU_SEMANTIC_DIM;slot++) {
        expected[slot]*=scale;
        assert(isfinite(actual[slot]));
        assert(fabsf(actual[slot]-expected[slot])<1e-6f);
        actual_norm+=(double)actual[slot]*actual[slot];
    }
    assert(norm==0?actual_norm==0:fabs(actual_norm-1.0)<1e-5);
}

int main(void) {
    prctl(PR_SET_DUMPABLE,0);
    float first[WU_SEMANTIC_DIM],again[WU_SEMANTIC_DIM];
    WUSemanticStats stats;
    memset(first,0xff,sizeof first);
    assert(wu_semantic_encode("before initialization",21,first)==0);
    assert_zero(first);
    assert(wu_semantic_init(0)==0);
    assert(wu_semantic_encode(NULL,12,first)==0);
    assert_zero(first);
    wu_semantic_stats(&stats);
    assert(stats.calls==1&&stats.hits==0&&stats.misses==0);

    assert(wu_semantic_init(1)==0);
    WebTextEncoder *reference=web_text_load(
        "build/webnav/reference/potion-tokenizer.json",
        "build/webnav/reference/potion-model.safetensors");
    assert(reference);
    assert(wu_semantic_encode(NULL,0,first)==0);
    assert_zero(first);
    const char *label="Select blue then red";
    assert(wu_semantic_encode(label,strlen(label),first)==0);
    assert_fold(reference,label,strlen(label),first);
    char copy[64];strcpy(copy,label);
    assert(wu_semantic_encode(copy,strlen(copy),again)==0);
    assert(!memcmp(first,again,sizeof first));
    wu_semantic_stats(&stats);
    assert(stats.calls==3&&stats.hits==1&&stats.misses==1);

    /* Different UTF-8 bytes remain separate cache keys even when the tokenizer
     * normalizes their accents/case to equal token vectors. */
    const char *unicode="Caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87";
    assert(wu_semantic_encode(unicode,strlen(unicode),first)==0);
    assert_fold(reference,unicode,strlen(unicode),first);
    assert(wu_semantic_encode(unicode,strlen(unicode),again)==0);
    assert(!memcmp(first,again,sizeof first));
    assert(wu_semantic_encode("caf\xc3\xa9",5,first)==0);
    assert(wu_semantic_encode("cafe",4,again)==0);
    wu_semantic_stats(&stats);
    assert(stats.hits==2&&stats.misses==4);
    /* Length is part of the key and input need not be NUL terminated. */
    assert(wu_semantic_encode(label,6,first)==0);
    assert_fold(reference,label,6,first);
    assert(wu_semantic_encode(label,6,again)==0);
    assert(!memcmp(first,again,sizeof first));
    wu_semantic_stats(&stats);
    assert(stats.hits==3&&stats.misses==5);

    char long_text[WU_SEMANTIC_CACHE_BYTES+1u];
    memset(long_text,' ',sizeof long_text);
    memcpy(long_text,"blue",4);
    assert(wu_semantic_encode(long_text,WU_SEMANTIC_CACHE_BYTES,first)==0);
    assert(wu_semantic_encode(long_text,WU_SEMANTIC_CACHE_BYTES,again)==0);
    assert(!memcmp(first,again,sizeof first));
    wu_semantic_stats(&stats);
    assert(stats.hits==4&&stats.misses==6);
    assert(wu_semantic_encode(long_text,sizeof long_text,first)==0);
    assert(wu_semantic_encode(long_text,sizeof long_text,again)==0);
    assert(!memcmp(first,again,sizeof first));
    assert_fold(reference,long_text,sizeof long_text,first);
    wu_semantic_stats(&stats);
    assert(stats.hits==4&&stats.misses==8);

    assert(wu_semantic_encode(NULL,1,first)==-1);
    assert_zero(first);
    const char invalid[]={(char)0xff};
    assert(wu_semantic_encode(invalid,sizeof invalid,first)==-1);
    assert_zero(first);
    assert(wu_semantic_encode(label,strlen(label),NULL)==-1);

    /* Reset makes the round-robin eviction order observable. */
    assert(wu_semantic_init(1)==0);
    for(unsigned i=0;i<WU_SEMANTIC_CACHE_ENTRIES+1u;i++) {
        int bytes=snprintf(copy,sizeof copy,"button %u",i);
        assert(bytes>0&&(size_t)bytes<sizeof copy);
        assert(wu_semantic_encode(copy,(size_t)bytes,first)==0);
    }
    assert(wu_semantic_encode("button 1",8,first)==0);
    assert(wu_semantic_encode("button 0",8,first)==0);
    wu_semantic_stats(&stats);
    assert(stats.hits==1&&stats.misses==WU_SEMANTIC_CACHE_ENTRIES+2u);
    web_text_free(reference);
    wu_semantic_close();
    wu_semantic_close();
    assert(wu_semantic_encode(label,strlen(label),first)==0);
    assert_zero(first);
    return 0;
}
