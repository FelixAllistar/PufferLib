#ifndef WEBNAV_FAMILY_LOADER_H
#define WEBNAV_FAMILY_LOADER_H
#include "family_api.h"
typedef struct {void *handle;const WFFamily *api;uint32_t *words;} WFLoaded;
int wf_open(WFLoaded *out,const char *library,char *error,size_t error_size);
void wf_close(WFLoaded *family);
int wf_reset(WFLoaded *family,uint32_t task,uint32_t first_seed);
int wf_batch_checked(WFLoaded *family);
int wf_observe(WFLoaded *family,unsigned lane,WFView *out);
int wf_apply(WFLoaded *family,unsigned lane,const WFAction *action);
int wf_view_valid(const WFView *view);
#endif
