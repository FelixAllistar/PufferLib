#ifndef WEBNAV_FINISH_H
#define WEBNAV_FINISH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define WFN_VERSION 1u
#define WFN_WORDS 65536u
#define WFN_JSON_BYTES 16384u
#define WFN_DETAIL_BYTES 2048u
enum { WFN_OPEN, WFN_SUBMITTED, WFN_EXPIRED };
enum { WFN_NAVIGATE, WFN_MUTATE, WFN_RETRIEVE };
enum { WFN_SUCCESS, WFN_ACTION_NOT_ALLOWED, WFN_PERMISSION_DENIED,
       WFN_NOT_FOUND, WFN_DATA_VALIDATION, WFN_UNKNOWN_ERROR };
typedef struct { uint32_t words[WFN_WORDS]; } WFNState;
typedef struct {
    uint32_t kind,status;
    /* Complete JSON values, not C-string coercions. Retrieval accepts an array
     * of public result items or null; other operations require null. Detail is
     * a JSON string or null, and must be null for SUCCESS. Neither field is
     * inferred from a task ID, private target or grader result. */
    const char *data_json;
    size_t data_bytes;
    const char *detail_json;
    size_t detail_bytes;
} WFNReply;

int wfn_reset(WFNState *);
int wfn_validate(const WFNState *);
/* All negative returns leave state unchanged. A valid submission to an already
 * terminal state is absorbed by Bend. Submission never grades the claim. */
int wfn_submit(WFNState *,const WFNReply *);
int wfn_expire(WFNState *);
/* Result is available only after explicit submit. Size excludes the trailing
 * NUL. out=NULL/capacity=0 queries size; -2 means insufficient capacity and
 * leaves out untouched. No number conversion or answer normalization occurs. */
int wfn_json(const WFNState *,char *out,size_t capacity,size_t *bytes);
#ifdef __cplusplus
}
#endif
#endif
