#include "kaggriculture.h"
#include "puffercpu.h"
#include "ini.h"

#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

enum KagSideKind {
    KAG_SIDE_MODEL,
    KAG_SIDE_CROP,
    KAG_SIDE_RULES,
    KAG_SIDE_RANDOM,
    KAG_SIDE_PASS,
    KAG_SIDE_SCRIPT,
    KAG_SIDE_ADAPTIVE,
};

enum KagStat {
    KAG_STAT_UNIT,
    KAG_STAT_PASS,
    KAG_STAT_MOVE,
    KAG_STAT_PLANT,
    KAG_STAT_WATER,
    KAG_STAT_HARVEST,
    KAG_STAT_ANIMAL_HARVEST,
    KAG_STAT_FERTILIZE,
    KAG_STAT_BUILD,
    KAG_STAT_BUILD_COOP,
    KAG_STAT_BUILD_PASTURE,
    KAG_STAT_DIG,
    KAG_STAT_INVENTORY,
    KAG_STAT_PLACE_ANIMAL,
    KAG_STAT_FEED,
    KAG_STAT_COLLECT_FERTILIZER,
    KAG_STAT_CARE,
    KAG_STAT_MARKET,
    KAG_STAT_BUY,
    KAG_STAT_BUY_SEED,
    KAG_STAT_BUY_PRODUCT,
    KAG_STAT_BUY_ANIMAL,
    KAG_STAT_SELL,
    KAG_STAT_SELL_FERTILIZER,
    KAG_STAT_HIRE,
    KAG_STAT_LAND,
    KAG_NUM_STATS,
};

typedef struct {
    int kind;
    int crop;
    int script_profile;
    char path[4096];
    Weights* weights;
    PufferNet* net;
    uint64_t stats[KAG_NUM_STATS];
} KagSide;

static int kag_quiet_load = 0;

static void kag_bind_demo(Env* env, obs_t* observations, float* actions,
        float* rewards, float* terminals, unsigned char* action_masks) {
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env->agents[player].observations = observations + player * OBS_SIZE;
        env->agents[player].actions = actions + player * NUM_ATNS;
        env->agents[player].rewards = rewards + player;
        env->agents[player].terminals = terminals + player;
        env->agents[player].action_mask = action_masks
            + player * KG_POLICY_ACTION_MASK_SIZE;
        env->agents[player].policy = player == 0 ? 0 : 1;
    }
}

static void kag_init_demo(Env* env, obs_t* observations, float* actions,
        float* rewards, float* terminals, unsigned char* action_masks,
        int headed, uint64_t seed) {
    KGConfig config;
    memset(env, 0, sizeof(*env));
    kg_config_default(&config);
    config.seed = seed;
    env->rng = (unsigned int)seed;
    env->render_enabled = headed;
    env->policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env->policy_max_hands = KG_MAX_HANDS;
    const char* market_slots_text = getenv("KAG_POLICY_MARKET_SLOTS");
    if (market_slots_text && market_slots_text[0]) {
        char* end = NULL;
        long slots = strtol(market_slots_text, &end, 10);
        if (!end || *end || slots < 1 || slots > KG_POLICY_MARKET_SLOTS) {
            fprintf(stderr, "KAG_POLICY_MARKET_SLOTS must be an integer from 1 through %d\n",
                KG_POLICY_MARKET_SLOTS);
            exit(2);
        }
        env->policy_market_slots = (int)slots;
    }
    const char* max_hands_text = getenv("KAG_POLICY_MAX_HANDS");
    if (max_hands_text && max_hands_text[0]) {
        char* end = NULL;
        long hands = strtol(max_hands_text, &end, 10);
        if (!end || *end || hands < 1 || hands > KG_MAX_HANDS) {
            fprintf(stderr, "KAG_POLICY_MAX_HANDS must be an integer from 1 through %d\n",
                KG_MAX_HANDS);
            exit(2);
        }
        env->policy_max_hands = (int)hands;
    }
    env->reward_potential_scale = 0.0001f;
    env->reward_win = 1.0f;
    env->reward_seed_value = 1.0f;
    env->reward_product_value = 1.0f;
    env->reward_crop_value = 1.0f;
    env->reward_animal_value = 1.0f;
    env->reward_land_value = 1.0f;
    env->reward_neglect_discount = 0.5f;
    env->reward_liquidation_days = 6.0f;
    env->num_agents = KG_NUM_PLAYERS;
    kg_init(&env->game_storage, &config);
    kag_bind_demo(env, observations, actions, rewards, terminals, action_masks);
    puf_reset(env);
}

static void kag_random_bot(Env* env, int player) {
    Agent* agent = &env->agents[player];
    unsigned char* mask = agent->action_mask;
    int offset = 0;
    for (int head = 0; head < NUM_ATNS; head++) {
        int size = KG_ACTION_SIZES[head];
        int legal = 0;
        for (int action = 0; action < size; action++) legal += mask[offset + action];
        int pick = legal ? (int)(rand_r(&env->rng) % (unsigned)legal) : 0;
        int selected = 0;
        for (int action = 0; action < size; action++) {
            if (!mask[offset + action]) continue;
            selected = action;
            if (pick-- == 0) break;
        }
        agent->actions[head] = (float)selected;
        offset += size;
    }
}

