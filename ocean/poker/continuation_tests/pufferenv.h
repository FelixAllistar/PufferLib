#pragma once
/* Proof-only minimal ABI shim. This exercises poker.h's actual hooks, not a
 * replacement training loop, and does not claim a full Puffer/PPO build. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <immintrin.h>
typedef struct Env Env;
typedef struct Log Log;
typedef struct Agent {
    float *observations,*actions,*action_mask,*rewards,*terminals;
    int policy;
} Agent;
typedef struct DictItem { const char* key; double value; const char* text; } DictItem;
typedef struct Dict { DictItem* items; int count; } Dict;
static DictItem* dict_find(Dict* d,const char* key) {
    for(int i=0;i<d->count;i++)if(!strcmp(d->items[i].key,key))return d->items+i;
    return NULL;
}
static double dict_get(Dict* d,const char* key) {DictItem* x=dict_find(d,key);return x?x->value:0;}
static const char* dict_get_str(Dict* d,const char* key){DictItem* x=dict_find(d,key);return x&&x->text?x->text:"None";}
static void dict_set(Dict* d,const char* key,double value){DictItem* x=dict_find(d,key);if(x)x->value=value;}
