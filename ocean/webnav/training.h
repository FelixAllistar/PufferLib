#ifndef WEBNAV_TRAINING_H
#define WEBNAV_TRAINING_H
#include <stdint.h>
#include "bridge.h"
#include "text_encoder.h"
#define WT_VERSION 1
#define WT_NODES 16
#define WT_SPANS 8
#define WT_ACTIONS (1+WT_NODES+WT_SPANS+10)
#define WT_NODE_FEATURES (512+32+32)
#define WT_SPAN_FEATURES (256+8)
#define WT_FEATURES (8+1024+32+WT_NODES*WT_NODE_FEATURES+WT_SPANS*WT_SPAN_FEATURES)
/* The ordered tokenizer is UTF-8 bytes, encoded as eight bits per byte.
 * Views retain complete bounded strings; features report clipped prefixes. */
enum {WT_OTHER,WT_BUTTON,WT_CHECKBOX,WT_INPUT,WT_LINK,WT_SELECT,WT_RADIO,WT_OPTION,WT_FOLDER,WT_FILE,WT_CELL};
enum {WT_VISIBLE=1,WT_ENABLED=2,WT_CHECKED=4,WT_FOCUSED=8,WT_EXPANDED=16,WT_CLICKABLE=32,WT_MENU=64};
typedef struct {unsigned ref,parent,role,flags,start,end,capacity,insert_limit; float x,y; char name[65],value[65];} WTNode;
typedef struct {unsigned count,spans,omitted,text_clipped,elapsed;char query[129];WTNode nodes[WT_NODES];char copy[WT_SPANS][65];unsigned copy_node[WT_SPANS];} WTView;
typedef struct WTFeatures WTFeatures;
#ifdef __cplusplus
extern "C" {
#endif
extern const char *wt_task_names[12];
/* Generator task IDs are the order in wt_task_names, not private row tags. */
void wt_request(uint32_t *row,unsigned task,uint32_t seed);
int wt_view(const uint32_t *row,WTView *view);
void wt_mask(const WTView *view,unsigned char mask[WT_ACTIONS]);
/* One public action; copy is an insertion at the current caret/selection.
 * Caller can select all first. Returns 0 for legal, -1 for invalid/no-op. */
int wt_action(uint32_t *row,const WTView *view,unsigned action,unsigned elapsed);
unsigned wt_done(const uint32_t *row);
float wt_reward(const uint32_t *row);
float wt_raw_reward(const uint32_t *row);
WTFeatures *wt_features_new(int potion);
void wt_features_free(WTFeatures *features);
void wt_features(WTFeatures *features,const WTView *view,float out[WT_FEATURES]);
int wt_expert(const WTView *view); /* public information only, generator validation */
#ifdef __cplusplus
}
#endif
#endif
