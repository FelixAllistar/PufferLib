/* Diagnostic only: frozen text vectors on the original public synonym pool. */
#include "../webnav/text_encoder.h"
#include "../webnav/families/click/expert_synonyms.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define GROUPS (sizeof(click_synonyms)/sizeof(click_synonyms[0]))
static float vectors[GROUPS][8][WEB_TEXT_DIM];
static unsigned sizes[GROUPS];
static unsigned next(unsigned *state) {
    *state=*state*1664525u+1013904223u;
    unsigned x=*state;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static float dot(const float *a,const float *b) {
    float sum=0;for(unsigned i=0;i<WEB_TEXT_DIM;i++)sum+=a[i]*b[i];return sum;
}
int main(void) {
    WebTextEncoder *encoder=web_text_load("build/webnav/reference/potion-tokenizer.json",
        "build/webnav/reference/potion-model.safetensors");
    if(!encoder){fputs("frozen Potion assets unavailable\n",stderr);return 2;}
    for(unsigned g=0;g<GROUPS;g++)for(unsigned i=0;i<8&&click_synonyms[g][i];i++) {
        const char *word=click_synonyms[g][i];
        if(web_text_encode(encoder,word,__builtin_strlen(word),vectors[g][i],NULL))return 2;
        sizes[g]++;
    }
    unsigned rng=91,correct=0,trials=10000,exact=0;
    for(unsigned trial=0;trial<trials;trial++) {
        unsigned groups[6],display[6],chosen=next(&rng)%6;
        for(unsigned i=0;i<6;i++) {
            unsigned g;
            do {
                g=next(&rng)%GROUPS;
                unsigned seen=0;for(unsigned j=0;j<i;j++)seen|=groups[j]==g;
                if(!seen)break;
            }while(1);
            groups[i]=g;display[i]=next(&rng)%sizes[g];
        }
        unsigned target=groups[chosen];
        if(sizes[target]<2){trial--;continue;}
        unsigned requested=(display[chosen]+1+next(&rng)%(sizes[target]-1))%sizes[target];
        const float *q=vectors[target][requested];
        float best=-INFINITY;unsigned winner=0;
        for(unsigned i=0;i<6;i++) {
            float score=dot(q,vectors[groups[i]][display[i]]);
            if(score>best){best=score;winner=i;}
        }
        correct+=winner==chosen;
        exact+=display[chosen]==requested;
    }
    printf("{\"model\":\"frozen Potion base 8M\",\"groups\":%zu,\"candidates\":6,\"distinct_synonym_trials\":%u,\"top1\":%.6f,\"exact_matches\":%u}\n",
        GROUPS,trials,(double)correct/trials,exact);
    web_text_free(encoder);
    return 0;
}