static void kag_reset_net(PufferNet* net) {
    if (!net) return;
    memset(net->mingru->state, 0,
        (size_t)net->mingru->num_layers * net->mingru->hidden_size * sizeof(float));
    memset(net->mingru->output, 0,
        (size_t)net->mingru->hidden_size * sizeof(float));
}

static const float* kag_model_forward(KagSide* side, const Agent* agent) {
    for (int i = 0; i < OBS_SIZE; i++) {
        side->net->obs[i] = (float)((obs_t*)agent->observations)[i]
            * (1.0f / 255.0f);
    }
    linear(side->net->encoder, side->net->obs);
    mingru(side->net->mingru, side->net->encoder->output);
    linear(side->net->decoder, side->net->mingru->output);
    return side->net->decoder->output;
}

static void kag_model_action(KagSide* side, Env* env, int player) {
    Agent* agent = &env->agents[player];
    const float* logits = kag_model_forward(side, agent);
    const unsigned char* mask = agent->action_mask;
    int offset = 0;
    for (int head = 0; head < NUM_ATNS; head++) {
        int size = KG_ACTION_SIZES[head];
        float max_logit = -INFINITY;
        for (int action = 0; action < size; action++) {
            if (mask[offset + action] && logits[offset + action] > max_logit) {
                max_logit = logits[offset + action];
            }
        }
        float sum = 0.0f;
        for (int action = 0; action < size; action++) {
            if (mask[offset + action]) sum += expf(logits[offset + action] - max_logit);
        }
        float target = rand_r(&env->rng) / ((float)RAND_MAX + 1.0f) * sum;
        int selected = 0;
        for (int action = 0; action < size; action++) {
            if (!mask[offset + action]) continue;
            selected = action;
            target -= expf(logits[offset + action] - max_logit);
            if (target <= 0.0f) break;
        }
        agent->actions[head] = (float)selected;
        offset += size;
    }
}

static int kag_has_suffix(const char* text, const char* suffix) {
    size_t n = strlen(text);
    size_t m = strlen(suffix);
    return n >= m && strcmp(text + n - m, suffix) == 0;
}

