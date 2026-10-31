#pragma once
#include <math.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
typedef uint8_t obs_t;
#include "pufferenv.h"
#include "pokemon_core.h"
#include "state_bank.h"
#include "smogon_sets.inc"
#include "semantic_contract.h"
#include "core_deck.h"
#include "behavior.h"
#include "species_labels.h"
#define OBS_SIZE PK_OBS
#define NUM_ATNS 1
#define ACT_SIZES {PK_ACTIONS}
#define PUF_POKEMON_DASHBOARD 1
#define SELFPLAY_MAX_BANKS 32
typedef struct { char path[4096], team[256], lead[64], name[64]; } PKNativeBank;
static PKNativeBank pk_native_banks[SELFPLAY_MAX_BANKS];
// OMNI training (training only): env.learner_team_pool names a league manifest. Each learner seat plays one
// of its bank teams (species constraint and lead), chosen uniformly per game; the policy still drafts the
// moves. Learner win rate per pool team logs as team_<name>_score.
#define PK_TEAM_POOL_MAX 16384 // per-team scores are logged only for pools of up to PK_POOL_LOG_MAX
#define PK_POOL_LOG_MAX 64
typedef struct { char team[256], lead[64], name[64]; } PKTeamChoice;
static PKTeamChoice pk_team_pool[PK_TEAM_POOL_MAX];
static int pk_team_pool_count;
// env.pool_free_fraction (training with a pool only): each seat instead drafts freely (no team constraint, free-pick)
// with this probability per game, so one run trains pool piloting and free-pick drafting together.
static double pk_pool_free_fraction;
static inline void pk_parse_fixed_team(PKGame* game, int player, const char* text);
static inline void pk_parse_lead(PKGame* game, int player, const char* text);
static int pk_native_count;
static double pk_native_fraction;
static double pk_core_probability = 1;
// env.force_core_until in (0,1]: the forced-core share falls linearly from force_core_prob to 0
// at that fraction of training, so late training is all free drafts. 0 keeps it constant.
static double pk_core_until = 0;
static char pk_learner_team[256]="None", pk_learner_lead[64]="None";
static int pk_current_selfplay=1;
// Opponent-randomness curriculum (training only): in native-league games the frozen bank's
// action becomes a uniformly random legal action with probability eps, annealed linearly from
// env.expert_random_start to 0 at env.expert_random_until of train.total_timesteps.
static double pk_expert_random_start, pk_expert_random_until=0.5, pk_progress_per_env_step;
// Sweep metric (OMNI-2, 2026-10-25): every frozen-league game finished in the last 20% of training, counted across
// all envs and never cleared (twice the score, so draws stay integers). The per-interval vs_league_score of the final
// log covers only the last partial interval (~100 games, +/- 0.1), too few to rank trials.
#define PK_LATE_FRACTION 0.8
static long long pk_late_twice_score, pk_late_games;
// Move exploration (training only), the move analogue of forced cores: with probability
// env.force_move_pairs_prob a learner's game deals each drafted Pokemon 2 random legal moves for its
// first slots (pk_deal_forced_moves); it picks the other 2. Annealed linearly to 0 at
// env.force_move_pairs_until of training (0 keeps it constant). Frozen opponents draft freely.
static double pk_move_pairs_prob, pk_move_pairs_until;
// 1 when the Pokemon's four moves are exactly a Smogon Gen 1 set (any format; smogon_sets.inc).
static inline int pk_smogon_set(const PKMon* mon) {
    uint8_t m[4]; memcpy(m,mon->moves,4);
    for(int i=1;i<4;i++) for(int j=i;j>0 && m[j]<m[j-1];j--) { uint8_t t=m[j]; m[j]=m[j-1]; m[j-1]=t; }
    if(!m[0] || mon->species<1 || mon->species>149) return 0;
    for(int k=pk_smogon_set_offset[mon->species];k<pk_smogon_set_offset[mon->species+1];k++)
        if(!memcmp(pk_smogon_sets[k],m,4)) return 1;
    return 0;
}
// 1 when the team-pool manifest at path has a bank with exactly this team and lead.
static inline int pk_pool_has(const char* path, const char* team, const char* lead) {
    FILE* file=fopen(path,"r");
    if(!file) { fprintf(stderr,"Cannot open team pool %s\n",path); exit(1); }
    fclose(file);
    Ini manifest={0}; puf_ini_load_file(&manifest,path);
    int count=(int)puf_ini_get(&manifest,"native","banks"),found=0;
    for(int b=0;b<count && !found;b++) {
        char section[32]; snprintf(section,sizeof(section),"bank.%d",b);
        found=!strcmp(puf_ini_get_str(&manifest,section,"team"),team) && !strcmp(puf_ini_get_str(&manifest,section,"lead"),lead);
    }
    puf_ini_free(&manifest);
    return found;
}
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
    // Frozen banks share the embed/mid widths, and every exported bank is 32/64.
    // An attention learner is fine: PUF_POLICY_ARCH gives the banks the MLP fusion.
    if(fraction>0 && (pk_embed!=32 || pk_mid!=64)) {
        printf("Pokemon native league: encoder %d/%d differs from the 32/64 banks; expert_fraction=0\n",pk_embed,pk_mid);
        fraction=0;
    }
    // The non-expert share plays the current policy, not frozen history.
    puf_ini_put(ini,"selfplay.enabled","0");
    if(fraction==0) {
        puf_ini_put(ini,"vec.num_policies","1");
        puf_ini_put(ini,"vec.hist_policy_percent","0");
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
        // Dashboard label for per-opponent win rates; manifests without names use the slot.
        DictItem* name=dict_find(puf_ini_section(&manifest,section,0),"name");
        if(name && name->str && *name->str) snprintf(pk_native_banks[b].name,sizeof(pk_native_banks[b].name),"%s",name->str);
        else snprintf(pk_native_banks[b].name,sizeof(pk_native_banks[b].name),"bank%d",b);
        // Checkpoint-side team and ABI must agree with the exported binding.
        char config[4096]; snprintf(config,sizeof(config),"%s",path);
        char* slash=strrchr(config,'/'); if(!slash) exit(1);
        strcpy(slash+1,"config.ini");
        Ini saved={0}; puf_ini_load_file(&saved,config);
        // An OMNI checkpoint (trained with env.learner_team_pool) may pilot any team of its own pool,
        // so one checkpoint can fill several banks (LeagueV2).
        DictItem* saved_pool=dict_find(puf_ini_section(&saved,"env",0),"learner_team_pool");
        int pooled=saved_pool && saved_pool->str && *saved_pool->str && strcmp(saved_pool->str,"None");
        if (pooled && !pk_pool_has(saved_pool->str,team,lead)) {
            fprintf(stderr,"Pokemon native league bank %d: team %s lead %s is not in its checkpoint's team pool %s\n",
                b,team,lead,saved_pool->str); exit(1);
        }
        if (puf_ini_get(&saved,"env","abi_version")!=PK_ABI_VERSION ||
            puf_ini_get(&saved,"env","policy_version")!=3 ||
            strcmp(puf_ini_get_str(&saved,"env","rules_sha"),PK_RULES_SHA) ||
            (!pooled && strcmp(puf_ini_get_str(&saved,"env","learner_team"),team)) ||
            (!pooled && strcmp(puf_ini_get_str(&saved,"env","learner_lead"),lead)) ||
            puf_ini_get(&saved,"policy","hidden_size")!=hidden ||
            puf_ini_get(&saved,"policy","num_layers")!=layers) {
            fprintf(stderr,"Pokemon native league checkpoint/team mismatch: %s\n",path); exit(1);
        }
        puf_ini_free(&saved);
    }
    const char* opponents = puf_ini_get_str(&manifest, "native", "opponents");
    FILE* list = fopen(opponents, "r");
    assert(list && "native.opponents must name a checkpoint list in bank order");
    for (int b = 0; b < count; b++) {
        char path[4096];
        assert(fgets(path, sizeof(path), list));
        path[strcspn(path, "\r\n")] = 0;
        assert(!strcmp(path, pk_native_banks[b].path) && "opponent list/bank mismatch");
    }
    assert(fgetc(list) == EOF);
    fclose(list);
    puf_ini_put(ini, "selfplay.initial_opponents", opponents);
    puf_ini_put(ini, "selfplay.enabled", "1");
    char value[32];
    snprintf(value,sizeof(value),"%d",count + 1); puf_ini_put(ini,"vec.num_policies",value);
    snprintf(value,sizeof(value),"%d",hidden); puf_ini_put(ini,"vec.hist_policy_hidden_size",value);
    snprintf(value,sizeof(value),"%d",layers); puf_ini_put(ini,"vec.hist_policy_num_layers",value);
    puf_ini_put(ini,"base.async","0");
    snprintf(value,sizeof(value),"%.17g",fraction); puf_ini_put(ini,"vec.hist_policy_percent",value);
    puf_ini_put(ini,"base.load_enemy_model_path","None");
    puf_ini_put(ini,"selfplay.opp_timeout_steps","0"); // immutable banks for this run
    puf_ini_put(ini,"selfplay.eval_pool_size","0");
    puf_ini_put(ini,"selfplay.eval_games","0");
    puf_ini_put(ini,"env.team_selection","1");
    // Exported banks are MinGRU policies, whatever memory network the learner uses.
    puf_ini_put(ini,"vec.hist_policy_network","mingru");
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
// The learner's encoder settings; pk_attn is switched per policy while the trainer builds them.
static PKAttn pk_attn_learner = {0, 128, 4};
// Frozen native-league banks are exported MLP-fusion checkpoints; frozen self-play
// history snapshots share the learner's encoder.
static inline void pk_policy_arch(int frozen) {
    pk_attn = frozen && pk_native_count ? pk_attn_off : pk_attn_learner;
}
#define PUF_POLICY_ARCH(frozen) pk_policy_arch(frozen)
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
    DictItem* embed=dict_find(puf_ini_section(ini,"policy",0),"pokemon_embed");
    DictItem* mid=dict_find(puf_ini_section(ini,"policy",0),"pokemon_mid");
    pk_embed=embed?(int)embed->value:32; pk_mid=mid?(int)mid->value:64;
    if(pk_embed<8 || pk_embed%8 || pk_mid<8 || pk_mid%8) {
        fprintf(stderr,"policy.pokemon_embed/pokemon_mid must be positive multiples of 8\n");exit(1);
    }
    pk_attn=pk_attn_off;
    DictItem* attn_layers=dict_find(puf_ini_section(ini,"policy",0),"pokemon_attention_layers");
    DictItem* attn_width=dict_find(puf_ini_section(ini,"policy",0),"pokemon_attention_width");
    DictItem* attn_heads=dict_find(puf_ini_section(ini,"policy",0),"pokemon_attention_heads");
    if(attn_layers)pk_attn.layers=(int)attn_layers->value;
    if(attn_width)pk_attn.width=(int)attn_width->value;
    if(attn_heads)pk_attn.heads=(int)attn_heads->value;
    if(!pk_attn_valid(pk_attn)) {
        fprintf(stderr,"policy.pokemon_attention_layers 0-8, width a multiple of 8 up to 512, heads dividing width\n");exit(1);
    }
    pk_attn_learner=pk_attn;
    pk_native_configure(ini,mode);
    pk_expert_random_start=0; pk_expert_random_until=0.5; pk_progress_per_env_step=0;
    pk_late_twice_score=0; pk_late_games=0;
    if(!strcmp(mode,"train")) {
        DictItem* start=dict_find(puf_ini_section(ini,"env",0),"expert_random_start");
        DictItem* until=dict_find(puf_ini_section(ini,"env",0),"expert_random_until");
        if(start) pk_expert_random_start=start->value;
        if(until) pk_expert_random_until=until->value;
        if(!(pk_expert_random_start>=0 && pk_expert_random_start<=1) || !(pk_expert_random_until>0 && pk_expert_random_until<=1)) {
            fprintf(stderr,"env.expert_random_start must be in [0,1] and env.expert_random_until in (0,1]\n");exit(1);
        }
        double total=puf_ini_get(ini,"train","total_timesteps"), agents=puf_ini_get(ini,"vec","total_agents");
        // Synchronous envs: every env step advances the global agent-step count by total_agents.
        pk_progress_per_env_step=total>0?agents/total:0;
        if(pk_expert_random_start>0)
            printf("Pokemon expert randomness: eps %.3g annealed to 0 at %.3g of training (native-league games only)\n",
                pk_expert_random_start,pk_expert_random_until);
    }
    pk_move_pairs_prob=pk_move_pairs_until=0;
    if(!strcmp(mode,"train")) {
        DictItem* prob=dict_find(puf_ini_section(ini,"env",0),"force_move_pairs_prob");
        DictItem* until=dict_find(puf_ini_section(ini,"env",0),"force_move_pairs_until");
        if(prob) pk_move_pairs_prob=prob->value;
        if(until) pk_move_pairs_until=until->value;
        if(!(pk_move_pairs_prob>=0 && pk_move_pairs_prob<=1) || !(pk_move_pairs_until>=0 && pk_move_pairs_until<=1)) {
            fprintf(stderr,"env.force_move_pairs_prob and env.force_move_pairs_until must be in [0,1]\n");exit(1);
        }
        if(pk_move_pairs_prob>0)
            printf("Pokemon forced move pairs: share %.3g of learner games%s%.3g of training\n",pk_move_pairs_prob,
                pk_move_pairs_until>0?", annealed to 0 at ":", constant; until=",pk_move_pairs_until);
    }
    pk_team_pool_count=0;
    DictItem* pool=dict_find(puf_ini_section(ini,"env",0),"learner_team_pool");
    // Cross-play (./puffer match, model vs model): env.match_team_pool gives both seats a pool team per game, so
    // two policies meet across every team pairing. In eval/match modes only; training uses learner_team_pool.
    if(strcmp(mode,"train")) pool=dict_find(puf_ini_section(ini,"env",0),"match_team_pool");
    if(pool && pool->str && *pool->str && strcmp(pool->str,"None")) {
        Ini manifest={0};
        puf_ini_load_file(&manifest,pool->str);
        int count=(int)puf_ini_get(&manifest,"native","banks");
        if(count<1 || count>PK_TEAM_POOL_MAX) { fprintf(stderr,"learner_team_pool %s: need 1..%d banks\n",pool->str,PK_TEAM_POOL_MAX); exit(1); }
        if(puf_ini_get(ini,"env","force_core_combos")!=0 || puf_ini_get(ini,"env","reset_state_prob")!=0) {
            fprintf(stderr,"learner_team_pool is incompatible with forced cores and snapshot resets\n"); exit(1);
        }
        for(int b=0;b<count;b++) {
            char section[32]; snprintf(section,sizeof(section),"bank.%d",b);
            const char* team=puf_ini_get_str(&manifest,section,"team");
            const char* lead=puf_ini_get_str(&manifest,section,"lead");
            DictItem* name=dict_find(puf_ini_section(&manifest,section,0),"name");
            PKTeamChoice* c=&pk_team_pool[b];
            if(strlen(team)>=sizeof(c->team) || strlen(lead)>=sizeof(c->lead)) exit(1);
            snprintf(c->team,sizeof(c->team),"%s",team);
            snprintf(c->lead,sizeof(c->lead),"%s",lead);
            if(name && name->str && *name->str) snprintf(c->name,sizeof(c->name),"%s",name->str);
            else snprintf(c->name,sizeof(c->name),"pool%d",b);
            PKGame check={0}; pk_parse_fixed_team(&check,0,c->team); pk_parse_lead(&check,0,c->lead); // exits if invalid
        }
        puf_ini_free(&manifest);
        pk_team_pool_count=count;
        DictItem* free_fraction=dict_find(puf_ini_section(ini,"env",0),"pool_free_fraction");
        pk_pool_free_fraction=free_fraction?free_fraction->value:0;
        if(pk_pool_free_fraction<0 || pk_pool_free_fraction>1) { fprintf(stderr,"pool_free_fraction must be in [0,1]\n"); exit(1); }
        printf("Pokemon learner team pool: %d teams from %s, one chosen per learner game\n",count,pool->str);
    }
    pk_current_selfplay=pk_native_count>0 || puf_ini_get(ini,"selfplay","enabled")==0;
    // ./puffer match / eval: an explicit env.opponent_team is kept, so a team can be played against a different team
    // (team-variant tests). Training and match without one still mirror the learner's team as before.
    int explicit_opponent=strcmp(mode,"train") && strcmp(puf_ini_get_str(ini,"env","opponent_team"),"None");
    if(pk_current_selfplay && !explicit_opponent) {
        puf_ini_put(ini,"env.opponent_team",pk_learner_team);
        puf_ini_put(ini,"env.opponent_lead",pk_learner_lead);
    }
    pk_core_free(&pk_core_deck);
    pk_core_probability=1; pk_core_until=0;
    DictItem* core_option=dict_find(puf_ini_section(ini,"env",0),"force_core_combos");
    double core=core_option?core_option->value:0;
    if(core!=0 && core!=1) { fprintf(stderr,"force_core_combos must be 0 or 1\n"); exit(1); }
    if(strcmp(mode,"train")) {
        core=0;
        puf_ini_put(ini,"env.force_core_combos","0");
        puf_ini_put(ini,"env.reset_state_prob","0");
    }
    if(core) {
        DictItem* probability=dict_find(puf_ini_section(ini,"env",0),"force_core_prob");
        pk_core_probability=probability?probability->value:1;
        if(!isfinite(pk_core_probability) || pk_core_probability<0 || pk_core_probability>1) {
            fprintf(stderr,"force_core_prob must be in [0,1]\n"); exit(1);
        }
        DictItem* until=dict_find(puf_ini_section(ini,"env",0),"force_core_until");
        pk_core_until=until?until->value:0;
        if(!(pk_core_until>=0 && pk_core_until<=1)) { fprintf(stderr,"force_core_until must be in [0,1]\n"); exit(1); }
        if(pk_core_until>0)
            printf("Pokemon forced cores: share %.3g annealed to 0 at %.3g of training\n",pk_core_probability,pk_core_until);
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
    // Keep each snapshot's cursor; parent config remains the viewer's architecture metadata.
    char paths[2][4096];
    assert(snprintf(paths[0], sizeof(paths[0]), "%s.ini", checkpoint) < sizeof(paths[0]));
    snprintf(paths[1], sizeof(paths[1]), "%s", checkpoint);
    char* slash = strrchr(paths[1], '/');
    assert(slash && (size_t)(slash - paths[1]) + 16 < sizeof(paths[1]));
    strcpy(slash + 1, "config.ini");
    for (int i = 0; i < 2; i++) {
        char temporary[4100];
        snprintf(temporary, sizeof(temporary), "%.4095s.tmp", paths[i]);
        FILE* fp = fopen(temporary, "w");
        assert(fp);
        puf_ini_write(fp, ini);
        assert(!ferror(fp));
        assert(fclose(fp) == 0);
        assert(rename(temporary, paths[i]) == 0);
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
#define PUF_LOAD_HOOK(checkpoint, ini) pk_validate_checkpoint(checkpoint)

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
    // Learner (seat 0) results by opponent: [0] current self-play, [b] native-league bank b-1.
    float vs_games[SELFPLAY_MAX_BANKS+1], vs_score[SELFPLAY_MAX_BANKS+1];
    float policy0_games, policy0_score;  // ./puffer match: games and score of the seat(s) run by policy 0
    float expert_random_sum, expert_random_steps, expert_random_actions;
    // Learner teams by move drafting: forced pairs vs free; mons whose set is exactly a Smogon set.
    float move_pair_teams, free_move_teams, smogon_sets_forced, smogon_sets_free;
    float pool_games[PK_POOL_LOG_MAX], pool_score[PK_POOL_LOG_MAX]; // seat 0 by pool team
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
    uint64_t expert_rng;
    unsigned long long env_steps;
    int behavior_enabled;
    float behavior_delta[2][PK_BEHAVIOR_DIM];
    float reset_state_prob;
    uint64_t reset_rng;
    int episode_steps, reset_bucket;
    int core_pending, core_valid[2], core_counted[2], core_just_drafted;
    int core_episode_selected; // -1 pending choice, 0 ordinary draft, 1 assigned core
    int pool_team[2]; // learner_team_pool index this game, -1 when none
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
        if(!env->game.reset_source && env->game.fixed_enabled[p]!=1 && env->game.fixed_enabled[p]!=2) {
            int sets=0;
            for(int i=0;i<6;i++) sets+=pk_smogon_set(&env->game.teams[p][i]);
            if(env->game.force_moves[p]) { env->log.move_pair_teams++; env->log.smogon_sets_forced+=sets; }
            else { env->log.free_move_teams++; env->log.smogon_sets_free+=sets; }
        }
    }
    env->episodes++;
    if (env->team_log_interval <= 0 ||
            (env->episodes - 1) % (unsigned)env->team_log_interval) return;
    char line[4096];
    int n = snprintf(line, sizeof(line),
        "{\"abi\":3,\"rules_sha\":\"" PK_RULES_SHA "\",\"time\":%lld,\"pid\":%ld,\"env\":%u,\"episode\":%llu,\"battle_seed\":\"%llu\","
        "\"draft_mode\":\"%s\",\"move_pairs\":[%d,%d],\"opponent_kind\":\"%s\",\"env_tag\":%d,"
        "\"banks\":[%d,%d],\"result\":%d,\"leads\":[\"%s\",\"%s\"],\"teams\":[",
        (long long)time(NULL), (long)getpid(), env->rng, env->episodes, (unsigned long long)env->game.battle_seed,
        env->game.fixed_enabled[0]==3 || env->game.fixed_enabled[1]==3 ? "forced_core" : env->game.reset_source ? "snapshot" : "normal",
        env->game.force_moves[0], env->game.force_moves[1],
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
// "sets:" teams (see pk_parse_fixed_team). Returns 0 on a malformed team.
static inline int pk_parse_exact_sets(PKGame* game, int player, const char* cursor) {
    for (int i = 0; i < 6; i++) {
        int v[5], used = 0;
        if (sscanf(cursor, "%d,%d,%d,%d,%d%n", &v[0], &v[1], &v[2], &v[3], &v[4], &used) != 5) return 0;
        if (v[0] < 1 || v[0] > 149) return 0;
        for (int k = 1; k < 5; k++) if (v[k] < 0 || v[k] > 164) return 0;
        for (int j = 0; j < i; j++) if (game->set_moves[player][j][0] == v[0]) return 0;
        for (int k = 0; k < 5; k++) game->set_moves[player][i][k] = (uint8_t)v[k];
        game->fixed_team[player][i] = v[0];
        cursor += used;
        if (i < 5 && *cursor++ != '/') return 0;
    }
    if (*cursor) return 0;
    game->fixed_enabled[player] = 4;
    game->required_count[player] = 6;
    return 1;
}
static inline void pk_parse_fixed_team(PKGame* game, int player, const char* text) {
    game->fixed_enabled[player] = 0;
    game->required_count[player] = 0;
    memset(game->set_moves[player], 0, sizeof(game->set_moves[player]));
    if (!text || !*text || !strcmp(text, "None")) return;
    // Exact sets: "sets:" then six "species,move,move,move,move" groups separated by '/' (catalog IDs; ';' starts an INI comment). The six
    // species are required (the policy drafts the order unless a lead is given) and each gets exactly its moves.
    if (!strncmp(text, "sets:", 5)) {
        if (pk_parse_exact_sets(game, player, text + 5)) return;
        fprintf(stderr, "Invalid Pokemon team: sets: needs six 'species,move,move,move,move' groups joined by '/': %s\n", text);
        exit(1);
    }
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
    env->game.move_rng=env->game.rng ^ UINT64_C(0x2545f4914f6cdd1d);
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
    // In-game trainer battles: env.trainer_file (ocean/pokemon/cartridge/trainers.tsv) and env.trainer_ratings (one
    // rating per trainer line, from the cartridge-AI round robin). Needs the cartridge-mode engine build.
    DictItem* trainer_file = dict_find(kwargs, "trainer_file");
    if (trainer_file && trainer_file->str && *trainer_file->str && strcmp(trainer_file->str, "None")) {
        if (!pk_trainers.count) {
            FILE* f = fopen(trainer_file->str, "r");
            if (!f) { fprintf(stderr, "Cannot open trainer_file %s\n", trainer_file->str); exit(1); }
            int cls, n;
            while (pk_trainers.count < PK_MAX_TRAINERS && fscanf(f, "%d %d", &cls, &n) == 2) {
                assert(n >= 1 && n <= 6);
                pk_trainers.sizes[pk_trainers.count] = (uint8_t)n;
                pk_trainers.cls[pk_trainers.count] = (uint8_t)cls;
                for (int k = 0; k < n; k++) {
                    int d, l, m[4];
                    if (fscanf(f, "%d %d %d %d %d %d", &d, &l, &m[0], &m[1], &m[2], &m[3]) != 6) exit(1);
                    pk_trainers.teams[pk_trainers.count][k] =
                        (PKTrainerMon){(uint8_t)d, (uint8_t)l, {(uint8_t)m[0], (uint8_t)m[1], (uint8_t)m[2], (uint8_t)m[3]}};
                }
                pk_trainers.count++;
            }
            fclose(f);
            DictItem* ratings = dict_find(kwargs, "trainer_ratings");
            FILE* r = ratings && ratings->str ? fopen(ratings->str, "r") : NULL;
            if (!r) { fprintf(stderr, "env.trainer_ratings is required with trainer_file\n"); exit(1); }
            for (int i = 0; i < pk_trainers.count; i++) if (fscanf(r, "%f", &pk_trainers.elo[i]) != 1) exit(1);
            fclose(r);
            printf("Pokemon trainers: %d parties from %s\n", pk_trainers.count, trainer_file->str);
        }
        env->game.trainer_mode = 1;
        env->game.trainer_mirror = pk_reward_option(kwargs, "trainer_mirror", 0.5f);
        env->game.trainer_window = pk_reward_option(kwargs, "trainer_window", 300);
        env->game.trainer_perfect = pk_reward_option(kwargs, "trainer_perfect", 0) != 0;
        env->game.trainer_pick = pk_reward_option(kwargs, "trainer_pick", 0) != 0;
        // AI challenge: env.trainer_ai_player (one party line, trainers.tsv format), env.trainer_ai_opponents
        // (comma-separated trainer indices), env.trainer_ai_badges (bit per badge), env.trainer_ai_perfect. Training
        // draws a random opponent each game (side 2 must be a frozen bank: vec.num_policies 2, hist_policy_percent 1);
        // a tournament plays each opponent trainer_tournament_games times.
        DictItem* player = dict_find(kwargs, "trainer_ai_player");
        if (player && player->str && *player->str && strcmp(player->str, "None") && !pk_ai_on) {
            FILE* pf = fopen(player->str, "r");
            int pcls, pn;
            if (!pf || fscanf(pf, "%d %d", &pcls, &pn) != 2 || pn < 1 || pn > 6) { fprintf(stderr, "Bad trainer_ai_player\n"); exit(1); }
            for (int k = 0; k < pn; k++) {
                int d, l, m[4];
                if (fscanf(pf, "%d %d %d %d %d %d", &d, &l, &m[0], &m[1], &m[2], &m[3]) != 6) exit(1);
                pk_ai_party[k] = (PKTrainerMon){(uint8_t)d, (uint8_t)l, {(uint8_t)m[0], (uint8_t)m[1], (uint8_t)m[2], (uint8_t)m[3]}};
            }
            fclose(pf);
            pk_ai_size = pn;
            DictItem* opp = dict_find(kwargs, "trainer_ai_opponents");
            for (char* tok = opp && opp->str ? strtok(strdup(opp->str), ",") : NULL; tok && pk_ai_count < 64; tok = strtok(NULL, ","))
                pk_ai_opponents[pk_ai_count++] = atoi(tok);
            if (!pk_ai_count) { fprintf(stderr, "trainer_ai_opponents is required\n"); exit(1); }
            pk_ai_badges = (int)pk_reward_option(kwargs, "trainer_ai_badges", 255);
            pk_ai_perfect = pk_reward_option(kwargs, "trainer_ai_perfect", 1) != 0;
            pk_ai_on = 1;
            printf("Pokemon AI challenge: %d-Pokemon party vs %d trainers\n", pn, pk_ai_count);
        }
        DictItem* tournament = dict_find(kwargs, "trainer_tournament");
        if (tournament && tournament->str && *tournament->str && strcmp(tournament->str, "None") && !pk_tournament_out) {
            pk_tournament_out = fopen(tournament->str, "w");
            if (!pk_tournament_out) { fprintf(stderr, "Cannot write %s\n", tournament->str); exit(1); }
            setvbuf(pk_tournament_out, NULL, _IOLBF, 1 << 16);
            pk_tournament_games = (long)pk_reward_option(kwargs, "trainer_tournament_games", 20);
            pk_tournament_tasks = (long)pk_trainers.count * (pk_trainers.count - 1) / 2 * pk_tournament_games;
            pk_tournament_sets = pk_reward_option(kwargs, "trainer_tournament_sets", 0) != 0;
            if (pk_tournament_sets) pk_build_sets();
            if (pk_ai_on) pk_tournament_tasks = (long)pk_ai_count * pk_tournament_games;
            printf("Pokemon trainer tournament: %ld games -> %s\n", pk_tournament_tasks, tournament->str);
        }
    }
    env->num_agents = 2;
    env->tag = env->boundary_reached = 0;
    memset(&env->log, 0, sizeof(env->log));
    for (int p = 0; p < 2; p++) env->agents[p].policy = p;
}
#define MY_VEC_INIT
Env* my_vec_init(int* size, int* starts, int* counts, Dict* vk, Dict* ek) {
    int total = dict_get(vk, "total_agents");
    int buffers = dict_get(vk, "num_buffers");
    int policies = dict_get(vk, "num_policies");
    assert(total > 0 && buffers > 0 && total % (2 * buffers) == 0);
    int games = total / 2;
    int per_buffer = games / buffers;
    int frozen_start = per_buffer - (int)(per_buffer * dict_get(vk, "hist_policy_percent"));
    Env* envs = (Env*)calloc(games, sizeof(Env));
    for (int buf = 0; buf < buffers; buf++) {
        starts[buf] = buf * per_buffer;
        counts[buf] = per_buffer;
        for (int e = 0; e < per_buffer; e++) {
            int index = starts[buf] + e;
            Env* env = envs + index;
            env->rng = index;
            env->game.next_task = index;
            env->game.task_stride = games;
            env->expert_rng = UINT64_C(0x5851f42d4c957f2d) ^ (UINT64_C(0x9e3779b97f4a7c15) * ((uint64_t)index + 1));
            puf_init(env, ek);
            int bank = policies > 1 && e >= frozen_start
                ? 1 + (e - frozen_start) % (policies - 1) : 0;
            env->tag = bank;
            env->agents[0].policy = 0;
            env->agents[1].policy = bank;
            if (pk_native_count && bank) {
                pk_parse_fixed_team(&env->game, 1, pk_native_banks[bank - 1].team);
                pk_parse_lead(&env->game, 1, pk_native_banks[bank - 1].lead);
            }
        }
    }
    *size = games;
    return envs;
}

static inline void pk_choose_move_pairs(Env* env) {
    double probability=pk_move_pairs_prob;
    if(pk_move_pairs_until>0) probability*=fmax(0.0,1.0-env->env_steps*pk_progress_per_env_step/pk_move_pairs_until);
    for(int p=0;p<2;p++) {
        int learner=env->agents[p].policy==0 || env->tag==0;
        env->game.force_moves[p]=learner && probability>0 &&
            (pk_random(&env->game.move_rng)>>11)*0x1.0p-53 < probability;
    }
}
static inline void pk_choose_pool_teams(Env* env) {
    for(int p=0;p<2;p++) {
        env->pool_team[p]=-1;
        if(!pk_team_pool_count || !(env->agents[p].policy==0 || env->tag==0)) continue;
        int i=(int)(pk_random(&env->game.move_rng)%(unsigned)pk_team_pool_count);
        if(pk_pool_free_fraction>0 && (pk_random(&env->game.move_rng)>>11)*0x1.0p-53 < pk_pool_free_fraction) {
            pk_parse_fixed_team(&env->game,p,NULL); pk_parse_lead(&env->game,p,NULL);
            continue;
        }
        pk_parse_fixed_team(&env->game,p,pk_team_pool[i].team);
        pk_parse_lead(&env->game,p,pk_team_pool[i].lead);
        env->pool_team[p]=i;
    }
}
static inline void pk_episode_start(Env* env) {
    env->episode_steps=0;
    env->reset_bucket=-1;
    pk_choose_move_pairs(env); // Before any draft reset, including the core path's deferred one.
    pk_choose_pool_teams(env);
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
            double probability=pk_core_probability;
            if(pk_core_until>0) probability*=fmax(0.0,1.0-env->env_steps*pk_progress_per_env_step/pk_core_until);
            env->core_episode_selected = probability>=1 || (probability>0 &&
                (pk_random(&env->reset_rng)>>11)*0x1.0p-53 < probability);
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
    env->env_steps++;
    if(pk_expert_random_start>0 && pk_native_count && env->tag>0) {
        double progress=env->env_steps*pk_progress_per_env_step;
        double eps=pk_expert_random_start*fmax(0.0,1.0-progress/pk_expert_random_until);
        env->log.expert_random_sum+=eps; env->log.expert_random_steps++;
        if(eps>0 && (pk_random(&env->expert_rng)>>11)*0x1.0p-53<eps) {
            actions[1]=pk_random_action(env->game.masks[1],&env->expert_rng);
            env->log.expert_random_actions++;
        }
    }
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
        // Every-set tournament: per finished cell, trainer i's score in half points over its games.
        // Tournament: task, P1 trainer, P2 trainer, result (1 P1 won, 2 P2 won, 3 tie, 5 time limit, 6 desync), then
        // per side its switches and each picked Pokemon (in pick order, the first led) as party index:uses of moves 1-4
        if (pk_tournament_out && pk_ai_on && env->game.task < pk_tournament_tasks) {
            fprintf(pk_tournament_out, "%ld %d %d\n", env->game.task, env->game.trainer_ids[1], result);
        } else if (pk_tournament_out && pk_tournament_sets && env->game.task < pk_tournament_tasks) {
            long cell = env->game.task / pk_tournament_games;
            int first_side = (env->game.task % pk_tournament_games) & 1;  // side of trainer i
            uint32_t score2 = result == 1 + first_side ? 2 : result == 2 - first_side ? 0 : 1;
            __atomic_fetch_add(&pk_cell_score2[cell], score2, __ATOMIC_RELAXED);
            if (__atomic_add_fetch(&pk_cell_done[cell], 1, __ATOMIC_ACQ_REL) == (uint32_t)pk_tournament_games)
                fprintf(pk_tournament_out, "%ld %u\n", cell, __atomic_load_n(&pk_cell_score2[cell], __ATOMIC_ACQUIRE));
        } else if (pk_tournament_out && env->game.task < pk_tournament_tasks) {
            PKGame* g = &env->game;
            char line[512];
            int n = snprintf(line, sizeof(line), "%ld %d %d %d", g->task, g->trainer_ids[0], g->trainer_ids[1], result);
            for (int p = 0; p < 2; p++) {
                n += snprintf(line + n, sizeof(line) - n, " %d", g->switches[p]);
                for (int k = 0; k < g->pick_count[p]; k++) {
                    uint16_t* u = g->move_uses[p][k];
                    n += snprintf(line + n, sizeof(line) - n, "%s%d:%d/%d/%d/%d", k ? "," : " ", g->picked[p][k],
                        u[0], u[1], u[2], u[3]);
                }
            }
            fprintf(pk_tournament_out, "%s\n", line);
        }
        env->log.mix_games[mix_group]++;
        float score = (outcome + 1.0f) * 0.5f;
        if(env->tag>=0 && env->tag<=SELFPLAY_MAX_BANKS) {env->log.vs_games[env->tag]++; env->log.vs_score[env->tag]+=score;}
        for(int p=0;p<2;p++) if(env->agents[p].policy==0 && env->agents[1-p].policy!=0) {
            env->log.policy0_games++; env->log.policy0_score+=p?1.0f-score:score;
        }
        if(env->tag>=1 && env->tag<=pk_native_count && env->env_steps*pk_progress_per_env_step>=PK_LATE_FRACTION) {
            __atomic_fetch_add(&pk_late_twice_score,(long long)(2*score+0.5f),__ATOMIC_RELAXED);
            __atomic_fetch_add(&pk_late_games,1,__ATOMIC_RELAXED);
        }
        if(env->pool_team[0]>=0 && env->pool_team[0]<PK_POOL_LOG_MAX) {env->log.pool_games[env->pool_team[0]]++; env->log.pool_score[env->pool_team[0]]+=score;}
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
    if(pk_move_pairs_prob>0) {
        double teams=log->move_pair_teams+log->free_move_teams;
        dict_set(out,"move_pair_team_fraction",teams?log->move_pair_teams/teams:0);
    }
    for(int i=0;i<pk_team_pool_count && pk_team_pool_count<=PK_POOL_LOG_MAX;i++) {
        char key[96]; snprintf(key,sizeof(key),"team_%s_score",pk_team_pool[i].name);
        dict_set(out,key,log->pool_games[i]?log->pool_score[i]/log->pool_games[i]:0);
    }
    dict_set(out,"smogon_set_rate_free",log->free_move_teams?log->smogon_sets_free/(6*log->free_move_teams):0);
    dict_set(out,"smogon_set_rate_forced",log->move_pair_teams?log->smogon_sets_forced/(6*log->move_pair_teams):0);
    if(pk_expert_random_start>0) {
        dict_set(out,"expert_random_eps",log->expert_random_steps?log->expert_random_sum/log->expert_random_steps:0);
        dict_set(out,"expert_random_action_rate",log->expert_random_steps?log->expert_random_actions/log->expert_random_steps:0);
    }
    // Learner win rate per opponent, so a league run shows which banks it beats or cannot.
    double vs_total=0;
    for(int b=0;b<=pk_native_count;b++) vs_total+=log->vs_games[b];
    for(int b=0;b<=pk_native_count;b++) {
        char key[96];
        snprintf(key,sizeof(key),"vs_%s_score",b?pk_native_banks[b-1].name:"self");
        dict_set(out,key,log->vs_games[b]?log->vs_score[b]/log->vs_games[b]:0);
        snprintf(key,sizeof(key),"vs_%s_share",b?pk_native_banks[b-1].name:"self");
        dict_set(out,key,vs_total?log->vs_games[b]/vs_total:0);
    }
    dict_set(out,"policy_0_score",log->policy0_games?log->policy0_score/log->policy0_games:0);
    dict_set(out,"policy_0_games",log->policy0_games);
    // All frozen-league games pooled: the LeagueV2 sweep metric.
    if(pk_native_count) {
        double score=0,games=0;
        for(int b=1;b<=pk_native_count;b++) { score+=log->vs_score[b]; games+=log->vs_games[b]; }
        dict_set(out,"vs_league_score",games?score/games:0);
        long long late=__atomic_load_n(&pk_late_games,__ATOMIC_RELAXED);
        dict_set(out,"vs_league_late",late?0.5*__atomic_load_n(&pk_late_twice_score,__ATOMIC_RELAXED)/late:0);
        dict_set(out,"vs_league_late_games",(double)late);
    }
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
