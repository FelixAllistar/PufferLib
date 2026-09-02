#ifndef PUFFERLIB_KAGGRICULTURE_PUBLIC_BOTS_H
#define PUFFERLIB_KAGGRICULTURE_PUBLIC_BOTS_H

/*
 * Native ports of three public Kaggriculture policies.
 *
 * These are deliberately planners, not action tapes.  Each call surveys the
 * current native state, rebuilds a bounded job list, assigns workers by
 * distance, and then commits an ordered market queue.  The constants mirror
 * the strategic ideas in the notebooks while the execution is independent of
 * Python, replay timing, and the opponent's private inventory.
 */

#define KG_PUBLIC_MAX_JOBS 256

typedef struct {
    int x;
    int y;
    int priority;
    int op;
    int arg;
} KGPublicJob;

KG_HD static inline KGUnitAction* kag_public_unit_action(KGAction* action, int unit) {
    return unit == 0 ? &action->farmer : &action->hands[unit - 1];
}

KG_HD static inline int kag_public_land_count(const KGPlayer* farm) {
    return kag_popcount((unsigned)farm->unlocked_mask);
}

KG_HD static inline int kag_public_stock(const KGPlayer* farm, int item) {
    int total = item >= 0 && item < KG_NUM_ITEMS ? farm->shed[item] : 0;
    if (item < 0 || item >= KG_NUM_ITEMS) return total;
    for (int unit = 0; unit < farm->unit_count; unit++) {
        total += farm->units[unit].inventory[item];
    }
    return total;
}

KG_HD static inline int kag_public_placed_animals(const KGPlayer* farm, int animal) {
    int count = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (kg_is_animal_tile(tile) && tile->animal == animal) count++;
    }
    return count;
}

KG_HD static inline int kag_public_total_animals(const KGPlayer* farm, int animal) {
    return kag_public_placed_animals(farm, animal)
        + kag_public_stock(farm, KG_ITEM_GOOSE + animal);
}

KG_HD static inline int kag_public_unlocked(const KGPlayer* farm, int x, int y) {
    return (farm->unlocked_mask & kg_quadrant(x, y, KG_OBS_BOARD)) != 0;
}

KG_HD static inline void kag_public_add_job(KGPublicJob jobs[KG_PUBLIC_MAX_JOBS],
        int* count, int x, int y, int priority, int op, int arg) {
    if (*count >= KG_PUBLIC_MAX_JOBS) return;
    jobs[*count] = (KGPublicJob){x, y, priority, op, arg};
    (*count)++;
}

/* The layouts intentionally form the same compact L-shaped opening used by
 * the public economic agents.  Later slots are spread into NE and SW after
 * those quadrants are actually purchased. */
KG_HD static inline void kag_public_structure_position(int slot, int* x, int* y) {
    if (slot < 0) slot = 0;
    if (slot >= 15) slot = 14;
    if (slot < 4) {
        /* Keep the opening herd adjacent to the shed's NW access tile (4,4).
         * The first crew only has one unlocked hand, so routing the first
         * animals to the far corner starves them; a compact L by the shed
         * avoids the wasted movement the public replay does not have. */
        static const int nx[4] = {3, 4, 3, 2};
        static const int ny[4] = {4, 3, 3, 4};
        *x = nx[slot];
        *y = ny[slot];
    } else if (slot < 10) {
        int local = slot - 4;
        *x = local < 4 ? 5 + (local & 1) : 7;
        *y = local < 4 ? local / 2 : local - 4;
    } else {
        int local = slot - 10;
        *x = local == 4 ? 2 : local & 1;
        *y = 5 + (local >= 2 && local < 4);
    }
}

KG_HD static inline int kag_public_animal_target(const KGState* game,
        const KGPlayer* farm, int profile) {
    int day = game->day;
    int placed = 0;
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        placed += kag_public_total_animals(farm, animal);
    }
    int target;
    if (profile == KAG_ADAPTIVE_HARVEST_PULSE) {
        target = day < 3 ? 1 : day < 10 ? 2 : day < 18 ? 3 : 4;
    } else if (profile == KAG_ADAPTIVE_STRUCTURED) {
        target = day < 7 ? 4 : day < 11 ? 11 : day <= 18 ? 15 : placed;
    } else if (profile == KAG_ADAPTIVE_THUNDER) {
        /* THUNDER opens 1 cow + 4 sheep and keeps a cash reserve. The herd
         * expands to 8 after the first land purchase, then sizes the rest by
         * realized animal-product prices in the day-10 window. */
        target = day < 6 ? 5 : day < 10 ? 8 : 12;
        if (day >= 10 && day <= 18) {
            if (game->market.prices[KG_ITEM_WOOL] >= 180) target += 2;
            if (game->market.prices[KG_ITEM_MILK] >= 150) target += 1;
            if (target > 15) target = 15;
        }
    } else {
        target = day < 5 ? 2 : day < 12 ? 3 : day < 20 ? 5 : placed;
    }
    if (day > 18) target = placed;
    if (target < placed) target = placed;
    return target;
}

KG_HD static inline int kag_public_structure_limit(const KGState* game,
        const KGPlayer* farm, int profile) {
    int target = kag_public_animal_target(game, farm, profile);
    if (profile == KAG_ADAPTIVE_HARVEST_PULSE) {
        return target > 2 ? 2 : target;
    }
    if (profile == KAG_ADAPTIVE_TRIAD) {
        return target > 3 ? 3 : target;
    }
    return target > 15 ? 15 : target;
}

