#ifndef PUFFERLIB_KAGGRICULTURE_BOTS_H
#define PUFFERLIB_KAGGRICULTURE_BOTS_H

/*
 * Native Kaggriculture opponents.
 *
 * The public submissions we collected are fixed-action/replay policies with
 * small state-dependent repair layers. Keeping the replay as packed uint16
 * actions gives us the useful behavior without shipping a Python runtime:
 * four tapes are decoded once at process startup and the step path only does
 * bounded array reads and copies into KGAction.
 */

#include "kaggriculture_tape_data.h"

#define KG_SCRIPT_FRAMES 720
#define KG_SCRIPT_MAX_HAND_ACTIONS 7000
#define KG_SCRIPT_MAX_MARKET_ACTIONS 1500
#define KG_SCRIPT_TAPE_RAW_SIZE 30000

enum {
    KG_SCRIPT_FRONTIER = 0,
    KG_SCRIPT_V20,
    KG_SCRIPT_MOON,
    KG_SCRIPT_HAMBURGER,
    KG_SCRIPT_LUGOVOY,
    KG_SCRIPT_THUNDER25,
    KG_SCRIPT_TOP,
    KG_SCRIPT_COUNT,
};

typedef struct {
    uint32_t hand_offset;
    uint32_t market_offset;
    uint8_t hand_count;
    uint8_t market_count;
    uint16_t farmer;
} KGScriptFrame;

typedef struct {
    KGScriptFrame frames[KG_SCRIPT_FRAMES];
    uint16_t hand_actions[KG_SCRIPT_MAX_HAND_ACTIONS];
    uint16_t market_actions[KG_SCRIPT_MAX_MARKET_ACTIONS];
    uint32_t hand_count;
    uint32_t market_count;
} KGScriptTape;

static KGScriptTape kag_script_tapes[KG_SCRIPT_COUNT];
static int kag_script_tapes_ready;

