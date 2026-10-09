#ifndef WEBNAV_UNIFIED_POLICY_H
#define WEBNAV_UNIFIED_POLICY_H
#include "capabilities.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WU_PARAMETER_BINS 257u
#define WU_TEXT_LIMIT 255u
#define WU_COPY_TOKENS 256u
#define WU_GLOBAL_FEATURES 288u
#define WU_NODE_FEATURES 192u
#define WU_OBS_SIZE (WU_GLOBAL_FEATURES+WF_MAX_NODES*WU_NODE_FEATURES)
#define WU_EXEC_COUNT (22u*(WF_MAX_NODES+1u))
#define WU_SET0 WU_EXEC_COUNT
#define WU_SET1 (WU_SET0+WU_PARAMETER_BINS)
#define WU_LITERAL (WU_SET1+WU_PARAMETER_BINS)
#define WU_TOKEN (WU_LITERAL+95u)
#define WU_VALUE (WU_TOKEN+WU_COPY_TOKENS)
#define WU_INSTRUCTION (WU_VALUE+WF_MAX_NODES)
#define WU_ACTIONS (WU_INSTRUCTION+1u)
typedef struct {
    uint32_t parameter0,parameter1,last_kind,last_ref,steps;
    size_t text_length;
    char text[WU_TEXT_LIMIT+1u];
} WUState;
/* One encoder/catalog for every family. Input is strictly public; no task ID,
 * private goals, per-task parsers or reward fields. Text has ordered bytes and
 * lexical features plus reserved shared frozen semantic vectors.
 * Slot zero targets the page; slot i+1 targets public node i. */
int wu_project(const WFView *,const WUCapabilities *,const WUState *,
               float observations[WU_OBS_SIZE],unsigned char mask[WU_ACTIONS]);
/* Returns 1 for simulator command, 0 for local parameter/text selection,
 * negative for invalid selection. Command text borrows state storage. */
int wu_decode(const WFView *,const WUCapabilities *,WUState *,uint32_t choice,
              uint32_t elapsed_ms,WFAction *);
#ifdef __cplusplus
}
#endif
#endif