KG_HD static inline int kag_public_structure_animal(int profile, int slot) {
    if (profile == KAG_ADAPTIVE_HARVEST_PULSE) return KG_GOOSE;
    if (profile == KAG_ADAPTIVE_TRIAD) {
        return slot == 2 ? KG_SHEEP : KG_COW;
    }
    if (profile == KAG_ADAPTIVE_THUNDER) {
        /* Opening herd is COW, SHEEP, SHEEP, SHEEP, SHEEP; later slots stay
         * cow-heavy, matching THUNDER's 8-12 cow / 4-10 sheep range. */
        if (slot == 0) return KG_COW;
        if (slot <= 4) return KG_SHEEP;
        return (slot % 4 == 0) ? KG_SHEEP : KG_COW;
    }
    /* Structured Economic's opening herd is COW,COW,COW,SHEEP, then
     * alternates toward a cow-heavy mix as capital compounds. */
    if (slot == 3 || (slot >= 7 && slot % 4 == 3)) return KG_SHEEP;
    return KG_COW;
}

KG_HD static inline int kag_public_is_structure(const KGTile* tile) {
    return tile->kind == KG_TILE_COOP || tile->kind == KG_TILE_PASTURE;
}

KG_HD static inline int kag_public_structure_counts(const KGPlayer* farm,
        int* empty_coops, int* empty_pastures) {
    int structures = 0;
    *empty_coops = 0;
    *empty_pastures = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (!kag_public_is_structure(tile)) continue;
        structures++;
        if (tile->animal != KG_ANIMAL_INVALID) continue;
        if (tile->kind == KG_TILE_COOP) (*empty_coops)++;
        else (*empty_pastures)++;
    }
    return structures;
}

/* Count existing structures globally before reserving layout slots. This is
 * important when a reset/profile switch leaves buildings outside this
 * profile's layout: those buildings still consume the structure target. */
KG_HD static inline int kag_public_build_plan(const KGPlayer* farm,
        int profile, int structure_limit, int* coop_builds,
        int* pasture_builds) {
    int existing_coops = 0;
    int existing_pastures = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        existing_coops += tile->kind == KG_TILE_COOP;
        existing_pastures += tile->kind == KG_TILE_PASTURE;
    }
    int desired_coops = 0;
    int desired_pastures = 0;
    for (int slot = 0; slot < structure_limit; slot++) {
        int animal = kag_public_structure_animal(profile, slot);
        if (KG_ANIMAL_DEFS[animal].structure == KG_TILE_COOP) desired_coops++;
        else desired_pastures++;
    }
    int need_coops = desired_coops - existing_coops;
    int need_pastures = desired_pastures - existing_pastures;
    if (need_coops < 0) need_coops = 0;
    if (need_pastures < 0) need_pastures = 0;
    int slots = 0;
    *coop_builds = 0;
    *pasture_builds = 0;
    for (int slot = 0; slot < structure_limit
            && (need_coops > 0 || need_pastures > 0); slot++) {
        int x;
        int y;
        kag_public_structure_position(slot, &x, &y);
        if (!kag_public_unlocked(farm, x, y)) continue;
        const KGTile* tile = &farm->tiles[kg_tile_index(x, y)];
        if (tile->kind != KG_TILE_EMPTY) continue;
        int animal = kag_public_structure_animal(profile, slot);
        int kind = KG_ANIMAL_DEFS[animal].structure;
        if ((kind == KG_TILE_COOP && need_coops <= 0)
                || (kind == KG_TILE_PASTURE && need_pastures <= 0)) {
            continue;
        }
        slots |= 1 << slot;
        if (kind == KG_TILE_COOP) {
            (*coop_builds)++;
            need_coops--;
        } else {
            (*pasture_builds)++;
            need_pastures--;
        }
    }
    return slots;
}

KG_HD static inline void kag_public_desired_animals(int profile,
        int structure_limit, int desired[KG_NUM_ANIMALS]) {
    memset(desired, 0, sizeof(int) * KG_NUM_ANIMALS);
    for (int slot = 0; slot < structure_limit; slot++) {
        int animal = kag_public_structure_animal(profile, slot);
        desired[animal]++;
    }
}

/* Pick from the profile's existing species sequence first, then fall back to
 * compatible stock already purchased under another profile. No incompatible
 * PLACE command is ever emitted. */
KG_HD static inline int kag_public_select_animal(int profile,
        int structure_limit, int structure_kind,
        const int available[KG_NUM_ANIMALS],
        const int need[KG_NUM_ANIMALS]) {
    for (int slot = 0; slot < structure_limit; slot++) {
        int animal = kag_public_structure_animal(profile, slot);
        if (KG_ANIMAL_DEFS[animal].structure == structure_kind
                && need[animal] > 0 && available[animal] > 0) {
            return animal;
        }
    }
    for (int slot = 0; slot < structure_limit; slot++) {
        int animal = kag_public_structure_animal(profile, slot);
        if (KG_ANIMAL_DEFS[animal].structure == structure_kind
                && available[animal] > 0) {
            return animal;
        }
    }
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        if (KG_ANIMAL_DEFS[animal].structure == structure_kind
                && available[animal] > 0) {
            return animal;
        }
    }
    return KG_ANIMAL_INVALID;
}

KG_HD static inline int kag_public_reserved_slot(int profile, int x, int y) {
    (void)profile;
    for (int slot = 0; slot < 15; slot++) {
        int sx;
        int sy;
        kag_public_structure_position(slot, &sx, &sy);
        if (sx == x && sy == y) {
            return slot;
        }
    }
    return -1;
}