static inline int kag_script_b64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (int)c - 'A';
    if (c >= 'a' && c <= 'z') return (int)c - 'a' + 26;
    if (c >= '0' && c <= '9') return (int)c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static inline uint16_t kag_script_read16(const uint8_t* p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static inline uint32_t kag_script_read32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8
        | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline int kag_script_b64_decode(const char* source,
        uint8_t* output, int capacity) {
    int out = 0;
    uint32_t value = 0;
    int bits = -8;
    for (const unsigned char* p = (const unsigned char*)source; *p; p++) {
        int digit = kag_script_b64_value(*p);
        if (digit < 0) continue;
        value = (value << 6) | digit;
        bits += 6;
        if (bits >= 0) {
            if (out >= capacity) return -1;
            output[out++] = (uint8_t)((value >> bits) & 255);
            bits -= 8;
            value = bits ? value & ((1u << bits) - 1u) : 0;
        }
    }
    return out;
}

static inline int kag_script_decode(const char* encoded,
        KGScriptTape* tape, uint8_t* raw) {
    int size = kag_script_b64_decode(encoded, raw, KG_SCRIPT_TAPE_RAW_SIZE);
    if (size < 16 || memcmp(raw, "KGT1", 4) != 0) return 0;
    uint16_t frames = kag_script_read16(raw + 4);
    uint16_t hand_count = kag_script_read16(raw + 6);
    uint32_t market_count = kag_script_read32(raw + 8);
    if (frames != KG_SCRIPT_FRAMES
            || hand_count > KG_SCRIPT_MAX_HAND_ACTIONS
            || market_count > KG_SCRIPT_MAX_MARKET_ACTIONS) {
        return 0;
    }
    size_t frame_bytes = (size_t)frames * 12;
    size_t hand_bytes = (size_t)hand_count * 2;
    size_t market_bytes = (size_t)market_count * 2;
    if (16 + frame_bytes + hand_bytes + market_bytes > (size_t)size) {
        return 0;
    }
    const uint8_t* frame_data = raw + 16;
    const uint8_t* hand_data = frame_data + frame_bytes;
    const uint8_t* market_data = hand_data + hand_bytes;
    for (int frame = 0; frame < KG_SCRIPT_FRAMES; frame++) {
        const uint8_t* p = frame_data + frame * 12;
        KGScriptFrame* dst = &tape->frames[frame];
        dst->hand_offset = kag_script_read32(p);
        dst->market_offset = kag_script_read32(p + 4);
        dst->hand_count = p[8];
        dst->market_count = p[9];
        dst->farmer = kag_script_read16(p + 10);
        if ((uint64_t)dst->hand_offset + dst->hand_count > hand_count
                || (uint64_t)dst->market_offset + dst->market_count
                    > market_count) {
            return 0;
        }
    }
    for (uint32_t i = 0; i < hand_count; i++) {
        tape->hand_actions[i] = kag_script_read16(hand_data + i * 2);
    }
    for (uint32_t i = 0; i < market_count; i++) {
        tape->market_actions[i] = kag_script_read16(market_data + i * 2);
    }
    tape->hand_count = hand_count;
    tape->market_count = market_count;
    return 1;
}

static inline void kag_script_init(void) {
    if (kag_script_tapes_ready) return;
    static uint8_t raw[KG_SCRIPT_TAPE_RAW_SIZE];
    int ok = kag_script_decode(KG_TAPE_FRONTIER_B64,
        &kag_script_tapes[KG_SCRIPT_FRONTIER], raw);
    ok &= kag_script_decode(KG_TAPE_V20_B64,
        &kag_script_tapes[KG_SCRIPT_V20], raw);
    ok &= kag_script_decode(KG_TAPE_MOON_B64,
        &kag_script_tapes[KG_SCRIPT_MOON], raw);
    ok &= kag_script_decode(KG_TAPE_HAMBURGER_B64,
        &kag_script_tapes[KG_SCRIPT_HAMBURGER], raw);
    ok &= kag_script_decode(KG_TAPE_LUGOVOY_B64,
        &kag_script_tapes[KG_SCRIPT_LUGOVOY], raw);
    ok &= kag_script_decode(KG_TAPE_THUNDER25_B64,
        &kag_script_tapes[KG_SCRIPT_THUNDER25], raw);
    ok &= kag_script_decode(KG_TAPE_TOP_B64,
        &kag_script_tapes[KG_SCRIPT_TOP], raw);
    if (!ok) {
        fprintf(stderr, "Kaggriculture native tape decode failed\n");
        abort();
    }
    kag_script_tapes_ready = 1;
}

KG_HD static inline KGUnitAction kag_script_unpack_unit(uint16_t packed) {
    return (KGUnitAction){
        (int)(packed & 31),
        (int)((packed >> 5) & 15) - 1,
        (int)((packed >> 9) & 127),
    };
}

KG_HD static inline KGMarketOrder kag_script_unpack_market(uint16_t packed) {
    return (KGMarketOrder){
        (int)(packed & 7),
        (int)((packed >> 3) & 15) - 1,
        (int)((packed >> 7) & 127),
    };
}

KG_HD static inline int kag_script_profile_valid(int profile) {
    return (unsigned)profile < KG_SCRIPT_COUNT;
}

KG_HD static inline void kag_script_action_from_tapes(const KGState* game,
        int player_id, int profile, KGAction* action,
        const KGScriptTape* tapes) {
    if (!kag_script_profile_valid(profile)) {
        action->hand_count = 0;
        action->market_count = 0;
        action->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
        action->hand_count = game->players[player_id].hand_count;
        for (int hand = 0; hand < action->hand_count; hand++) {
            action->hands[hand] = (KGUnitAction){KG_OP_PASS, -1, 1};
        }
        return;
    }
    const KGPlayer* farm = &game->players[player_id];
    const KGScriptTape* tape = &tapes[profile];
    int step = game->step < KG_SCRIPT_FRAMES
        ? game->step : KG_SCRIPT_FRAMES - 1;
    const KGScriptFrame* frame = &tape->frames[step];
    action->hand_count = 0;
    action->market_count = 0;
    action->farmer = kag_script_unpack_unit(frame->farmer);
    action->hand_count = farm->hand_count;
    for (int hand = 0; hand < action->hand_count; hand++) {
        action->hands[hand] = hand < frame->hand_count
            ? kag_script_unpack_unit(tape->hand_actions[
                frame->hand_offset + hand])
            : (KGUnitAction){KG_OP_PASS, -1, 1};
    }
    int limit = frame->market_count;
    if (limit > game->config.max_market_orders_per_turn) {
        limit = game->config.max_market_orders_per_turn;
    }
    if (limit > KG_MAX_MARKET_ORDERS) limit = KG_MAX_MARKET_ORDERS;
    action->market_count = limit;
    for (int order = 0; order < limit; order++) {
        action->market[order] = kag_script_unpack_market(
            tape->market_actions[frame->market_offset + order]);
    }
}

static inline void kag_script_action(const KGState* game, int player_id,
        int profile, KGAction* action) {
    kag_script_init();
    kag_script_action_from_tapes(game, player_id, profile, action,
        kag_script_tapes);
}

KG_HD static inline KGUnitAction* kag_script_unit_action(KGAction* action, int unit) {
    return unit == 0 ? &action->farmer : &action->hands[unit - 1];
}

/* Repair only the information lost while projecting a rich scripted command
 * into the compact policy ABI. PICKUP has no quantity head, so PICKUP 1 may
 * collect several animals. This helper is deliberately not part of the live
 * scripted bot: the unprojected tape already has exact quantities and must
 * remain byte-for-byte comparable with its Python export. */
KG_HD static inline void kag_compact_animal_repair(const KGState* game,
        int player_id, KGAction* action) {
    const KGPlayer* farm = &game->players[player_id];
    uint64_t claimed_structures[KG_TILE_WORDS] = {0};
    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitState* unit_state = &farm->units[unit];
        int held_animal = -1;
        for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
            if (unit_state->inventory[KG_ITEM_GOOSE + animal] > 0) {
                held_animal = animal;
                break;
            }
        }
        if (held_animal < 0) continue;
        int structure = KG_ANIMAL_DEFS[held_animal].structure;
        int best = -1;
        int best_distance = 0x7fffffff;
        for (int candidate = 0; candidate < KG_MAX_TILES; candidate++) {
            const KGTile* destination = &farm->tiles[candidate];
            if (destination->kind != structure
                    || destination->animal != KG_ANIMAL_INVALID
                    || (claimed_structures[candidate >> 6]
                        & (1ULL << (candidate & 63)))) continue;
            int tx = candidate % KG_MAX_BOARD_SIZE;
            int ty = candidate / KG_MAX_BOARD_SIZE;
            int dx = (int)unit_state->x - tx;
            int dy = (int)unit_state->y - ty;
            int distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance < best_distance) {
                best = candidate;
                best_distance = distance;
            }
        }
        if (best < 0) continue;
        claimed_structures[best >> 6] |= 1ULL << (best & 63);
        int tx = best % KG_MAX_BOARD_SIZE;
        int ty = best / KG_MAX_BOARD_SIZE;
        KGUnitAction* command = kag_script_unit_action(action, unit);
        if (unit_state->x == tx && unit_state->y == ty) {
            *command = (KGUnitAction){KG_OP_PLACE,
                KG_ITEM_GOOSE + held_animal, 1};
        } else if (unit_state->x != tx) {
            *command = (KGUnitAction){unit_state->x < tx
                ? KG_OP_EAST : KG_OP_WEST, -1, 1};
        } else {
            *command = (KGUnitAction){unit_state->y < ty
                ? KG_OP_SOUTH : KG_OP_NORTH, -1, 1};
        }
    }
}

