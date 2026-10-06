#ifndef SWAT_FEEDBACK_H
#define SWAT_FEEDBACK_H
#include "sim.h"
typedef struct SwatPlayedLayout {
    bool present;
    uint32_t seed,policy_id,fingerprint;
    int tokens[SWAT_LAYOUT_TOKENS],difficulty,played_ticks,end;
    float civilian_damage;
} SwatPlayedLayout;
typedef struct SwatFeedback {
    SwatPlayedLayout previous,current;
    int last_episode,last_tick;
    bool voted;
} SwatFeedback;
void swat_feedback_track(SwatFeedback* feedback,const SwatSim* sim,bool playing);
bool swat_feedback_ready(const SwatFeedback* feedback);
// choice 0=previous, 1=current, 2=tie. Appends only after an explicit UI choice.
bool swat_feedback_save(SwatFeedback* feedback,const char* path,int choice);
#endif
