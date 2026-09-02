"""PufferLib Kaggriculture submission: native MinGRU policy, NumPy runtime.

This is a direct adapter for the semantic 1280-byte observation and conditional
47-head action tree used by the native C trainer (17 unit + ten 3-head market
slots). Each of the sixteen supported farm hands has an independent head.
The accompanying checkpoint contains only our trained
PufferLib policy weights.
"""

import math
import os
import sys

import numpy as np


ITEMS = (
    "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON",
    "EGG", "MILK", "WOOL", "FERTILIZER", "GOOSE", "COW", "SHEEP",
)
CROPS = ITEMS[:5]
PRODUCTS = ITEMS[:9]
ANIMALS = ITEMS[9:]
ITEM_ID = {name: i for i, name in enumerate(ITEMS)}
CROP_ID = {name: i for i, name in enumerate(CROPS)}
ANIMAL_ID = {name: i for i, name in enumerate(ANIMALS)}
SHOP_ID = {name: i for i, name in enumerate((
    "BAKERY", "BRUNCH_SPOT", "FARMERS_MARKET", "ICE_CREAM_SHOP",
    "PET_CAFE", "PIZZA_SHOP", "SMOOTHIE_SHOP", "YARN_STORE",
))}
QUADRANT_BITS = {"NW": 1, "NE": 2, "SW": 4, "SE": 8}
SEED_COST = (10, 20, 50, 100, 80)
ANIMAL_COST = (300, 400, 500)
MARKET_THROUGHPUT = (400, 450, 200, 100, 300, 332, 122, 105, 200)
MARKET_BASE = (25, 35, 60, 120, 250, 50, 160, 200, 100)
MARKET_BELOW_FUNC = ("sqrt", "hinge", "hinge", "sqrt", "log",
                     "hinge", "sqrt", "log", "linear")
MARKET_BELOW_TARGET = (.80, 1.00, .40, .70, .20, .40, .60, .20, .40)
MARKET_ABOVE_FUNC = ("log", "sqrt", "sqrt", "linear", "sq",
                     "log", "linear", "sq", "linear")
MARKET_ABOVE_TARGET = (.20, .70, .60, 1.60, 3.60, .20, 1.60, 3.20, .40)
FIRST_YIELD_DAY = (2, 2, 8, 10, 10)
ANIMAL_STRUCTURE = ("COOP", "PASTURE", "PASTURE")

OBS_SIZE = 1280
HIDDEN_SIZE = 32
NUM_LAYERS = 2
DIRECT_HANDS = 16
OVERFLOW_COHORTS = 0
UNIT_HEADS = 1 + DIRECT_HANDS + OVERFLOW_COHORTS
ALL = 0x7FFFFFFF
MARKET_SLOTS = 10
MARKET_COMMANDS = 21
MARKET_QUANTITIES = (1, 2, 3, 4, 5, 6, 8, 10)
HEAD_SIZES = (44,) * UNIT_HEADS + (2, MARKET_COMMANDS, 8) * MARKET_SLOTS
HEAD_OFFSETS = tuple(np.cumsum((0,) + HEAD_SIZES))
MASK_SIZE = HEAD_OFFSETS[-1]
MAX_HANDS = DIRECT_HANDS
# encoder + decoder(mask+value) + 2 MinGRU layers

def _infer_arch(float_count):
    """Infer (hidden, layers) from the flat checkpoint size."""
    for layers in range(1, 9):
        for hidden in (16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512,
                768, 1024):
            total = hidden * OBS_SIZE + (MASK_SIZE + 1) * hidden
            total += layers * 3 * hidden * hidden
            if total == float_count:
                return hidden, layers
    raise RuntimeError(f"Unexpected PufferLib checkpoint shape: {float_count} floats")


def _get(obj, key, default=None):
    if obj is None:
        return default
    try:
        return obj.get(key, default)
    except AttributeError:
        return getattr(obj, key, default)


def _u8(value):
    return max(0, min(255, int(value)))


def _u8_scale(value, scale):
    value = int(value)
    if value <= 0:
        return 0
    if value >= scale:
        return 255
    return (value * 255) // scale


def _u8_signed(value, magnitude):
    value = int(value)
    if value <= -magnitude:
        return 0
    if value >= magnitude:
        return 255
    return ((value + magnitude) * 255) // (2 * magnitude)


def _unlocked_mask(farm):
    mask = 0
    for name in _get(farm, "unlocked_quadrants", ()):
        mask |= QUADRANT_BITS.get(name, 0)
    return mask


def _tile_entity(tile):
    if tile is None:
        return 0
    if tile == "LOCKED":
        return 1
    kind = _get(tile, "kind", "")
    if kind == "WEED":
        return 2
    if kind == "PLANT":
        return 5 + CROP_ID.get(_get(tile, "crop", ""), 0)
    if kind in ("COOP", "PASTURE"):
        animal = _get(tile, "animal")
        if animal in ANIMAL_ID:
            return 10 + ANIMAL_ID[animal]
        return 3 if kind == "COOP" else 4
    return 0


def _occupied_animal(tile):
    return (_get(tile, "kind") in ("COOP", "PASTURE")
            and _get(tile, "animal") in ANIMAL_ID)


def _positions(farm):
    return [list(_get(farm, "farmer", (4, 4)))] + [
        list(pos) for pos in _get(farm, "hands", ())
    ]


def _inventory(private, unit_id):
    inventories = _get(private, "inventories", ())
    return inventories[unit_id] if unit_id < len(inventories) else {}