KG_HD static inline int kag_public_crop_for(const KGState* game, int profile,
        int x, int y, int local, const int rank[KG_NUM_CROPS]) {
    if (profile == KAG_ADAPTIVE_STRUCTURED) {
        int quadrant = kg_quadrant(x, y, KG_OBS_BOARD);
        int cell = (y % 5) * 5 + (x % 5);
        if (quadrant == 1) {
            if (cell < 10) return KG_MELON;
            if (cell < 14) return KG_WHEAT;
            if (cell < 16) return KG_CARROT;
            return KG_STRAWBERRY;
        }
        if (quadrant == 2 || quadrant == 4) {
            if (cell < 4) return KG_WHEAT;
            if (cell < 5) return KG_CARROT;
            return KG_STRAWBERRY;
        }
        if (cell < 5) return KG_WHEAT;
        if (cell < 7) return KG_CARROT;
        return KG_STRAWBERRY;
    }
    if (profile == KAG_ADAPTIVE_TRIAD) {
        if (local % 5 == 0) return KG_WHEAT;
        if (game->day < 20 && game->market.prices[KG_MELON] >= 180) {
            return KG_MELON;
        }
        return local % 3 == 0 ? KG_CARROT : KG_WHEAT;
    }
    if (profile == KAG_ADAPTIVE_THUNDER) {
        /* Six revenue lines but no carrot/tomato: wheat is the feed lane,
         * melon fills the early premium window, strawberry supplies the
         * mid-season, and everything reverts to wheat for the day-20 spike. */
        int bucket = local % 10;
        if (bucket < 4) return KG_WHEAT;
        if (game->day < 18 && bucket < 6) return KG_MELON;
        if (game->day < 22) return KG_STRAWBERRY;
        return KG_WHEAT;
    }
    /* Harvest Pulse keeps a wheat/feed lane and fills the profitable opening
     * with melons while their first yield still fits in the season. */
    if (local % 4 == 0) return KG_WHEAT;
    if (game->day < 20 && game->market.prices[KG_MELON] >= 160) {
        return KG_MELON;
    }
    return rank[local % 3];
}

KG_HD static inline int kag_public_crop_target(const KGState* game,
        const KGPlayer* farm, int profile, int structure_limit) {
    (void)profile;
    int land = kag_public_land_count(farm);
    int capacity = land * 25 - structure_limit;
    if (capacity < 2) capacity = 2;
    if (profile == KAG_ADAPTIVE_THUNDER) {
        int target = game->day < 4 ? 6
            : game->day < 7 ? 14
            : game->day < 11 ? 24
            : game->day < 20 ? 34 : capacity;
        /* The documented day-20 wheat replant is a whole-farm refill. */
        if (game->day >= 20) target = capacity;
        if (target > capacity) target = capacity;
        return target;
    }
    int target = game->day < 3 ? 4
        : game->day < 7 ? 10
        : game->day < 12 ? 16
        : game->day < 20 ? 22 : capacity;
    if (target > capacity) target = capacity;
    return target;
}

