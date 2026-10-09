/* Standalone pretrained-encoder ranking diagnostic, not RL policy accuracy.
 * Fixture copied verbatim from ocean/webnav/tests/text_cases.json, the 24 cases
 * consumed by ocean/webnav/tools/text_probe.sh. Keep queries, order and expected
 * choices identical when comparing the pinned full vector and signed fold. */
#define _POSIX_C_SOURCE 200809L
#include "semantic.h"
#include "../text_encoder.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

typedef struct { const char *text[4]; unsigned expected; } RankingCase;
static const RankingCase cases[] = {
    {{"buy this item","Help center","Add to cart","Order history"},1},
    {{"where is my parcel","Track shipment","Change password","Returns"},0},
    {{"sign out","Register","Login","Logout"},2},
    {{"change my password","Account security","Payment methods","Delivery address"},0},
    {{"send the message","Discard draft","Send email","Archive conversation"},1},
    {{"delete the draft","Send email","Save draft","Discard draft"},2},
    {{"find installation instructions","Pricing","Getting started","Contact sales"},1},
    {{"return a purchased item","Refunds and returns","Product reviews","Delivery estimates"},0},
    {{"cheapest first","Price: high to low","Price: low to high","Newest arrivals"},1},
    {{"most expensive first","Price: high to low","Price: low to high","Newest arrivals"},0},
    {{"do not delete the message","Delete the message","Keep the message","Forward the message"},1},
    {{"select the blue shirt","Red shirt","Blue shirt","Blue shoes"},1},
    {{"select order 1042","Order 1043","Order 1042","Order 1402"},1},
    {{"price below 20 dollars","Price 25 dollars","Price 19 dollars","Price 20 dollars"},1},
    {{"quantity three","Quantity 2","Quantity 3","Quantity 4"},1},
    {{"billing address","Shipping address","Billing address","Email address"},1},
    {{"open documentation version 2.1","Documentation 2.0","Documentation 2.1","Documentation 1.2"},1},
    {{"mark all messages as read","Mark as unread","Mark as read","Delete all messages"},1},
    {{"available products only","Out of stock","In stock","All products"},1},
    {{"save without publishing","Publish now","Save draft","Discard changes"},1},
    {{"translate from English to French","French to English","English to French","English to German"},1},
    {{"translate from French to English","French to English","English to French","German to English"},0},
    {{"support phone number","Technical support telephone","Sales email","Office address"},0},
    {{"size medium","Size small","Size medium","Size large"},1}
};
#define CASE_COUNT (sizeof cases / sizeof cases[0])
#define STRING_COUNT (CASE_COUNT * 4u)
#define COLD_REPEATS 100u
#define WARM_REPEATS 10000u
static float full_vectors[CASE_COUNT][4][WEB_TEXT_DIM];
static float folded_vectors[CASE_COUNT][4][WU_SEMANTIC_DIM];
static volatile double checksum;
static void consume_vector(const float *vector,unsigned index) {
    checksum+=vector[index];
}
/* The indirect volatile call makes the materialized vector observable without
 * adding a dimension-sized volatile store loop to the timing. */
static void (*volatile consume)(const float *,unsigned)=consume_vector;

static double now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
}
static unsigned winner(const float *vectors,unsigned dim) {
    float best=-INFINITY;
    unsigned chosen=0;
    for(unsigned option=0;option<3;option++) {
        float score=0;
        for(unsigned d=0;d<dim;d++)score+=vectors[d]*vectors[(option+1u)*dim+d];
        /* Match the reference probe's deterministic first-option tie break. */
        if(score>best) { best=score;chosen=option; }
    }
    return chosen;
}
static const char *sample(unsigned i) {
    unsigned at=i%STRING_COUNT;
    return cases[at/4u].text[at%4u];
}
static int encode_error(const char *text) {
    fprintf(stderr,"semantic probe could not encode: %s\n",text);
    return -1;
}
/* No reset API is needed and neither model is reloaded. A complete ring of
 * distinct non-fixture strings evicts every entry before a measured cold call.
 * Eviction encodes and their cache counters are excluded from cold timings. */