def _encode_tile(tile, day):
    entity = _tile_entity(tile)
    kind = _get(tile, "kind", "")
    if kind == "PLANT":
        age = day - int(_get(tile, "planted_day", day))
        yield_units = min(63, max(0, int(_get(tile, "yield_units", 0))))
        watered = int(bool(_get(tile, "watered_today", False)))
        neglect = min(3, max(0, int(_get(tile, "consecutive_unwatered", 0))))
        until = int(_get(tile, "fertilized_until_day", -1))
        fertilized = min(7, until - day + 1) if until >= day else 0
        at_risk = neglect > 0 and not watered
        return (_u8_scale(entity, 12), _u8_scale(age, 30),
                _u8_scale(yield_units, 63) // 2 + watered * 128,
                at_risk * 128 + fertilized * 16)
    if _occupied_animal(tile):
        age = day - int(_get(tile, "placed_day", day))
        yield_units = min(63, max(0, int(_get(tile, "yield_units", 0))))
        fed = int(bool(_get(tile, "fed_today", False)))
        cared = int(bool(_get(tile, "cared_today", False)))
        neglect = min(3, max(0, int(_get(tile, "consecutive_unfed", 0))))
        available = int(bool(_get(tile, "fertilizer_available", False)))
        pending = min(31, max(0, int(_get(tile, "pending_care_bonus", 0))))
        at_risk = neglect > 0 and not fed
        return (_u8_scale(entity, 12), _u8_scale(age, 30),
                _u8_scale(yield_units, 63) // 2 + fed * 128,
                at_risk * 128 + cared * 64 + available * 32
                + min(15, pending) * 2)
    return _u8_scale(entity, 12), 0, 0, 0


def _encode_unit_routes(farm, ux, uy, day):
    tiles = _get(farm, "tiles", ())
    current = tiles[uy][ux]
    kind = _get(current, "kind", "")
    status = 0
    if current is None:
        status |= 128
    elif kind == "WEED":
        status |= 64
    elif kind == "PLANT":
        watered = bool(_get(current, "watered_today", False))
        if not watered:
            status |= 128
        crop = CROP_ID.get(_get(current, "crop", ""), 0)
        if (int(_get(current, "yield_units", 0)) > 0
                and day - int(_get(current, "planted_day", day))
                >= FIRST_YIELD_DAY[crop]):
            status |= 64
        if int(_get(current, "consecutive_unwatered", 0)) > 0 \
                and not watered:
            status |= 32
    elif _occupied_animal(current):
        if not bool(_get(current, "fed_today", False)):
            status |= 128
        if int(_get(current, "yield_units", 0)) > 0:
            status |= 64
        if not bool(_get(current, "cared_today", False)):
            status |= 32
        if bool(_get(current, "fertilizer_available", False)):
            status |= 16
    if _shed_adjacent(ux, uy):
        status |= 8

    best = [[999, ux, uy] for _ in range(5)]
    present = 0
    for y, row in enumerate(tiles):
        for x, tile in enumerate(row):
            tile_kind = _get(tile, "kind", "")
            distance = abs(x - ux) + abs(y - uy)
            maintain = ((tile_kind == "PLANT"
                         and not bool(_get(tile, "watered_today", False)))
                        or (_occupied_animal(tile)
                            and (not bool(_get(tile, "fed_today", False))
                                 or not bool(_get(tile, "cared_today", False)))))
            if maintain and distance < best[0][0]:
                best[0] = [distance, x, y]
                present |= 1
            # Native routing assigns each tile one class. Maintenance wins
            # over harvest when an entity currently needs water/feed/care.
            harvestable = (not maintain
                           and int(_get(tile, "yield_units", 0)) > 0)
            if tile_kind == "PLANT":
                crop = CROP_ID.get(_get(tile, "crop", ""), 0)
                harvestable &= (day - int(_get(tile, "planted_day", day))
                                >= FIRST_YIELD_DAY[crop])
            else:
                harvestable &= _occupied_animal(tile)
            if harvestable and distance < best[1][0]:
                best[1] = [distance, x, y]
                present |= 2
            if tile is None and distance < best[2][0]:
                best[2] = [distance, x, y]
                present |= 4
            if tile_kind == "WEED" and distance < best[3][0]:
                best[3] = [distance, x, y]
                present |= 8
    for x, y in ((4, 4), (5, 4), (4, 5), (5, 5)):
        distance = abs(x - ux) + abs(y - uy)
        if distance < best[4][0]:
            best[4] = [distance, x, y]
    present |= 16
    result = [_u8_scale(_tile_entity(current), 12), status, present * 8]
    for _, x, y in best:
        result.extend((_u8_signed(x - ux, 9), _u8_signed(y - uy, 9)))
    return result


def _view_units(view, count):
    if view <= DIRECT_HANDS:
        return [view] if view < count else []
    if OVERFLOW_COHORTS == 0:
        return []
    first = 1 + DIRECT_HANDS + view - (1 + DIRECT_HANDS)
    return list(range(first, count, OVERFLOW_COHORTS))