static void kag_find_latest(const char* dir, char* out, size_t out_size,
        time_t* best) {
    DIR* directory = opendir(dir);
    if (!directory) return;
    struct dirent* entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat info;
        if (stat(path, &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
            kag_find_latest(path, out, out_size, best);
        } else if (S_ISREG(info.st_mode) && kag_has_suffix(path, ".bin")
                && info.st_ctime >= *best) {
            *best = info.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }
    closedir(directory);
}

static int kag_resolve_model(const char* spec, char* out, size_t out_size) {
    if (strcmp(spec, "latest") != 0) {
        snprintf(out, out_size, "%s", spec);
        return 1;
    }
    time_t best = 0;
    out[0] = 0;
    kag_find_latest("checkpoints/kaggriculture", out, out_size, &best);
    if (!out[0]) {
        fprintf(stderr, "No .bin under checkpoints/kaggriculture; train first.\n");
        return 0;
    }
    return 1;
}

static int kag_model_config_path(const char* model, char* out, size_t out_size) {
    const char* marker = strstr(model, "checkpoints/kaggriculture/");
    if (!marker) return 0;
    marker += strlen("checkpoints/kaggriculture/");
    const char* slash = strchr(marker, '/');
    if (!slash) return 0;
    snprintf(out, out_size, "logs/kaggriculture/%.*s.ini", (int)(slash - marker), marker);
    struct stat info;
    return stat(out, &info) == 0 && S_ISREG(info.st_mode);
}

static int kag_infer_model_arch(int float_count, int obs_size, int logits,
        int* hidden_out, int* layers_out) {
    for (int layers = 1; layers <= 8; layers++) {
        for (int hidden = 8; hidden <= 2048; hidden++) {
            size_t encoder = ((size_t)hidden * obs_size + 7) & ~(size_t)7;
            size_t decoder = ((size_t)(logits + 1) * hidden + 7) & ~(size_t)7;
            size_t recurrent = ((size_t)3 * hidden * hidden + 7) & ~(size_t)7;
            size_t total = encoder + decoder + (size_t)layers * recurrent;
            if (total == (size_t)float_count) {
                *hidden_out = hidden;
                *layers_out = layers;
                return 0;
            }
        }
    }
    return -1;
}

static int kag_load_model_side(KagSide* side, const char* spec) {
    if (!kag_resolve_model(spec, side->path, sizeof(side->path))) return 0;
    int hidden = 32;
    int layers = 2;
    Ini ini = {0};
    puf_ini_load_file(&ini, "config/default.ini");
    puf_ini_load_file(&ini, "config/kaggriculture.ini");
    char config_path[4096];
    if (kag_model_config_path(side->path, config_path, sizeof(config_path))) {
        puf_ini_load_file(&ini, config_path);
    }
    hidden = puf_ini_get_int(&ini, "policy", "hidden_size");
    layers = puf_ini_get_int(&ini, "policy", "num_layers");
    int model_mask = puf_ini_get_int(&ini, "vec", "action_mask_size");
    puf_ini_free(&ini);
    if (model_mask != KG_POLICY_ACTION_MASK_SIZE) {
        fprintf(stderr, "%s uses action mask %d; current Kaggriculture uses %d. "
            "Retrain this incompatible checkpoint.\n",
            side->path, model_mask, KG_POLICY_ACTION_MASK_SIZE);
        return 0;
    }

    struct stat model_info;
    int logits = 0;
    for (int head = 0; head < NUM_ATNS; head++) logits += KG_ACTION_SIZES[head];
    size_t encoder_floats = ((size_t)hidden * OBS_SIZE + 7) & ~(size_t)7;
    size_t decoder_floats = ((size_t)(logits + 1) * hidden + 7) & ~(size_t)7;
    size_t recurrent_floats = ((size_t)3 * hidden * hidden + 7) & ~(size_t)7;
    size_t expected_floats = encoder_floats + decoder_floats
        + (size_t)layers * recurrent_floats;
    if (stat(side->path, &model_info) != 0) {
        fprintf(stderr, "%s cannot be stat'ed\n", side->path);
        return 0;
    }
    if ((size_t)model_info.st_size != expected_floats * sizeof(float)
            && model_info.st_size % (off_t)sizeof(float) == 0) {
        int inferred_hidden = 0;
        int inferred_layers = 0;
        int float_count = (int)(model_info.st_size / (off_t)sizeof(float));
        if (kag_infer_model_arch(float_count, OBS_SIZE, logits,
                &inferred_hidden, &inferred_layers) == 0) {
            hidden = inferred_hidden;
            layers = inferred_layers;
            encoder_floats = ((size_t)hidden * OBS_SIZE + 7) & ~(size_t)7;
            decoder_floats = ((size_t)(logits + 1) * hidden + 7) & ~(size_t)7;
            recurrent_floats = ((size_t)3 * hidden * hidden + 7) & ~(size_t)7;
            expected_floats = encoder_floats + decoder_floats
                + (size_t)layers * recurrent_floats;
        }
    }
    if ((size_t)model_info.st_size != expected_floats * sizeof(float)) {
        fprintf(stderr, "%s has the wrong native network shape (expected %zu "
            "bytes for obs=%d, hidden=%d, layers=%d). Retrain this incompatible "
            "checkpoint.\n", side->path, expected_floats * sizeof(float),
            OBS_SIZE, hidden, layers);
        return 0;
    }

    side->weights = load_weights(side->path);
    if (!side->weights) return 0;
    int action_sizes[] = ACT_SIZES;
    side->net = make_puffernet(side->weights, 1, OBS_SIZE, hidden, layers,
        action_sizes, NUM_ATNS);
    if (!kag_quiet_load) {
        printf("Loaded %s (hidden=%d layers=%d)\n", side->path, hidden, layers);
    }
    return 1;
}

static int kag_parse_side(KagSide* side, const char* spec) {
    memset(side, 0, sizeof(*side));
    if (!strcmp(spec, "rules") || !strcmp(spec, "starter")) {
        side->kind = KAG_SIDE_RULES;
        side->crop = KG_CROP_INVALID;
        return 1;
    }
    if (!strcmp(spec, "wheat")) {
        side->kind = KAG_SIDE_CROP;
        side->crop = KG_WHEAT;
        return 1;
    }
    if (!strcmp(spec, "carrot")) {
        side->kind = KAG_SIDE_CROP;
        side->crop = KG_CARROT;
        return 1;
    }
    if (!strcmp(spec, "melon")) {
        side->kind = KAG_SIDE_CROP;
        side->crop = KG_MELON;
        return 1;
    }
    if (!strcmp(spec, "frontier") || !strcmp(spec, "night")) {
        side->kind = KAG_SIDE_SCRIPT;
        side->script_profile = KG_SCRIPT_FRONTIER;
        return 1;
    }
    if (!strcmp(spec, "v20") || !strcmp(spec, "weed")) {
        side->kind = KAG_SIDE_SCRIPT;
        side->script_profile = KG_SCRIPT_V20;
        return 1;
    }
    if (!strcmp(spec, "moon") || !strcmp(spec, "melons")) {
        side->kind = KAG_SIDE_SCRIPT;
        side->script_profile = KG_SCRIPT_MOON;
        return 1;
    }
    if (!strcmp(spec, "hamburger") || !strcmp(spec, "tran")) {
        side->kind = KAG_SIDE_SCRIPT;
        side->script_profile = KG_SCRIPT_HAMBURGER;
        return 1;
    }
    if (!strcmp(spec, "top")) {
        side->kind = KAG_SIDE_SCRIPT;
        side->script_profile = KG_SCRIPT_TOP;
        return 1;
    }
    if (!strcmp(spec, "fields")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_FIELDS;
        return 1;
    }
    if (!strcmp(spec, "scenario")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_SCENARIO;
        return 1;
    }
    if (!strcmp(spec, "soil")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_SOIL;
        return 1;
    }
    if (!strcmp(spec, "kaito")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_KAITO;
        return 1;
    }
    if (!strcmp(spec, "shield")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_SHIELD;
        return 1;
    }
    if (!strcmp(spec, "frontier12")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_FRONTIER12;
        return 1;
    }
    if (!strcmp(spec, "pulse") || !strcmp(spec, "harvest_pulse")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_HARVEST_PULSE;
        return 1;
    }
    if (!strcmp(spec, "structured") || !strcmp(spec, "economic")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_STRUCTURED;
        return 1;
    }
    if (!strcmp(spec, "triad") || !strcmp(spec, "adaptive")) {
        side->kind = KAG_SIDE_ADAPTIVE;
        side->script_profile = KAG_ADAPTIVE_TRIAD;
        return 1;
    }
    if (!strcmp(spec, "random")) {
        side->kind = KAG_SIDE_RANDOM;
        return 1;
    }
    if (!strcmp(spec, "pass")) {
        side->kind = KAG_SIDE_PASS;
        return 1;
    }
    side->kind = KAG_SIDE_MODEL;
    return kag_load_model_side(side, spec);
}

static void kag_track_side(KagSide* side, const Env* env, int player) {
    const Agent* agent = &env->agents[player];
    const KGPlayer* farm = &env->game_storage.players[player];
    int controlled = farm->unit_count < KG_POLICY_UNITS
        ? farm->unit_count : KG_POLICY_UNITS;
    for (int unit = 0; unit < controlled; unit++) {
        const KGUnitState* unit_state = &farm->units[unit];
        const KGTile* tile = &farm->tiles[kg_tile_index(
            unit_state->x, unit_state->y)];
        KGPolicyUnitSpec spec = kag_unit_spec(kag_discrete_index(
            agent->actions[unit], KG_POLICY_UNIT_COMMANDS));
        side->stats[KAG_STAT_UNIT]++;
        if (spec.op == KG_OP_PASS) side->stats[KAG_STAT_PASS]++;
        else if (spec.op >= KG_OP_NORTH && spec.op <= KG_OP_WEST) {
            side->stats[KAG_STAT_MOVE]++;
        } else if (spec.op == KG_OP_PLANT) side->stats[KAG_STAT_PLANT]++;
        else if (spec.op == KG_OP_WATER) side->stats[KAG_STAT_WATER]++;
        else if (spec.op == KG_OP_HARVEST) {
            side->stats[KAG_STAT_HARVEST]++;
            if (kg_is_animal_tile(tile)) side->stats[KAG_STAT_ANIMAL_HARVEST]++;
        } else if (spec.op == KG_OP_FERTILIZE) {
            side->stats[KAG_STAT_FERTILIZE]++;
        } else if (spec.op == KG_OP_BUILD_COOP) {
            side->stats[KAG_STAT_BUILD]++;
            side->stats[KAG_STAT_BUILD_COOP]++;
        } else if (spec.op == KG_OP_BUILD_PASTURE) {
            side->stats[KAG_STAT_BUILD]++;
            side->stats[KAG_STAT_BUILD_PASTURE]++;
        }
        else if (spec.op == KG_OP_DIG) side->stats[KAG_STAT_DIG]++;
        else if (spec.op == KG_OP_PICKUP || spec.op == KG_OP_DROP
                || spec.op == KG_OP_PLACE) {
            side->stats[KAG_STAT_INVENTORY]++;
            if (spec.op == KG_OP_PLACE && spec.arg >= KG_ITEM_GOOSE
                    && spec.arg <= KG_ITEM_SHEEP) {
                side->stats[KAG_STAT_PLACE_ANIMAL]++;
            }
        } else if (spec.op == KG_OP_FEED) {
            side->stats[KAG_STAT_FEED]++;
        } else if (spec.op == KG_OP_COLLECT_FERTILIZER) {
            side->stats[KAG_STAT_COLLECT_FERTILIZER]++;
        } else if (spec.op == KG_OP_CARE) {
            side->stats[KAG_STAT_CARE]++;
        }
    }
    for (int order = 0; order < KG_POLICY_MARKET_SLOTS; order++) {
        int continue_head = KG_POLICY_MARKET_HEAD_OFFSET + 3 * order;
        if (kag_discrete_index(agent->actions[continue_head],
                KG_POLICY_MARKET_CONTINUE_ACTIONS)
                != PUFFER_CONDITIONAL_CONTINUE) break;
        KGPolicyMarketSpec market = kag_market_spec(kag_discrete_index(
            agent->actions[continue_head + 1], KG_POLICY_MARKET_COMMANDS));
        side->stats[KAG_STAT_MARKET]++;
        if (market.op == KG_MARKET_BUY_SEED) {
            side->stats[KAG_STAT_BUY]++;
            side->stats[KAG_STAT_BUY_SEED]++;
        } else if (market.op == KG_MARKET_BUY_PRODUCT) {
            side->stats[KAG_STAT_BUY]++;
            side->stats[KAG_STAT_BUY_PRODUCT]++;
        } else if (market.op == KG_MARKET_BUY_ANIMAL) {
            side->stats[KAG_STAT_BUY]++;
            side->stats[KAG_STAT_BUY_ANIMAL]++;
        }
        if (market.op == KG_MARKET_SELL) {
            side->stats[KAG_STAT_SELL]++;
            if (market.item == KG_ITEM_FERTILIZER) {
                side->stats[KAG_STAT_SELL_FERTILIZER]++;
            }
        }
        if (market.op == KG_MARKET_HIRE) side->stats[KAG_STAT_HIRE]++;
        if (market.op == KG_MARKET_BUY_LAND) side->stats[KAG_STAT_LAND]++;
    }
}

static void kag_track_native_action(KagSide* side, const Env* env, int player,
        const KGAction* action) {
    const KGState* game = &env->game_storage;
    const KGPlayer* farm = &game->players[player];
    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitAction* command = unit == 0
            ? &action->farmer : &action->hands[unit - 1];
        side->stats[KAG_STAT_UNIT]++;
        if (command->op == KG_OP_PASS) side->stats[KAG_STAT_PASS]++;
        else if (command->op >= KG_OP_NORTH && command->op <= KG_OP_WEST) {
            side->stats[KAG_STAT_MOVE]++;
        } else if (command->op == KG_OP_PLANT) side->stats[KAG_STAT_PLANT]++;
        else if (command->op == KG_OP_WATER) side->stats[KAG_STAT_WATER]++;
        else if (command->op == KG_OP_HARVEST) {
            side->stats[KAG_STAT_HARVEST]++;
            const KGUnitState* state = &farm->units[unit];
            const KGTile* tile = &farm->tiles[kg_tile_index(state->x, state->y)];
            if (kg_is_animal_tile(tile)) side->stats[KAG_STAT_ANIMAL_HARVEST]++;
        } else if (command->op == KG_OP_FERTILIZE) {
            side->stats[KAG_STAT_FERTILIZE]++;
        } else if (command->op == KG_OP_BUILD_COOP) {
            side->stats[KAG_STAT_BUILD]++;
            side->stats[KAG_STAT_BUILD_COOP]++;
        } else if (command->op == KG_OP_BUILD_PASTURE) {
            side->stats[KAG_STAT_BUILD]++;
            side->stats[KAG_STAT_BUILD_PASTURE]++;
        } else if (command->op == KG_OP_DIG) side->stats[KAG_STAT_DIG]++;
        else if (command->op == KG_OP_PICKUP || command->op == KG_OP_DROP
                || command->op == KG_OP_PLACE) {
            side->stats[KAG_STAT_INVENTORY]++;
            if (command->op == KG_OP_PLACE
                    && command->arg >= KG_ITEM_GOOSE
                    && command->arg <= KG_ITEM_SHEEP) {
                side->stats[KAG_STAT_PLACE_ANIMAL]++;
            }
        } else if (command->op == KG_OP_FEED) side->stats[KAG_STAT_FEED]++;
        else if (command->op == KG_OP_COLLECT_FERTILIZER) {
            side->stats[KAG_STAT_COLLECT_FERTILIZER]++;
        } else if (command->op == KG_OP_CARE) side->stats[KAG_STAT_CARE]++;
    }
    for (int order = 0; order < action->market_count; order++) {
        const KGMarketOrder* market = &action->market[order];
        side->stats[KAG_STAT_MARKET]++;
        if (market->op == KG_MARKET_BUY_SEED) {
            side->stats[KAG_STAT_BUY]++;
            side->stats[KAG_STAT_BUY_SEED]++;
        } else if (market->op == KG_MARKET_BUY_PRODUCT) {
            side->stats[KAG_STAT_BUY]++;
            side->stats[KAG_STAT_BUY_PRODUCT]++;
        } else if (market->op == KG_MARKET_BUY_ANIMAL) {
            side->stats[KAG_STAT_BUY]++;
            side->stats[KAG_STAT_BUY_ANIMAL]++;
        } else if (market->op == KG_MARKET_SELL) {
            side->stats[KAG_STAT_SELL]++;
            if (market->item == KG_ITEM_FERTILIZER) {
                side->stats[KAG_STAT_SELL_FERTILIZER]++;
            }
        } else if (market->op == KG_MARKET_HIRE) {
            side->stats[KAG_STAT_HIRE]++;
        } else if (market->op == KG_MARKET_BUY_LAND) {
            side->stats[KAG_STAT_LAND]++;
        }
    }
}