KG_HD static inline int kag_public_job_count(const KGState* game, int player_id,
        int profile, KGPublicJob jobs[KG_PUBLIC_MAX_JOBS],
        int seed_need[KG_NUM_CROPS]) {
    const KGPlayer* farm = &game->players[player_id];
    int count = 0;
    int rank[KG_NUM_CROPS];
    int seed_budget[KG_NUM_CROPS];
    int crop_count = 0;
    int target_crops;
    int structure_limit = kag_public_structure_limit(game, farm, profile);
    memcpy(seed_budget, farm->seeds, sizeof(seed_budget));
    memset(seed_need, 0, sizeof(int) * KG_NUM_CROPS);
    kag_bot_crop_rank(game, player_id, rank);

    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (tile->kind == KG_TILE_PLANT) crop_count++;
        if (tile->kind == KG_TILE_LOCKED) continue;
        int x = tile_id % KG_MAX_BOARD_SIZE;
        int y = tile_id / KG_MAX_BOARD_SIZE;
        int age = game->day - tile->planted_day;
        if (tile->kind == KG_TILE_WEED) {
            kag_public_add_job(jobs, &count, x, y, 1, KG_OP_DIG, -1);
        } else if (tile->kind == KG_TILE_PLANT) {
            const KGCropDef* def = &KG_CROP_DEFS[tile->crop];
            int bonus_window = !def->ongoing
                && age >= (def->max_yield_day + 1) / 2
                && age <= def->max_yield_day;
            if (!tile->watered_today) {
                kag_public_add_job(jobs, &count, x, y,
                    tile->consecutive_unwatered ? 0 : (bonus_window ? 1 : 2),
                    KG_OP_WATER, -1);
            } else if (tile->yield_units > 0
                    && age >= def->first_yield_day) {
                kag_public_add_job(jobs, &count, x, y, 2,
                    KG_OP_HARVEST, -1);
            }
        } else if (kg_is_animal_tile(tile)) {
            if (!tile->fed_today) {
                kag_public_add_job(jobs, &count, x, y, 0, KG_OP_FEED, -1);
            } else if (tile->yield_units > 0) {
                kag_public_add_job(jobs, &count, x, y, 1,
                    KG_OP_HARVEST, -1);
            } else if (!tile->cared_today) {
                kag_public_add_job(jobs, &count, x, y, 2, KG_OP_CARE, -1);
            } else if (tile->fertilizer_available) {
                kag_public_add_job(jobs, &count, x, y, 3,
                    KG_OP_COLLECT_FERTILIZER, -1);
            }
        }
    }

    /* Build only the global structure shortfall, while retaining the source
     * policies' deliberate positions and species sequence. */
    int empty_coops;
    int empty_pastures;
    int structures = kag_public_structure_counts(farm,
        &empty_coops, &empty_pastures);
    int desired_structures = structure_limit;
    int coop_builds;
    int pasture_builds;
    int build_slots = kag_public_build_plan(farm, profile,
        desired_structures, &coop_builds, &pasture_builds);
    for (int slot = 0; slot < desired_structures; slot++) {
        if (!(build_slots & (1 << slot))) continue;
        int x;
        int y;
        kag_public_structure_position(slot, &x, &y);
        int animal = kag_public_structure_animal(profile, slot);
        int op = KG_ANIMAL_DEFS[animal].structure == KG_TILE_COOP
            ? KG_OP_BUILD_COOP : KG_OP_BUILD_PASTURE;
        kag_public_add_job(jobs, &count, x, y, 4, op, -1);
    }

    /* Empty compatible structures are useful wherever they are on the farm,
     * not only when they happen to coincide with the current layout slots. */
    int desired_animals[KG_NUM_ANIMALS];
    int place_need[KG_NUM_ANIMALS];
    int held_animals[KG_NUM_ANIMALS] = {0};
    kag_public_desired_animals(profile, desired_structures, desired_animals);
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        place_need[animal] = desired_animals[animal]
            - kag_public_placed_animals(farm, animal);
        if (place_need[animal] < 0) place_need[animal] = 0;
        int item = KG_ITEM_GOOSE + animal;
        for (int unit = 0; unit < farm->unit_count; unit++) {
            held_animals[animal] += farm->units[unit].inventory[item];
        }
    }
    int planned_coop_places = 0;
    int planned_pasture_places = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (!kag_public_is_structure(tile)
                || tile->animal != KG_ANIMAL_INVALID) {
            continue;
        }
        int animal = kag_public_select_animal(profile, desired_structures,
            tile->kind, held_animals, place_need);
        if (animal == KG_ANIMAL_INVALID) continue;
        int x = tile_id % KG_MAX_BOARD_SIZE;
        int y = tile_id / KG_MAX_BOARD_SIZE;
        kag_public_add_job(jobs, &count, x, y, 3,
            KG_OP_PLACE, KG_ITEM_GOOSE + animal);
        held_animals[animal]--;
        if (place_need[animal] > 0) place_need[animal]--;
        if (tile->kind == KG_TILE_COOP) planned_coop_places++;
        else planned_pasture_places++;
    }

    target_crops = kag_public_crop_target(game, farm, profile,
        structures + coop_builds + pasture_builds);
    int planned = crop_count;
    int local = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES && planned < target_crops;
            tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (tile->kind != KG_TILE_EMPTY) continue;
        int x = tile_id % KG_MAX_BOARD_SIZE;
        int y = tile_id / KG_MAX_BOARD_SIZE;
        if (!kag_public_unlocked(farm, x, y)) continue;
        int reserved = kag_public_reserved_slot(profile, x, y);
        if (reserved >= 0 && (build_slots & (1 << reserved))) continue;
        int crop = kag_public_crop_for(game, profile, x, y, local++, rank);
        const KGCropDef* def = &KG_CROP_DEFS[crop];
        if (game->day + def->first_yield_day >= 29) continue;
        seed_need[crop]++;
        if (seed_budget[crop] > 0) {
            kag_public_add_job(jobs, &count, x, y, 5,
                KG_OP_PLANT, crop);
            seed_budget[crop]--;
            planned++;
        }
    }

    /* Wheat is the shared animal feed resource. One pickup job transfers one
     * wheat, so expose exactly the uncovered shortfall to the executor. */
    int animals = 0;
    int unfed = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (!kg_is_animal_tile(tile)) continue;
        animals++;
        if (!tile->fed_today) unfed++;
    }
    int carried_wheat = 0;
    for (int unit = 0; unit < farm->unit_count; unit++) {
        carried_wheat += farm->units[unit].inventory[KG_ITEM_WHEAT];
    }
    int wheat_pickups = unfed - carried_wheat;
    if (wheat_pickups < 0) wheat_pickups = 0;
    if (wheat_pickups > farm->shed[KG_ITEM_WHEAT]) {
        wheat_pickups = farm->shed[KG_ITEM_WHEAT];
    }
    KGPosition access[4];
    kg_shed_access_count(game->config.board_size, access);
    for (int i = 0; i < wheat_pickups; i++) {
        kag_public_add_job(jobs, &count, access[0].x, access[0].y, 0,
            KG_OP_PICKUP, KG_ITEM_WHEAT);
    }

    int premium_need = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (tile->kind == KG_TILE_PLANT
                && (tile->crop == KG_MELON || tile->crop == KG_STRAWBERRY)) {
            premium_need++;
        }
    }
    if (farm->shed[KG_ITEM_FERTILIZER] > 0 && premium_need > 0) {
        int batches = farm->shed[KG_ITEM_FERTILIZER] > 4
            ? 4 : farm->shed[KG_ITEM_FERTILIZER];
        for (int i = 0; i < batches; i++) {
            kag_public_add_job(jobs, &count, access[0].x, access[0].y, 3,
                KG_OP_PICKUP, KG_ITEM_FERTILIZER);
        }
    }

    /* Only withdraw animal stock that has a currently empty, compatible
     * structure. Planned builds become pickup capacity on the next turn. */
    int animal_stock[KG_NUM_ANIMALS];
    int pickup_count[KG_NUM_ANIMALS] = {0};
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        animal_stock[animal] = farm->shed[KG_ITEM_GOOSE + animal];
    }
    int open_coops = empty_coops - planned_coop_places;
    int open_pastures = empty_pastures - planned_pasture_places;
    while (open_coops > 0) {
        int animal = kag_public_select_animal(profile, desired_structures,
            KG_TILE_COOP, animal_stock, place_need);
        if (animal == KG_ANIMAL_INVALID || pickup_count[animal] >= 3) break;
        kag_public_add_job(jobs, &count, access[0].x, access[0].y, 3,
            KG_OP_PICKUP, KG_ITEM_GOOSE + animal);
        animal_stock[animal]--;
        pickup_count[animal]++;
        if (place_need[animal] > 0) place_need[animal]--;
        open_coops--;
    }
    while (open_pastures > 0) {
        int animal = kag_public_select_animal(profile, desired_structures,
            KG_TILE_PASTURE, animal_stock, place_need);
        if (animal == KG_ANIMAL_INVALID) break;
        if (pickup_count[animal] >= 3) {
            animal_stock[animal] = 0;
            continue;
        }
        kag_public_add_job(jobs, &count, access[0].x, access[0].y, 3,
            KG_OP_PICKUP, KG_ITEM_GOOSE + animal);
        animal_stock[animal]--;
        pickup_count[animal]++;
        if (place_need[animal] > 0) place_need[animal]--;
        open_pastures--;
    }

    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitState* state = &farm->units[unit];
        int non_wheat = 0;
        for (int item = 1; item < KG_NUM_ITEMS; item++) {
            non_wheat += state->inventory[item];
        }
        if (non_wheat > 0 || (state->inventory_order_count > 0
                && game->day >= 28)) {
            kag_public_add_job(jobs, &count, access[0].x, access[0].y, 6,
                KG_OP_DROP, -1);
        }
    }
    (void)animals;
    return count;
}

