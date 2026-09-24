#pragma once
#include "ini.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Called after native CLI overrides are applied, including in each clean
// sweep worker. Rewards contain only explicitly configured event terms.
static void retro_configure(Ini* ini, const char* mode) {
    // Persist the actual execution engine in checkpoint/sweep sidecars.
    if(!dict_find(puf_ini_section(ini,"env",0),"cpu_backend")) {
#ifdef RETRO_DEFAULT_CPU_BLOCKS
        puf_ini_set(puf_ini_section(ini,"env",0),"cpu_backend","blocks");
#else
        puf_ini_set(puf_ini_section(ini,"env",0),"cpu_backend","reference");
#endif
    }
    double gamma=puf_ini_get(ini,"train","gamma");
    if(!std::isfinite(gamma)||gamma<=0||gamma>=1)
        throw std::runtime_error("retro: train.gamma must be between 0 and 1");
    char value[64];
    double scale=puf_ini_get(ini,"env","reward_scale");
    double completion=puf_ini_get(ini,"env","completion_reward");
    DictItem* speed_option=dict_find(puf_ini_section(ini,"env",0),"completion_time_bonus");
    double speed=speed_option?speed_option->value:0;
    DictItem* coin_option=dict_find(puf_ini_section(ini,"env",0),"coin_reward");
    double coin=coin_option?coin_option->value:0;
    DictItem* idle_option=dict_find(puf_ini_section(ini,"env",0),"idle_penalty");
    double idle=idle_option?idle_option->value:0;
    for(const char* key:{"area_transition_reward","area_transition_timer_bonus"}) {
        DictItem* option=dict_find(puf_ini_section(ini,"env",0),key);
        if(option&&option->value!=0)
            throw std::runtime_error("retro: area transition rewards removed; legacy options must be zero");
    }
    DictItem* grace_option=dict_find(puf_ini_section(ini,"env",0),"idle_grace_decisions");
    double grace=grace_option?grace_option->value:8;
    double death=puf_ini_get(ini,"env","death_penalty");
    double score=puf_ini_get(ini,"env","score_scale");
    double clip=puf_ini_get(ini,"train","reward_clip");
    double skip=puf_ini_get(ini,"env","frameskip");
    double spacing=puf_ini_get(ini,"env","checkpoint_distance");
    double checkpoint=puf_ini_get(ini,"env","checkpoint_reward");
    if(!std::isfinite(spacing)||spacing<1||spacing>3400||spacing!=floor(spacing)
        ||!std::isfinite(checkpoint)||checkpoint<0
        ||!std::isfinite(coin)||coin<0||!std::isfinite(idle)||idle<0
        ||!std::isfinite(grace)||grace<0||grace>100000||grace!=floor(grace))
        throw std::runtime_error("retro: invalid checkpoint/coin/idle/area reward configuration");
    // Coin deltas are bounded to the two-digit ROM counter; idle and confirmed
    // area transitions emit at most one event per native frame. The skip
    // multiplier is conservative for clip safety when frameskip changes.
    DictItem* segment_option=dict_find(puf_ini_section(ini,"env",0),"pipe_segment_bonus");
    double segment=segment_option?segment_option->value:0;
    if(!std::isfinite(segment)||segment<0) throw std::runtime_error("retro: invalid pipe segment bonus");
    double upper=scale*(skip*(completion+speed+ceil(3400/spacing)*checkpoint+99*coin+idle+segment)+999999*score);
    double lower=scale*(death+idle);
    if(!std::isfinite(clip)||clip<0)
        throw std::runtime_error("retro: reward_clip must be finite and nonnegative (0 disables clipping)");
    if(!std::isfinite(scale)||scale<=0||!std::isfinite(completion)||completion<0
        ||!std::isfinite(death)||death<0||!std::isfinite(score)||score<0
        ||!std::isfinite(speed)||speed<0||!std::isfinite(upper)||!std::isfinite(lower)
        ||(clip>0&&(upper>clip||lower>clip)))
        throw std::runtime_error("retro: reward_scale/weights exceed the clip-safe bound; reduce reward_scale");
    if(strcmp(mode,"sweep")) return;
    const char* metric=puf_ini_get_str(ini,"sweep","metric");
    if(strcmp(metric,"distance")&&strcmp(metric,"perf")&&strcmp(metric,"score")&&strcmp(metric,"speed"))
        throw std::runtime_error("retro sweep: metric must be distance, perf, score or speed");

    if(!strcmp(puf_ini_get_str(ini,"base","load_model_path"),"latest"))
        throw std::runtime_error("retro sweep: use None or a fixed checkpoint path, not moving latest");
    if(strcmp(puf_ini_get_str(ini,"env","backend"),"quicknes"))
        throw std::runtime_error("retro sweep: ROM backend quicknes is required");
    if(!std::isfinite(skip)||skip<1||skip>16||skip!=floor(skip))
        throw std::runtime_error("retro sweep: frameskip must be an integer in 1..16");
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

// Distance means mean nonnegative forward pixels from each attempt's start,
// not Mario points, absolute world X, or the clear-first composite objective.
static double retro_panel_distance(int start_x,int furthest_x) {
    return std::max(0.0,(double)furthest_x-start_x);
}
static double retro_panel_objective(const char* metric,int clears,int attempts,
        double mean_progress,double mean_distance,double mean_clear_frames=0,int frame_budget=3600,
        int best_clear_frames=0) {
    if(!strcmp(metric,"speed")) {
        if(frame_budget<1||!std::isfinite(mean_clear_frames)||mean_clear_frames<0
            ||mean_clear_frames>frame_budget||attempts<1||clears<0||clears>attempts
            ||(clears>0&&(mean_clear_frames==0||best_clear_frames<1
                ||best_clear_frames>mean_clear_frames))
            ||(clears==0&&(mean_clear_frames!=0||best_clear_frames!=0)))
            throw std::runtime_error("retro panel: invalid clear time");
        // Speedrun objective v2: any one-frame PB improvement wins over
        // the entire mean-time tie-break. Clear count never enters ranking.
        // Zero clears rank below even a last-frame finish; no progress credit.
        if(!clears) return -1;
        return frame_budget-best_clear_frames+0.5*(1.0-mean_clear_frames/frame_budget);
    }
    if(!strcmp(metric,"distance")) {
        if(!std::isfinite(mean_distance)||mean_distance<0||attempts<1)
            throw std::runtime_error("retro panel: invalid distance");
        return mean_distance;
    }
    if(!strcmp(metric,"perf")||!strcmp(metric,"score"))
        return retro_panel_score(clears,attempts,mean_progress);
    throw std::runtime_error("retro panel: metric must be distance, perf, score or speed");
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
