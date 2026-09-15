#pragma once
#include <math.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "pufferenv.h"
#include "pokemon_core.h"
#include "state_bank.h"
#include "core_deck.h"
#include "behavior.h"
#include "species_labels.h"
typedef uint8_t obs_t;
#define OBS_SIZE PK_OBS
#define NUM_ATNS 1
#define ACT_SIZES {PK_ACTIONS}
#define PUF_POKEMON_DASHBOARD 1
#define SELFPLAY_MAX_BANKS 32
typedef struct { char path[4096], team[256], lead[64]; } PKNativeBank;
static PKNativeBank pk_native_banks[SELFPLAY_MAX_BANKS];
static int pk_native_count;
static double pk_native_fraction;
static double pk_core_probability = 1;
static char pk_learner_team[256]="None", pk_learner_lead[64]="None";
static int pk_current_selfplay=1;
static inline void pk_native_configure(Ini* ini, const char* mode) {
    pk_native_count = 0;
    pk_native_fraction = 0;
    // Frozen league opponents are independent of learner/history selfplay.
    if (strcmp(mode,"train")) return;
    DictItem* item = dict_find(puf_ini_section(ini,"env",0),"native_league");
    if (strcmp(mode,"train") || !item || !item->str || !*item->str || !strcmp(item->str,"None")) return;
    DictItem* fraction_option=dict_find(puf_ini_section(ini,"env",0),"expert_fraction");
    double fraction=fraction_option?fraction_option->value:1;
    if(!isfinite(fraction) || fraction<0 || fraction>1) {
        fprintf(stderr,"expert_fraction must be in [0,1]\n"); exit(1);
    }
    // The non-expert share plays the current policy, not frozen history.
    puf_ini_put(ini,"selfplay.enabled","0");
    if(fraction==0) {
        puf_ini_put(ini,"vec.num_frozen_banks","0");
        puf_ini_put(ini,"vec.frozen_bank_pct","0");
        printf("Pokemon native league: expert_fraction=0; current-policy self-play only\n");
        return;
    }
    Ini manifest = {0};
    puf_ini_load_file(&manifest,item->str);
    int count = (int)puf_ini_get(&manifest,"native","banks");
    int hidden = (int)puf_ini_get(&manifest,"native","hidden_size");
    int layers = (int)puf_ini_get(&manifest,"native","num_layers");
    if (count < 1 || count > SELFPLAY_MAX_BANKS || hidden < 1 || layers < 1 ||
            strcmp(puf_ini_get_str(&manifest,"native","rules_sha"),PK_RULES_SHA)) {
        fprintf(stderr,"Invalid free-pick Pokemon league; export a fresh ABI 3 league\n"); exit(1);
    }
    int buffers=(int)puf_ini_get(ini,"vec","num_buffers");
    int agents=(int)puf_ini_get(ini,"vec","total_agents");
    if(buffers<1 || agents<2 || agents%(2*buffers) ||
            (int)((float)fraction*(agents/(2*buffers)))<count) {
        fprintf(stderr,"expert_fraction must allocate at least one environment per expert per buffer; increase total_agents or expert_fraction\n"); exit(1);
    }
    for (int b=0;b<count;b++) {
        char section[32]; snprintf(section,sizeof(section),"bank.%d",b);
        const char* path=puf_ini_get_str(&manifest,section,"path");
        const char* team=puf_ini_get_str(&manifest,section,"team");
        const char* lead=puf_ini_get_str(&manifest,section,"lead");
        if (strlen(path)>=sizeof(pk_native_banks[b].path) || strlen(team)>=sizeof(pk_native_banks[b].team)) exit(1);
        snprintf(pk_native_banks[b].path,sizeof(pk_native_banks[b].path),"%s",path);
        snprintf(pk_native_banks[b].team,sizeof(pk_native_banks[b].team),"%s",team);
        if(strlen(lead)>=sizeof(pk_native_banks[b].lead))exit(1);
        snprintf(pk_native_banks[b].lead,sizeof(pk_native_banks[b].lead),"%s",lead);
        // Checkpoint-side team and ABI must agree with the exported binding.
        char config[4096]; snprintf(config,sizeof(config),"%s",path);
        char* slash=strrchr(config,'/'); if(!slash) exit(1);
        strcpy(slash+1,"config.ini");
        Ini saved={0}; puf_ini_load_file(&saved,config);
        if (puf_ini_get(&saved,"env","abi_version")!=PK_ABI_VERSION ||
            puf_ini_get(&saved,"env","policy_version")!=3 ||
            strcmp(puf_ini_get_str(&saved,"env","rules_sha"),PK_RULES_SHA) ||
            strcmp(puf_ini_get_str(&saved,"env","learner_team"),team) ||
            strcmp(puf_ini_get_str(&saved,"env","learner_lead"),lead) ||
            puf_ini_get(&saved,"policy","hidden_size")!=hidden ||
            puf_ini_get(&saved,"policy","num_layers")!=layers) {
            fprintf(stderr,"Pokemon native league checkpoint/team mismatch: %s\n",path); exit(1);
        }
        puf_ini_free(&saved);
    }
    char value[32];
    snprintf(value,sizeof(value),"%d",count); puf_ini_put(ini,"vec.num_frozen_banks",value);
    snprintf(value,sizeof(value),"%d",hidden); puf_ini_put(ini,"vec.frozen_bank_hidden_size",value);
    snprintf(value,sizeof(value),"%d",layers); puf_ini_put(ini,"vec.frozen_bank_num_layers",value);
    puf_ini_put(ini,"base.async","0");
    snprintf(value,sizeof(value),"%.17g",fraction); puf_ini_put(ini,"vec.frozen_bank_pct",value);
    puf_ini_put(ini,"vec.seat_balance","0");
    puf_ini_put(ini,"selfplay.opponent_pool",pk_native_banks[0].path);
    puf_ini_put(ini,"selfplay.opponent_pool_weights","1");
    puf_ini_put(ini,"selfplay.opponent_league","None");
    puf_ini_put(ini,"base.load_enemy_model_path","None");
    puf_ini_put(ini,"selfplay.opponent_pool_prob","1");
    puf_ini_put(ini,"selfplay.opp_timeout_steps","0"); // immutable banks for this run
    puf_ini_put(ini,"selfplay.eval_pool_size","0");
    puf_ini_put(ini,"env.team_selection","1");
    puf_ini_put(ini,"train.epoch_sampling","1"); puf_ini_put(ini,"train.prio_alpha","0"); puf_ini_put(ini,"train.prio_beta0","0");
    pk_native_count=count;
    pk_native_fraction=fraction;
    printf("Pokemon native league: %d frozen bank slots; expert_env_fraction=%.9g current_selfplay_env_fraction=%.9g; only learner trains\n",
        count,fraction,1-fraction);
    puf_ini_free(&manifest);
}
static inline const char* pk_native_bank_path(int bank, const char* fallback) {
    if (!pk_native_count) return fallback;
    assert(bank>=0 && bank<pk_native_count);
    return pk_native_banks[bank].path;
}
#define PUF_SELFPLAY_BANK_PATH(bank,path) pk_native_bank_path(bank,path)
#define PUF_FROZEN_LEAGUE_ACTIVE() (pk_native_count > 0)
static inline void pk_name_slug(const char* name, char slug[64]) {
    int n = 0;
    for (; *name && n < 62; name++) {
        if (isalnum((unsigned char)*name)) slug[n++] = (char)tolower((unsigned char)*name);
        else if (n && slug[n-1] != '_') slug[n++] = '_';
    }
    if (n && slug[n-1] == '_') n--;
    slug[n] = 0;
}
static inline void pk_set_slug(int species, char slug[64]) {
    pk_name_slug(pk_species_names[species],slug);
}
static inline void pk_species_label(int species, char name[64]) {
    snprintf(name,64,"%s",species>=1 && species<=149 ? pk_species_labels[species] : "Unknown");
}
// Stable full metrics remain available in run logs; only the dashboard ranks
// and combines variants into species. Zero-frequency species are not displayed.
static inline int pk_top_species_group(Dict* log, const char* prefix, const char* group,
        int ids[6], double frequencies[6]) {
    double totals[150] = {0};
    for (int set=1;set<=149;set++) {
        char slug[64], key[128];
        pk_set_slug(set,slug);
        snprintf(key,sizeof(key),"%s%s/%s",prefix,group,slug);
        DictItem* item = dict_find(log,key);
        if (item && isfinite(item->value) && item->value>0) totals[set] += item->value;
    }
    int count=0;
    for (;count<6;count++) {
        int best=0;
        for(int species=1;species<=149;species++) if(totals[species]>totals[best]) best=species;
        if(!best) break;
        ids[count]=best; frequencies[count]=totals[best]; totals[best]=0;
    }
    return count;
}
static inline int pk_top_species(Dict* log, const char* prefix, int leads,
        int ids[6], double frequencies[6]) {
    return pk_top_species_group(log,prefix,leads?"lead":pk_core_deck.cards?"free_team":"team",ids,frequencies);
}
static inline void pk_configure(Ini* ini, const char* mode) {
    const char* team=puf_ini_get_str(ini,"env","learner_team");
    const char* lead=puf_ini_get_str(ini,"env","learner_lead");
    if(strlen(team)>=sizeof(pk_learner_team) || strlen(lead)>=sizeof(pk_learner_lead)) {
        fprintf(stderr,"Pokemon team/lead option too long\n");exit(1);
    }
    snprintf(pk_learner_team,sizeof(pk_learner_team),"%s",team);
    snprintf(pk_learner_lead,sizeof(pk_learner_lead),"%s",lead);
    pk_native_configure(ini,mode);
    pk_current_selfplay=pk_native_count>0 || puf_ini_get(ini,"selfplay","enabled")==0;
    if(pk_current_selfplay) {
        puf_ini_put(ini,"env.opponent_team",pk_learner_team);
        puf_ini_put(ini,"env.opponent_lead",pk_learner_lead);
    }
    pk_core_free(&pk_core_deck);
    pk_core_probability=1;
    DictItem* core_option=dict_find(puf_ini_section(ini,"env",0),"force_core_combos");
    double core=core_option?core_option->value:0;
    if(core!=0 && core!=1) { fprintf(stderr,"force_core_combos must be 0 or 1\n"); exit(1); }
    if(strcmp(mode,"train")) { core=0; puf_ini_put(ini,"env.force_core_combos","0"); }
    if(core) {
        DictItem* probability=dict_find(puf_ini_section(ini,"env",0),"force_core_prob");
        pk_core_probability=probability?probability->value:1;
        if(!isfinite(pk_core_probability) || pk_core_probability<0 || pk_core_probability>1) {
            fprintf(stderr,"force_core_prob must be in [0,1]\n"); exit(1);
        }
        if(puf_ini_get(ini,"env","reset_state_prob")!=0 ||
                puf_ini_get(ini,"env","team_selection")!=1 ||
                puf_ini_get(ini,"vec","num_buffers")!=1 || puf_ini_get(ini,"base","async")!=0 ||
                puf_ini_get(ini,"base","reset_every_horizon")!=0 ||
                strcmp(puf_ini_get_str(ini,"env","learner_team"),"None") ||
                strcmp(puf_ini_get_str(ini,"env","opponent_team"),"None") ||
                strcmp(pk_learner_lead,"None")) {
            fprintf(stderr,"Core coverage requires fresh unrestricted drafts, reset_state_prob=0, one synchronous buffer, reset_every_horizon=0 (native_league experts are supported)\n"); exit(1);
        }
        double seed=puf_ini_get(ini,"env","core_seed");
        if(!isfinite(seed) || seed<0 || seed>UINT32_MAX || floor(seed)!=seed ||
                !pk_core_init(&pk_core_deck,puf_ini_get_str(ini,"env","core_pool"),(uint64_t)seed)) {
            fprintf(stderr,"Invalid core_seed or core_pool: use all or 3..149 distinct species IDs\n"); exit(1);
        }
        puf_ini_put(ini,"env.core_observation_version","1");
        double assigned=puf_ini_get(ini,"env","core_start_assigned"),drafted=puf_ini_get(ini,"env","core_start_drafted");
        if(!isfinite(assigned) || !isfinite(drafted) || assigned<0 || drafted<0 || assigned>9007199254740991.0 ||
           floor(assigned)!=assigned || floor(drafted)!=drafted || !pk_core_seek(&pk_core_deck,(uint64_t)assigned,(uint64_t)drafted)) {
            fprintf(stderr,"Invalid core curriculum cursor\n");exit(1);
        }
        printf("PK_CORE_DECK v=2 species=%d combinations=%u seed=%.0f probability=%.9g schedule=serial-env-seat-order individual_moves=learned extra_slots=learned\n",
            pk_core_deck.species_count,pk_core_deck.count,seed,pk_core_probability);
    }
    double reset_prob=puf_ini_get(ini,"env","reset_state_prob");
    if(!isfinite(reset_prob) || reset_prob<0 || reset_prob>1) { fprintf(stderr,"Invalid reset_state_prob\n"); exit(1); }
    if(!strcmp(mode,"train") && reset_prob>0) {
        for(int b=0;b<pk_native_count;b++) if(strcmp(pk_native_banks[b].team,"None") || strcmp(pk_native_banks[b].lead,"None")) {
            fprintf(stderr,"Snapshot banks require unrestricted opponents; prescribed expert teams cannot be restored from unrelated teams\n");exit(1);
        }
        const char* path=puf_ini_get_str(ini,"env","reset_state_bank");
        if(!pk_state_load(&pk_state_bank,path)) { fprintf(stderr,"Invalid Pokemon state bank: %s\n",path); exit(1); }
        printf("PK_STATE_BANK v=2 states=%zu bytes=%zu probability=%.9g mix=0.4,0.4,0.2 memory=zero flag=476\n",
            pk_state_bank.count,pk_state_bank.count*sizeof(PKGame),reset_prob);
    }
    if (puf_ini_get(ini, "env", "abi_version") != PK_ABI_VERSION ||
            puf_ini_get(ini, "vec", "action_mask_size") != PK_ACTIONS) {
        fprintf(stderr, "Pokemon free-pick requires ABI 3 and action_mask_size=168; start a fresh policy\n");
        exit(1);
    }
    puf_ini_set(puf_ini_section(ini, "env", 0), "catalog_sha", PK_CATALOG_SHA);
    puf_ini_set(puf_ini_section(ini, "env", 0), "rules_sha", PK_RULES_SHA);
    puf_ini_put(ini,"env.policy_version","3");
    char value[64];
    snprintf(value, sizeof(value), "%.17g", puf_ini_get(ini, "train", "gamma"));
    puf_ini_put(ini, "env.reward_gamma", value);
    if ((puf_ini_get(ini, "env", "reward_hp_scale") != 0 ||
            puf_ini_get(ini, "env", "reward_ko_scale") != 0) &&
            puf_ini_get(ini, "train", "reward_clip") != 0) {
        fprintf(stderr, "Pokemon potential shaping requires train.reward_clip=0\n");
        exit(1);
    }
}
#define PUF_CONFIGURE(ini, mode) pk_configure(ini, mode)
static inline void pk_checkpoint_config(const char* checkpoint, Ini* ini) {
    if(pk_core_deck.cards) {
        char value[64];snprintf(value,sizeof(value),"%llu",(unsigned long long)pk_core_deck.assigned);
        puf_ini_set(puf_ini_section(ini,"env",0),"core_next_assigned",value);
        snprintf(value,sizeof(value),"%llu",(unsigned long long)pk_core_deck.drafted);
        puf_ini_set(puf_ini_section(ini,"env",0),"core_next_drafted",value);
    }
    char path[4096], temporary[4100];
    snprintf(path, sizeof(path), "%s", checkpoint);
    char* slash = strrchr(path, '/');
    if (!slash || (size_t)(slash - path) + 16 >= sizeof(path)) abort();
    strcpy(slash + 1, "config.ini");
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE* fp = fopen(temporary, "w");
    if (!fp) { perror("Pokemon checkpoint config"); exit(1); }
    puf_ini_write(fp, ini);
    int failed = ferror(fp);
    if (fclose(fp) != 0) failed = 1;
    if (failed || rename(temporary, path)) {
        fprintf(stderr, "Could not save Pokemon checkpoint config %s\n", path);
        exit(1);
    }
}
#define PUF_CHECKPOINT_HOOK(checkpoint, ini) pk_checkpoint_config(checkpoint, ini)
static inline void pk_validate_checkpoint(const char* checkpoint) {
    char path[4096];
    if(strlen(checkpoint)>=sizeof(path))exit(1);
    snprintf(path,sizeof(path),"%s",checkpoint);
    char* slash=strrchr(path,'/');
    if(!slash || (size_t)(slash-path)+16>=sizeof(path)) {
        fprintf(stderr,"Pokemon checkpoint requires parent config.ini\n");exit(1);
    }
    strcpy(slash+1,"config.ini");Ini saved={0};puf_ini_load_file(&saved,path);
    if(puf_ini_get(&saved,"env","abi_version")!=3 || puf_ini_get(&saved,"env","policy_version")!=3 ||
       strcmp(puf_ini_get_str(&saved,"env","rules_sha"),PK_RULES_SHA)) {
        fprintf(stderr,"Incompatible Pokemon checkpoint %s; requires semantic free-pick ABI/policy 3 and matching rules\n",checkpoint);exit(1);
    }
    puf_ini_free(&saved);
}
#define PUF_VALIDATE_CHECKPOINT(path) pk_validate_checkpoint(path)