KG_HD static inline int kag_public_route(const KGPlayer* farm,
        const KGUnitState* unit, int tx, int ty) {
    return kag_bot_route(farm, unit, tx, ty);
}

KG_HD static inline void kag_public_assign_jobs(const KGState* game,
        const KGPlayer* farm, KGPublicJob jobs[KG_PUBLIC_MAX_JOBS],
        int job_count, KGAction* action) {
    (void)game;
    uint64_t claimed[(KG_PUBLIC_MAX_JOBS + 63) / 64] = {0};

    /* Local maintenance is committed before global routing. Claim only the
     * matching local job; the unrelated job this worker used to claim remains
     * available to the rest of the crew. */
    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitState* state = &farm->units[unit];
        const KGUnitAction* command = kag_public_unit_action(action, unit);
        if (command->op == KG_OP_PASS) continue;
        for (int job = 0; job < job_count; job++) {
            if (claimed[job >> 6] & (1ULL << (job & 63))) continue;
            const KGPublicJob* candidate = &jobs[job];
            if (candidate->x == state->x && candidate->y == state->y
                    && candidate->op == command->op
                    && (candidate->arg == command->arg
                        || candidate->arg < 0 || command->arg < 0)) {
                claimed[job >> 6] |= 1ULL << (job & 63);
                break;
            }
        }
    }

    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitState* state = &farm->units[unit];
        KGUnitAction* command = kag_public_unit_action(action, unit);
        if (command->op != KG_OP_PASS) continue;
        int best = -1;
        int best_score = 0x7fffffff;
        for (int job = 0; job < job_count; job++) {
            if (claimed[job >> 6] & (1ULL << (job & 63))) continue;
            const KGPublicJob* candidate = &jobs[job];
            if (candidate->op == KG_OP_FEED
                    && state->inventory[KG_ITEM_WHEAT] <= 0) {
                continue;
            }
            if (candidate->op == KG_OP_PICKUP) {
                if (candidate->arg == KG_ITEM_WHEAT
                        && state->inventory[KG_ITEM_WHEAT] > 0) {
                    continue;
                }
                if (candidate->arg == KG_ITEM_FERTILIZER
                        && state->inventory[KG_ITEM_FERTILIZER] > 0) {
                    continue;
                }
                if (candidate->arg >= KG_ITEM_GOOSE
                        && candidate->arg <= KG_ITEM_SHEEP) {
                    int carrying_animal = 0;
                    for (int item = KG_ITEM_GOOSE;
                            item <= KG_ITEM_SHEEP; item++) {
                        carrying_animal |= state->inventory[item] > 0;
                    }
                    if (carrying_animal) continue;
                }
            }
            if (candidate->op == KG_OP_PLACE) {
                if (candidate->arg < KG_ITEM_GOOSE
                        || candidate->arg > KG_ITEM_SHEEP
                        || state->inventory[candidate->arg] <= 0) {
                    continue;
                }
                const KGTile* target = &farm->tiles[kg_tile_index(
                    candidate->x, candidate->y)];
                int animal = candidate->arg - KG_ITEM_GOOSE;
                if (target->kind != KG_ANIMAL_DEFS[animal].structure
                        || target->animal != KG_ANIMAL_INVALID) {
                    continue;
                }
            }
            int distance = kag_abs((int)state->x - candidate->x)
                + kag_abs((int)state->y - candidate->y);
            int score = candidate->priority * 64 + distance;
            if (score < best_score) {
                best = job;
                best_score = score;
            }
        }
        if (best < 0) continue;
        claimed[best >> 6] |= 1ULL << (best & 63);
        const KGPublicJob* job = &jobs[best];
        if (state->x == job->x && state->y == job->y) {
            *command = (KGUnitAction){job->op, job->arg,
                job->op == KG_OP_DROP ? KG_POLICY_N_ALL : 1};
        } else {
            *command = (KGUnitAction){
                kag_public_route(farm, state, job->x, job->y), -1, 1};
        }
    }
}

KG_HD static inline void kag_public_local_maintenance(const KGState* game,
        const KGPlayer* farm, int profile, KGAction* action) {
    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitState* state = &farm->units[unit];
        const KGTile* tile = &farm->tiles[kg_tile_index(state->x, state->y)];
        KGUnitAction* command = kag_public_unit_action(action, unit);
        if (tile->kind == KG_TILE_WEED) {
            *command = (KGUnitAction){KG_OP_DIG, -1, 1};
        } else if (tile->kind == KG_TILE_PLANT && !tile->watered_today) {
            *command = (KGUnitAction){KG_OP_WATER, -1, 1};
        } else if (kg_is_animal_tile(tile) && !tile->fed_today
                && state->inventory[KG_ITEM_WHEAT] > 0) {
            *command = (KGUnitAction){KG_OP_FEED, -1, 1};
        } else if (kg_is_animal_tile(tile) && tile->yield_units > 0) {
            *command = (KGUnitAction){KG_OP_HARVEST, -1, 1};
        } else if (kg_is_animal_tile(tile) && !tile->cared_today) {
            *command = (KGUnitAction){KG_OP_CARE, -1, 1};
        } else if (kg_is_animal_tile(tile) && tile->fertilizer_available) {
            *command = (KGUnitAction){KG_OP_COLLECT_FERTILIZER, -1, 1};
        } else if (tile->kind == KG_TILE_PLANT
                && state->inventory[KG_ITEM_FERTILIZER] > 0
                && tile->fertilized_until_day < game->day
                && (tile->crop == KG_MELON || tile->crop == KG_STRAWBERRY
                    || profile == KAG_ADAPTIVE_TRIAD)) {
            *command = (KGUnitAction){KG_OP_FERTILIZE, -1, 1};
        }
    }
}

KG_HD static inline int kag_public_add_order(KGAction* action, int limit,
        int op, int item, int n) {
    if (n <= 0 || action->market_count >= limit) return 0;
    action->market[action->market_count++] = (KGMarketOrder){op, item, n};
    return 1;
}