/*
 * These are intentionally narrow recovery overrides. They only fire when a
 * tape has drifted onto a state that the public replay did not contain:
 * weeds, an unwatered plant, or an animal about to miss its daily basic need.
 * They do not rewrite routing, market ordering, or crop selection.
 */
KG_HD static inline void kag_script_repair(const KGState* game, int player_id,
        int profile, KGAction* action) {
    if (profile == KG_SCRIPT_FRONTIER) return;
    const KGPlayer* farm = &game->players[player_id];
    int units = farm->unit_count;
    for (int unit = 0; unit < units; unit++) {
        const KGUnitState* unit_state = &farm->units[unit];
        const KGTile* tile = &farm->tiles[
            kg_tile_index(unit_state->x, unit_state->y)];
        KGUnitAction* command = kag_script_unit_action(action, unit);
        if (tile->kind == KG_TILE_WEED) {
            *command = (KGUnitAction){KG_OP_DIG, -1, 1};
            continue;
        }
        if (tile->kind == KG_TILE_PLANT && !tile->watered_today) {
            *command = (KGUnitAction){KG_OP_WATER, -1, 1};
            continue;
        }
        if (!kg_is_animal_tile(tile)) continue;
        if (!tile->fed_today && unit_state->inventory[KG_ITEM_WHEAT] > 0) {
            *command = (KGUnitAction){KG_OP_FEED, -1, 1};
            continue;
        }
        if (tile->yield_units > 0 && command->op == KG_OP_PASS) {
            *command = (KGUnitAction){KG_OP_HARVEST, -1, 1};
            continue;
        }
        if (tile->fertilizer_available && command->op == KG_OP_PASS) {
            *command = (KGUnitAction){KG_OP_COLLECT_FERTILIZER, -1, 1};
            continue;
        }
        if (tile->fed_today && !tile->cared_today
                && command->op == KG_OP_PASS) {
            *command = (KGUnitAction){KG_OP_CARE, -1, 1};
        }
    }
}

static inline const char* kag_script_name(int profile) {
    switch (profile) {
        case KG_SCRIPT_FRONTIER: return "frontier";
        case KG_SCRIPT_V20: return "v20";
        case KG_SCRIPT_MOON: return "moon";
        case KG_SCRIPT_HAMBURGER: return "hamburger";
        case KG_SCRIPT_LUGOVOY: return "lugovoy";
        case KG_SCRIPT_THUNDER25: return "thunder25";
        case KG_SCRIPT_TOP: return "top";
        default: return "script";
    }
}

#endif
