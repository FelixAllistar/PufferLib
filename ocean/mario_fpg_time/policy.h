#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void* fpt_policy_load(const char* path,int hidden,int layers);
void fpt_policy_reset(void* policy);
const float* fpt_policy_logits(void* policy,const float* observation);
void fpt_policy_free(void* policy);
#ifdef __cplusplus
}
#endif