KG_HD static inline int kag_public_sell_cap(int limit) {
    return limit > 5 ? limit - 5 : limit;
}

KG_HD static inline int kag_public_sell_item(int index) {
    switch (index) {
        case 0: return KG_ITEM_MELON;
        case 1: return KG_ITEM_STRAWBERRY;
        case 2: return KG_ITEM_MILK;
        case 3: return KG_ITEM_WOOL;
        case 4: return KG_ITEM_EGG;
        case 5: return KG_ITEM_TOMATO;
        case 6: return KG_ITEM_CARROT;
        case 7: return KG_ITEM_WHEAT;
        default: return KG_ITEM_FERTILIZER;
    }
}

KG_HD static inline void kag_public_market(const KGState* game, int player_id,
        int profile, const int seed_need[KG_NUM_CROPS], KGAction* action) {
    const KGPlayer* farm = &game->players[player_id];
    int limit = game->config.max_market_orders_per_turn;
    if (limit > KG_MAX_MARKET_ORDERS) limit = KG_MAX_MARKET_ORDERS;
    int money = farm->money;
    int animals = 0;
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        animals += kag_public_total_animals(farm, animal);
    }

    int sell_count = 0;
    for (int index = 0; index < KG_NUM_PRODUCTS
            && sell_count < kag_public_sell_cap(limit); index++) {
        int item = kag_public_sell_item(index);
        int amount = farm->shed[item];
        if (item == KG_ITEM_WHEAT) {
            int keep = animals * 2 + 3;
            amount -= keep;
        } else if (item == KG_ITEM_FERTILIZER) {
            int keep = profile == KAG_ADAPTIVE_STRUCTURED ? 2 : 1;
            amount -= keep;
        }
        if (amount <= 0) continue;
        int price = game->market.prices[item];
        int base = KG_MARKET_DEFS[item].base;
        if (price <= 1 && game->day < 28) {
            amount = amount > 2 ? 2 : amount;
        } else if (price * 4 < base && amount > 4) {
            amount = (amount + 1) / 2;
        }
        if (!kag_public_add_order(action, limit, KG_MARKET_SELL,
                item, amount)) continue;
        sell_count++;
        money += amount * price;
    }

    int land = kag_public_land_count(farm);
    int extra = land - 1;
    int expansion_reserve = 150;
    if (profile == KAG_ADAPTIVE_STRUCTURED && game->day < 9) {
        expansion_reserve = extra == 0 ? 1500 : 2500;
    }
    int buy_land = 0;
    if (extra < 2 && game->day < 20) {
        if (profile == KAG_ADAPTIVE_STRUCTURED) {
            buy_land = game->day == 5 || game->day == 9;
        } else if (profile == KAG_ADAPTIVE_HARVEST_PULSE) {
            buy_land = game->day == 7 || game->day == 14;
        } else {
            buy_land = game->day == 8 || game->day == 15;
        }
    }
    int land_price = extra == 0 ? 1000 : extra == 1 ? 2000 : 4000;
    if (buy_land && extra >= 0 && extra < 3
            && money >= land_price + 500) {
        if (kag_public_add_order(action, limit, KG_MARKET_BUY_LAND, -1, 1)) {
            money -= land_price;
            land++;
        }
    }

    int target_animals = kag_public_animal_target(game, farm, profile);
    int structure_limit = kag_public_structure_limit(game, farm, profile);
    int empty_coops;
    int empty_pastures;
    kag_public_structure_counts(farm, &empty_coops, &empty_pastures);
    int coop_builds;
    int pasture_builds;
    kag_public_build_plan(farm, profile, structure_limit,
        &coop_builds, &pasture_builds);
    int missing = target_animals - animals;
    if (missing > 0 && game->day <= 18) {
        int cow_total = kag_public_total_animals(farm, KG_COW);
        int sheep_total = kag_public_total_animals(farm, KG_SHEEP);
        int type = KG_COW;
        if (profile == KAG_ADAPTIVE_HARVEST_PULSE) type = KG_GOOSE;
        else if (profile == KAG_ADAPTIVE_TRIAD && sheep_total < 1
                && cow_total >= 2) type = KG_SHEEP;
        else if (profile == KAG_ADAPTIVE_STRUCTURED
                && sheep_total * 3 < cow_total + sheep_total) type = KG_SHEEP;
        /* Empty pastures are shared cow/sheep capacity; subtract all already
         * purchased compatible stock before buying another animal. */
        int animal_capacity;
        if (KG_ANIMAL_DEFS[type].structure == KG_TILE_COOP) {
            animal_capacity = empty_coops + coop_builds
                - kag_public_stock(farm, KG_ITEM_GOOSE);
        } else {
            animal_capacity = empty_pastures + pasture_builds
                - kag_public_stock(farm, KG_ITEM_COW)
                - kag_public_stock(farm, KG_ITEM_SHEEP);
        }
        if (animal_capacity < 0) animal_capacity = 0;
        if (missing > animal_capacity) missing = animal_capacity;
        int item = KG_ITEM_GOOSE + type;
        int cost = KG_ANIMAL_DEFS[type].cost;
        int reserve = profile == KAG_ADAPTIVE_STRUCTURED ? 250 : 350;
        if (reserve < expansion_reserve) reserve = expansion_reserve;
        int affordable = (money - reserve) / cost;
        if (affordable > missing) affordable = missing;
        if (affordable > 2) affordable = 2;
        if (affordable > 0) {
            kag_public_add_order(action, limit, KG_MARKET_BUY_ANIMAL,
                item, affordable);
            money -= affordable * cost;
        }
    }

    int wheat = kag_public_stock(farm, KG_ITEM_WHEAT);
    int planned_animals = animals + (target_animals - animals > 0
        ? target_animals - animals : 0);
    int wheat_target = planned_animals * 2 + 3;
    int wheat_missing = wheat_target - wheat;
    if (wheat_missing > 0 && planned_animals > 0
            && action->market_count < limit) {
        int price = game->market.prices[KG_ITEM_WHEAT];
        int affordable = price > 0 ? (money - expansion_reserve) / price : 0;
        if (affordable < wheat_missing) wheat_missing = affordable;
        if (wheat_missing > 0) {
            kag_public_add_order(action, limit, KG_MARKET_BUY_PRODUCT,
                KG_ITEM_WHEAT, wheat_missing);
            money -= wheat_missing * price;
        }
    }

    for (int crop = 0; crop < KG_NUM_CROPS && action->market_count < limit;
            crop++) {
        int missing_seeds = seed_need[crop] - farm->seeds[crop];
        int price = KG_CROP_DEFS[crop].seed_cost;
        int affordable = price > 0 ? (money - expansion_reserve) / price : 0;
        if (missing_seeds > affordable) missing_seeds = affordable;
        if (missing_seeds > 0) {
            kag_public_add_order(action, limit, KG_MARKET_BUY_SEED,
                crop, missing_seeds);
            money -= missing_seeds * price;
        }
    }

    if (profile == KAG_ADAPTIVE_STRUCTURED && game->day >= 10
            && game->day < 20 && farm->shed[KG_ITEM_FERTILIZER] < 2
            && money >= expansion_reserve + 450
            && action->market_count < limit) {
        kag_public_add_order(action, limit, KG_MARKET_BUY_PRODUCT,
            KG_ITEM_FERTILIZER, 1);
        money -= KG_MARKET_DEFS[KG_ITEM_FERTILIZER].base;
    }

    int desired_hands = profile == KAG_ADAPTIVE_HARVEST_PULSE ? 7
        : profile == KAG_ADAPTIVE_STRUCTURED ? 12 : 6;
    if (game->day < 29) {
        int hires = farm->hand_count;
        int hires_today = farm->hires_today;
        while (hires < desired_hands && action->market_count < limit) {
            int cost = kg_hire_cost(hires_today,
                game->config.farm_hand_cost_mult);
            if (money < cost + expansion_reserve) break;
            kag_public_add_order(action, limit, KG_MARKET_HIRE, -1, 1);
            money -= cost;
            hires++;
            hires_today++;
        }
    }
    (void)land;
}