static void kag_side_action(KagSide* side, Env* env, int player) {
    env->demo_bots[player] = KAG_BOT_NONE;
    if (side->kind == KAG_SIDE_MODEL) kag_model_action(side, env, player);
    else if (side->kind == KAG_SIDE_RULES) {
        env->demo_bots[player] = KAG_BOT_MIXED;
        kag_clear_policy_actions(&env->agents[player]);
    } else if (side->kind == KAG_SIDE_CROP) {
        env->demo_bots[player] = KAG_BOT_CROP_BASE + side->crop;
        kag_clear_policy_actions(&env->agents[player]);
    } else if (side->kind == KAG_SIDE_SCRIPT) {
        env->demo_bots[player] = KAG_BOT_SCRIPT_BASE + side->script_profile;
        kag_clear_policy_actions(&env->agents[player]);
        KGAction preview;
        kag_script_action(&env->game_storage, player,
            side->script_profile, &preview);
        kag_script_repair(&env->game_storage, player,
            side->script_profile, &preview);
        kag_track_native_action(side, env, player, &preview);
    } else if (side->kind == KAG_SIDE_ADAPTIVE) {
        env->demo_bots[player] = KAG_BOT_ADAPTIVE_BASE + side->script_profile;
        kag_clear_policy_actions(&env->agents[player]);
        KGAction preview;
        kag_adaptive_action(&env->game_storage, player,
            side->script_profile, &preview);
        kag_track_native_action(side, env, player, &preview);
    }
    else if (side->kind == KAG_SIDE_RANDOM) kag_random_bot(env, player);
    else kag_clear_policy_actions(&env->agents[player]);
    if (env->demo_bots[player] == KAG_BOT_NONE) {
        kag_track_side(side, env, player);
    }
}

