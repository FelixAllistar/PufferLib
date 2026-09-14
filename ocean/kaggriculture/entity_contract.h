#pragma once

/* Fresh policy ABI. All observations are floats; scaling never clips values. */
#define KAG_POLICY_VERSION 3
#define KAG_OBSERVATION_ENTITIES 3
#define KAG_GLOBAL_OFFSET 0
#define KAG_GLOBAL_FEATURES 128
#define KAG_PRODUCT_OFFSET 128
#define KAG_PRODUCT_COUNT 9
#define KAG_PRODUCT_FEATURES 56
#define KAG_PLOT_OFFSET 632
#define KAG_PLOT_COUNT 8
#define KAG_PLOT_FEATURES 24
#define KAG_WORKER_OFFSET 824
#define KAG_WORKER_COUNT 17
#define KAG_WORKER_FEATURES 32
#define KAG_TASK_OFFSET 1368
#define KAG_TASK_FEATURES 56
#define KAG_ENTITY_OBS_SIZE 1424
#define KAG_FUSION_WIDTH (64 + 9 * 32 + 8 * 32 + 17 * 16)
#define KAG_TASK_LOGITS (17 * 44)
#define KAG_MARKET_LOGITS (10 * (2 + 21 + 8))
#define KAG_ALL_LOGITS (KAG_TASK_LOGITS + KAG_MARKET_LOGITS)
#define KAG_OBS_RESET_SOURCE_INDEX 31
/* The trainer/optimizer flattens matrices without inter-tensor gaps. Pad the
 * augmented feature dimension, not the allocation between matrices, so every
 * registered parameter/gradient tensor occupies a multiple of 16 bf16 bytes. */
#define KAG_AUG_WIDTH(features) (((features) + 8) & ~7)

typedef struct {
    int valid;
    int mode;
    int executor;
    int interval;
    int score_features;
} KagController;

typedef struct {
    float money_scale;
    float quality_scale;
    float quality_idle_cost;
    float pbrs_scale;
    float gamma;
    float cash_weight;
    float stock_weight;
    float crop_weight;
    float animal_weight;
    /* Independent experiment switches: 0 terminal, 1 dense. */
    int money_timing;
    int quality_timing;
    float growth_land;
    float growth_crop;
    float growth_animal;
    float alive_daily;
    int target_plots;   /* Total owned plots, including the initial plot. */
    int target_animals;
    int target_crops;   /* -1 derives planting capacity minus target_animals. */
} KagRewardConfig;

typedef struct {
    int start_cash;
    int start_step;
    float coverage_sum;
    float idle_sum;
    float phi;
    float discount;
    float discounted_pbrs;
    float money_reward;
    float quality_reward;
    int previous_cash;
    int peak_plots;
    int peak_crops;
    int peak_animals;
    float growth_land_reward;
    float growth_crop_reward;
    float growth_animal_reward;
    float alive_reward;
} KagRewardState;

typedef struct {
    int valid;
    int inventory;
    /* 8 order proceeds, 8 order successor prices, then 8 bulk pairs. */
    float quotes[32];
} KagQuoteCache;