KG_HD static inline void kag_public_action(const KGState* game, int player_id,
        int profile, KGAction* action) {
    const KGPlayer* farm = &game->players[player_id];
    action->hand_count = 0;
    action->market_count = 0;
    action->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
    action->hand_count = farm->hand_count;
    for (int hand = 0; hand < action->hand_count; hand++) {
        action->hands[hand] = (KGUnitAction){KG_OP_PASS, -1, 1};
    }
    KGPublicJob jobs[KG_PUBLIC_MAX_JOBS];
    int seed_need[KG_NUM_CROPS];
    int job_count = kag_public_job_count(game, player_id, profile,
        jobs, seed_need);
    kag_public_local_maintenance(game, farm, profile, action);
    kag_public_assign_jobs(game, farm, jobs, job_count, action);
    kag_public_market(game, player_id, profile, seed_need, action);
}

/* THUNDER THUNDER's price-adaptive portfolio. The opening and land schedule
 * are fixed; the herd and crop mix are then sized by realized market prices.
 * This keeps the behavior as a planner (not a replay) so it survives state
 * drift in competitive self-play. */
KG_HD static inline int kag_thunder_sell_item(int index) {
    switch (index) {
        case 0: return KG_ITEM_FERTILIZER;
        case 1: return KG_ITEM_WOOL;
        case 2: return KG_ITEM_MELON;
        case 3: return KG_ITEM_STRAWBERRY;
        case 4: return KG_ITEM_MILK;
        case 5: return KG_ITEM_EGG;
        case 6: return KG_ITEM_WHEAT;
        default: return KG_ITEM_CARROT;
    }
}