static void kag_free_side(KagSide* side) {
    if (side->net) free_puffernet(side->net);
    free(side->weights);
}

static void kag_usage(const char* exe) {
    printf("Usage:\n");
    printf("  %s watch [SIDE_A] [SIDE_B]\n", exe);
    printf("  %s bench [steps] [SIDE_A] [SIDE_B]\n", exe);
    printf("  %s jsd [steps] LABEL=MODEL.bin [LABEL=MODEL.bin ...]\n", exe);
    printf("Sides: latest, MODEL.bin, rules, wheat, carrot, melon, frontier, night, v20, moon, hamburger, fields, scenario, soil, kaito, shield, frontier12, pulse, structured, triad, random, pass\n");
    printf("  rules: adaptive prices/demand, visible supply, hands, land, liquidation\n");
    printf("One side is mirrored: '%s watch latest' plays latest vs itself.\n", exe);
}

static double kag_kl_term(double probability, double mixture) {
    return probability > 0.0 ? probability * log(probability / mixture) : 0.0;
}

/* Probability that this policy's market queue reaches a head, from its own
 * masked softmax probabilities. Unit heads are always reached; deeper slots
 * are weighted by the policy's continuation probabilities. */
static float kag_probe_reach(const float* probs, int head) {
    if (head < KG_POLICY_MARKET_HEAD_OFFSET) return 1.0f;
    int relative = head - KG_POLICY_MARKET_HEAD_OFFSET;
    int slot = relative / PUFFER_CONDITIONAL_HEADS_PER_SLOT;
    int node = relative % PUFFER_CONDITIONAL_HEADS_PER_SLOT;
    float reach = 1.0f;
    for (int prev = 0; prev < slot; prev++) {
        int cont_head = KG_POLICY_MARKET_HEAD_OFFSET + 3 * prev;
        int off = 0;
        for (int h = 0; h < cont_head; h++) off += KG_ACTION_SIZES[h];
        reach *= probs[off + 1];
        if (reach == 0.0f) return 0.0f;
    }
    if (node == 0) return reach;
    int cont_head = KG_POLICY_MARKET_HEAD_OFFSET + 3 * slot;
    int off = 0;
    for (int h = 0; h < cont_head; h++) off += KG_ACTION_SIZES[h];
    reach *= probs[off + 1];
    if (reach == 0.0f) return 0.0f;
    if (node == 1) return reach;
    int command_head = cont_head + 1;
    int coff = 0;
    for (int h = 0; h < command_head; h++) coff += KG_ACTION_SIZES[h];
    float quant = 0.0f;
    for (int id = 0; id < KG_POLICY_MARKET_QUANTITY_COMMANDS; id++) {
        quant += probs[coff + id];
    }
    return reach * quant;
}