static int evict_cache(void) {
    char text[64];
    float vector[WU_SEMANTIC_DIM];
    for(unsigned i=0;i<WU_SEMANTIC_CACHE_ENTRIES;i++) {
        int bytes=snprintf(text,sizeof text,"semantic probe eviction filler %u",i);
        if(bytes<0||(size_t)bytes>=sizeof text)return -1;
        if(wu_semantic_encode(text,(size_t)bytes,vector))return encode_error(text);
    }
    return 0;
}

int main(void) {
    if(prctl(PR_SET_DUMPABLE,0)) { perror("PR_SET_DUMPABLE");return 2; }
    if(now()<0) { perror("CLOCK_MONOTONIC");return 2; }
    WebTextEncoder *encoder=web_text_load("build/webnav/reference/potion-tokenizer.json",
        "build/webnav/reference/potion-model.safetensors");
    if(!encoder) { fputs("pinned Potion assets unavailable\n",stderr);return 2; }
    int status=2;
    if(wu_semantic_init(1)) { fputs("signed-fold encoder initialization failed\n",stderr);goto done; }
    unsigned full_correct=0,folded_correct=0,mismatches=0;
    unsigned full_nondeterministic=0,folded_nondeterministic=0;
    unsigned full_winners[CASE_COUNT],folded_winners[CASE_COUNT];
    for(unsigned c=0;c<CASE_COUNT;c++) {
        for(unsigned s=0;s<4;s++) {
            const char *text=cases[c].text[s];
            size_t bytes=strlen(text);
            if(web_text_encode(encoder,text,bytes,full_vectors[c][s],NULL)||
                wu_semantic_encode(text,bytes,folded_vectors[c][s])) { encode_error(text);goto done; }
        }
        full_winners[c]=winner(&full_vectors[c][0][0],WEB_TEXT_DIM);
        folded_winners[c]=winner(&folded_vectors[c][0][0],WU_SEMANTIC_DIM);
        full_correct+=full_winners[c]==cases[c].expected;
        folded_correct+=folded_winners[c]==cases[c].expected;
        mismatches+=full_winners[c]!=folded_winners[c];
    }
    WUSemanticStats ranking_stats;
    wu_semantic_stats(&ranking_stats);
    /* Full vectors are recomputed; folded vectors must match exact cache hits. */
    for(unsigned c=0;c<CASE_COUNT;c++)for(unsigned s=0;s<4;s++) {
        const char *text=cases[c].text[s];
        float full[WEB_TEXT_DIM],folded[WU_SEMANTIC_DIM];
        if(web_text_encode(encoder,text,strlen(text),full,NULL)||
            wu_semantic_encode(text,strlen(text),folded)) { encode_error(text);goto done; }
        full_nondeterministic+=memcmp(full,full_vectors[c][s],sizeof full)!=0;
        folded_nondeterministic+=memcmp(folded,folded_vectors[c][s],sizeof folded)!=0;
    }

    double full_cold_seconds=0,folded_cold_seconds=0;
    unsigned cold_calls=0,cold_hits=0,cold_misses=0;
    for(unsigned i=0;i<COLD_REPEATS;i++) {
        const char *text=sample(i);
        size_t bytes=strlen(text);
        float full[WEB_TEXT_DIM],folded[WU_SEMANTIC_DIM];
        /* Both measured paths encode the same original bytes exactly once. */
        double start=now();
        int error=web_text_encode(encoder,text,bytes,full,NULL);
        full_cold_seconds+=now()-start;
        if(error) { encode_error(text);goto done; }
        consume(full,i%WEB_TEXT_DIM);
        if(evict_cache())goto done;
        WUSemanticStats before,after;
        wu_semantic_stats(&before);
        start=now();
        error=wu_semantic_encode(text,bytes,folded);
        folded_cold_seconds+=now()-start;
        wu_semantic_stats(&after);
        if(error) { encode_error(text);goto done; }
        cold_calls+=(unsigned)(after.calls-before.calls);
        cold_hits+=(unsigned)(after.hits-before.hits);
        cold_misses+=(unsigned)(after.misses-before.misses);
        consume(folded,i%WU_SEMANTIC_DIM);
    }
    for(unsigned i=0;i<STRING_COUNT;i++) {
        float folded[WU_SEMANTIC_DIM];
        const char *text=sample(i);
        if(wu_semantic_encode(text,strlen(text),folded)) { encode_error(text);goto done; }
    }
    float full[WEB_TEXT_DIM],folded[WU_SEMANTIC_DIM];
    double start=now();
    for(unsigned i=0;i<WARM_REPEATS;i++) {
        unsigned at=i%STRING_COUNT;
        memcpy(full,full_vectors[at/4u][at%4u],sizeof full);
        consume(full,i%WEB_TEXT_DIM);
    }
    double full_cached_seconds=now()-start;
    WUSemanticStats before_warm,after_warm;
    wu_semantic_stats(&before_warm);
    start=now();
    for(unsigned i=0;i<WARM_REPEATS;i++) {
        const char *text=sample(i);
        if(wu_semantic_encode(text,strlen(text),folded)) { encode_error(text);goto done; }
        consume(folded,i%WU_SEMANTIC_DIM);
    }
    double folded_cached_seconds=now()-start;
    wu_semantic_stats(&after_warm);
    printf("{\"scope\":\"standalone pretrained encoder ranking; not RL policy performance\","
        "\"fixture\":\"ocean/webnav/tests/text_cases.json\",\"cases\":%zu,"
        "\"full256_correct\":%u,\"fold32_correct\":%u,\"full256_accuracy\":%.6f,"
        "\"fold32_accuracy\":%.6f,\"ranking_mismatches\":%u,"
        "\"full256_nondeterministic_strings\":%u,\"fold32_nondeterministic_strings\":%u,"
        "\"cold_repeats\":%u,\"warm_repeats\":%u,"
        "\"full256_uncached_us_per_string\":%.6f,"
        "\"full256_cached_materialize_us_per_string\":%.6f,"
        "\"fold32_uncached_us_per_string\":%.6f,\"fold32_cached_us_per_string\":%.6f,"
        "\"cold_cache\":{\"calls\":%u,\"hits\":%u,\"misses\":%u},"
        "\"warm_cache\":{\"calls\":%llu,\"hits\":%llu,\"misses\":%llu},"
        "\"ranking_cache\":{\"calls\":%llu,\"hits\":%llu,\"misses\":%llu},"
        "\"total_cache\":{\"calls\":%llu,\"hits\":%llu,\"misses\":%llu},"
        "\"cold_cache_eviction_outside_timer\":true,\"checksum\":%.9g,\"results\":[",
        CASE_COUNT,full_correct,folded_correct,(double)full_correct/CASE_COUNT,
        (double)folded_correct/CASE_COUNT,mismatches,full_nondeterministic,folded_nondeterministic,
        COLD_REPEATS,WARM_REPEATS,full_cold_seconds*1e6/COLD_REPEATS,
        full_cached_seconds*1e6/WARM_REPEATS,folded_cold_seconds*1e6/COLD_REPEATS,
        folded_cached_seconds*1e6/WARM_REPEATS,cold_calls,cold_hits,cold_misses,
        (unsigned long long)(after_warm.calls-before_warm.calls),
        (unsigned long long)(after_warm.hits-before_warm.hits),
        (unsigned long long)(after_warm.misses-before_warm.misses),
        (unsigned long long)ranking_stats.calls,(unsigned long long)ranking_stats.hits,
        (unsigned long long)ranking_stats.misses,(unsigned long long)after_warm.calls,
        (unsigned long long)after_warm.hits,(unsigned long long)after_warm.misses,(double)checksum);
    for(unsigned c=0;c<CASE_COUNT;c++)
        printf("%s{\"case\":%u,\"expected\":%u,\"full256\":%u,\"fold32\":%u}",
            c?",":"",c,cases[c].expected,full_winners[c],folded_winners[c]);
    puts("]}");
    status=(full_nondeterministic||folded_nondeterministic||cold_hits||
        cold_calls!=COLD_REPEATS||cold_misses!=COLD_REPEATS||
        after_warm.hits-before_warm.hits!=WARM_REPEATS||
        after_warm.misses!=before_warm.misses)?1:0;
done:
    wu_semantic_close();
    web_text_free(encoder);
    return status;
}