struct Log {
    float perf, score, episode_return, episode_length;
    float slot_0_score, slot_1_score, draw_rate, timeout_rate;
    float battle_turns, invalid_actions;
    float root_games, reset_games, root_score_sum, reset_score_sum;
    float root_steps, reset_steps, opening_starts, midgame_starts, endgame_starts;
    float team_samples, team_picks[150], lead_picks[150], n;
    float core_team_samples, core_free_picks[150], core_forced_picks[150];
    float normal_team_samples, normal_team_picks[150], move_picks[166], moves_selected;
    float mix_games[4], mix_steps[4]; // normal/self, normal/expert, core/self, core/expert
};
struct Env {
    Log log;
    PKGame game;
    unsigned int rng;
    int num_agents, tag, boundary_reached;
    Agent agents[2];
    float reward_win, reward_hp_scale, reward_ko_scale, reward_gamma;
    float episode_return;
    unsigned long long episodes;
    int team_log_interval;
    long long team_log_max_bytes;
    char team_log_path[512];
    int audit;
    int behavior_enabled;
    float behavior_delta[2][PK_BEHAVIOR_DIM];
    float reset_state_prob;
    uint64_t reset_rng;
    int episode_steps, reset_bucket;
    int core_pending, core_valid[2], core_counted[2], core_just_drafted;
    int core_episode_selected; // -1 pending choice, 0 ordinary draft, 1 assigned core
    uint32_t core_card[2];
};
// Opt-in adapter-boundary diagnostics; no trainer or learning changes.
static inline void pk_audit(Env* env, int inputs) {
    if (!env->audit) return;
    for (int p=0;p<2;p++) {
        Agent* agent=&env->agents[p];
        const char* problem=NULL;
        int legal=0;
        for(int a=0;a<PK_ACTIONS;a++) {
            if(env->game.masks[p][a]>1) problem="non-binary mask";
            legal+=env->game.masks[p][a]!=0;
        }
        if(!legal) problem="empty legal mask";
        if(!agent->action_mask || memcmp(agent->action_mask,env->game.masks[p],PK_ACTIONS)) problem="published mask differs from game mask";
        if(memcmp(agent->observations,env->game.obs[p],PK_OBS)) problem="published observation differs from game observation";
        if(memcmp(env->game.obs[p]+480,env->game.masks[p],PK_ACTIONS)) problem="observation mask differs from game mask";
        if(inputs) {
            float a=agent->actions[0];
            if(!problem) {
                if(!isfinite(a) || a<0 || a>=PK_ACTIONS || floorf(a)!=a) problem="non-finite/out-of-range incoming action";
                else if(!env->game.masks[p][(int)a]) problem="masked-out incoming action";
            }
        } else {
            float r=agent->rewards[0], t=agent->terminals[0];
            if(!isfinite(r)) problem="non-finite outgoing reward";
            if(t!=0 && t!=1) problem="invalid outgoing terminal";
            if(r!=-env->agents[1-p].rewards[0]) problem="non-zero-sum outgoing rewards";
        }
        if(problem) {
            fprintf(stderr,"Pokemon audit: %s; side=%d env=%u tag=%d role=%d episode=%llu picks=%d phase=%d updates=%d battle_seed=%llu rng=%llu action=%g reward=%g terminal=%g legal=",
                problem,p,env->rng,env->tag,agent->policy,env->episodes,env->game.picks,env->game.phase,
                env->game.updates,(unsigned long long)env->game.battle_seed,(unsigned long long)env->game.rng,
                inputs?agent->actions[0]:-1,agent->rewards[0],agent->terminals[0]);
            for(int a=0;a<PK_ACTIONS;a++) if(env->game.masks[p][a]) fprintf(stderr,"%d,",a);
            fprintf(stderr," obs_match=%d mask_match=%d\n",
                !memcmp(agent->observations,env->game.obs[p],PK_OBS),
                agent->action_mask && !memcmp(agent->action_mask,env->game.masks[p],PK_ACTIONS));
            fflush(stderr);
            abort();
        }
    }
}
static inline void pk_record_teams(Env* env) {
    for (int p = 0; p < 2; p++) if (env->agents[p].policy == 0 || env->tag==0) {
        env->log.team_samples++;
        for (int i = 0; i < 6; i++) {
            const PKMon* mon=&env->game.teams[p][i];
            env->log.team_picks[mon->species]++;
            for(int m=0;m<4;m++) if(mon->moves[m]) {
                env->log.move_picks[mon->moves[m]]++;
                env->log.moves_selected++;
            }
        }
        env->log.lead_picks[env->game.teams[p][0].species]++;
        if(env->game.fixed_enabled[p]==3) {
            env->log.core_team_samples++;
            for(int i=0;i<6;i++) {
                int set=env->game.teams[p][i].species;
                if(pk_required_species(&env->game,p,set)) env->log.core_forced_picks[set]++;
                else env->log.core_free_picks[set]++;
            }
        } else if(!env->game.fixed_enabled[p] && !env->game.reset_source) {
            env->log.normal_team_samples++;
            for(int i=0;i<6;i++) env->log.normal_team_picks[env->game.teams[p][i].species]++;
        }
    }
    env->episodes++;
    if (env->team_log_interval <= 0 ||
            (env->episodes - 1) % (unsigned)env->team_log_interval) return;
    char line[4096];
    int n = snprintf(line, sizeof(line),
        "{\"abi\":3,\"rules_sha\":\"" PK_RULES_SHA "\",\"time\":%lld,\"pid\":%ld,\"env\":%u,\"episode\":%llu,\"battle_seed\":\"%llu\","
        "\"draft_mode\":\"%s\",\"opponent_kind\":\"%s\",\"env_tag\":%d,"
        "\"banks\":[%d,%d],\"result\":%d,\"leads\":[\"%s\",\"%s\"],\"teams\":[",
        (long long)time(NULL), (long)getpid(), env->rng, env->episodes, (unsigned long long)env->game.battle_seed,
        env->game.fixed_enabled[0]==3 || env->game.fixed_enabled[1]==3 ? "forced_core" : env->game.reset_source ? "snapshot" : "normal",
        pk_native_count>0 && env->tag>0 ? "expert" : env->tag>0 ? "frozen_history" : "current_selfplay",env->tag,
        env->agents[0].policy, env->agents[1].policy, env->game.result,
        pk_species_names[env->game.teams[0][0].species], pk_species_names[env->game.teams[1][0].species]);
    for (int p = 0; p < 2; p++) {
        n += snprintf(line + n, sizeof(line) - n, "%s[", p ? "," : "");
        for (int i = 0; i < 6; i++) n += snprintf(line + n, sizeof(line) - n,
            "%s\"%s\"", i ? "," : "", pk_species_names[env->game.teams[p][i].species]);
        n += snprintf(line + n, sizeof(line) - n, "]");
    }
    n += snprintf(line + n, sizeof(line) - n, "],\"moves\":[");
    for (int p = 0; p < 2; p++) {
        n += snprintf(line + n, sizeof(line) - n, "%s[", p ? "," : "");
        for (int i = 0; i < 6; i++) {
            n += snprintf(line + n, sizeof(line) - n, "%s[", i ? "," : "");
            for (int m = 0; m < 4; m++) n += snprintf(line + n, sizeof(line) - n,
                "%s\"%s\"", m ? "," : "", pk_move_name(env->game.teams[p][i].moves[m]));
            n += snprintf(line + n, sizeof(line) - n, "]");
        }
        n += snprintf(line + n, sizeof(line) - n, "]");
    }
    n += snprintf(line + n, sizeof(line) - n, "]}\n");
    assert(n > 0 && n < (int)sizeof(line));
    // One append write per sample, so CPU workers do not interleave JSON rows.
    int fd = open(env->team_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    ssize_t wrote = -1;
    if (fd >= 0) {
        struct stat st;
        // Serialize the size check and append across workers/processes.
        if (flock(fd, LOCK_EX) == 0 && fstat(fd, &st) == 0) {
            if (st.st_size + n > env->team_log_max_bytes) {
                env->team_log_interval = 0;
                close(fd);
                return;
            }
            do { wrote = write(fd, line, (size_t)n); } while (wrote < 0 && errno == EINTR);
        }
        close(fd);
    }
    if (wrote != n) {
        fprintf(stderr, "Could not append Pokemon team log %s; disabling this env's file logging\n", env->team_log_path);
        env->team_log_interval = 0;
    }
}
static inline float pk_reward_option(Dict* kwargs, const char* key, float fallback) {
    DictItem* item = dict_find(kwargs, key);
    return item ? (float)item->value : fallback;
}
static inline float pk_potential(const Env* env) {
    if (env->game.phase != PK_PHASE_BATTLE) return 0;
    float hp = 0, ko = 0;
    for (int i = 0; i < 6; i++) {
        const uint8_t* own = env->game.obs[0] + 16 + i * 32;
        const uint8_t* foe = env->game.obs[1] + 16 + i * 32;
        hp += ((float)own[1] - foe[1]) / 255.0f;
        ko += (float)foe[3] - own[3];
    }
    return (env->reward_hp_scale * hp + env->reward_ko_scale * ko) / 6.0f;
}
static inline void pk_publish(Env* env) {
    for (int p = 0; p < 2; p++) {
        memcpy(env->agents[p].observations, env->game.obs[p], PK_OBS);
        if (env->agents[p].action_mask)
            memcpy(env->agents[p].action_mask, env->game.masks[p], PK_ACTIONS);
    }
    pk_audit(env,0);
}
static inline void pk_parse_fixed_team(PKGame* game, int player, const char* text) {
    game->fixed_enabled[player] = 0;
    game->required_count[player] = 0;
    if (!text || !*text || !strcmp(text, "None")) return;
    int sets[6];
    const char* cursor = text;
    int mode = !strncmp(text,"species:",8) ? 4 : !strncmp(text,"required:",9) ? 2 : 1;
    if (mode == 2) cursor += 9;
    if (mode == 4) cursor += 8;
    int count = 0;
    for (int i = 0; i < 6; i++) {
        const char* end=strchr(cursor,',');
        if(!end) end=cursor+strlen(cursor);
        char token[64]; size_t len=(size_t)(end-cursor);
        if(!len || len>=sizeof(token)) goto invalid;
        memcpy(token,cursor,len); token[len]=0;
        char* numeric_end;
        long set = strtol(token, &numeric_end, 10);
        if(mode==4 && (numeric_end==token || *numeric_end)) set=pk_species_id(token);
        else if(numeric_end==token || *numeric_end) goto invalid;
        if(mode==4 ? (set<1 || set>149) : (set<0 || set>=PK_SETS)) goto invalid;
        sets[i] = (int)set;
        for (int j = 0; j < i; j++)
            if (mode==4 ? sets[j]==sets[i] : pk_species(sets[j]) == pk_species(sets[i])) goto invalid;
        count++;
        if (!*end) break;
        if (i == 5) goto invalid;
        cursor = end + 1;
    }
    if (mode == 1 && count != 6) goto invalid;
    memcpy(game->fixed_team[player], sets, count*sizeof(int));
    game->fixed_enabled[player] = mode;
    game->required_count[player] = count;
    return;
invalid:
    fprintf(stderr, "Invalid Pokemon team: species: followed by 1..6 distinct names/IDs, or legacy catalog controls: %s\n", text);
    exit(1);
}
static inline void pk_parse_lead(PKGame* game, int player, const char* text) {
    game->fixed_lead[player]=0;
    if(!text || !*text || !strcmp(text,"None") || !strcmp(text,"0")) return;
    char* end; long id=strtol(text,&end,10);
    if(end==text || *end) id=pk_species_id(text);
    if(id<1 || id>149 || (game->required_count[player]==6 && !pk_required_species(game,player,(int)id)) ||
       (game->fixed_enabled[player]==1 && pk_species(game->fixed_team[player][0])!=id)) {
        fprintf(stderr,"Invalid or incompatible Pokemon lead: %s\n",text); exit(1);
    }
    game->fixed_lead[player]=(int)id;
}
void puf_init(Env* env, Dict* kwargs) {
    const char* audit=getenv("PUFFER_POKEMON_AUDIT");
    env->audit=audit && !strcmp(audit,"1");
    int generation = (int)dict_get(kwargs, "generation");
    int format = (int)dict_get(kwargs, "format");
    int draft = (int)dict_get(kwargs, "team_selection");
    int max_updates = (int)dict_get(kwargs, "max_updates");
    if (generation != 1 || format != 0 || (draft != 0 && draft != 1) || max_updates < 1) {
        fprintf(stderr, "pokemon supports generation=1, format=0 (RBY OU free-pick), "
            "team_selection=0|1, max_updates>0\n");
        exit(1);
    }
    env->game.rng = (uint64_t)(uint32_t)dict_get(kwargs, "seed") ^
        (UINT64_C(0x9e3779b97f4a7c15) * ((uint64_t)env->rng + 1));
    env->game.draft = draft;
    env->game.max_updates = max_updates;
    const char* team_keys[] = {"learner_team", "opponent_team"};
    const char* lead_keys[] = {"learner_lead", "opponent_lead"};
    for (int p = 0; p < 2; p++) {
        DictItem* team = dict_find(kwargs, team_keys[p]);
        pk_parse_fixed_team(&env->game, p, team ? team->str : NULL);
        DictItem* lead = dict_find(kwargs, lead_keys[p]);
        pk_parse_lead(&env->game, p, lead ? lead->str : NULL);
    }
    env->reward_win = pk_reward_option(kwargs, "reward_win", 1);
    env->reward_hp_scale = pk_reward_option(kwargs, "reward_hp_scale", 0);
    env->reward_ko_scale = pk_reward_option(kwargs, "reward_ko_scale", 0);
    env->reward_gamma = pk_reward_option(kwargs, "reward_gamma", 0.999f);
    env->behavior_enabled = pk_reward_option(kwargs,"behavior_sleep",0)!=0 ||
                            pk_reward_option(kwargs,"behavior_paralysis",0)!=0;
    env->reset_state_prob=pk_reward_option(kwargs,"reset_state_prob",0);
    env->core_pending=env->core_just_drafted=0;
    env->core_episode_selected=-1;
    memset(env->core_valid,0,sizeof(env->core_valid));
    memset(env->core_counted,0,sizeof(env->core_counted));
    env->reset_rng=env->game.rng ^ UINT64_C(0x76cb834b803aecb3);
    if (!isfinite(env->reset_state_prob) || env->reset_state_prob<0 || env->reset_state_prob>1 ||
            (env->reset_state_prob>0 && (!pk_state_bank.states || !draft || max_updates!=512 ||
            env->game.fixed_enabled[0] || env->game.fixed_enabled[1] || env->game.fixed_lead[0] || env->game.fixed_lead[1] || env->behavior_enabled ||
            env->reward_hp_scale || env->reward_ko_scale))) {
        fprintf(stderr,"State-bank resets require a loaded bank, unrestricted draft, max_updates=512 and win-only rewards\n"); exit(1);
    }
    env->episodes = 0;
    env->team_log_interval = (int)pk_reward_option(kwargs, "team_log_interval", 0);
    env->team_log_max_bytes = (long long)pk_reward_option(kwargs, "team_log_max_bytes", 8388608);
    if (env->team_log_max_bytes < 1) { fprintf(stderr, "team_log_max_bytes must be positive\n"); exit(1); }
    DictItem* path = dict_find(kwargs, "team_log_path");
    snprintf(env->team_log_path, sizeof(env->team_log_path), "%s",
        path && path->str ? path->str : "logs/pokemon/teams.jsonl");
    if (!isfinite(env->reward_win) || env->reward_win <= 0 ||
            !isfinite(env->reward_hp_scale) || env->reward_hp_scale < 0 ||
            !isfinite(env->reward_ko_scale) || env->reward_ko_scale < 0 ||
            !isfinite(env->reward_gamma) || env->reward_gamma < 0 || env->reward_gamma > 1) {
        fprintf(stderr, "Invalid Pokemon reward settings\n"); exit(1);
    }
    env->num_agents = 2;
    env->tag = env->boundary_reached = 0;
    memset(&env->log, 0, sizeof(env->log));
    for (int p = 0; p < 2; p++) env->agents[p].policy = p;
}
static inline void pk_episode_start(Env* env) {
    env->episode_steps=0;
    env->reset_bucket=-1;
    if(pk_core_deck.cards) {
        // Parallel workers only request a reset. The serial batch hook supplies
        // cores and publishes the new observations before the next inference.
        env->core_pending=1;
        return;
    }
    // Do not consume the original game stream for choosing reset sources.
    if(env->reset_state_prob>0 && (pk_random(&env->reset_rng)>>11)*0x1.0p-53 < env->reset_state_prob) {
        unsigned draw=(unsigned)(pk_random(&env->reset_rng)%5);
        int k=draw<2?0:draw<4?1:2;
        size_t index=pk_state_bank.offsets[k]+pk_random(&env->reset_rng)%pk_state_bank.header.counts[k];
        pk_state_restore(&env->game,&pk_state_bank.states[index],pk_random(&env->reset_rng));
        env->reset_bucket=k;
    } else pk_game_reset(&env->game);
}
void puf_reset(Env* env) {
    pk_episode_start(env);
    env->episode_return = 0;
    for (int p = 0; p < 2; p++) {
        env->agents[p].rewards[0] = 0;
        env->agents[p].terminals[0] = 0;
    }
    if(!env->core_pending) pk_publish(env);
}
static inline void pk_core_flush(Env* envs, int count) {
    if(!pk_core_deck.cards) return;
    for(int i=0;i<count;i++) {
        Env* env=&envs[i];
        pk_core_deck.drafted+=(uint64_t)env->core_just_drafted;
        env->core_just_drafted=0;
        if(!env->core_pending) continue;
        if(env->core_episode_selected<0) {
            env->core_episode_selected = pk_core_probability>=1 || (pk_core_probability>0 &&
                (pk_random(&env->reset_rng)>>11)*0x1.0p-53 < pk_core_probability);
        }
        for(int p=0;p<2;p++) {
            if(env->tag>0 && env->agents[p].policy!=0) continue; // Frozen opponents keep their own draft/team.
            if(!env->core_episode_selected) {
                env->game.fixed_enabled[p]=0; env->game.required_count[p]=0;
                env->core_valid[p]=env->core_counted[p]=0;
                continue; // Ordinary drafts do not advance the exhaustive deck.
            }
            if(!env->core_valid[p]) {
                env->core_card[p]=pk_core_deal(&pk_core_deck);
                env->core_valid[p]=1; env->core_counted[p]=0;
                if(pk_core_deck.cursor==pk_core_deck.count) {
                    printf("PK_CORE_CYCLE cycle=%llu assigned=%u/%u total_assigned=%llu drafts_completed=%llu (assigned is not completed)\n",
                        (unsigned long long)pk_core_deck.cycle,pk_core_deck.cursor,pk_core_deck.count,
                        (unsigned long long)pk_core_deck.assigned,(unsigned long long)pk_core_deck.drafted);
                    fflush(stdout);
                }
            }
            env->game.fixed_enabled[p]=3; env->game.required_count[p]=3;
            for(int j=0;j<3;j++) env->game.fixed_team[p][j]=(env->core_card[p]>>(8*j))&255;
        }
        pk_game_reset(&env->game);
        env->core_pending=0;
        pk_publish(env); // Does not change rewards or terminal flags.
    }
}
#define PUF_CPU_POST_STEP(envs,count) pk_core_flush(envs,count)
static inline void pk_native_bank_loaded(Env* envs, int count, int bank, const char* path) {
    if (!pk_native_count) return;
    assert(!strcmp(path,pk_native_banks[bank].path));
    for(int i=0;i<count;i++) if(envs[i].tag==bank+1) {
        Env* env=&envs[i];
        for(int p=0;p<2;p++) {
            int learner=env->agents[p].policy==0;
            pk_parse_fixed_team(&env->game,p,learner?pk_learner_team:pk_native_banks[bank].team);
            pk_parse_lead(&env->game,p,learner?pk_learner_lead:pk_native_banks[bank].lead);
        }
        puf_reset(env);
    }
    pk_core_flush(envs,count);
}
#define PUF_SELFPLAY_BANK_LOADED(envs,count,bank,path) pk_native_bank_loaded(envs,count,bank,path)
void puf_step(Env* env) {
    pk_audit(env,1);
    float old_potential = pk_potential(env);
    float old_behavior[2][PK_BEHAVIOR_DIM]={{0}};
    if (env->behavior_enabled && env->game.phase==PK_PHASE_BATTLE)
        for (int p=0;p<2;p++) pk_behavior(&env->game.battle,p,old_behavior[p]);
    int actions[2];
    for (int p = 0; p < 2; p++) {
        float value = env->agents[p].actions[0];
        actions[p] = isfinite(value) && value >= 0 && value < PK_ACTIONS && floorf(value) == value
            ? (int)value : -1;
        env->agents[p].rewards[0] = 0;
        env->agents[p].terminals[0] = 0;
    }
    env->episode_steps++;
    int mix_group=2*(env->game.fixed_enabled[0]==3 || env->game.fixed_enabled[1]==3) +
        (pk_native_count>0 && env->tag>0);
    env->log.mix_steps[mix_group]++;
    if(env->game.reset_source) env->log.reset_steps++; else env->log.root_steps++;
    int result = pk_game_step(&env->game, actions[0], actions[1]);
    if(pk_core_deck.cards && env->game.phase==PK_PHASE_BATTLE) for(int p=0;p<2;p++) {
        if(env->core_valid[p] && !env->core_counted[p]) {
            for(int j=0;j<3;j++) {
                int found=0;
                for(int k=0;k<6;k++) found+=env->game.teams[p][k].species==env->game.fixed_team[p][j];
                assert(found==1);
            }
            env->core_counted[p]=1; env->core_just_drafted++;
        }
    }
    if (env->behavior_enabled) {
        for (int p=0;p<2;p++) {
            float after[PK_BEHAVIOR_DIM]={0};
            if (env->game.phase==PK_PHASE_BATTLE) pk_behavior(&env->game.battle,p,after);
            for (int k=0;k<PK_BEHAVIOR_DIM;k++)
                env->behavior_delta[p][k]=pk_behavior_progress(old_behavior[p][k],after[k],k);
        }
    }
    if (result == 4) {
        fprintf(stderr, "pokemon engine error at update %d\n", env->game.updates);
        abort(); // Simulator failures must not silently become training draws.
    }
    float outcome = result == 1 ? 1.0f : result == 2 ? -1.0f : 0.0f;
    float reward = env->reward_win * outcome + env->reward_gamma *
        (result ? 0.0f : pk_potential(env)) - old_potential;
    env->agents[0].rewards[0] = reward;
    env->agents[1].rewards[0] = -reward;
    env->episode_return += reward;
    if (result) {
        env->log.mix_games[mix_group]++;
        float score = (outcome + 1.0f) * 0.5f;
        env->agents[0].terminals[0] = env->agents[1].terminals[0] = 1;
        env->log.perf += score;
        env->log.score += score;
        env->log.episode_return += env->episode_return;
        env->log.episode_length += env->episode_steps;
        if(env->game.reset_source) { env->log.reset_games++; env->log.reset_score_sum+=score; }
        else { env->log.root_games++; env->log.root_score_sum+=score; }
        if(env->reset_bucket==0) env->log.opening_starts++;
        if(env->reset_bucket==1) env->log.midgame_starts++;
        if(env->reset_bucket==2) env->log.endgame_starts++;
        env->log.slot_0_score += score;
        env->log.slot_1_score += 1.0f - score;
        env->log.draw_rate += outcome == 0;
        env->log.timeout_rate += result == 5;
        env->log.battle_turns += pk_turn(&env->game.battle);
        env->log.invalid_actions += env->game.invalid_actions;
        env->log.n++;
        pk_record_teams(env);
        if (env->tag > 0) env->boundary_reached = 1;
        memset(env->core_valid,0,sizeof(env->core_valid));
        env->core_episode_selected=-1;
        pk_episode_start(env); // Preserve rewards/terminals of completed transition.
        env->episode_return = 0;
    }
    if(!env->core_pending) pk_publish(env);
}
void puf_log(Log* log, Dict* out) {
    const char* groups[4]={"normal_self","normal_expert","core_self","core_expert"};
    double games=0,steps=0;
    for(int i=0;i<4;i++) { games+=log->mix_games[i]; steps+=log->mix_steps[i]; }
    for(int i=0;i<4;i++) {
        char key[80];
        snprintf(key,sizeof(key),"%s_games",groups[i]); dict_set(out,key,log->mix_games[i]);
        snprintf(key,sizeof(key),"%s_steps",groups[i]); dict_set(out,key,log->mix_steps[i]);
        snprintf(key,sizeof(key),"%s_game_fraction",groups[i]); dict_set(out,key,games?log->mix_games[i]/games:0);
        snprintf(key,sizeof(key),"%s_games_pct",groups[i]); dict_set(out,key,games?100*log->mix_games[i]/games:0);
        snprintf(key,sizeof(key),"%s_step_fraction",groups[i]); dict_set(out,key,steps?log->mix_steps[i]/steps:0);
    }
    dict_set(out,"configured_expert_fraction",pk_native_fraction);
    if(pk_core_deck.cards) {
        dict_set(out,"core_combinations",pk_core_deck.count);
        dict_set(out,"core_cycle",(double)pk_core_deck.cycle);
        dict_set(out,"core_cycle_assigned",pk_core_deck.cursor);
        dict_set(out,"core_total_assigned",(double)pk_core_deck.assigned);
        dict_set(out,"core_drafts_completed",(double)pk_core_deck.drafted);
    }
    dict_set(out,"root_score",log->root_games?log->root_score_sum/log->root_games:0);
    dict_set(out,"reset_score",log->reset_games?log->reset_score_sum/log->reset_games:0);
    dict_set(out,"reset_episode_fraction",(log->root_games+log->reset_games)?log->reset_games/(log->root_games+log->reset_games):0);
    dict_set(out,"reset_step_fraction",(log->root_steps+log->reset_steps)?log->reset_steps/(log->root_steps+log->reset_steps):0);
    dict_set(out,"opening_fraction",log->reset_games?log->opening_starts/log->reset_games:0);
    dict_set(out,"midgame_fraction",log->reset_games?log->midgame_starts/log->reset_games:0);
    dict_set(out,"endgame_fraction",log->reset_games?log->endgame_starts/log->reset_games:0);
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "slot_0_score", log->slot_0_score);
    dict_set(out, "slot_1_score", log->slot_1_score);
    dict_set(out, "opponent_score", log->slot_1_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "timeout_rate", log->timeout_rate);
    dict_set(out, "battle_turns", log->battle_turns);
    dict_set(out, "invalid_actions", log->invalid_actions);
    for (int i = 1; i <=149; i++) {
        char slug[64], key[96];
        pk_set_slug(i, slug);
        snprintf(key, sizeof(key), "team/%s", slug);
        dict_set(out, key, log->team_samples ? log->team_picks[i] / log->team_samples : 0);
        snprintf(key, sizeof(key), "lead/%s", slug);
        dict_set(out, key, log->team_samples ? log->lead_picks[i] / log->team_samples : 0);
        if(pk_core_deck.cards) {
            snprintf(key,sizeof(key),"free_team/%s",slug);
            dict_set(out,key,log->core_team_samples?log->core_free_picks[i]/log->core_team_samples:0);
            snprintf(key,sizeof(key),"forced_team/%s",slug);
            dict_set(out,key,log->core_team_samples?log->core_forced_picks[i]/log->core_team_samples:0);
        }
        snprintf(key,sizeof(key),"normal_team/%s",slug);
        dict_set(out,key,log->normal_team_samples?log->normal_team_picks[i]/log->normal_team_samples:0);
    }
    dict_set(out,"moves_per_pokemon",log->team_samples?log->moves_selected/(6*log->team_samples):0);
    for(int m=1;m<=164;m++) {
        char slug[64],key[96]; pk_name_slug(pk_move_name(m),slug);
        snprintf(key,sizeof(key),"move/%s",slug);
        dict_set(out,key,log->team_samples?log->move_picks[m]/(6*log->team_samples):0);
    }
}
void puf_render(Env* env) {
    if (!IsWindowReady()) { InitWindow(1000, 620, "PufferLib: Gen 1 Pokemon"); SetTargetFPS(30); }
    BeginDrawing();
    ClearBackground((Color){24, 28, 36, 255});
    DrawText(env->game.phase != PK_PHASE_BATTLE ? "Private species / move selection" : "Gen 1 battle", 25, 20, 24, RAYWHITE);
    for (int p = 0; p < 2; p++) {
        int x = 25 + p * 490;
        DrawText(TextFormat("Player %d", p + 1), x, 65, 22, SKYBLUE);
        for (int i = 0; i < env->game.picks; i++) {
            int hp = env->game.phase == PK_PHASE_BATTLE ? env->game.obs[p][16 + i * 32 + 1] : 255;
            int active = env->game.phase == PK_PHASE_BATTLE && env->game.obs[p][4] == i + 1;
            DrawText(TextFormat("%s%s%s  %d%%", active ? "> " : "", pk_species_names[env->game.teams[p][i].species],
                i == 0 ? " [lead]" : "", hp * 100 / 255), x, 105 + i * 42, 17, active ? GOLD : RAYWHITE);
            DrawRectangle(x, 126 + i * 42, 400, 5, DARKGRAY);
            DrawRectangle(x, 126 + i * 42, hp * 400 / 255, 5, active ? GOLD : SKYBLUE);
        }
        if (env->game.phase == PK_PHASE_BATTLE) {
            const uint8_t* obs = env->game.obs[p];
            int active = obs[4] - 1;
            DrawText(TextFormat("Last move: %s", pk_move_name(obs[8])), x, 370, 17, SKYBLUE);
            for (int m = 0; m < 4; m++) DrawText(TextFormat("%d: %s (PP %d)", m + 1,
                pk_move_name(obs[16 + active * 32 + 8 + m]), obs[16 + active * 32 + 12 + m]),
                x, 400 + m * 24, 16, LIGHTGRAY);
        }
    }
    if (env->game.result) DrawText(env->game.result == 1 ? "PLAYER 1 WINS" :
        env->game.result == 2 ? "PLAYER 2 WINS" : "DRAW", 340, 525, 24, GOLD);
    else DrawText(TextFormat("Turn %d", env->game.phase == PK_PHASE_BATTLE ? pk_turn(&env->game.battle) : 0), 25, 525, 20, RAYWHITE);
    DrawText("Space: pause   N: step   Up/Down: speed   Enter: skip result", 25, 565, 16, GRAY);
    DrawText("Spectator shows both teams; policies receive private observations.", 25, 590, 15, GRAY);
    EndDrawing();
}
void puf_close(Env* env) { (void)env; if (IsWindowReady()) CloseWindow(); }
