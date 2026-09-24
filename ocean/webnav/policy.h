#ifndef WEBNAV_POLICY_H
#define WEBNAV_POLICY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *webnav_policy_load(const char *path,int hidden,int layers);
void webnav_policy_reset(void *policy);
int webnav_policy_action(void *policy,const uint32_t *obs,const uint32_t *mask);
void webnav_policy_free(void *policy);
const float *webnav_policy_logits(void *policy);
#ifdef __cplusplus
}
#endif
#endif
