#ifndef WEBNAV_OBSERVATION_H
#define WEBNAV_OBSERVATION_H
#include "bridge.h"
/* Baseline byte features, not an NLP tokenizer. Every byte is preserved;
 * extra a..d indicators make this four-letter pilot learnable by a small MLP. */
#define WEBNAV_FEATURES (WEBNAV_OBS * 5)
static inline void webnav_features(const uint32_t *bytes, float *out) {
    for (int i=0;i<WEBNAV_OBS;i++) {
        out[5*i]=(float)bytes[i]/255.0f;
        for(int j=0;j<4;j++)out[5*i+j+1]=bytes[i]==(uint32_t)(97+j);
    }
}
#endif
