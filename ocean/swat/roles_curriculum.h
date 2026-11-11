// Role task training curriculum (CONTRACT.md "Curriculum"). Stages live in
// roles_curriculum.ini, one [section] per stage in file order; roles_stage=N
// in config/swat.ini runs stage N, whose keys override the [env] values. A
// stage may only change task difficulty (mission, occupancy, layout pool,
// hostile fire, opponents, escape, time limit, which roles are controlled).
// Shaped reward coefficients, seeds, layout seeds and the evaluation-only
// occupancy are rejected, so the curriculum can never alter what a reward
// means or reach the held-out evaluation suite (roles_eval.h).
// promote_metric / promote_at / promote_window describe when an operator
// moves a run to the next stage (a puf_log metric averaged over that many
// training episodes); they are recorded here, not applied by the env.
#ifndef SWAT_ROLES_CURRICULUM_H
#define SWAT_ROLES_CURRICULUM_H
#include <string.h>
#include "roles_split.h"

#define SWAT_ROLES_CURRICULUM_PATH "ocean/swat/roles_curriculum.ini"
#define SWAT_ROLES_MAX_STAGES 32

static const char* const swat_roles_stage_keys[]={
    "roles_mission","motel_occupancy","roles_layouts","hostile_fire","max_ticks",
    "roles_scripted","roles_scripted_rate","suspect_escape",
    "roles_officers","roles_suspects","roles_civilians",
};
static const char* const swat_roles_stage_meta[]={"promote_metric","promote_at","promote_window"};
// Metrics a stage may be promoted on: puf_log keys that do not depend on
// reward coefficients.
static const char* const swat_roles_promote_metrics[]={"perf","scripted_perf","arrests","civilians_safe"};

static bool swat_roles_key_in(const char* key,const char* const* keys,int count) {
    for(int i=0;i<count;i++) if(!strcmp(key,keys[i])) return true;
    return false;
}
#define SWAT_ROLES_COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))

// NULL when the stage is a valid training stage, else the reason.
static const char* swat_roles_stage_check(Dict* stage,char* reason,size_t size) {
    bool metric=false,threshold=false;
    for(int i=0;i<stage->size;i++) {
        const DictItem* item=&stage->items[i];
        if(swat_roles_key_in(item->key,swat_roles_stage_meta,SWAT_ROLES_COUNT(swat_roles_stage_meta))) {
            if(!strcmp(item->key,"promote_metric")) {
                metric=item->str && swat_roles_key_in(item->str,swat_roles_promote_metrics,SWAT_ROLES_COUNT(swat_roles_promote_metrics));
                if(!metric) { snprintf(reason,size,"promote_metric must be perf, scripted_perf, arrests or civilians_safe"); return reason; }
            }
            if(!strcmp(item->key,"promote_at")) threshold=true;
            if(!strcmp(item->key,"promote_window") && (item->value<1 || floor(item->value)!=item->value)) {
                snprintf(reason,size,"promote_window must be a positive integer"); return reason;
            }
            continue;
        }
        if(!swat_roles_key_in(item->key,swat_roles_stage_keys,SWAT_ROLES_COUNT(swat_roles_stage_keys))) {
            snprintf(reason,size,"'%s' is not a curriculum difficulty key (rewards, seeds and layouts stay in [env])",item->key);
            return reason;
        }
        if(!strcmp(item->key,"motel_occupancy") && item->value==SWAT_ROLES_EVAL_OCCUPANCY) {
            snprintf(reason,size,"motel_occupancy=%d is reserved for the held-out evaluation suite",SWAT_ROLES_EVAL_OCCUPANCY);
            return reason;
        }
    }
    if(!metric || !threshold) { snprintf(reason,size,"a stage needs promote_metric and promote_at"); return reason; }
    return NULL;
}

// Loads and validates every stage of the curriculum file; exits on error,
// like the trainer's own config loader.
static void swat_roles_curriculum_load(Ini* ini,const char* path) {
    memset(ini,0,sizeof(*ini));
    puf_ini_load_file(ini,path);
    if(ini->num_sections<1 || ini->num_sections>SWAT_ROLES_MAX_STAGES) {
        fprintf(stderr,"swat roles: %s must define 1 to %d stages\n",path,SWAT_ROLES_MAX_STAGES); exit(1);
    }
    for(int s=0;s<ini->num_sections;s++) {
        char reason[160];
        if(swat_roles_stage_check(&ini->sections[s],reason,sizeof(reason))) {
            fprintf(stderr,"swat roles: %s stage %d [%s]: %s\n",path,s,ini->sections[s].name,reason); exit(1);
        }
    }
}

// kwargs with stage `stage` of the curriculum applied on top (difficulty
// keys only; promotion metadata stays out of the env's kwargs).
static void swat_roles_apply_stage(Dict* kwargs,Dict* merged,const Ini* curriculum,int stage) {
    dict_copy(merged,kwargs);
    const Dict* section=&curriculum->sections[stage];
    for(int i=0;i<section->size;i++) {
        const DictItem* item=&section->items[i];
        if(swat_roles_key_in(item->key,swat_roles_stage_meta,SWAT_ROLES_COUNT(swat_roles_stage_meta))) continue;
        DictItem* out=dict_set_str(merged,item->key,item->str ? item->str : "");
        out->value=item->value;
    }
}
#endif
