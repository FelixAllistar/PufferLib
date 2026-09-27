#pragma once
#include "ini.h"
#include <cassert>
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
        int best_clear_frames=0,double mean_checkpoints=0) {
    if(!strcmp(metric,"checkpoints")) {
        assert(std::isfinite(mean_checkpoints)&&mean_checkpoints>=0);
        return mean_checkpoints;
    }
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
    throw std::runtime_error("retro panel: metric must be checkpoints, distance, perf, score or speed");
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

#ifdef PUFFER_RETRO
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;

// The ordinary trainer is unchanged unless [panel] repeats is explicitly enabled.
// Run the existing CPU evaluator after training workers stop. Never fork CUDA state.
static int retro_post_train_eval(Ini* ini,const char* checkpoint,const char* config,
        Dict* log,float* score) {
    Dict* panel=puf_ini_section(ini,"panel",1);
    DictItem* repeats=dict_find(panel,"repeats");
    if(!repeats||repeats->value==0) return 0;
    assert(repeats->value>=1&&repeats->value<=32);
    char output[4096];
    assert(snprintf(output,sizeof(output),"%s.panel.tsv",checkpoint)<(int)sizeof(output));
    const char* metric=puf_ini_get_str(ini,"sweep","metric");
    const char* args[]={"build/retro/sweep_eval",checkpoint,"--config",config,
        "--full-run","--levels",dict_get_str(panel,"levels"),
        "--repeats",dict_get_str(panel,"repeats"),
        "--seed",dict_get_str(panel,"seed"),"--frames",dict_get_str(panel,"frames"),
        "--frameskip",dict_get_str(panel,"frameskip"),
        "--workers",dict_get_str(panel,"workers"),"--metric",metric,
        "--output",output,nullptr};
    pid_t pid;
    assert(posix_spawn(&pid,args[0],nullptr,nullptr,(char* const*)args,environ)==0
        && "build the Retro evaluation panel with make -C ocean/retro panel");
    int status;
    assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0
        && "Retro post-training panel failed");
    FILE* file=fopen(output,"r");
    assert(file);
    char line[4096]; int found=0;
    while(fgets(line,sizeof(line),file)) {
        double value,progress,clear_frames,clear_rate,checkpoints; int clears,attempts; long frames;
        if(sscanf(line,"# score=%lf clears=%d attempts=%d progress=%lf frames=%ld",
                &value,&clears,&attempts,&progress,&frames)==5) {
            assert(std::isfinite(value)&&attempts>0);
            *score=value; found++;
            dict_set(log,"panel/score",value);
            dict_set(log,"panel/attempts",attempts);
            dict_set(log,"panel/clears",clears);
        }
        if(sscanf(line,"# clear_frames=%lf clear_rate=%lf",&clear_frames,&clear_rate)==2) {
            dict_set(log,"panel/clear_rate",clear_rate);
            dict_set(log,"panel/clear_frames",clear_frames);
        }
        if(sscanf(line,"# checkpoints=%lf",&checkpoints)==1)
            dict_set(log,"panel/checkpoints",checkpoints);
    }
    assert(!ferror(file)&&found==1);
    fclose(file);
    fprintf(stderr,"retro_eval metric=%s score=%.6f clears=%.0f/%.0f report=%s\n",
        metric,*score,dict_get(log,"panel/clears"),dict_get(log,"panel/attempts"),output);
    return 1;
}
#endif
