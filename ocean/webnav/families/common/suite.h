#ifndef WEBNAV_FAMILY_SUITE_H
#define WEBNAV_FAMILY_SUITE_H
#include "loader.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* One serialized host-side transport for heterogeneous family batches.
 * Family and task identifiers are scheduling metadata, never WFView fields. */
typedef struct {
    WFLoaded *families;
    size_t *lane_offsets; /* family_count + 1; exclusive prefix sums */
    size_t *task_offsets; /* family_count + 1; exclusive prefix sums */
    char **task_names;    /* globally unique "family / task" names */
    unsigned char *dirty;
    size_t family_count,total_lanes,total_tasks;
    int poisoned;        /* a failed batch may have partially changed rows */
} WFSuite;

typedef struct {
    uint32_t status,elapsed_ms,deadline_ms;
    float raw_reward,timed_reward;
    size_t global_task;
} WFSuiteResult;

/* Paths are caller supplied. Open stages deterministic initial resets; flush
 * must succeed before observing or applying actions. Close accepts zeroed or
 * partially opened suites. */
int wf_suite_open(WFSuite *suite,const char *const *libraries,size_t count,
                  char *error,size_t error_size);
void wf_suite_close(WFSuite *suite);
int wf_suite_find_lane(const WFSuite *suite,size_t global_lane,
                       size_t *family,size_t *local_lane);
int wf_suite_find_task(const WFSuite *suite,size_t global_task,
                       size_t *family,size_t *local_task);
const char *wf_suite_task_name(const WFSuite *suite,size_t global_task);
int wf_suite_reset_all(WFSuite *suite,uint32_t seed);
int wf_suite_reset_lane(WFSuite *suite,size_t global_lane,
                        size_t global_task,uint32_t seed);
int wf_suite_flush(WFSuite *suite);
int wf_suite_observe(const WFSuite *suite,size_t global_lane,WFView *view);
int wf_suite_apply(WFSuite *suite,size_t global_lane,const WFAction *action);
/* Host-side diagnostics only; never included in the policy's public view. */
int wf_suite_result(const WFSuite *suite,size_t global_lane,WFSuiteResult *out);

#ifdef __cplusplus
}
#endif
#endif
