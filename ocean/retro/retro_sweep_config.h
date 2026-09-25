#pragma once
#include "ini.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