static int kag_behavior_jsd(int argc, char** argv) {
    int arg = 2;
    int steps = 720;
    if (arg < argc && strspn(argv[arg], "0123456789") == strlen(argv[arg])) {
        steps = atoi(argv[arg++]);
    }
    int count = argc - arg;
    if (steps <= 0 || count < 2 || count > 64) {
        fprintf(stderr, "jsd requires positive steps and 2..64 LABEL=MODEL checkpoints\n");
        return 2;
    }
    int seeds = 1;
    const char* seeds_text = getenv("KAG_JSD_SEEDS");
    if (seeds_text && seeds_text[0]) {
        seeds = atoi(seeds_text);
        if (seeds < 1 || seeds > 32) {
            fprintf(stderr, "KAG_JSD_SEEDS must be an integer from 1 through 32\n");
            return 2;
        }
    }

    KagSide* models = (KagSide*)calloc((size_t)count, sizeof(KagSide));
    char (*labels)[256] = calloc((size_t)count, sizeof(*labels));
    double* sums = (double*)calloc((size_t)count * count, sizeof(double));
    double* samples = (double*)calloc((size_t)count * count, sizeof(double));
    float* probabilities = (float*)calloc(
        (size_t)count * KG_POLICY_ACTION_MASK_SIZE, sizeof(float));
    float* reach = (float*)calloc((size_t)count * NUM_ATNS, sizeof(float));
    if (!models || !labels || !sums || !samples || !probabilities || !reach) {
        return 1;
    }

    kag_quiet_load = 1;
    for (int i = 0; i < count; i++) {
        const char* spec = argv[arg + i];
        const char* equals = strchr(spec, '=');
        if (!equals || equals == spec || !equals[1]
                || (size_t)(equals - spec) >= sizeof(labels[i])) {
            fprintf(stderr, "invalid jsd model spec: %s\n", spec);
            return 2;
        }
        snprintf(labels[i], sizeof(labels[i]), "%.*s", (int)(equals - spec), spec);
        models[i].kind = KAG_SIDE_MODEL;
        if (!kag_load_model_side(&models[i], equals + 1)) return 1;
    }

    for (int s = 0; s < seeds; s++) {
        uint64_t seed = 42 + 1000003u * (uint64_t)s;
        Env env;
        obs_t observations[KG_NUM_PLAYERS * OBS_SIZE] = {0};
        float actions[KG_NUM_PLAYERS * NUM_ATNS] = {0};
        float rewards[KG_NUM_PLAYERS] = {0};
        float terminals[KG_NUM_PLAYERS] = {0};
        unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE] = {0};
        kag_init_demo(&env, observations, actions, rewards, terminals,
            masks, 0, seed);

        for (int step = 0; step < steps; step++) {
            Agent* probe = &env.agents[0];
            const unsigned char* mask = probe->action_mask;
            for (int i = 0; i < count; i++) {
                const float* logits = kag_model_forward(&models[i], probe);
                int offset = 0;
                for (int head = 0; head < NUM_ATNS; head++) {
                    int size = KG_ACTION_SIZES[head];
                    float maximum = -INFINITY;
                    for (int action = 0; action < size; action++) {
                        if (mask[offset + action]) {
                            maximum = fmaxf(maximum, logits[offset + action]);
                        }
                    }
                    float total = 0.0f;
                    for (int action = 0; action < size; action++) {
                        float value = mask[offset + action]
                            ? expf(logits[offset + action] - maximum) : 0.0f;
                        probabilities[(size_t)i * KG_POLICY_ACTION_MASK_SIZE
                            + offset + action] = value;
                        total += value;
                    }
                    if (total > 0.0f) {
                        for (int action = 0; action < size; action++) {
                            probabilities[(size_t)i * KG_POLICY_ACTION_MASK_SIZE
                                + offset + action] /= total;
                        }
                    }
                    offset += size;
                }
                for (int head = 0; head < NUM_ATNS; head++) {
                    reach[(size_t)i * NUM_ATNS + head] = kag_probe_reach(
                        probabilities + (size_t)i * KG_POLICY_ACTION_MASK_SIZE,
                        head);
                }
            }

            int offset = 0;
            for (int head = 0; head < NUM_ATNS; head++) {
                int size = KG_ACTION_SIZES[head];
                int legal = 0;
                for (int action = 0; action < size; action++) {
                    legal += mask[offset + action] != 0;
                }
                if (legal > 1) {
                    for (int a = 0; a < count; a++) {
                        for (int b = a + 1; b < count; b++) {
                            double weight = (double)reach[
                                (size_t)a * NUM_ATNS + head]
                                * reach[(size_t)b * NUM_ATNS + head];
                            if (weight <= 0.0) continue;
                            double jsd = 0.0;
                            for (int action = 0; action < size; action++) {
                                if (!mask[offset + action]) continue;
                                double p = probabilities[(size_t)a
                                    * KG_POLICY_ACTION_MASK_SIZE
                                    + offset + action];
                                double q = probabilities[(size_t)b
                                    * KG_POLICY_ACTION_MASK_SIZE
                                    + offset + action];
                                double m = 0.5 * (p + q);
                                jsd += 0.5 * (kag_kl_term(p, m)
                                    + kag_kl_term(q, m));
                            }
                            sums[a * count + b] += weight * jsd;
                            samples[a * count + b] += weight;
                        }
                    }
                }
                offset += size;
            }

            env.demo_bots[0] = KAG_BOT_MIXED;
            env.demo_bots[1] = KAG_BOT_MIXED;
            kag_clear_policy_actions(&env.agents[0]);
            kag_clear_policy_actions(&env.agents[1]);
            puf_step(&env);
            if (terminals[0]) {
                for (int i = 0; i < count; i++) kag_reset_net(models[i].net);
            }
        }
        puf_close(&env);
    }

    printf("policy");
    for (int i = 0; i < count; i++) printf("\t%s", labels[i]);
    putchar('\n');
    for (int a = 0; a < count; a++) {
        printf("%s", labels[a]);
        for (int b = 0; b < count; b++) {
            int lo = a < b ? a : b;
            int hi = a < b ? b : a;
            double value = a == b || !samples[lo * count + hi] ? 0.0
                : sums[lo * count + hi] / samples[lo * count + hi];
            printf("\t%.9f", value);
        }
        putchar('\n');
    }

    for (int i = 0; i < count; i++) kag_free_side(&models[i]);
    free(probabilities);
    free(samples);
    free(sums);
    free(labels);
    free(models);
    free(reach);
    return 0;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "watch";
    if (!strcmp(mode, "jsd")) return kag_behavior_jsd(argc, argv);
    int headless = !strcmp(mode, "bench") || !strcmp(mode, "--headless");
    if (strcmp(mode, "watch") && strcmp(mode, "bench") && strcmp(mode, "--headless")) {
        kag_usage(argv[0]);
        return 1;
    }
    int arg = 2;
    int steps = 100000;
    if (headless && arg < argc && strspn(argv[arg], "0123456789") == strlen(argv[arg])) {
        steps = atoi(argv[arg++]);
    }
    const char* spec0 = arg < argc ? argv[arg++] : "rules";
    const char* spec1 = arg < argc ? argv[arg] : spec0;

    Env env;
    obs_t observations[KG_NUM_PLAYERS * OBS_SIZE] = {0};
    float actions[KG_NUM_PLAYERS * NUM_ATNS] = {0};
    float rewards[KG_NUM_PLAYERS] = {0};
    float terminals[KG_NUM_PLAYERS] = {0};
    unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE] = {0};
    KagSide sides[KG_NUM_PLAYERS] = {0};
    if (!kag_parse_side(&sides[0], spec0) || !kag_parse_side(&sides[1], spec1)) {
        kag_free_side(&sides[0]);
        kag_free_side(&sides[1]);
        return 1;
    }

    kag_init_demo(&env, observations, actions, rewards, terminals,
        masks, !headless, 42);
    env.render_names[0] = spec0;
    env.render_names[1] = spec1;
    printf("Kaggriculture: %s vs %s\n", spec0, spec1);
    if (!headless) {
        puf_render(&env);
        if (!IsWindowReady()) return 2;
        printf("SPACE pause/resume, PERIOD single-step, ESC quit\n");
    }

    clock_t start = clock();
    int frame = 0;
    int paused = 0;
    while (headless ? frame < steps : !WindowShouldClose()) {
        if (!headless) {
            if (IsKeyPressed(KEY_SPACE)) paused = !paused;
            if (paused && !IsKeyPressed(KEY_PERIOD)) {
                puf_render(&env);
                continue;
            }
        }
        kag_side_action(&sides[0], &env, 0);
        kag_side_action(&sides[1], &env, 1);
        puf_step(&env);
        if (terminals[0]) {
            kag_reset_net(sides[0].net);
            kag_reset_net(sides[1].net);
        }
        if (!headless) puf_render(&env);
        frame++;
    }

    if (headless) {
        double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
        printf("%.0f agent-steps/s, games=%.0f, score=%.3f, draw=%.3f, "
               "avg_money=(%.0f, %.0f)\n",
            (double)(frame * KG_NUM_PLAYERS) / elapsed, env.log.n,
            env.log.n ? env.log.perf / env.log.n : 0.0,
            env.log.n ? env.log.draw_rate / env.log.n : 0.0,
            env.log.n ? env.log.score / env.log.n : 0.0,
            env.log.n ? env.log.opponent_score / env.log.n : 0.0);
        for (int player = 0; player < KG_NUM_PLAYERS; player++) {
            KagSide* side = &sides[player];
            double units = side->stats[KAG_STAT_UNIT]
                ? (double)side->stats[KAG_STAT_UNIT] : 1.0;
            printf("P%d %-12s pass=%5.1f%% move=%5.1f%% plant=%5.1f%% "
                   "water=%5.1f%% harvest=%5.1f%% build=%5.1f%% dig=%5.1f%% "
                   "inv=%5.1f%% orders/turn=%.2f buy=%llu sell=%llu hire=%llu land=%llu "
                   "animal_harvest=%llu fertilize=%llu coop=%llu pasture=%llu "
                   "animal_place=%llu feed=%llu care=%llu fert_collect=%llu "
                   "seed_buy=%llu product_buy=%llu animal_buy=%llu "
                   "fert_sell=%llu\n",
                player, player ? spec1 : spec0,
                100.0 * side->stats[KAG_STAT_PASS] / units,
                100.0 * side->stats[KAG_STAT_MOVE] / units,
                100.0 * side->stats[KAG_STAT_PLANT] / units,
                100.0 * side->stats[KAG_STAT_WATER] / units,
                100.0 * side->stats[KAG_STAT_HARVEST] / units,
                100.0 * side->stats[KAG_STAT_BUILD] / units,
                100.0 * side->stats[KAG_STAT_DIG] / units,
                100.0 * side->stats[KAG_STAT_INVENTORY] / units,
                frame ? (double)side->stats[KAG_STAT_MARKET] / frame : 0.0,
                (unsigned long long)side->stats[KAG_STAT_BUY],
                (unsigned long long)side->stats[KAG_STAT_SELL],
                (unsigned long long)side->stats[KAG_STAT_HIRE],
                (unsigned long long)side->stats[KAG_STAT_LAND],
                (unsigned long long)side->stats[KAG_STAT_ANIMAL_HARVEST],
                (unsigned long long)side->stats[KAG_STAT_FERTILIZE],
                (unsigned long long)side->stats[KAG_STAT_BUILD_COOP],
                (unsigned long long)side->stats[KAG_STAT_BUILD_PASTURE],
                (unsigned long long)side->stats[KAG_STAT_PLACE_ANIMAL],
                (unsigned long long)side->stats[KAG_STAT_FEED],
                (unsigned long long)side->stats[KAG_STAT_CARE],
                (unsigned long long)side->stats[KAG_STAT_COLLECT_FERTILIZER],
                (unsigned long long)side->stats[KAG_STAT_BUY_SEED],
                (unsigned long long)side->stats[KAG_STAT_BUY_PRODUCT],
                (unsigned long long)side->stats[KAG_STAT_BUY_ANIMAL],
                (unsigned long long)side->stats[KAG_STAT_SELL_FERTILIZER]);
        }
    }
    puf_close(&env);
    kag_free_side(&sides[0]);
    kag_free_side(&sides[1]);
    return 0;
}
