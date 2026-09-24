#ifndef WT_POLICY_H
#define WT_POLICY_H
#include "training.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WTPolicy WTPolicy;
WTPolicy *wt_policy_load(const char *path);
void wt_policy_reset(WTPolicy *p);
int wt_policy_action(WTPolicy *p,const WTView *v);
const float *wt_policy_logits(WTPolicy *p);
void wt_policy_free(WTPolicy *p);
#ifdef __cplusplus
}
#endif
#endif
