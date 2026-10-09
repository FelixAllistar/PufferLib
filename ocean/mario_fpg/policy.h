#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void* fpg_policy_load(const char* path,int hidden,int layers);
void fpg_policy_reset(void* p);
void fpg_policy_logits(void* p,const float* obs,float* logits);
int fpg_policy_action(void* p,const float* obs,int deterministic);
void fpg_policy_free(void* p);
#ifdef __cplusplus
}
#endif
