#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void* fpt_policy_load(const char* path,int hidden,int layers);
void fpt_policy_reset(void* policy);
// Copy recurrent carry before an observation intervention; weights stay owned
// by each policy. Returns zero if the policy shapes are incompatible.
int fpt_policy_copy_state(void* destination,const void* source);
const float* fpt_policy_logits(void* policy,const float* observation);
void fpt_policy_free(void* policy);
#ifdef __cplusplus
}
#endif
