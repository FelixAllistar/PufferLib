#pragma once
#include <math.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "pufferenv.h"
#include "pokemon_core.h"
#include "species_labels.h"
typedef uint8_t obs_t;
#define OBS_SIZE PK_OBS
#define NUM_ATNS 1
#define ACT_SIZES {PK_ACTIONS}
#define PUF_POKEMON_DASHBOARD 1
#define SELFPLAY_MAX_BANKS 32
typedef struct { char path[4096], team[256]; } PKNativeBank;
static PKNativeBank pk_native_banks[SELFPLAY_MAX_BANKS];
static int pk_native_count;
static inline void pk_native_configure(Ini* ini, const char* mode) {
    pk_native_count = 0;
    // Frozen league opponents are independent of learner/history selfplay.
    if (strcmp(mode,"train")) return;
    DictItem* item = dict_find(puf_ini_section(ini,"env",0),"native_league");
    if (strcmp(mode,"train") || !item || !item->str || !*item->str || !strcmp(item->str,"None")) return;
    Ini manifest = {0};
    puf_ini_load_file(&manifest,item->str);
    int count = (int)puf_ini_get(&manifest,"native","banks");
    int hidden = (int)puf_ini_get(&manifest,"native","hidden_size");
    int layers = (int)puf_ini_get(&manifest,"native","num_layers");
    if (count < 1 || count > SELFPLAY_MAX_BANKS || hidden < 1 || layers < 1 ||
            strcmp(puf_ini_get_str(&manifest,"native","catalog_sha"),PK_CATALOG_SHA)) {
        fprintf(stderr,"Invalid Pokemon native league; run ocean/pokemon/league.sh export\n"); exit(1);
    }
    for (int b=0;b<count;b++) {
        char section[32]; snprintf(section,sizeof(section),"bank.%d",b);
        const char* path=puf_ini_get_str(&manifest,section,"path");
        const char* team=puf_ini_get_str(&manifest,section,"team");
        if (strlen(path)>=sizeof(pk_native_banks[b].path) || strlen(team)>=sizeof(pk_native_banks[b].team)) exit(1);
        snprintf(pk_native_banks[b].path,sizeof(pk_native_banks[b].path),"%s",path);
        snprintf(pk_native_banks[b].team,sizeof(pk_native_banks[b].team),"%s",team);
        // Checkpoint-side team and ABI must agree with the exported binding.
        char config[4096]; snprintf(config,sizeof(config),"%s",path);
        char* slash=strrchr(config,'/'); if(!slash) exit(1);
        strcpy(slash+1,"config.ini");
        Ini saved={0}; puf_ini_load_file(&saved,config);
        if (puf_ini_get(&saved,"env","abi_version")!=PK_ABI_VERSION ||
            strcmp(puf_ini_get_str(&saved,"env","catalog_sha"),PK_CATALOG_SHA) ||
            strcmp(puf_ini_get_str(&saved,"env","learner_team"),team) ||
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
    puf_ini_put(ini,"vec.frozen_bank_pct","1"); puf_ini_put(ini,"vec.seat_balance","0");
    puf_ini_put(ini,"selfplay.opponent_pool",pk_native_banks[0].path);
    puf_ini_put(ini,"selfplay.opponent_pool_weights","1");
    puf_ini_put(ini,"selfplay.opponent_league","None");
    puf_ini_put(ini,"base.load_enemy_model_path","None");
    puf_ini_put(ini,"selfplay.opponent_pool_prob","1");
    puf_ini_put(ini,"selfplay.opp_timeout_steps","0"); // immutable banks for this run
    puf_ini_put(ini,"selfplay.eval_pool_size","0");
    puf_ini_put(ini,"env.learner_team","None"); puf_ini_put(ini,"env.opponent_team","None");
    puf_ini_put(ini,"env.team_selection","1");
    puf_ini_put(ini,"train.epoch_sampling","1"); puf_ini_put(ini,"train.prio_alpha","0"); puf_ini_put(ini,"train.prio_beta0","0");
    pk_native_count=count;
    printf("Pokemon native league: %d frozen bank slots; only unrestricted learner trains\n",count);
    puf_ini_free(&manifest);
}
static inline const char* pk_native_bank_path(int bank, const char* fallback) {
    if (!pk_native_count) return fallback;
    assert(bank>=0 && bank<pk_native_count);
    return pk_native_banks[bank].path;
}
#define PUF_SELFPLAY_BANK_PATH(bank,path) pk_native_bank_path(bank,path)
#define PUF_FROZEN_LEAGUE_ACTIVE() (pk_native_count > 0)
static inline void pk_set_slug(int set, char slug[64]) {
    const char* name = pk_set_name(set);
    int n = 0;
    for (; *name && n < 62; name++) {
        if (isalnum((unsigned char)*name)) slug[n++] = (char)tolower((unsigned char)*name);
        else if (n && slug[n-1] != '_') slug[n++] = '_';
    }
    if (n && slug[n-1] == '_') n--;
    slug[n] = 0;
}
static inline void pk_species_label(int species, char name[64]) {
    snprintf(name,64,"%s",species>=1 && species<=149 ? pk_species_labels[species] : "Unknown");
}
// Stable full metrics remain available in run logs; only the dashboard ranks
// and combines variants into species. Zero-frequency species are not displayed.
static inline int pk_top_species(Dict* log, const char* prefix, int leads,
        int ids[6], double frequencies[6]) {
    double totals[150] = {0};
    for (int set=0;set<PK_SETS;set++) {
        char slug[64], key[128];
        pk_set_slug(set,slug);
        snprintf(key,sizeof(key),"%s%s/%s",prefix,leads?"lead":"team",slug);
        DictItem* item = dict_find(log,key);
        if (item && isfinite(item->value) && item->value>0) totals[pk_species(set)] += item->value;
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
static inline void pk_configure(Ini* ini, const char* mode) {
    pk_native_configure(ini,mode);
    Dict* rules = puf_ini_section(ini, "env", 0);
    DictItem* learner = dict_find(rules, "learner_team");
    DictItem* opponent = dict_find(rules, "opponent_team");
    int fixed = (learner && learner->str && strcmp(learner->str, "None") && *learner->str) ||
        (opponent && opponent->str && strcmp(opponent->str, "None") && *opponent->str);
    if (fixed && !strcmp(mode, "train")) {
        const char* pool = puf_ini_get_str(ini, "selfplay", "opponent_pool");
        if (puf_ini_get(ini, "vec", "seat_balance") != 0 ||
                puf_ini_get(ini, "vec", "num_frozen_banks") != 1 ||
                puf_ini_get(ini, "vec", "frozen_bank_pct") != 1 ||
                puf_ini_get(ini, "selfplay", "enabled") != 1 ||
                puf_ini_get(ini, "selfplay", "opponent_pool_prob") != 1 ||
                !*pool || !strcmp(pool, "None") || strchr(pool, ',') ||
                strcmp(puf_ini_get_str(ini, "selfplay", "opponent_league"), "None") ||
                strcmp(puf_ini_get_str(ini, "base", "load_enemy_model_path"), "None") ||
                puf_ini_get(ini, "selfplay", "eval_pool_size") != 0) {
            fprintf(stderr, "Fixed-team training requires the Pokemon league's single frozen opponent layout; use ocean/pokemon/league.sh\n");
            exit(1);
        }
    }
    if (puf_ini_get(ini, "env", "abi_version") != PK_ABI_VERSION ||
            puf_ini_get(ini, "vec", "action_mask_size") != PK_ACTIONS) {
        fprintf(stderr, "Pokemon requires ABI 2 and action_mask_size=160; old catalog checkpoints are incompatible\n");
        exit(1);
    }
    puf_ini_set(puf_ini_section(ini, "env", 0), "catalog_sha", PK_CATALOG_SHA);
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

struct Log {
    float perf, score, episode_return, episode_length;
    float slot_0_score, slot_1_score, draw_rate, timeout_rate;
    float battle_turns, invalid_actions;
    float team_samples, team_picks[PK_SETS], lead_picks[PK_SETS], n;
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
            fprintf(stderr,"Pokemon audit: %s; side=%d env=%u tag=%d role=%d episode=%llu picks=%d selecting_set=%d updates=%d battle_seed=%llu rng=%llu action=%g reward=%g terminal=%g legal=",
                problem,p,env->rng,env->tag,agent->policy,env->episodes,env->game.picks,env->game.selecting_set,
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
    for (int p = 0; p < 2; p++) if (env->agents[p].policy == 0) {
        env->log.team_samples++;
        for (int i = 0; i < 6; i++) env->log.team_picks[env->game.teams[p][i]]++;
        env->log.lead_picks[env->game.teams[p][0]]++;
    }
    env->episodes++;
    if (env->team_log_interval <= 0 ||
            (env->episodes - 1) % (unsigned)env->team_log_interval) return;
    char line[4096];
    int n = snprintf(line, sizeof(line),
        "{\"abi\":2,\"catalog_sha\":\"" PK_CATALOG_SHA "\",\"time\":%lld,\"pid\":%ld,\"env\":%u,\"episode\":%llu,\"battle_seed\":\"%llu\","
        "\"banks\":[%d,%d],\"result\":%d,\"leads\":[\"%s\",\"%s\"],\"teams\":[",
        (long long)time(NULL), (long)getpid(), env->rng, env->episodes, (unsigned long long)env->game.battle_seed,
        env->agents[0].policy, env->agents[1].policy, env->game.result,
        pk_set_name(env->game.teams[0][0]), pk_set_name(env->game.teams[1][0]));
    for (int p = 0; p < 2; p++) {
        n += snprintf(line + n, sizeof(line) - n, "%s[", p ? "," : "");
        for (int i = 0; i < 6; i++) n += snprintf(line + n, sizeof(line) - n,
            "%s\"%s\"", i ? "," : "", pk_set_name(env->game.teams[p][i]));
        n += snprintf(line + n, sizeof(line) - n, "]");
    }
    n += snprintf(line + n, sizeof(line) - n, "],\"moves\":[");
    for (int p = 0; p < 2; p++) {
        n += snprintf(line + n, sizeof(line) - n, "%s[", p ? "," : "");
        for (int i = 0; i < 6; i++) {
            n += snprintf(line + n, sizeof(line) - n, "%s[", i ? "," : "");
            for (int m = 0; m < 4; m++) n += snprintf(line + n, sizeof(line) - n,
                "%s\"%s\"", m ? "," : "", pk_move_name(pk_set_move(env->game.teams[p][i], m)));
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
    if (env->game.picks < 6) return 0;
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
    int mode = !strncmp(text,"required:",9) ? 2 : 1;
    if (mode == 2) cursor += 9;
    int count = 0;
    for (int i = 0; i < 6; i++) {
        char* end;
        long set = strtol(cursor, &end, 10);
        if (end == cursor || set < 0 || set >= PK_SETS ||
                (*end != ',' && *end != 0)) goto invalid;
        sets[i] = (int)set;
        for (int j = 0; j < i; j++)
            if (pk_species(sets[j]) == pk_species(sets[i])) goto invalid;
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
    fprintf(stderr, "Invalid Pokemon team: six set IDs, or required: followed by 1..6 distinct species' set IDs: %s\n", text);
    exit(1);
}
void puf_init(Env* env, Dict* kwargs) {
    const char* audit=getenv("PUFFER_POKEMON_AUDIT");
    env->audit=audit && !strcmp(audit,"1");
    int generation = (int)dict_get(kwargs, "generation");
    int format = (int)dict_get(kwargs, "format");
    int draft = (int)dict_get(kwargs, "team_selection");
    int max_updates = (int)dict_get(kwargs, "max_updates");
    if (generation != 1 || format != 0 || (draft != 0 && draft != 1) || max_updates < 1) {
        fprintf(stderr, "pokemon supports generation=1, format=0 (RBY OU catalog), "
            "team_selection=0|1, max_updates>0\n");
        exit(1);
    }
    env->game.rng = (uint64_t)(uint32_t)dict_get(kwargs, "seed") ^
        (UINT64_C(0x9e3779b97f4a7c15) * ((uint64_t)env->rng + 1));
    env->game.draft = draft;
    env->game.max_updates = max_updates;
    const char* team_keys[] = {"learner_team", "opponent_team"};
    for (int p = 0; p < 2; p++) {
        DictItem* team = dict_find(kwargs, team_keys[p]);
        pk_parse_fixed_team(&env->game, p, team ? team->str : NULL);
    }
    env->reward_win = pk_reward_option(kwargs, "reward_win", 1);
    env->reward_hp_scale = pk_reward_option(kwargs, "reward_hp_scale", 0);
    env->reward_ko_scale = pk_reward_option(kwargs, "reward_ko_scale", 0);
    env->reward_gamma = pk_reward_option(kwargs, "reward_gamma", 0.999f);
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
void puf_reset(Env* env) {
    pk_game_reset(&env->game);
    env->episode_return = 0;
    for (int p = 0; p < 2; p++) {
        env->agents[p].rewards[0] = 0;
        env->agents[p].terminals[0] = 0;
    }
    pk_publish(env);
}
static inline void pk_native_bank_loaded(Env* envs, int count, int bank, const char* path) {
    if (!pk_native_count) return;
    assert(!strcmp(path,pk_native_banks[bank].path));
    for(int i=0;i<count;i++) if(envs[i].tag==bank+1) {
        Env* env=&envs[i];
        for(int p=0;p<2;p++)
            pk_parse_fixed_team(&env->game,p,env->agents[p].policy==0?"None":pk_native_banks[bank].team);
        puf_reset(env);
    }
}
#define PUF_SELFPLAY_BANK_LOADED(envs,count,bank,path) pk_native_bank_loaded(envs,count,bank,path)
void puf_step(Env* env) {
    pk_audit(env,1);
    float old_potential = pk_potential(env);
    int actions[2];
    for (int p = 0; p < 2; p++) {
        float value = env->agents[p].actions[0];
        actions[p] = isfinite(value) && value >= 0 && value < PK_ACTIONS && floorf(value) == value
            ? (int)value : -1;
        env->agents[p].rewards[0] = 0;
        env->agents[p].terminals[0] = 0;
    }
    int result = pk_game_step(&env->game, actions[0], actions[1]);
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
        float score = (outcome + 1.0f) * 0.5f;
        env->agents[0].terminals[0] = env->agents[1].terminals[0] = 1;
        env->log.perf += score;
        env->log.score += score;
        env->log.episode_return += env->episode_return;
        env->log.episode_length += env->game.updates + (env->game.draft ? 12 : 0);
        env->log.slot_0_score += score;
        env->log.slot_1_score += 1.0f - score;
        env->log.draw_rate += outcome == 0;
        env->log.timeout_rate += result == 5;
        env->log.battle_turns += pk_turn(&env->game.battle);
        env->log.invalid_actions += env->game.invalid_actions;
        env->log.n++;
        pk_record_teams(env);
        if (env->tag > 0) env->boundary_reached = 1;
        pk_game_reset(&env->game); // Preserve rewards/terminals of completed transition.
        env->episode_return = 0;
    }
    pk_publish(env);
}
void puf_log(Log* log, Dict* out) {
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
    for (int i = 0; i < PK_SETS; i++) {
        char slug[64], key[96];
        pk_set_slug(i, slug);
        snprintf(key, sizeof(key), "team/%s", slug);
        dict_set(out, key, log->team_samples ? log->team_picks[i] / log->team_samples : 0);
        snprintf(key, sizeof(key), "lead/%s", slug);
        dict_set(out, key, log->team_samples ? log->lead_picks[i] / log->team_samples : 0);
    }
}
void puf_render(Env* env) {
    if (!IsWindowReady()) { InitWindow(1000, 620, "PufferLib: Gen 1 Pokemon"); SetTargetFPS(30); }
    BeginDrawing();
    ClearBackground((Color){24, 28, 36, 255});
    DrawText(env->game.picks < 6 ? "Private team selection" : "Gen 1 battle", 25, 20, 24, RAYWHITE);
    for (int p = 0; p < 2; p++) {
        int x = 25 + p * 490;
        DrawText(TextFormat("Player %d", p + 1), x, 65, 22, SKYBLUE);
        for (int i = 0; i < env->game.picks; i++) {
            int hp = env->game.picks == 6 ? env->game.obs[p][16 + i * 32 + 1] : 255;
            int active = env->game.picks == 6 && env->game.obs[p][4] == i + 1;
            DrawText(TextFormat("%s%s%s  %d%%", active ? "> " : "", pk_set_name(env->game.teams[p][i]),
                i == 0 ? " [lead]" : "", hp * 100 / 255), x, 105 + i * 42, 17, active ? GOLD : RAYWHITE);
            DrawRectangle(x, 126 + i * 42, 400, 5, DARKGRAY);
            DrawRectangle(x, 126 + i * 42, hp * 400 / 255, 5, active ? GOLD : SKYBLUE);
        }
        if (env->game.picks == 6) {
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
    else DrawText(TextFormat("Turn %d", env->game.picks == 6 ? pk_turn(&env->game.battle) : 0), 25, 525, 20, RAYWHITE);
    DrawText("Space: pause   N: step   Up/Down: speed   Enter: skip result", 25, 565, 16, GRAY);
    DrawText("Spectator shows both teams; policies receive private observations.", 25, 590, 15, GRAY);
    EndDrawing();
}
void puf_close(Env* env) { (void)env; if (IsWindowReady()) CloseWindow(); }
