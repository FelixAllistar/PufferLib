#include "feedback.h"
#include "settings.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void swat_feedback_track(SwatFeedback* f,const SwatSim* s,bool playing) {
    if(s->config.mission!=SWAT_GENERATED) { f->last_episode=s->episode; f->last_tick=s->tick; return; }
    if(!f->current.present || f->current.fingerprint!=s->layout.fingerprint) {
        if(f->current.present && f->current.played_ticks>=300) f->previous=f->current;
        memset(&f->current,0,sizeof(f->current)); f->voted=false;
        f->current.present=true; f->current.seed=s->layout.seed; f->current.policy_id=s->layout.policy_id;
        f->current.fingerprint=s->layout.fingerprint; f->current.difficulty=s->layout.difficulty;
        memcpy(f->current.tokens,s->layout.tokens,sizeof(f->current.tokens));
        f->last_episode=s->episode; f->last_tick=s->tick;
    }
    int elapsed=s->episode==f->last_episode ? s->tick-f->last_tick : 0;
    if(playing && elapsed>0 && f->current.played_ticks<3600000) f->current.played_ticks+=elapsed>6 ? 6 : elapsed;
    f->current.end=s->end; f->current.civilian_damage=s->totals.civilian_damage;
    f->last_episode=s->episode; f->last_tick=s->tick;
}
bool swat_feedback_ready(const SwatFeedback* f) {
    return !f->voted && f->previous.present && f->current.present && f->current.played_ticks>=300 &&
        f->previous.fingerprint!=f->current.fingerprint;
}
static int record(char* out,size_t capacity,const SwatPlayedLayout* p) {
    int n=snprintf(out,capacity,"{\"version\":1,\"seed\":%u,\"policy_id\":%u,\"fingerprint\":%u,\"difficulty\":%d,\"tokens\":[",
        p->seed,p->policy_id,p->fingerprint,p->difficulty);
    for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) n+=snprintf(out+n,capacity-(size_t)n,"%s%d",i ? "," : "",p->tokens[i]);
    n+=snprintf(out+n,capacity-(size_t)n,"],\"played_ticks\":%d,\"outcome\":%d,\"civilian_damage\":%.3f}",p->played_ticks,p->end,p->civilian_damage);
    return n;
}
bool swat_feedback_save(SwatFeedback* f,const char* path,int choice) {
    if(!swat_feedback_ready(f) || choice<0 || choice>2) { errno=EINVAL; return false; }
    if(!swat_settings_prepare_path(path)) return false;
    char a[768],b[768],line[2048]; record(a,sizeof(a),&f->previous); record(b,sizeof(b),&f->current);
    const char* choices[]={"a","b","tie"};
    int n=snprintf(line,sizeof(line),"{\"version\":1,\"source\":\"player\",\"time\":%lld,\"a\":%s,\"b\":%s,\"choice\":\"%s\"}\n",
        (long long)time(NULL),a,b,choices[choice]);
    if(n<0 || (size_t)n>=sizeof(line)) { errno=EOVERFLOW; return false; }
    FILE* file=fopen(path,"ab"); if(!file) return false;
    bool ok=fwrite(line,1,(size_t)n,file)==(size_t)n;
    if(fclose(file)!=0) ok=false;
    if(ok) f->voted=true;
    return ok;
}