KG_HD static inline void kag_thunder_market(const KGState* game, int player_id,
        const int seed_need[KG_NUM_CROPS], KGAction* action) {
    const KGPlayer* farm = &game->players[player_id];
    int limit = game->config.max_market_orders_per_turn;
    if (limit > KG_MAX_MARKET_ORDERS) limit = KG_MAX_MARKET_ORDERS;
    int money = farm->money;
    int day = game->day;

    /* Modest opening: the wheat feed buffer is bought first (the public
     * 8c/4s replay buys 14 wheat and then fills the basket from the remainder),
     * so the first animals do not starve before crops and fertilizer income
     * come online. */
    if (day == 0 && farm->hand_count == 0 && farm->hires_today == 0) {
        kag_public_add_order(action, limit, KG_MARKET_BUY_PRODUCT,
            KG_ITEM_WHEAT, 14);
        for (int i = 0; i < 4; i++) {
            kag_public_add_order(action, limit, KG_MARKET_HIRE, -1, 1);
        }
        kag_public_add_order(action, limit, KG_MARKET_BUY_ANIMAL,
            KG_ITEM_COW, 1);
        kag_public_add_order(action, limit, KG_MARKET_BUY_ANIMAL,
            KG_ITEM_SHEEP, 4);
        kag_public_add_order(action, limit, KG_MARKET_BUY_SEED, KG_MELON, 5);
        kag_public_add_order(action, limit, KG_MARKET_BUY_SEED, KG_WHEAT, 5);
        kag_public_add_order(action, limit, KG_MARKET_HIRE, -1, 1);
        return;
    }

    int animals = 0;
    int cows = kag_public_total_animals(farm, KG_COW);
    int sheep = kag_public_total_animals(farm, KG_SHEEP);
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        animals += kag_public_total_animals(farm, animal);
    }

    /* Fertilizer is first-come-first-served; race it, then impact-order the
     * premium lines, and reserve wheat until the day-24+ endgame dump. */
    int sell_count = 0;
    for (int index = 0; index < KG_NUM_PRODUCTS
            && sell_count < kag_public_sell_cap(limit); index++) {
        int item = kag_thunder_sell_item(index);
        int amount = farm->shed[item];
        if (item == KG_ITEM_WHEAT) {
            if (day < 23) {
                amount = 0;
            } else {
                amount -= animals * 2 + 3;
            }
        } else if (item == KG_ITEM_FERTILIZER) {
            amount -= 2;
        }
        if (amount <= 0) continue;
        int price = game->market.prices[item];
        int base = KG_MARKET_DEFS[item].base;
        if (price <= 1 && day < 28) {
            amount = amount > 2 ? 2 : amount;
        } else if (price * 4 < base && amount > 4) {
            amount = (amount + 1) / 2;
        }
        if (!kag_public_add_order(action, limit, KG_MARKET_SELL,
                item, amount)) {
            continue;
        }
        sell_count++;
        money += amount * price;
    }

    int land = kag_public_land_count(farm);
    int extra = land - 1;
    int land_price = extra == 0 ? 1000 : extra == 1 ? 2000 : 4000;
    if (extra < 2 && (day == 6 || day == 10)
            && money >= land_price + 500) {
        if (kag_public_add_order(action, limit, KG_MARKET_BUY_LAND, -1, 1)) {
            money -= land_price;
            land++;
        }
    }

    int target_animals = kag_public_animal_target(game, farm,
        KAG_ADAPTIVE_THUNDER);
    int structure_limit = kag_public_structure_limit(game, farm,
        KAG_ADAPTIVE_THUNDER);
    int empty_coops;
    int empty_pastures;
    kag_public_structure_counts(farm, &empty_coops, &empty_pastures);
    int coop_builds;
    int pasture_builds;
    kag_public_build_plan(farm, KAG_ADAPTIVE_THUNDER, structure_limit,
        &coop_builds, &pasture_builds);
    int missing = target_animals - animals;
    if (missing > 0 && day >= 1 && day <= 18) {
        int type = KG_COW;
        if (sheep < 4) {
            type = KG_SHEEP;
        } else if (cows >= 12) {
            type = KG_SHEEP;
        } else if (sheep < 7 && cows >= 8
                && game->market.prices[KG_ITEM_WOOL] >= 180) {
            type = KG_SHEEP;
        }
        int animal_capacity;
        if (KG_ANIMAL_DEFS[type].structure == KG_TILE_COOP) {
            animal_capacity = empty_coops + coop_builds
                - kag_public_stock(farm, KG_ITEM_GOOSE);
        } else {
            animal_capacity = empty_pastures + pasture_builds
                - kag_public_stock(farm, KG_ITEM_COW)
                - kag_public_stock(farm, KG_ITEM_SHEEP);
        }
        if (animal_capacity < 0) animal_capacity = 0;
        if (missing > animal_capacity) missing = animal_capacity;
        int item = KG_ITEM_GOOSE + type;
        int cost = KG_ANIMAL_DEFS[type].cost;
        int reserve = 350;
        int affordable = (money - reserve) / cost;
        if (affordable > missing) affordable = missing;
        if (affordable > 2) affordable = 2;
        if (affordable > 0) {
            kag_public_add_order(action, limit, KG_MARKET_BUY_ANIMAL,
                item, affordable);
            money -= affordable * cost;
        }
    }

    int wheat = kag_public_stock(farm, KG_ITEM_WHEAT);
    int wheat_target = animals * 2 + 3;
    int wheat_missing = wheat_target - wheat;
    if (wheat_missing > 0 && animals > 0 && action->market_count < limit) {
        int price = game->market.prices[KG_ITEM_WHEAT];
        int affordable = price > 0 ? (money - 350) / price : 0;
        if (affordable < wheat_missing) wheat_missing = affordable;
        if (wheat_missing > 0) {
            kag_public_add_order(action, limit, KG_MARKET_BUY_PRODUCT,
                KG_ITEM_WHEAT, wheat_missing);
            money -= wheat_missing * price;
        }
    }

    for (int crop = 0; crop < KG_NUM_CROPS && action->market_count < limit;
            crop++) {
        if (crop == KG_CARROT || crop == KG_TOMATO) continue;
        int missing_seeds = seed_need[crop] - farm->seeds[crop];
        int price = KG_CROP_DEFS[crop].seed_cost;
        int affordable = price > 0 ? (money - 350) / price : 0;
        if (missing_seeds > affordable) missing_seeds = affordable;
        if (missing_seeds > 0) {
            kag_public_add_order(action, limit, KG_MARKET_BUY_SEED,
                crop, missing_seeds);
            money -= missing_seeds * price;
        }
    }

    int desired_hands = day < 7 ? 4 : day < 10 ? 7 : 14;
    if (day < 29) {
        int hires = farm->hand_count;
        int hires_today = farm->hires_today;
        while (hires < desired_hands && action->market_count < limit) {
            int cost = kg_hire_cost(hires_today,
                game->config.farm_hand_cost_mult);
            if (money < cost + 350) break;
            kag_public_add_order(action, limit, KG_MARKET_HIRE, -1, 1);
            money -= cost;
            hires++;
            hires_today++;
        }
    }
    (void)land;
}

KG_HD static inline void kag_thunder_action(const KGState* game, int player_id,
        KGAction* action) {
    const KGPlayer* farm = &game->players[player_id];
    action->hand_count = 0;
    action->market_count = 0;
    action->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
    action->hand_count = farm->hand_count;
    for (int hand = 0; hand < action->hand_count; hand++) {
        action->hands[hand] = (KGUnitAction){KG_OP_PASS, -1, 1};
    }
    KGPublicJob jobs[KG_PUBLIC_MAX_JOBS];
    int seed_need[KG_NUM_CROPS];
    int job_count = kag_public_job_count(game, player_id,
        KAG_ADAPTIVE_THUNDER, jobs, seed_need);
    kag_public_local_maintenance(game, farm, KAG_ADAPTIVE_THUNDER, action);
    kag_public_assign_jobs(game, farm, jobs, job_count, action);
    kag_thunder_market(game, player_id, seed_need, action);
}

#endif
