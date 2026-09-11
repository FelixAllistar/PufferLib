#pragma once
#include "ini.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Called after native CLI overrides are applied, including in each clean
// sweep worker. Keep learner and potential-shaping discounts coupled.
static void retro_configure(Ini* ini, const char* mode) {
    double gamma=puf_ini_get(ini,"train","gamma");
    if(!std::isfinite(gamma)||gamma<=0||gamma>=1)
        throw std::runtime_error("retro: train.gamma must be between 0 and 1");
    char value[64]; snprintf(value,sizeof(value),"%.17g",gamma);
    puf_ini_put(ini,"env.potential_gamma",value);
    double scale=puf_ini_get(ini,"env","reward_scale");
    double completion=puf_ini_get(ini,"env","completion_reward");
    double death=puf_ini_get(ini,"env","death_penalty");
    double score=puf_ini_get(ini,"env","score_scale");
    double clip=puf_ini_get(ini,"train","reward_clip");
    double skip=puf_ini_get(ini,"env","frameskip");
    double spacing=puf_ini_get(ini,"env","checkpoint_distance");
    double checkpoint=puf_ini_get(ini,"env","checkpoint_reward");
    if(!std::isfinite(spacing)||spacing<1||spacing>3400||spacing!=floor(spacing)
        ||!std::isfinite(checkpoint)||checkpoint<0)
        throw std::runtime_error("retro: invalid checkpoint_distance/checkpoint_reward");
    double upper=scale*(1+skip*(completion+ceil(3400/spacing)*checkpoint)+999999*score);
    double lower=scale*(1+death);
    if(!std::isfinite(scale)||scale<=0||completion<0||death<0||score<0
        ||!std::isfinite(upper)||!std::isfinite(lower)
        ||(clip>0&&(upper>clip||lower>clip)))
        throw std::runtime_error("retro: reward_scale/weights exceed the clip-safe bound; reduce reward_scale");
    if(strcmp(mode,"sweep")) return;

    if(strcmp(puf_ini_get_str(ini,"base","load_model_path"),"None"))
        throw std::runtime_error("retro sweep: use base.load_model_path=None; trials must start from the same fresh initialization");
    if(strcmp(puf_ini_get_str(ini,"env","backend"),"quicknes")
        ||strcmp(puf_ini_get_str(ini,"env","spawn_levels"),"all")
        ||puf_ini_get(ini,"env","frameskip")!=1)
        throw std::runtime_error("retro sweep: ROM backend, all levels and one-frame controls are required");
    if(clip!=1)
        throw std::runtime_error("retro sweep: keep reward_clip=1 and use the common clip-safe reward_scale");
    double steps=puf_ini_get(ini,"sweep","trial_timesteps");
    if(!std::isfinite(steps)||steps<1||steps!=floor(steps))
        throw std::runtime_error("retro sweep: trial_timesteps must be a positive integer");
    snprintf(value,sizeof(value),"%.0f",steps);
    puf_ini_put(ini,"train.total_timesteps",value);
    // Equal final training budgets; no moving 'latest' anchor or mid-run
    // checkpoint lottery. The standard final save still occurs at interval 0.
    puf_ini_put(ini,"base.checkpoint_interval","0");
    puf_ini_put(ini,"sweep.downsample","1");
}

static double retro_panel_progress(int start_x, int furthest_x) {
    return std::max(0.0,std::min(1.0,(furthest_x-start_x)/3400.0));
}

// For a fixed panel, ONE extra clear beats the entire progress contribution.
// Progress only orders equal-clear candidates; it is not a game reward or a
// claim that X/3400 is a calibrated fraction of every level.
static double retro_panel_score(int clears, int attempts, double mean_progress) {
    if(attempts<1||clears<0||clears>attempts||!std::isfinite(mean_progress)
        ||mean_progress<0||mean_progress>1)
        throw std::runtime_error("retro panel: invalid score inputs");
    return 100.0*(clears+0.5*mean_progress)/attempts;
}

// Native decoder layout: 64 action logits, THEN one value (not vice versa).
static int retro_panel_action(const float* row,unsigned random_word,bool deterministic) {
    int best=0;
    for(int a=0;a<64;a++) {
        if(!std::isfinite(row[a])) throw std::runtime_error("nonfinite policy logits");
        if(row[a]>row[best]) best=a;
    }
    if(deterministic) return best;
    double probabilities[64],total=0;
    for(int a=0;a<64;a++) total+=(probabilities[a]=exp((double)row[a]-row[best]));
    double threshold=((double)random_word+0.5)/4294967296.0*total;
    for(int a=0;a<63;a++) { threshold-=probabilities[a]; if(threshold<0) return a; }
    return 63;
}
