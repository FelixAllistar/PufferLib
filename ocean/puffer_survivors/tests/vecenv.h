#pragma once
#include <stddef.h>

typedef struct { double value; } DictItem;
typedef struct Dict Dict;
static inline DictItem* dict_get_unsafe(Dict* d, const char* name) { (void)d; (void)name; return NULL; }
static inline void dict_set(Dict* d, const char* name, float value) { (void)d; (void)name; (void)value; }

typedef float FloatTensor;
typedef void* cudaStream_t;

typedef struct {
    int size;
    int total_agents;
    void* gpu_observations;
    float* gpu_actions;
    float* gpu_rewards;
    float* gpu_terminals;
} StaticVec;