def _encode_observation_v2(obs):
    player = int(_get(obs, "player", 0))
    farms = _get(obs, "farms", ())
    me = farms[player]
    opponent = farms[1 - player]
    private = _get(obs, "private", {})
    day = int(_get(obs, "day", 0))
    hour = int(_get(obs, "hour", 0))
    step = int(_get(obs, "step", day * 24 + hour))
    me_money = int(_get(me, "money", 0))
    opp_money = int(_get(opponent, "money", 0))
    own_positions = _positions(me)
    opp_positions = _positions(opponent)
    out = np.zeros(OBS_SIZE, dtype=np.uint8)
    k = 0

    global_features = (
        _u8_scale(me_money, 20000),
        _u8_scale(opp_money, 20000),
        _u8_signed(me_money - opp_money, 20000),
        _u8_scale(step, 720),
        _u8_scale(day, 29), _u8_scale(hour, 23),
        _u8_scale(_unlocked_mask(me), 15),
        _u8_scale(_unlocked_mask(opponent), 15),
        _u8_scale(_get(me, "hires_today", 0), 16),
        _u8_scale(_get(opponent, "hires_today", 0), 16),
        _u8_scale(len(own_positions), 16),
        _u8_scale(len(opp_positions), 16),
        sum(1 << SHOP_ID[name] for name in _get(_get(obs, "town", {}),
            "unlocked_shops", ()) if name in SHOP_ID),
    )
    out[k:k + 13] = global_features
    k += 13

    summary = [0] * 32
    for view_player, farm in enumerate((me, opponent)):
        base = 0 if view_player == 0 else 20
        for y, row in enumerate(_get(farm, "tiles", ())):
            for x, tile in enumerate(row):
                out[k:k + 4] = _encode_tile(tile, day)
                k += 4
                kind = _get(tile, "kind", "")
                if tile is None:
                    summary[base] += 1
                elif kind == "WEED":
                    summary[base + 1] += 1
                elif kind == "PLANT":
                    summary[base + 2] += 1
                    watered = bool(_get(tile, "watered_today", False))
                    if int(_get(tile, "consecutive_unwatered", 0)) > 0 \
                            and not watered:
                        summary[base + 5] += 1
                    if not watered:
                        summary[base + 6] += 1
                    if view_player == 0:
                        crop = CROP_ID.get(_get(tile, "crop", ""), -1)
                        if crop >= 0:
                            summary[10 + crop] += 1
                            if int(_get(tile, "yield_units", 0)) > 0:
                                summary[15 + crop] += 1
                elif _occupied_animal(tile):
                    summary[base + 3] += 1
                    fed = bool(_get(tile, "fed_today", False))
                    if int(_get(tile, "consecutive_unfed", 0)) > 0 and not fed:
                        summary[base + 7] += 1
                    if not fed:
                        summary[base + 8] += 1
                elif kind in ("COOP", "PASTURE"):
                    summary[base + 4] += 1
                if view_player == 0 and (x >= 5 or y >= 5) \
                        and (kind == "PLANT" or _occupied_animal(tile)):
                    summary[9] += 1

    shed = _get(private, "shed", {})
    seeds = _get(private, "seeds", {})
    summary[29] = sum(int(_get(shed, name, 0)) for name in ITEMS)
    summary[30] = sum(int(_get(seeds, name, 0)) for name in CROPS)
    summary[31] = sum(int(_get(_inventory(private, unit), name, 0))
                      for unit in range(len(own_positions)) for name in ITEMS)
    out[k:k + 32] = [_u8_scale(value, 100) for value in summary]
    k += 32

    for name in ITEMS:
        out[k] = _u8_scale(_get(shed, name, 0), 100)
        k += 1
    for name in CROPS:
        out[k] = _u8_scale(_get(seeds, name, 0), 100)
        k += 1

    for view in range(UNIT_HEADS):
        unit_ids = _view_units(view, len(own_positions))
        if unit_ids:
            present = 0
            wheat = 0
            fertilizer = 0
            for unit_id in unit_ids:
                inv = _inventory(private, unit_id)
                for item_id, name in enumerate(ITEMS):
                    count = int(_get(inv, name, 0))
                    if count:
                        present |= 1 << item_id
                    if name == "WHEAT":
                        wheat += count
                    elif name == "FERTILIZER":
                        fertilizer += count
            out[k:k + 7] = (
                _u8_scale(len(unit_ids), 16),
                _u8_scale(sum(own_positions[i][0] for i in unit_ids)
                          // len(unit_ids), 9),
                _u8_scale(sum(own_positions[i][1] for i in unit_ids)
                          // len(unit_ids), 9),
                present & 255, present >> 8,
                _u8_scale(wheat, 100), _u8_scale(fertilizer, 100),
            )
            ux = sum(own_positions[i][0] for i in unit_ids) // len(unit_ids)
            uy = sum(own_positions[i][1] for i in unit_ids) // len(unit_ids)
            out[k + 7:k + 20] = _encode_unit_routes(me, ux, uy, day)
        k += 20

    for view in range(UNIT_HEADS):
        unit_ids = _view_units(view, len(opp_positions))
        if unit_ids:
            out[k:k + 3] = (
                _u8_scale(len(unit_ids), 16),
                _u8_scale(sum(opp_positions[i][0] for i in unit_ids)
                          // len(unit_ids), 9),
                _u8_scale(sum(opp_positions[i][1] for i in unit_ids)
                          // len(unit_ids), 9),
            )
        k += 3

    market = _get(obs, "market", {})
    inventory = _get(market, "inventory", {})
    prices = _get(market, "prices", {})
    for item_id, name in enumerate(PRODUCTS):
        out[k] = _u8_signed(_get(inventory, name, 0) - 10000,
                            MARKET_THROUGHPUT[item_id] * 4)
        k += 1
    for name in PRODUCTS:
        out[k] = _u8_scale(_get(prices, name, 0), 1000)
        k += 1
    if k != OBS_SIZE:
        raise RuntimeError("Kaggriculture observation ABI mismatch")
    return out


def encode_observation(obs):
    """Mirror kag_write_observation byte-for-byte; trailing bytes are padding."""
    player = int(_get(obs, "player", 0))
    farms = _get(obs, "farms", ())
    me, opponent = farms[player], farms[1 - player]
    private = _get(obs, "private", {})
    day = int(_get(obs, "day", 0))
    hour = int(_get(obs, "hour", 0))
    step = int(_get(obs, "step", day * 24 + hour))
    episode_steps, turns_per_day = 720, 24
    season_days = episode_steps // turns_per_day
    own_positions, opp_positions = _positions(me), _positions(opponent)
    out = np.zeros(OBS_SIZE, dtype=np.uint8)
    k = 0

    def put(value):
        nonlocal k
        out[k] = value
        k += 1

    own_money = int(_get(me, "money", 0))
    opp_money = int(_get(opponent, "money", 0))
    put(_u8_scale(own_money, 100000))
    put(_u8_scale(opp_money, 100000))
    put(_u8_signed(own_money - opp_money, 100000))
    put(_u8_scale(step, episode_steps))
    put(_u8_scale(day, season_days - 1))
    put(_u8_scale(hour, turns_per_day - 1))
    put(_u8_scale(episode_steps - step, episode_steps))
    for unlocked in (_unlocked_mask(me), _unlocked_mask(opponent)):
        for bit in range(4):
            put(255 if unlocked & (1 << bit) else 0)
    put(_u8_scale(_get(me, "hires_today", 0), 16))
    put(_u8_scale(_get(opponent, "hires_today", 0), 16))
    put(_u8_scale(len(own_positions), 16))
    put(_u8_scale(len(opp_positions), 16))
    shops = 0
    for name in _get(_get(obs, "town", {}), "unlocked_shops", ()):
        if name in SHOP_ID:
            shops |= 1 << SHOP_ID[name]
    for bit in range(8):
        put(255 if shops & (1 << bit) else 0)
    season_phase = min(3, 4 * day // season_days)
    for phase in range(4):
        put(255 if phase == season_phase else 0)

    # Public farm summaries have a stable absolute-player ordering in the
    # native ABI. Private/global fields above remain observer-relative.
    for farm in farms:
        entity = [[0] * 13 for _ in range(4)]
        state = [[0] * 8 for _ in range(4)]
        crop_state = [[0] * 6 for _ in CROPS]
        animal_state = [[0] * 6 for _ in ANIMALS]
        for y, row in enumerate(_get(farm, "tiles", ())):
            for x, tile in enumerate(row):
                quadrant = (x >= 5) + 2 * (y >= 5)
                entity[quadrant][_tile_entity(tile)] += 1
                kind = _get(tile, "kind", "")
                if kind == "PLANT":
                    crop = CROP_ID.get(_get(tile, "crop", ""), -1)
                    if crop < 0:
                        continue
                    age = day - int(_get(tile, "planted_day", day))
                    needs = not bool(_get(tile, "watered_today", False))
                    risk = needs and int(_get(tile, "consecutive_unwatered", 0)) > 0
                    yield_units = int(_get(tile, "yield_units", 0))
                    harvestable = yield_units > 0 and age >= FIRST_YIELD_DAY[crop]
                    cared = int(_get(tile, "fertilized_until_day", -1)) >= day
                    for feature, value in enumerate(
                            (age, yield_units, needs, risk, harvestable, cared)):
                        state[quadrant][feature] += int(value)
                    for feature, value in enumerate(
                            (1, needs, risk, harvestable, yield_units, age)):
                        crop_state[crop][feature] += int(value)
                elif _occupied_animal(tile):
                    animal = ANIMAL_ID[_get(tile, "animal")]
                    age = day - int(_get(tile, "placed_day", day))
                    needs = not bool(_get(tile, "fed_today", False))
                    risk = needs and int(_get(tile, "consecutive_unfed", 0)) > 0
                    yield_units = int(_get(tile, "yield_units", 0))
                    cared = bool(_get(tile, "cared_today", False))
                    available = bool(_get(tile, "fertilizer_available", False))
                    pending = int(_get(tile, "pending_care_bonus", 0))
                    for feature, value in enumerate((age, yield_units, needs, risk,
                            yield_units > 0, cared, available, pending)):
                        state[quadrant][feature] += int(value)
                    for feature, value in enumerate(
                            (1, needs, risk, yield_units, cared, available)):
                        animal_state[animal][feature] += int(value)
        for quadrant in range(4):
            for value in entity[quadrant]:
                put(_u8_scale(value, 25))
            put(_u8_scale(state[quadrant][0], 25 * 30))
            put(_u8_scale(state[quadrant][1], 25 * 16))
            for feature in range(2, 7):
                put(_u8_scale(state[quadrant][feature], 25))
            put(_u8_scale(state[quadrant][7], 25 * 16))
        for values in crop_state:
            for feature in range(4):
                put(_u8_scale(values[feature], 100))
            put(_u8_scale(values[4], 100 * 16))
            put(_u8_scale(values[5], 100 * 30))
        for values in animal_state:
            for feature, value in enumerate(values):
                put(_u8_scale(value, 100 * 16 if feature == 3 else 100))

    shed, seeds = _get(private, "shed", {}), _get(private, "seeds", {})
    for name in ITEMS:
        put(_u8_scale(_get(shed, name, 0), 100))
    for name in CROPS:
        put(_u8_scale(_get(seeds, name, 0), 100))

    for view in range(UNIT_HEADS):
        unit_ids = _view_units(view, len(own_positions))
        if not unit_ids:
            k += 48
            continue
        ux = sum(own_positions[i][0] for i in unit_ids) // len(unit_ids)
        uy = sum(own_positions[i][1] for i in unit_ids) // len(unit_ids)
        routes = _encode_unit_routes(me, ux, uy, day)
        put(_u8_scale(len(unit_ids), 16))
        put(_u8_scale(ux, 9))
        put(_u8_scale(uy, 9))
        current_entity = _tile_entity(_get(me, "tiles", ())[uy][ux])
        for kind in range(13):
            put(255 if kind == current_entity else 0)
        for bit in (128, 64, 32, 16, 8):
            put(255 if routes[1] & bit else 0)
        for name in ITEMS:
            put(_u8_scale(sum(int(_get(_inventory(private, unit), name, 0))
                              for unit in unit_ids), 100))
        present = routes[2] // 8
        for route in range(5):
            put(255 if present & (1 << route) else 0)
        for value in routes[3:13]:
            put(value)

    for view in range(UNIT_HEADS):
        unit_ids = _view_units(view, len(opp_positions))
        if not unit_ids:
            k += 3
            continue
        put(_u8_scale(len(unit_ids), 16))
        put(_u8_scale(sum(opp_positions[i][0] for i in unit_ids)
                      // len(unit_ids), 9))
        put(_u8_scale(sum(opp_positions[i][1] for i in unit_ids)
                      // len(unit_ids), 9))

    market = _get(obs, "market", {})
    inventory = _get(market, "inventory", {})
    prices = _get(market, "prices", {})
    for item_id, name in enumerate(PRODUCTS):
        put(_u8_signed(_get(inventory, name, 0) - 10000,
                       MARKET_THROUGHPUT[item_id] * 4))
    for name in PRODUCTS:
        put(_u8_scale(_get(prices, name, 0), 1000))
    if k > OBS_SIZE:
        raise RuntimeError(f"Kaggriculture observation overflow: {k}")
    return out


def _shed_adjacent(x, y):
    return (x, y) in ((4, 4), (5, 4), (4, 5), (5, 5))


def _can_move(farm, x, y):
    # Official 1.32.3+ movement is bounds-only. In particular, hired hands may
    # spawn on and move through locked shed-access tiles.
    return 0 <= x < 10 and 0 <= y < 10


def _unit_mask(obs, unit_id):
    player = int(_get(obs, "player", 0))
    farm = _get(obs, "farms", ())[player]
    private = _get(obs, "private", {})
    positions = _positions(farm)
    mask = np.zeros(44, dtype=bool)
    mask[0] = True
    if unit_id >= len(positions):
        return mask
    x, y = positions[unit_id]
    tile = _get(farm, "tiles", ())[y][x]
    for action, (dx, dy) in enumerate(((0, -1), (0, 1), (1, 0), (-1, 0)), 1):
        mask[action] = _can_move(farm, x + dx, y + dy)

    adjacent = _shed_adjacent(x, y)
    shed = _get(private, "shed", {})
    inv = _inventory(private, unit_id)
    if adjacent:
        for item_id, name in enumerate(ITEMS):
            mask[5 + item_id] = int(_get(shed, name, 0)) > 0
        mask[17] = any(int(_get(inv, name, 0)) > 0 for name in ITEMS)
    if tile == "LOCKED":
        return mask

    kind = _get(tile, "kind", "")
    if tile is None:
        seeds = _get(private, "seeds", {})
        for crop_id, name in enumerate(CROPS):
            mask[18 + crop_id] = int(_get(seeds, name, 0)) > 0
        mask[26] = True  # BUILD_COOP
        mask[27] = True  # BUILD_PASTURE
    elif kind == "PLANT":
        mask[23] = not bool(_get(tile, "watered_today", False))
        crop = CROP_ID.get(_get(tile, "crop", ""), -1)
        if crop >= 0:
            age = int(_get(obs, "day", 0)) - int(_get(tile, "planted_day", 0))
            mask[24] = (int(_get(tile, "yield_units", 0)) > 0
                        and age >= FIRST_YIELD_DAY[crop])
        mask[25] = int(_get(inv, "FERTILIZER", 0)) > 0
    elif _occupied_animal(tile):
        mask[24] = int(_get(tile, "yield_units", 0)) > 0
        mask[29] = (not bool(_get(tile, "fed_today", False))
                    and int(_get(inv, "WHEAT", 0)) > 0)
        mask[30] = bool(_get(tile, "fertilizer_available", False))
        mask[31] = not bool(_get(tile, "cared_today", False))
    if tile is not None and tile != "LOCKED" and not _occupied_animal(tile):
        mask[28] = True  # DIG

    shed_total = sum(int(_get(shed, name, 0)) for name in ITEMS)
    shed_room = adjacent and shed_total < 100
    for item_id, name in enumerate(ITEMS):
        carried = int(_get(inv, name, 0)) > 0
        if item_id >= 9:
            animal_id = item_id - 9
            legal = (carried and kind == ANIMAL_STRUCTURE[animal_id]
                     and _get(tile, "animal") is None)
        else:
            legal = carried and shed_room
        mask[32 + item_id] = legal
    return mask


def _fib(n):
    a, b = 1, 1
    for _ in range(int(n)):
        a, b = b, a + b
    return a


def _market_spec_v2(action_id):
    if action_id == 0:
        return None, None, 1
    if action_id < 6:
        return "BUY_SEED", action_id - 1, 1
    if action_id < 8:
        return "BUY_PRODUCT", (0, 8)[action_id - 6], 1
    if action_id < 11:
        return "BUY_ANIMAL", 9 + action_id - 8, 1
    if action_id < 20:
        return "SELL", action_id - 11, -1
    if action_id == 20:
        return "HIRE", None, 1
    return "BUY_LAND", None, 1


def _action_mask_v2(obs):
    player = int(_get(obs, "player", 0))
    farm = _get(obs, "farms", ())[player]
    private = _get(obs, "private", {})
    hands = _get(farm, "hands", ())
    mask = np.zeros(MASK_SIZE, dtype=bool)
    mask[0:44] = _unit_mask(obs, 0)
    for unit in range(1, DIRECT_HANDS + 1):
        mask[44 * unit:44 * (unit + 1)] = _unit_mask(obs, unit)
    for cohort in range(OVERFLOW_COHORTS):
        slot = 1 + DIRECT_HANDS + cohort
        cohort_mask = np.zeros(44, dtype=bool)
        cohort_mask[0] = True
        for unit in range(1 + DIRECT_HANDS + cohort,
                          len(hands) + 1, OVERFLOW_COHORTS):
            cohort_mask |= _unit_mask(obs, unit)
        mask[44 * slot:44 * (slot + 1)] = cohort_mask

    money = int(_get(farm, "money", 0))
    shed = _get(private, "shed", {})
    prices = _get(_get(obs, "market", {}), "prices", {})
    tiles = _get(farm, "tiles", ())
    empty_tiles = sum(tile is None for row in tiles for tile in row)
    seeds = _get(private, "seeds", {})
    pending_seeds = sum(int(_get(seeds, crop, 0)) for crop in CROPS)
    useful_seeds = min(empty_tiles, len(hands) + 1)
    inventories = _get(private, "inventories", ())
    plant_count = 0
    animal_count = 0
    weed_count = 0
    vacant_structures = [0, 0, 0]
    unlocked_tiles = 0
    used_tiles = 0
    for row in tiles:
        for tile in row:
            if tile == "LOCKED":
                continue
            unlocked_tiles += 1
            kind = _get(tile, "kind") if isinstance(tile, dict) else None
            if kind == "PLANT":
                used_tiles += 1
                plant_count += 1
            elif kind in ("COOP", "PASTURE"):
                used_tiles += 1
                animal = _get(tile, "animal")
                if animal is not None:
                    animal_count += 1
                else:
                    if kind == "COOP":
                        vacant_structures[0] += 1
                    else:
                        vacant_structures[1] += 1
                        vacant_structures[2] += 1
            elif kind == "WEED":
                weed_count += 1

    def total_private_item(item_id):
        name = ITEMS[item_id]
        return (int(_get(shed, name, 0))
                + sum(int(_get(inv, name, 0)) for inv in inventories))

    slot_legal = np.zeros(MARKET_ACTIONS, dtype=bool)
    for action_id in range(MARKET_ACTIONS):
        op, item_id, quantity = _market_spec_v2(action_id)
        legal = False
        if op is None:
            legal = True
        elif quantity == -1 and op != "SELL":
            legal = False
        elif op == "BUY_SEED":
            legal = (quantity <= useful_seeds - pending_seeds
                     and money >= SEED_COST[item_id] * quantity + 100)
        elif op == "BUY_PRODUCT":
            useful = 2 * animal_count if item_id == 0 else plant_count
            legal = (quantity <= useful - total_private_item(item_id)
                     and money >= int(_get(prices, ITEMS[item_id], 0))
                     * quantity + 100)
        elif op == "BUY_ANIMAL":
            animal_id = item_id - 9
            stock = (total_private_item(9) if animal_id == 0 else
                     total_private_item(10) + total_private_item(11))
            legal = (quantity <= (vacant_structures[animal_id]
                                  - stock)
                     and total_private_item(0) >= animal_count + quantity
                     and money >= ANIMAL_COST[animal_id] * quantity + 100)
        elif op == "SELL":
            legal = int(_get(shed, ITEMS[item_id], 0)) > 0
        elif op == "HIRE":
            desired_units = min(12, 1 + (plant_count + animal_count + 3) // 4)
            legal = (len(hands) + 1 < desired_units
                     and money >= _fib(_get(farm, "hires_today", 0)) + 100)
        elif op == "BUY_LAND":
            extra = len(_get(farm, "unlocked_quadrants", ())) - 1
            legal = (0 <= extra < 3
                     and used_tiles * 10 >= unlocked_tiles * 9
                     and (len(hands) + 1) * 10 >= unlocked_tiles
                     and money >= (1000, 2000, 4000)[extra] + 500)
        slot_legal[action_id] = legal
    # The learned controller chooses one compact transaction per turn. The
    # simulator itself retains the official ten-order queue.
    market_base = 44 * UNIT_HEADS
    for head in range(MARKET_HEADS):
        start = market_base + head * MARKET_ACTIONS
        mask[start:start + MARKET_ACTIONS] = slot_legal
    return mask


def _market_spec(command_id):
    if command_id < 5:
        return "BUY_SEED", command_id
    if command_id < 7:
        return "BUY_PRODUCT", (0, 8)[command_id - 5]
    if command_id < 10:
        return "BUY_ANIMAL", 9 + command_id - 7
    if command_id < 19:
        return "SELL", command_id - 10
    if command_id == 19:
        return "HIRE", None
    return "BUY_LAND", None


def _market_shape(name, value, throughput):
    value = max(0.0, float(value))
    if name == "linear":
        return value
    if name == "sq":
        return value * value
    if name == "sqrt":
        return math.sqrt(value)
    if name == "log":
        return math.log1p(value)
    if name == "hinge":
        ratio = value / throughput if throughput > 0 else value
        return ratio + 8.0 * max(0.0, ratio - 1.0) ** 2
    return value


def _market_buy_price(obs, item_id):
    """Quote one product purchase at inventory-1, matching the simulator."""
    market = _get(obs, "market", {})
    inventory = int(_get(_get(market, "inventory", {}), ITEMS[item_id], 0)) - 1
    base = MARKET_BASE[item_id]
    throughput = MARKET_THROUGHPUT[item_id]
    below = inventory < 10000
    name = MARKET_BELOW_FUNC[item_id] if below else MARKET_ABOVE_FUNC[item_id]
    target = (MARKET_BELOW_TARGET[item_id] if below
              else MARKET_ABOVE_TARGET[item_id])
    distance = 10000 - inventory if below else inventory - 10000
    delta = (target * base * _market_shape(name, distance, throughput)
             / _market_shape(name, throughput, throughput))
    value = base + delta if below else base - delta
    return max(1, int(round(value)))


def action_mask(obs):
    """Simulator legality only; no strategy or usefulness heuristics."""
    player = int(_get(obs, "player", 0))
    farm = _get(obs, "farms", ())[player]
    private = _get(obs, "private", {})
    hands = _get(farm, "hands", ())
    mask = np.zeros(MASK_SIZE, dtype=bool)
    for unit in range(1 + DIRECT_HANDS):
        mask[44 * unit:44 * (unit + 1)] = _unit_mask(obs, unit)
    for cohort in range(OVERFLOW_COHORTS):
        slot = 1 + DIRECT_HANDS + cohort
        cohort_mask = np.zeros(44, dtype=bool)
        cohort_mask[0] = True
        for unit in range(1 + DIRECT_HANDS + cohort,
                          len(hands) + 1, OVERFLOW_COHORTS):
            cohort_mask |= _unit_mask(obs, unit)
        mask[44 * slot:44 * (slot + 1)] = cohort_mask

    money = int(_get(farm, "money", 0))
    shed = _get(private, "shed", {})
    shed_total = sum(int(_get(shed, name, 0)) for name in ITEMS)
    command_mask = np.zeros(MARKET_COMMANDS, dtype=bool)
    for command in range(MARKET_COMMANDS):
        op, item_id = _market_spec(command)
        if op == "BUY_SEED":
            legal = money >= SEED_COST[item_id]
        elif op == "BUY_PRODUCT":
            legal = (shed_total < 100
                     and money >= _market_buy_price(obs, item_id))
        elif op == "BUY_ANIMAL":
            legal = (shed_total < 100
                     and money >= ANIMAL_COST[item_id - 9])
        elif op == "SELL":
            legal = True  # an earlier queue command can create inventory
        elif op == "HIRE":
            legal = (len(hands) < MAX_HANDS
                     and money >= _fib(_get(farm, "hires_today", 0)))
        else:
            extra = len(_get(farm, "unlocked_quadrants", ())) - 1
            legal = 0 <= extra < 3 and money >= (1000, 2000, 4000)[extra]
        command_mask[command] = legal
    has_command = bool(np.any(command_mask))
    base = 44 * UNIT_HEADS
    for slot in range(MARKET_SLOTS):
        mask[base:base + 2] = (True, has_command)
        mask[base + 2:base + 2 + MARKET_COMMANDS] = command_mask
        mask[base + 2 + MARKET_COMMANDS:base + 2 + MARKET_COMMANDS + 8] = True
        base += 2 + MARKET_COMMANDS + 8
    return mask


def _unit_action(action_id):
    if action_id == 0:
        return ["PASS"]
    if action_id < 5:
        return [("NORTH", "SOUTH", "EAST", "WEST")[action_id - 1]]
    if action_id < 17:
        return ["PICKUP", ITEMS[action_id - 5], ALL]
    if action_id == 17:
        return ["DROP"]
    if action_id < 23:
        return ["PLANT", CROPS[action_id - 18]]
    if action_id < 32:
        return [["WATER"], ["HARVEST"], ["FERTILIZE"], ["BUILD_COOP"],
                ["BUILD_PASTURE"], ["DIG"], ["FEED"],
                ["COLLECT_FERTILIZER"], ["CARE"]][action_id - 23]
    return ["PLACE", ITEMS[action_id - 32], ALL]


def decode_actions(obs, actions):
    player = int(_get(obs, "player", 0))
    farm = _get(obs, "farms", ())[player]
    hands = _get(farm, "hands", ())
    def hand_slot(hand):
        unit = hand + 1
        if unit <= DIRECT_HANDS:
            return unit
        if OVERFLOW_COHORTS == 0:
            return None
        return 1 + DIRECT_HANDS + (unit - 1 - DIRECT_HANDS) % OVERFLOW_COHORTS

    result = {
        "farmer": _unit_action(int(actions[0])),
        "hands": [(_unit_action(int(actions[hand_slot(hand)]))
                   if hand_slot(hand) is not None else ["PASS"])
                  for hand in range(len(hands))],
        "market": [],
    }
    for slot in range(MARKET_SLOTS):
        head = UNIT_HEADS + 3 * slot
        if int(actions[head]) != 1:
            break
        command = int(actions[head + 1])
        op, item_id = _market_spec(command)
        quantity = 1
        if command < 19:
            quantity = MARKET_QUANTITIES[int(actions[head + 2])]
        if op == "BUY_SEED":
            result["market"].append([op, CROPS[item_id], quantity])
        elif op in ("BUY_PRODUCT", "BUY_ANIMAL", "SELL"):
            result["market"].append([op, ITEMS[item_id], quantity])
        else:
            result["market"].append([op])
    return result


class NativeMinGRU:
    def __init__(self, path):
        flat = np.fromfile(path, dtype=np.float32)
        self.hidden, self.layers_n = _infer_arch(flat.size)
        hidden = self.hidden
        num_layers = self.layers_n
        index = 0
        count = hidden * OBS_SIZE
        self.encoder = flat[index:index + count].reshape(hidden, OBS_SIZE)
        index += count
        count = (MASK_SIZE + 1) * hidden
        self.decoder = flat[index:index + count].reshape(MASK_SIZE + 1, hidden)
        index += count
        self.layers = []
        for _ in range(num_layers):
            count = 3 * hidden * hidden
            self.layers.append(flat[index:index + count].reshape(3 * hidden,
                                                                  hidden))
            index += count
        self.state = np.zeros((num_layers, hidden), dtype=np.float32)

    @staticmethod
    def _sigmoid(value):
        return 1.0 / (1.0 + np.exp(-np.clip(value, -80.0, 80.0)))

    def reset(self):
        self.state.fill(0)

    def clone(self):
        """Return an independent read-only-weight recurrent policy copy.

        The checkpoint arrays are immutable during inference and may be shared;
        only the per-layer hidden state needs to be copied.  Offline branch
        evaluation normally resets state per snapshot, while live episode
        comparisons use this helper when a branch must preserve history.
        """
        import copy

        result = copy.copy(self)
        result.encoder = self.encoder
        result.decoder = self.decoder
        result.layers = self.layers
        result.state = self.state.copy()
        return result

    def forward(self, obs):
        hidden = self.hidden
        x = self.encoder @ (obs.astype(np.float32) * (1.0 / 255.0))
        for layer_id, weights in enumerate(self.layers):
            combined = weights @ x
            h = combined[:hidden]
            gate = self._sigmoid(combined[hidden:2 * hidden])
            candidate = np.empty_like(h)
            positive = h >= 0
            candidate[positive] = h[positive] + 0.5
            candidate[~positive] = self._sigmoid(h[~positive])
            state = self.state[layer_id]
            next_state = state + gate * (candidate - state)
            highway = self._sigmoid(combined[2 * hidden:])
            x = highway * next_state + (1.0 - highway) * x
            self.state[layer_id] = next_state
        return self.decoder @ x


_CODE_DIR = os.path.dirname(os.path.abspath(sys._getframe().f_code.co_filename))
_MODEL_OVERRIDE = os.environ.get("PUFFERLIB_MODEL_PATH")
_MODEL_CANDIDATES = tuple(path for path in (
    os.path.join(_CODE_DIR, "kaggriculture_v4.bin"),
    "/kaggle_simulations/agent/kaggriculture_v4.bin",
    os.path.abspath("kaggriculture_v4.bin"),
) if path)
_MODEL_PATH = (_MODEL_OVERRIDE if _MODEL_OVERRIDE is not None else
               next((path for path in _MODEL_CANDIDATES if os.path.isfile(path)),
                    _MODEL_CANDIDATES[0]))
_MODEL = NativeMinGRU(_MODEL_PATH) if os.path.isfile(_MODEL_PATH) else None
_POLICY_SEED = int(os.environ.get("PUFFERLIB_POLICY_SEED", "97"))
_DETERMINISTIC = os.environ.get("PUFFERLIB_DETERMINISTIC", "1") != "0"
_RNG = np.random.default_rng(73)

# ``importlib.util.spec_from_file_location`` (used by our export test and by
# some competition runners) does not automatically add the archive directory
# to sys.path.  Make adjacent optional runtime modules reliably importable.
if _CODE_DIR not in sys.path:
    sys.path.insert(0, _CODE_DIR)

# The optional learned macro overlay is loaded only when its model artifact is
# packaged.  Raw package exports remain byte-for-byte compatible; enhanced
# packages provide macro_learned_72_ridge.npz plus the pure top-bot executor.
try:
    from macro_overlay import make_overlay
    _MACRO_OVERLAY = make_overlay()
except Exception:
    _MACRO_OVERLAY = None

# Structured macro checkpoints use the same tensors as primitive policies but
# assign different meanings to the first three heads.  The runtime is present
# only in a dedicated macro export; ordinary historical packages therefore
# retain their original primitive behavior.
try:
    from native_macro_runtime import NativeMacroRuntime
    _NATIVE_MACRO = NativeMacroRuntime()
except Exception:
    _NATIVE_MACRO = None


def _sample_heads(logits, mask):
    actions = np.zeros(len(HEAD_SIZES), dtype=np.int32)

    def sample(head):
        global _RNG
        start, end = HEAD_OFFSETS[head], HEAD_OFFSETS[head + 1]
        legal = np.flatnonzero(mask[start:end])
        values = logits[start:end][legal]
        if _DETERMINISTIC:
            actions[head] = legal[int(np.argmax(values))]
            return
        values = values - np.max(values)
        probabilities = np.exp(values)
        probabilities /= np.sum(probabilities)
        actions[head] = legal[_RNG.choice(legal.size, p=probabilities)]

    for head in range(UNIT_HEADS):
        sample(head)
    for slot in range(MARKET_SLOTS):
        continuation = UNIT_HEADS + 3 * slot
        sample(continuation)
        if actions[continuation] == 0:
            break
        sample(continuation + 1)
        if actions[continuation + 1] < 19:
            sample(continuation + 2)
    return actions


def agent(obs):
    global _RNG
    day = int(_get(obs, "day", 0))
    hour = int(_get(obs, "hour", 0))

    # Enhanced exports are controlled by the parity-tested top-bot executor
    # (and, when explicitly enabled, its public-state macro proposal layer).
    # Do this before the neural forward pass: the checkpoint remains in the
    # archive for legacy/raw exports, but it is not a silent behavior fallback
    # for the hierarchical submission.
    if (_MACRO_OVERLAY is not None
            and _MACRO_OVERLAY.top_bot is not None):
        fallback = _MACRO_OVERLAY.fallback(obs, None)
        return _MACRO_OVERLAY.action(obs, fallback)

    if _MODEL is None:
        raise FileNotFoundError(f"PufferLib checkpoint not found: {_MODEL_PATH}")
    if day == 0 and hour == 0:
        _MODEL.reset()
        _RNG = np.random.default_rng(
            _POLICY_SEED + int(_get(obs, "player", 0)))
    encoded = encode_observation(obs)
    if _NATIVE_MACRO is not None:
        encoded = _NATIVE_MACRO.fill_observation(obs, encoded)
        mask = _NATIVE_MACRO.action_mask(obs)
    else:
        mask = action_mask(obs)
    logits = _MODEL.forward(encoded)[:MASK_SIZE]
    learned_actions = _sample_heads(logits, mask)
    learned_fallback = (lambda: _NATIVE_MACRO.decode(obs, learned_actions)) \
        if _NATIVE_MACRO is not None else \
        (lambda: decode_actions(obs, learned_actions))
    if _MACRO_OVERLAY is None:
        return learned_fallback()
    fallback = _MACRO_OVERLAY.fallback(obs, learned_fallback)
    return _MACRO_OVERLAY.action(obs, fallback)
