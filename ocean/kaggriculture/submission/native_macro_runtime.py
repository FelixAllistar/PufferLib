"""Portable runtime for native Kaggriculture structured macro policies.

This module mirrors ``macro_mode = 2`` from ``kaggriculture.h``.  It keeps the
trained 1280-byte observation and 1058-logit ABI, fills the strategic tail,
constructs the structured action mask, decodes intent/quantity/target heads,
and expands the selected intent into ordinary Kaggle actions.

Only public state and the acting player's private inventory are used.  The
runtime deliberately has no Ridge/MPC artifact and never reads an opponent's
private inventory.
"""

from __future__ import annotations

import copy
import importlib.util
import pathlib

import numpy as np


CROPS = ("WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON")
PRODUCTS = CROPS + ("EGG", "MILK", "WOOL", "FERTILIZER")
ANIMALS = ("GOOSE", "COW", "SHEEP")
ITEMS = PRODUCTS + ANIMALS
SEED_COST = (10, 20, 50, 100, 80)
ANIMAL_COST = (300, 400, 500)
CROP_DEF = (
    # seed cost, first yield day, interval, max yield, ongoing
    (10, 2, 0, 6, False),
    (20, 2, 0, 4, False),
    (50, 8, 1, 4, True),
    (100, 10, 2, 4, True),
    (80, 10, 0, 6, False),
)
ANIMAL_DEF = (
    # cost, structure, first yield, interval, max held, product
    (300, "COOP", 4, 1, 4, "EGG"),
    (400, "PASTURE", 8, 2, 6, "MILK"),
    (500, "PASTURE", 6, 3, 6, "WOOL"),
)
QUADRANT_BITS = {"NW": 1, "NE": 2, "SW": 4, "SE": 8}
QUANTITIES = (1, 2, 4, 8, 12, 20, 32, 64)
TARGETS = (0, 1, 2, 4, 8)

MACRO_COUNT = 44
MACRO_HOLD = 0
MACRO_PLANT_BASE = 1
MACRO_ANIMAL_BASE = 6
MACRO_EXPAND = 9
MACRO_SELL_BASE = 10
MACRO_SELL_ALL = 19
MACRO_BUY_SEED_BASE = 20
MACRO_BUY_ANIMAL_BASE = 25
MACRO_HIRE = 28
MACRO_HARVEST = 29
MACRO_MAINTAIN = 30
MACRO_DIVERSIFY = 31
MACRO_BUY_WHEAT = 32
MACRO_BUY_FERTILIZER = 33
MACRO_CASH_OUT = 34
MACRO_RESERVED_BASE = 35

OBS_OFFSET = 1198
UNIT_HEADS = 17
UNIT_COMMANDS = 44
MARKET_SLOTS = 10
MARKET_STRIDE = 31
MASK_SIZE = 1058
SCORE_SCALE = 10000.0
EPISODE_STEPS = 720
TURNS_PER_DAY = 24
DIRECT_HANDS = 16
SHED_CAPACITY = 100
MAX_MARKET_ORDERS = 10
JOB_MAINTAIN = 1
JOB_HARVEST = 2
JOB_DIG = 4
JOB_PLANT = 8
JOB_ALL = JOB_MAINTAIN | JOB_HARVEST | JOB_DIG | JOB_PLANT
STRUCTURE_POSITIONS = (
    (3, 4), (4, 3), (3, 3), (2, 4),
    (5, 0), (6, 0), (5, 1), (6, 1), (7, 0), (7, 1),
    (0, 5), (1, 5), (0, 6), (1, 6), (2, 5),
)
MARKET_DEFAULTS = {
    "WHEAT": (25, 10000, 400, "sqrt", .80, "log", .20),
    "CARROT": (35, 10000, 450, "hinge", 1.00, "sqrt", .70),
    "TOMATO": (60, 10000, 200, "hinge", .40, "sqrt", .60),
    "STRAWBERRY": (120, 10000, 100, "sqrt", .70, "linear", 1.60),
    "MELON": (250, 10000, 300, "log", .20, "sq", 3.60),
    "EGG": (50, 10000, 332, "hinge", .40, "log", .20),
    "MILK": (160, 10000, 122, "sqrt", .60, "linear", 1.60),
    "WOOL": (200, 10000, 105, "log", .20, "sq", 3.20),
    "FERTILIZER": (100, 10000, 200, "linear", .40, "linear", .40),
}


def _load_top_bot():
    try:
        import native_macro_top_bot as module
        return module
    except Exception:
        source = pathlib.Path(__file__).resolve().parent / "top_bot" / "main.py"
        spec = importlib.util.spec_from_file_location("native_macro_top_bot", source)
        if spec is None or spec.loader is None:
            raise ImportError(f"cannot load macro executor: {source}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module


TOP = _load_top_bot()


def _route(farm, unit, tx, ty):
    """Macro routing, including escape from the locked SE shed access."""
    x, y = unit
    unlocked = _get(farm, "unlocked_quadrants", ())
    quadrant = "SE" if x >= 5 and y >= 5 else "NE" if x >= 5 \
        else "SW" if y >= 5 else "NW"
    if quadrant not in unlocked:
        if x >= 5 and y >= 5:
            if "NE" in unlocked:
                return ["NORTH"]
            if "SW" in unlocked:
                return ["WEST"]
            return ["NORTH"]
        if x >= 5:
            return ["WEST"]
        if y >= 5:
            return ["NORTH"]
    if x != tx:
        return ["EAST" if x < tx else "WEST"]
    if y != ty:
        return ["SOUTH" if y < ty else "NORTH"]
    return ["PASS"]


def _get(obj, key, default=None):
    if obj is None:
        return default
    try:
        return obj.get(key, default)
    except AttributeError:
        return getattr(obj, key, default)


def _step(obs):
    return int(_get(obs, "step", int(_get(obs, "day", 0)) * 24
                    + int(_get(obs, "hour", 0))))


def _player(obs):
    return int(_get(obs, "player", 0))


def _farm(obs):
    return _get(obs, "farms", ())[_player(obs)]


def _private(obs):
    return _get(obs, "private", {})


def _tiles(obs):
    return _get(_farm(obs), "tiles", ())


def _kind(tile):
    return _get(tile, "kind", "")


def _is_animal(tile):
    return _kind(tile) in ("COOP", "PASTURE") and _get(tile, "animal") in ANIMALS


def _positions(obs):
    farm = _farm(obs)
    return [list(_get(farm, "farmer", (4, 4)))] + [
        list(pos) for pos in _get(farm, "hands", ())
    ]


def _inventory(obs, unit):
    inventories = _get(_private(obs), "inventories", ())
    return inventories[unit] if unit < len(inventories) else {}


def _shed(obs):
    return _get(_private(obs), "shed", {})


def _seeds(obs):
    return _get(_private(obs), "seeds", {})


def _unlocked_mask(obs):
    mask = 0
    for name in _get(_farm(obs), "unlocked_quadrants", ()):
        mask |= QUADRANT_BITS.get(name, 0)
    return mask


def _quadrant(x, y):
    return 1 << ((1 if x >= 5 else 0) + (2 if y >= 5 else 0))


def _u8_scale(value, maximum):
    value = int(value)
    if value <= 0:
        return 0
    if value >= maximum:
        return 255
    return (value * 255) // maximum


def _fib(index):
    a, b = 1, 1
    for _ in range(max(0, int(index))):
        a, b = b, a + b
    return a


def _empty_tiles(obs):
    return sum(tile is None for row in _tiles(obs) for tile in row)


def _reclaimable_tiles(obs):
    return sum(tile is None or _kind(tile) == "WEED"
               for row in _tiles(obs) for tile in row)


def _reclaimable_tiles_in_target(obs, target):
    return sum((tile is None or _kind(tile) == "WEED")
               and (not target or _quadrant(x, y) == target)
               for y, row in enumerate(_tiles(obs))
               for x, tile in enumerate(row))


def _seed_stock(obs):
    seeds = _seeds(obs)
    return sum(int(_get(seeds, crop, 0)) for crop in CROPS)


def _seed_purchase_room(obs):
    return max(0, _reclaimable_tiles(obs) - _seed_stock(obs))


def _item_stock(obs, item):
    total = int(_get(_shed(obs), item, 0))
    for unit in range(len(_positions(obs))):
        total += int(_get(_inventory(obs, unit), item, 0))
    return total


def _animal_room(obs, animal_id):
    structure = ANIMAL_DEF[animal_id][1]
    return sum(_kind(tile) == structure and _get(tile, "animal") is None
               for row in _tiles(obs) for tile in row)


def _animal_purchase_room(obs, animal_id):
    structure = ANIMAL_DEF[animal_id][1]
    compatible = sum(_item_stock(obs, animal)
                     for index, animal in enumerate(ANIMALS)
                     if ANIMAL_DEF[index][1] == structure)
    return max(0, _animal_room(obs, animal_id) - compatible)


def _shed_total(obs):
    return sum(int(_get(_shed(obs), item, 0)) for item in ITEMS)


def _feed_shortfall(obs):
    need = sum(_is_animal(tile) and not bool(_get(tile, "fed_today", False))
               for row in _tiles(obs) for tile in row)
    available = int(_get(_shed(obs), "WHEAT", 0))
    available += sum(int(_get(_inventory(obs, unit), "WHEAT", 0))
                     for unit in range(len(_positions(obs))))
    return max(0, need - available)


def _product_buy_cost(obs, item, units):
    """Exact successive marginal quotes used by the official buy loop."""
    market = _get(obs, "market", {})
    inventory = _get(_get(market, "inventory", {}), item)
    if inventory is None:
        return units * int(_get(_get(market, "prices", {}), item, 0))
    params = _get(_get(market, "params", {}), item)
    if params is None:
        base, center, throughput, below_func, below_target, above_func, above_target = \
            MARKET_DEFAULTS[item]
        params = {"base": base, "I0": center, "T": throughput,
                  "below_func": below_func, "below_target": below_target,
                  "above_func": above_func, "above_target": above_target}

    def shape(name, value, throughput):
        import math
        value = max(0.0, float(value))
        if name == "linear":
            return value
        if name == "sq":
            return value * value
        if name == "sqrt":
            return math.sqrt(value)
        if name == "log":
            return math.log1p(value)
        if name == "log10":
            return math.log10(1.0 + value)
        if name == "hinge":
            ratio = value / throughput if throughput > 0 else value
            return ratio + 8.0 * max(0.0, ratio - 1.0) ** 2
        return value

    def quote(level):
        base = float(_get(params, "base", 25))
        center = float(_get(params, "I0", 10000))
        throughput = float(_get(params, "T", 400))
        below = level < center
        name = _get(params, "below_func" if below else "above_func",
                    "sqrt" if below else "log")
        target = float(_get(params, "below_target" if below else "above_target",
                            .80 if below else .20))
        distance = center - level if below else level - center
        denominator = shape(name, throughput, throughput)
        delta = target * base * shape(name, distance, throughput) / denominator
        return max(1, int(round(base + delta if below else base - delta)))

    return sum(quote(int(inventory) - unit - 1) for unit in range(units))


def _feed_cost(obs):
    return _product_buy_cost(obs, "WHEAT", _feed_shortfall(obs))


def _can_invest_after_feed(obs, cost, shed_units):
    feed = _feed_shortfall(obs)
    room = SHED_CAPACITY - _shed_total(obs) - feed
    money = int(_get(_farm(obs), "money", 0)) - _feed_cost(obs)
    return money >= int(cost) and room >= int(shed_units)


def _remaining_days(obs):
    remaining = EPISODE_STEPS - _step(obs)
    return 0 if remaining <= 0 else (remaining + TURNS_PER_DAY - 1) // TURNS_PER_DAY


def _crop_events(obs, crop_id):
    _cost, first, interval, max_yield, ongoing = CROP_DEF[crop_id]
    remaining = _remaining_days(obs)
    if remaining <= first:
        return 0
    events = (1 + (remaining - first - 1) // interval) if ongoing else max_yield
    return min(events, max_yield)


def _animal_events(obs, animal_id):
    _cost, _structure, first, interval, _held, _product = ANIMAL_DEF[animal_id]
    remaining = _remaining_days(obs)
    if remaining <= first:
        return 0
    return 1 + (remaining - first - 1) // interval


def _liquidation(obs):
    return _remaining_days(obs) <= 2


def _plant_needs_water(obs, tile):
    if _kind(tile) != "PLANT" or bool(_get(tile, "watered_today", False)):
        return False
    crop = _get(tile, "crop")
    if crop not in TOP.CROP_DEF:
        return False
    _cost, _first, max_day, _interval, _yield, ongoing = TOP.CROP_DEF[crop]
    age = int(_get(obs, "day", 0)) - int(_get(tile, "planted_day", 0))
    bonus = not ongoing and (max_day + 1) // 2 <= age <= max_day
    return int(_get(tile, "consecutive_unwatered", 0)) > 0 or bonus or ongoing


def _work_items(obs):
    work = 0
    empty = 0
    for row in _tiles(obs):
        for tile in row:
            if tile is None:
                empty += 1
            elif _kind(tile) == "WEED":
                work += 1
            elif _kind(tile) == "PLANT":
                work += int(not bool(_get(tile, "watered_today", False))
                            or int(_get(tile, "yield_units", 0)) > 0)
            elif _is_animal(tile):
                work += int(not bool(_get(tile, "fed_today", False))
                            or not bool(_get(tile, "cared_today", False))
                            or int(_get(tile, "yield_units", 0)) > 0
                            or bool(_get(tile, "fertilizer_available", False)))
    return work + min(_seed_stock(obs), empty)


def _desired_hands(obs):
    desired_units = (_work_items(obs) + 3) // 4
    desired = max(0, desired_units - 1)
    desired = min(desired, 4 * len(_get(_farm(obs), "unlocked_quadrants", ())))
    return min(desired, DIRECT_HANDS)


def _land_price(obs):
    extra = len(_get(_farm(obs), "unlocked_quadrants", ())) - 1
    return (1000, 2000, 4000)[extra] if 0 <= extra < 3 else 0


def _market_buy_legal(obs, kind, item=None):
    money = int(_get(_farm(obs), "money", 0))
    if kind == "BUY_SEED":
        return money >= SEED_COST[CROPS.index(item)]
    if kind == "BUY_PRODUCT":
        price = _product_buy_cost(obs, item, 1)
        return price > 0 and money >= price and _shed_total(obs) < SHED_CAPACITY
    if kind == "BUY_ANIMAL":
        cost = ANIMAL_COST[ANIMALS.index(item)]
        return money >= cost and _shed_total(obs) < SHED_CAPACITY
    if kind == "HIRE":
        return (len(_get(_farm(obs), "hands", ())) < 240
                and money >= _fib(_get(_farm(obs), "hires_today", 0)))
    if kind == "BUY_LAND":
        price = _land_price(obs)
        return price > 0 and money >= price
    return False


def _maintain_can_progress(obs):
    wheat = int(_get(_shed(obs), "WHEAT", 0))
    wheat += sum(int(_get(_inventory(obs, unit), "WHEAT", 0))
                 for unit in range(len(_positions(obs))))
    unfed = False
    for row in _tiles(obs):
        for tile in row:
            if _plant_needs_water(obs, tile):
                return True
            if not _is_animal(tile):
                continue
            if not bool(_get(tile, "fed_today", False)):
                unfed = True
            elif not bool(_get(tile, "cared_today", False)):
                return True
    if not unfed:
        return False
    return wheat > 0 or _market_buy_legal(obs, "BUY_PRODUCT", "WHEAT")


def _diversify_can_progress(obs):
    if _maintain_can_progress(obs):
        return True
    reclaimable = _reclaimable_tiles(obs)
    money = int(_get(_farm(obs), "money", 0))
    if (reclaimable > 0
            and (int(_get(_seeds(obs), "WHEAT", 0)) > 0
                 or money >= SEED_COST[0])
            and _crop_events(obs, 0) > 0):
        return True
    for row in _tiles(obs):
        for tile in row:
            if _kind(tile) == "PLANT":
                crop = CROPS.index(_get(tile, "crop"))
                age = int(_get(obs, "day", 0)) - int(_get(tile, "planted_day", 0))
                if (int(_get(tile, "yield_units", 0)) > 0
                        and age >= CROP_DEF[crop][1]):
                    return True
            if (_is_animal(tile)
                    and (int(_get(tile, "yield_units", 0)) > 0
                         or bool(_get(tile, "fertilizer_available", False)))):
                return True
    if any(int(_get(_shed(obs), item, 0)) > 0 for item in PRODUCTS):
        return True
    land = len(_get(_farm(obs), "unlocked_quadrants", ()))
    if (int(_get(obs, "day", 0)) in (4, 9) and land < 3
            and money >= (1500 if land == 1 else 3000)
            and _market_buy_legal(obs, "BUY_LAND")):
        return True
    hands = len(_get(_farm(obs), "hands", ()))
    desired_hands = 4 if land == 1 else 8 if land == 2 else 12
    if hands < desired_hands and _market_buy_legal(obs, "HIRE"):
        return True
    has_pasture = any(_kind(tile) == "PASTURE"
                       for row in _tiles(obs) for tile in row)
    if not has_pasture and _positions(obs):
        for x, y in STRUCTURE_POSITIONS:
            if _tiles(obs)[y][x] is None:
                return True
    return False


def candidate_legal(obs, macro):
    if macro < 0 or macro >= MACRO_RESERVED_BASE:
        return False
    if macro == MACRO_HOLD:
        return True
    if macro == MACRO_DIVERSIFY:
        if _liquidation(obs) or not _diversify_can_progress(obs):
            return False
        return True
    if MACRO_PLANT_BASE <= macro < MACRO_PLANT_BASE + 5:
        crop = macro - MACRO_PLANT_BASE
        return (_reclaimable_tiles(obs) > 0 and _crop_events(obs, crop) > 0
                and (int(_get(_seeds(obs), CROPS[crop], 0)) > 0
                     or _can_invest_after_feed(obs, SEED_COST[crop], 0)))
    if MACRO_ANIMAL_BASE <= macro < MACRO_ANIMAL_BASE + 3:
        animal = macro - MACRO_ANIMAL_BASE
        stock = _item_stock(obs, ANIMALS[animal])
        return (not _liquidation(obs) and _animal_events(obs, animal) > 0
                and (_animal_room(obs, animal) > 0 or _empty_tiles(obs) > 0)
                and (stock > 0
                     or (_market_buy_legal(obs, "BUY_ANIMAL", ANIMALS[animal])
                         and _can_invest_after_feed(obs, ANIMAL_COST[animal], 1))))
    if macro == MACRO_EXPAND:
        return (not _liquidation(obs)
                and any(_crop_events(obs, crop) > 0 for crop in range(5))
                and _market_buy_legal(obs, "BUY_LAND")
                and _can_invest_after_feed(obs, _land_price(obs), 0))
    if MACRO_SELL_BASE <= macro < MACRO_SELL_BASE + 9:
        return int(_get(_shed(obs), PRODUCTS[macro - MACRO_SELL_BASE], 0)) > 0
    if macro in (MACRO_SELL_ALL, MACRO_CASH_OUT):
        return any(int(_get(_shed(obs), item, 0)) > 0 for item in PRODUCTS)
    if MACRO_BUY_SEED_BASE <= macro < MACRO_BUY_SEED_BASE + 5:
        crop = macro - MACRO_BUY_SEED_BASE
        return (not _liquidation(obs) and _crop_events(obs, crop) > 0
                and _seed_purchase_room(obs) > 0
                and _market_buy_legal(obs, "BUY_SEED", CROPS[crop])
                and _can_invest_after_feed(obs, SEED_COST[crop], 0))
    if MACRO_BUY_ANIMAL_BASE <= macro < MACRO_BUY_ANIMAL_BASE + 3:
        animal = macro - MACRO_BUY_ANIMAL_BASE
        return (not _liquidation(obs) and _animal_events(obs, animal) > 0
                and _animal_purchase_room(obs, animal) > 0
                and _market_buy_legal(obs, "BUY_ANIMAL", ANIMALS[animal])
                and _can_invest_after_feed(obs, ANIMAL_COST[animal], 1))
    if macro == MACRO_HIRE:
        farm = _farm(obs)
        turns_today = TURNS_PER_DAY - int(_get(obs, "hour", 0))
        hands = len(_get(farm, "hands", ()))
        return (not _liquidation(obs) and turns_today > 4
                and hands < DIRECT_HANDS and hands < _desired_hands(obs)
                and _market_buy_legal(obs, "HIRE")
                and _can_invest_after_feed(obs,
                                           _fib(_get(farm, "hires_today", 0)), 0))
    if macro == MACRO_HARVEST:
        for row in _tiles(obs):
            for tile in row:
                if _kind(tile) == "PLANT":
                    crop = CROPS.index(_get(tile, "crop"))
                    age = int(_get(obs, "day", 0)) - int(_get(tile, "planted_day", 0))
                    if int(_get(tile, "yield_units", 0)) > 0 and age >= CROP_DEF[crop][1]:
                        return True
                elif _is_animal(tile) and (int(_get(tile, "yield_units", 0)) > 0
                                           or bool(_get(tile, "fertilizer_available", False))):
                    return True
        return False
    if macro == MACRO_MAINTAIN:
        return _maintain_can_progress(obs)
    if macro in (MACRO_BUY_WHEAT, MACRO_BUY_FERTILIZER):
        item = "WHEAT" if macro == MACRO_BUY_WHEAT else "FERTILIZER"
        return ((item == "WHEAT" or not _liquidation(obs))
                and _market_buy_legal(obs, "BUY_PRODUCT", item)
                and (item == "WHEAT"
                     or _can_invest_after_feed(
                         obs, int(_get(_get(_get(obs, "market", {}), "prices", {}), item, 0)), 1)))
    return False


def _crop_score(obs, crop):
    events = _crop_events(obs, crop)
    if events <= 0:
        return -10000.0
    prices = _get(_get(obs, "market", {}), "prices", {})
    return events * int(_get(prices, CROPS[crop], 0)) - SEED_COST[crop]


def _animal_score(obs, animal):
    events = _animal_events(obs, animal)
    if events <= 0:
        return -10000.0
    cost, _structure, _first, _interval, max_held, product = ANIMAL_DEF[animal]
    prices = _get(_get(obs, "market", {}), "prices", {})
    return (events * max_held * int(_get(prices, product, 0))
            - events * 2 * int(_get(prices, "WHEAT", 0)) - cost)


def candidate_score(obs, macro):
    score = 0.0
    prices = _get(_get(obs, "market", {}), "prices", {})
    if MACRO_PLANT_BASE <= macro < MACRO_PLANT_BASE + 5:
        score = _crop_score(obs, macro - MACRO_PLANT_BASE)
    elif MACRO_ANIMAL_BASE <= macro < MACRO_ANIMAL_BASE + 3:
        score = _animal_score(obs, macro - MACRO_ANIMAL_BASE)
    elif macro == MACRO_EXPAND:
        best = max(0.0, *(_crop_score(obs, crop) for crop in range(5)),
                   *(_animal_score(obs, animal) for animal in range(3)))
        spare = _reclaimable_tiles(obs)
        pressure = max(0, min(8, max(8 - spare, _seed_stock(obs) - spare)))
        score = best * pressure - _land_price(obs)
    elif MACRO_SELL_BASE <= macro < MACRO_SELL_BASE + 9:
        item = PRODUCTS[macro - MACRO_SELL_BASE]
        score = int(_get(_shed(obs), item, 0)) * int(_get(prices, item, 0))
    elif macro in (MACRO_SELL_ALL, MACRO_CASH_OUT):
        score = sum(int(_get(_shed(obs), item, 0)) * int(_get(prices, item, 0))
                    for item in PRODUCTS)
    elif MACRO_BUY_SEED_BASE <= macro < MACRO_BUY_SEED_BASE + 5:
        crop = macro - MACRO_BUY_SEED_BASE
        score = _crop_score(obs, crop) - int(_get(_seeds(obs), CROPS[crop], 0)) * SEED_COST[crop]
    elif MACRO_BUY_ANIMAL_BASE <= macro < MACRO_BUY_ANIMAL_BASE + 3:
        animal = macro - MACRO_BUY_ANIMAL_BASE
        score = _animal_score(obs, animal) - _item_stock(obs, ANIMALS[animal]) * ANIMAL_COST[animal]
    elif macro == MACRO_HIRE:
        fraction = (TURNS_PER_DAY - int(_get(obs, "hour", 0))) / TURNS_PER_DAY
        score = 250.0 * fraction - _fib(_get(_farm(obs), "hires_today", 0))
    elif macro == MACRO_HARVEST:
        for row in _tiles(obs):
            for tile in row:
                amount = int(_get(tile, "yield_units", 0))
                if amount <= 0:
                    continue
                item = (_get(tile, "crop") if _kind(tile) == "PLANT" else
                        ANIMAL_DEF[ANIMALS.index(_get(tile, "animal"))][5]
                        if _is_animal(tile) else None)
                if item in PRODUCTS:
                    score += amount * int(_get(prices, item, 0))
    elif macro == MACRO_MAINTAIN:
        score = 250.0
    elif macro == MACRO_DIVERSIFY:
        score = 100.0
    elif macro == MACRO_BUY_WHEAT:
        score = -2.0 * int(_get(prices, "WHEAT", 0))
    elif macro == MACRO_BUY_FERTILIZER:
        score = -2.0 * int(_get(prices, "FERTILIZER", 0))
    return score if candidate_legal(obs, macro) else -10000.0


def score_byte(obs, macro):
    normalized = max(-1.0, min(1.0, candidate_score(obs, macro) / SCORE_SCALE))
    quantized = max(0, min(127, int((normalized + 1.0) * 63.5 + 0.5)))
    return quantized | (128 if candidate_legal(obs, macro) else 0)


def _copy_action(action):
    return copy.deepcopy(action)


def _base_action(obs, fixed_crop=None, plant_limit=-1, target=0,
                 job_flags=JOB_ALL):
    """Mirror ``kag_bot_action_filtered_ex`` for structured mode."""
    farm = _farm(obs)
    private = _private(obs)
    rank = TOP._crop_rank(obs, _player(obs))
    seed_budget = dict(_get(private, "seeds", {}))
    seed_need = {crop: 0 for crop in CROPS}
    jobs = []
    for y, row in enumerate(_tiles(obs)):
        for x, tile in enumerate(row):
            if not _is_animal(tile):
                continue
            if job_flags & JOB_MAINTAIN and not bool(_get(tile, "fed_today", False)):
                jobs.append((0, x, y, "FEED", None))
            elif job_flags & JOB_HARVEST and int(_get(tile, "yield_units", 0)) > 0:
                jobs.append((1, x, y, "HARVEST", None))
            elif job_flags & JOB_HARVEST and bool(_get(tile, "fertilizer_available", False)):
                jobs.append((1, x, y, "COLLECT_FERTILIZER", None))
            elif job_flags & JOB_MAINTAIN and not bool(_get(tile, "cared_today", False)):
                jobs.append((2, x, y, "CARE", None))
    requested = 0
    planned = 0
    slot = 0
    for y, row in enumerate(_tiles(obs)):
        for x, tile in enumerate(row):
            if tile == "LOCKED":
                continue
            kind = _kind(tile)
            if kind == "PLANT":
                planted = int(_get(tile, "planted_day", _get(obs, "day", 0)))
                age = int(_get(obs, "day", 0)) - planted
                first = TOP.CROP_DEF[_get(tile, "crop")][1]
                missed = int(_get(tile, "consecutive_unwatered", 0))
                if job_flags & JOB_MAINTAIN and _plant_needs_water(obs, tile):
                    jobs.append((0 if missed else 2, x, y, "WATER", None))
                elif (job_flags & JOB_HARVEST
                      and int(_get(tile, "yield_units", 0)) > 0 and age >= first):
                    jobs.append((1, x, y, "HARVEST", None))
            elif (kind == "WEED" and job_flags & JOB_DIG
                  and (not target or _quadrant(x, y) == target)):
                jobs.append((3, x, y, "DIG", None))
            elif tile is None and job_flags & JOB_PLANT:
                # Filtering precedes slot assignment. This is observable for
                # targets other than NW because crop slots are row-major.
                if target and _quadrant(x, y) != target:
                    continue
                crop_slot = slot
                slot += 1
                crop = fixed_crop if fixed_crop is not None else TOP._crop_for(crop_slot, rank)
                # Viability is lifecycle-only; a temporarily low visible
                # product quote cannot veto a crop with future yield events.
                viable = _crop_events(obs, CROPS.index(crop)) > 0
                if (plant_limit < 0 or requested < plant_limit) and viable:
                    seed_need[crop] += 1
                    requested += 1
                if ((plant_limit < 0 or planned < plant_limit) and viable
                        and int(seed_budget.get(crop, 0)) > 0):
                    jobs.append((4, x, y, "PLANT", crop))
                    seed_budget[crop] = int(seed_budget.get(crop, 0)) - 1
                    planned += 1

    positions = _positions(obs)
    commands = [["PASS"] for _ in positions]
    unfed = sum(_is_animal(tile) and not bool(_get(tile, "fed_today", False))
                for row in _tiles(obs) for tile in row)
    pickup_assigned = False
    claimed = set()
    # Native commits jobs under a worker before global routing.
    for worker, (x, y) in enumerate(positions):
        inv = _inventory(obs, worker)
        candidates = [(priority, index, op, arg)
                      for index, (priority, tx, ty, op, arg) in enumerate(jobs)
                      if index not in claimed and (tx, ty) == (x, y)
                      and (op != "FEED" or int(_get(inv, "WHEAT", 0)) > 0)]
        if candidates:
            _priority, index, op, arg = min(candidates)
            commands[worker] = [op, arg] if arg is not None else [op]
            claimed.add(index)
    for worker, (x, y) in enumerate(positions):
        if commands[worker][0] != "PASS":
            continue
        inv = _inventory(obs, worker)
        if (unfed and int(_get(inv, "WHEAT", 0)) == 0 and not pickup_assigned
                and int(_get(_shed(obs), "WHEAT", 0)) > 0):
            if (x, y) in ((4, 4), (5, 4), (4, 5), (5, 5)):
                commands[worker] = ["PICKUP", "WHEAT",
                                    min(unfed, int(_get(_shed(obs), "WHEAT", 0)))]
            else:
                commands[worker] = _route(farm, (x, y), 4, 4)
            pickup_assigned = True
            continue
        best = None
        best_score = 2 ** 31 - 1
        for index, (priority, tx, ty, op, _arg) in enumerate(jobs):
            if index in claimed:
                continue
            if op == "FEED" and int(_get(inv, "WHEAT", 0)) <= 0:
                continue
            score = priority * 32 + abs(x - tx) + abs(y - ty)
            if score < best_score:
                best, best_score = index, score
        if best is None:
            continue
        claimed.add(best)
        _priority, tx, ty, op, arg = jobs[best]
        commands[worker] = ([op, arg] if arg is not None else [op]) \
            if (x, y) == (tx, ty) else _route(farm, (x, y), tx, ty)

    market = []
    for product in PRODUCTS:
        count = int(_get(_shed(obs), product, 0))
        if product == "WHEAT" and unfed:
            count -= unfed
        if count > 0 and len(market) < 10:
            market.append(["SELL", product, count])
    land = len(_get(farm, "unlocked_quadrants", ()))
    missing_feed = _feed_shortfall(obs)
    if missing_feed > 0 and len(market) < MAX_MARKET_ORDERS:
        market.append(["BUY_PRODUCT", "WHEAT", missing_feed])
    if not _liquidation(obs):
        if (fixed_crop is None and int(_get(obs, "day", 0)) in (4, 9)
                and land < 3 and int(_get(farm, "money", 0)) >= (1500 if land == 1 else 3000)
                and len(market) < MAX_MARKET_ORDERS):
            market.append(["BUY_LAND"])
        for crop in CROPS:
            missing = seed_need[crop] - int(_get(_seeds(obs), crop, 0))
            if missing > 0 and len(market) < MAX_MARKET_ORDERS:
                market.append(["BUY_SEED", crop, missing])
        desired = 4 if land == 1 else 8 if land == 2 else 12
        for _ in range(len(_get(farm, "hands", ())), desired):
            if len(market) >= MAX_MARKET_ORDERS:
                break
            market.append(["HIRE"])
    return {"farmer": commands[0], "hands": commands[1:], "market": market}


def _feed_order(order):
    return (isinstance(order, list) and len(order) >= 2
            and order[0] == "BUY_PRODUCT" and order[1] == "WHEAT")


def _keep_feed_and(obs, action, predicate):
    """Mirror ``kag_macro_keep_feed_and`` including its feed reservation."""
    kept = []
    feed_remaining = _feed_shortfall(obs)
    for source in action.get("market", ()):
        order = list(source)
        feed = _feed_order(order)
        selected = predicate(order)
        if feed and not selected:
            if feed_remaining <= 0:
                continue
            order[2] = min(int(order[2]), feed_remaining)
            feed_remaining -= int(order[2])
        if feed or selected:
            kept.append(order)
    action["market"] = kept[:MAX_MARKET_ORDERS]


def _prioritize_feed(action):
    market = action.get("market", [])
    action["market"] = ([order for order in market if _feed_order(order)]
                        + [order for order in market if not _feed_order(order)])


def _strip_commands(action, growth=False):
    commands = [action["farmer"], *action.get("hands", ())]
    blocked = {"PLANT"}
    if growth:
        blocked |= {"BUILD_COOP", "BUILD_PASTURE"}
    for command in commands:
        if command and command[0] in blocked:
            command[:] = ["PASS"]


def _append(action, order):
    if len(action.setdefault("market", [])) < 10:
        action["market"].append(order)


def _cap_orders(action, op, item, maximum):
    kept = []
    remaining = max(0, int(maximum))
    for order in action.get("market", ()):
        matches = order and order[0] == op and (item is None or (len(order) > 1 and order[1] == item))
        if matches:
            if remaining <= 0:
                continue
            amount = int(order[2]) if len(order) > 2 else 1
            amount = min(amount, remaining)
            remaining -= amount
            order = [*order[:2], amount] if len(order) > 1 else list(order)
        kept.append(order)
    action["market"] = kept[:10]


def _assign_public_jobs(obs, action, jobs):
    """Assign selected-animal jobs like ``kag_public_assign_jobs``."""
    positions = _positions(obs)
    commands = [action["farmer"], *action.get("hands", ())]
    claimed = set()

    # Preserve and claim local maintenance already committed by _base_action.
    for unit, (x, y) in enumerate(positions):
        command = commands[unit]
        if not command or command[0] == "PASS":
            continue
        command_arg = command[1] if len(command) > 1 else None
        for index, (_priority, tx, ty, op, arg) in enumerate(jobs):
            if index in claimed or (tx, ty) != (x, y) or op != command[0]:
                continue
            if arg is None or command_arg is None or arg == command_arg:
                claimed.add(index)
                break

    for unit, (x, y) in enumerate(positions):
        command = commands[unit]
        if command and command[0] != "PASS":
            continue
        inventory = _inventory(obs, unit)
        best = None
        best_score = 2 ** 31 - 1
        for index, (priority, tx, ty, op, arg) in enumerate(jobs):
            if index in claimed:
                continue
            if op == "FEED" and int(_get(inventory, "WHEAT", 0)) <= 0:
                continue
            if op == "PICKUP":
                if arg in ("WHEAT", "FERTILIZER") and int(_get(inventory, arg, 0)) > 0:
                    continue
                if arg in ANIMALS and any(
                        int(_get(inventory, animal, 0)) > 0 for animal in ANIMALS):
                    continue
            if op == "PLACE":
                if arg not in ANIMALS or int(_get(inventory, arg, 0)) <= 0:
                    continue
                target = _tiles(obs)[ty][tx]
                animal_id = ANIMALS.index(arg)
                if (_kind(target) != ANIMAL_DEF[animal_id][1]
                        or _get(target, "animal") is not None):
                    continue
            score = priority * 64 + abs(x - tx) + abs(y - ty)
            if score < best_score:
                best, best_score = index, score
        if best is None:
            continue
        claimed.add(best)
        _priority, tx, ty, op, arg = jobs[best]
        if (x, y) == (tx, ty):
            command[:] = [op, arg, 1] if arg is not None else [op]
        else:
            command[:] = _route(_farm(obs), (x, y), tx, ty)


def _selected_animal_action(obs, animal_id, quantity):
    """Execute exactly one policy-selected livestock species and batch."""
    action = _base_action(obs, job_flags=JOB_MAINTAIN)
    _keep_feed_and(
        obs, action,
        lambda order: order[:2] == ["BUY_ANIMAL", ANIMALS[animal_id]],
    )
    action["market"] = [order for order in action["market"]
                        if not (order and order[0] == "BUY_ANIMAL")]

    animal = ANIMALS[animal_id]
    structure = ANIMAL_DEF[animal_id][1]
    quantity = max(1, int(quantity))
    selected_stock = _item_stock(obs, animal)
    selected_batch_stock = min(selected_stock, quantity)
    empty_room = _animal_room(obs, animal_id)
    desired_purchase = max(0, quantity - selected_batch_stock)
    build_needed = max(0, quantity - empty_room)

    jobs = []
    place_slots = min(selected_batch_stock, empty_room)
    for y, row in enumerate(_tiles(obs)):
        for x, tile in enumerate(row):
            if (place_slots > 0 and _kind(tile) == structure
                    and _get(tile, "animal") is None):
                jobs.append((0, x, y, "PLACE", animal))
                place_slots -= 1

    carried = sum(int(_get(_inventory(obs, unit), animal, 0))
                  for unit in range(len(_positions(obs))))
    carried_batch = min(carried, quantity)
    pickup_count = min(quantity - carried_batch,
                       empty_room - carried_batch,
                       int(_get(_shed(obs), animal, 0)))
    for _ in range(max(0, pickup_count)):
        jobs.append((1, 4, 4, "PICKUP", animal))

    build_op = "BUILD_COOP" if structure == "COOP" else "BUILD_PASTURE"
    reserved = set()
    for x, y in STRUCTURE_POSITIONS:
        if build_needed <= 0:
            break
        if _tiles(obs)[y][x] is not None:
            continue
        jobs.append((2, x, y, build_op, None))
        reserved.add((x, y))
        build_needed -= 1
    for y, row in enumerate(_tiles(obs)):
        for x, tile in enumerate(row):
            if build_needed <= 0:
                break
            if tile is not None or (x, y) in reserved:
                continue
            jobs.append((2, x, y, build_op, None))
            build_needed -= 1
        if build_needed <= 0:
            break

    _assign_public_jobs(obs, action, jobs)
    commands = [action["farmer"], *action.get("hands", ())]
    built_now = sum(bool(command) and command[0] == build_op for command in commands)
    picked_now = sum(bool(command) and command[0] == "PICKUP"
                     and len(command) > 1 and command[1] == animal
                     for command in commands)
    capacity = max(0, empty_room + built_now - selected_batch_stock)
    desired_purchase = min(desired_purchase, capacity)
    shed_room = max(0, SHED_CAPACITY - _shed_total(obs)
                    + picked_now - _feed_shortfall(obs))
    desired_purchase = min(desired_purchase, shed_room)
    money_after_feed = int(_get(_farm(obs), "money", 0)) - _feed_cost(obs)
    affordable = max(0, money_after_feed // ANIMAL_COST[animal_id])
    desired_purchase = min(desired_purchase, affordable)
    if not _liquidation(obs) and desired_purchase > 0:
        _append(action, ["BUY_ANIMAL", animal, desired_purchase])
    return action


def _diversify_action(obs):
    """Small public-state bridge for the native Structured public planner."""
    action = _base_action(obs)
    # Keep the historical portable approximation isolated from selected
    # livestock intents: it may open the first pasture, but never buys or
    # places an unselected animal.
    if not any(_kind(tile) == "PASTURE" for row in _tiles(obs) for tile in row):
        commands = [action["farmer"], *action.get("hands", ())]
        for x, y in STRUCTURE_POSITIONS:
            if _tiles(obs)[y][x] is not None:
                continue
            for unit, position in enumerate(_positions(obs)):
                if commands[unit][0] != "PASS":
                    continue
                commands[unit][:] = (["BUILD_PASTURE"] if tuple(position) == (x, y)
                                     else _route(_farm(obs), position, x, y))
                return action
    return action


def execute_macro(obs, macro, quantity, target):
    quantity = max(1, int(quantity))
    if not candidate_legal(obs, macro):
        macro = MACRO_HOLD
    if MACRO_PLANT_BASE <= macro < MACRO_PLANT_BASE + 5:
        crop = CROPS[macro - MACRO_PLANT_BASE]
        action = _base_action(obs, crop, quantity, target,
                              JOB_MAINTAIN | JOB_DIG | JOB_PLANT)
        _keep_feed_and(obs, action,
                       lambda order: order[:2] == ["BUY_SEED", crop])
        _cap_orders(action, "BUY_SEED", crop, quantity)
        return action
    if MACRO_ANIMAL_BASE <= macro < MACRO_ANIMAL_BASE + 3:
        animal_id = macro - MACRO_ANIMAL_BASE
        return _selected_animal_action(obs, animal_id, quantity)
    if macro == MACRO_EXPAND:
        action = _base_action(obs, job_flags=JOB_MAINTAIN)
        _keep_feed_and(obs, action,
                       lambda order: order and order[0] == "BUY_LAND")
        action["market"] = [order for order in action["market"] if order[0] != "BUY_LAND"]
        _strip_commands(action)
        if not _liquidation(obs):
            _append(action, ["BUY_LAND"])
        return action
    if MACRO_SELL_BASE <= macro < MACRO_SELL_BASE + 9:
        item = PRODUCTS[macro - MACRO_SELL_BASE]
        action = _base_action(obs, job_flags=JOB_MAINTAIN)
        _keep_feed_and(obs, action,
                       lambda order: order[:2] == ["SELL", item])
        _cap_orders(action, "SELL", item, quantity)
        if int(_get(_shed(obs), item, 0)) > 0 and not any(
                order[:2] == ["SELL", item] for order in action["market"]):
            _append(action, ["SELL", item, quantity])
        _strip_commands(action)
        return action
    if macro in (MACRO_SELL_ALL, MACRO_CASH_OUT):
        action = _base_action(obs, job_flags=JOB_MAINTAIN)
        _keep_feed_and(obs, action,
                       lambda order: order and order[0] == "SELL")
        if macro == MACRO_SELL_ALL:
            prices = _get(_get(obs, "market", {}), "prices", {})
            sales = [order for order in action["market"] if order[0] == "SELL"]
            sales.sort(key=lambda order: int(_get(prices, order[1], 0)),
                       reverse=True)
            sales_iter = iter(sales)
            action["market"] = [next(sales_iter) if order[0] == "SELL" else order
                                for order in action["market"]]
            _cap_orders(action, "SELL", None, quantity)
        _strip_commands(action)
        return action
    if MACRO_BUY_SEED_BASE <= macro < MACRO_BUY_SEED_BASE + 5:
        crop_id = macro - MACRO_BUY_SEED_BASE
        crop = CROPS[crop_id]
        quantity = min(quantity, _seed_purchase_room(obs))
        if _liquidation(obs) or _crop_events(obs, crop_id) <= 0:
            quantity = 0
        action = _base_action(obs, crop, job_flags=JOB_MAINTAIN)
        _keep_feed_and(obs, action,
                       lambda order: order[:2] == ["BUY_SEED", crop])
        action["market"] = [order for order in action["market"]
                            if order[:2] != ["BUY_SEED", crop]]
        if quantity > 0:
            _append(action, ["BUY_SEED", crop, quantity])
        _strip_commands(action)
        return action
    if MACRO_BUY_ANIMAL_BASE <= macro < MACRO_BUY_ANIMAL_BASE + 3:
        animal_id = macro - MACRO_BUY_ANIMAL_BASE
        animal = ANIMALS[animal_id]
        quantity = min(quantity, _animal_purchase_room(obs, animal_id))
        if _liquidation(obs) or _animal_events(obs, animal_id) <= 0:
            quantity = 0
        action = _base_action(obs, job_flags=JOB_MAINTAIN)
        _keep_feed_and(obs, action,
                       lambda order: order[:2] == ["BUY_ANIMAL", animal])
        action["market"] = [order for order in action["market"]
                            if not (order and order[0] == "BUY_ANIMAL")]
        if quantity > 0:
            _append(action, ["BUY_ANIMAL", animal, quantity])
        _strip_commands(action)
        return action
    if macro == MACRO_HIRE:
        action = _base_action(obs, job_flags=JOB_MAINTAIN)
        room = max(0, _desired_hands(obs) - len(_get(_farm(obs), "hands", ())))
        quantity = 0 if _liquidation(obs) else min(quantity, room)
        _keep_feed_and(obs, action,
                       lambda order: order and order[0] == "HIRE")
        action["market"] = [order for order in action["market"] if order[0] != "HIRE"]
        for _ in range(quantity):
            _append(action, ["HIRE"])
        _strip_commands(action)
        return action
    if macro == MACRO_DIVERSIFY:
        action = _diversify_action(obs)
        _prioritize_feed(action)
        return action
    if macro in (MACRO_HARVEST, MACRO_MAINTAIN):
        action = _base_action(obs, job_flags=(JOB_HARVEST
                                              if macro == MACRO_HARVEST
                                              else JOB_MAINTAIN))
        _keep_feed_and(obs, action, lambda _order: False)
        return action
    if macro in (MACRO_BUY_WHEAT, MACRO_BUY_FERTILIZER):
        item = "WHEAT" if macro == MACRO_BUY_WHEAT else "FERTILIZER"
        if item == "FERTILIZER" and _liquidation(obs):
            quantity = 0
        action = _base_action(obs, job_flags=JOB_MAINTAIN)
        _keep_feed_and(obs, action,
                       lambda order: order[:2] == ["BUY_PRODUCT", item])
        action["market"] = [order for order in action["market"]
                            if order[:2] != ["BUY_PRODUCT", item]]
        if item == "WHEAT":
            quantity = max(quantity, _feed_shortfall(obs))
        if quantity > 0:
            _append(action, ["BUY_PRODUCT", item, quantity])
        _strip_commands(action)
        return action
    action = _base_action(obs, job_flags=JOB_MAINTAIN)
    _keep_feed_and(obs, action, lambda _order: False)
    _strip_commands(action, growth=True)
    return action


class NativeMacroRuntime:
    """Stateful mode-2 adapter for one or both competition seats."""

    def __init__(self):
        self._state = {}

    def reset(self, player=None):
        if player is None:
            self._state.clear()
        else:
            self._state[player] = {"last_step": -1, "intent": 0,
                                   "quantity": 0, "target": 0, "ticks": 0}

    def _entry(self, obs):
        player = _player(obs)
        step = _step(obs)
        entry = self._state.get(player)
        if entry is None or step == 0 or step < entry["last_step"]:
            self.reset(player)
            entry = self._state[player]
        entry["last_step"] = step
        return entry

    def fill_observation(self, obs, encoded):
        entry = self._entry(obs)
        encoded[OBS_OFFSET:OBS_OFFSET + MACRO_COUNT] = [
            score_byte(obs, macro) for macro in range(MACRO_COUNT)
        ]
        encoded[OBS_OFFSET + MACRO_COUNT] = _u8_scale(entry["ticks"], 1)
        encoded[OBS_OFFSET + MACRO_COUNT + 1] = _u8_scale(entry["intent"], MACRO_COUNT - 1)
        encoded[OBS_OFFSET + MACRO_COUNT + 2] = _u8_scale(entry["quantity"], 64)
        encoded[OBS_OFFSET + MACRO_COUNT + 3] = _u8_scale(entry["target"], 8)
        return encoded

    def action_mask(self, obs):
        mask = np.zeros(MASK_SIZE, dtype=bool)
        for macro in range(MACRO_COUNT):
            mask[macro] = candidate_legal(obs, macro)
        mask[MACRO_HOLD] = True
        quantity_base = UNIT_COMMANDS
        mask[quantity_base:quantity_base + len(QUANTITIES)] = True
        target_base = 2 * UNIT_COMMANDS
        unlocked = _unlocked_mask(obs)
        for index, target in enumerate(TARGETS):
            mask[target_base + index] = (target == 0
                or bool(unlocked & target)
                and _reclaimable_tiles_in_target(obs, target) > 0)
        for unit in range(3, UNIT_HEADS):
            mask[unit * UNIT_COMMANDS] = True
        market_base = UNIT_HEADS * UNIT_COMMANDS
        for slot in range(MARKET_SLOTS):
            mask[market_base + slot * MARKET_STRIDE] = True
        return mask

    def decode(self, obs, actions):
        entry = self._entry(obs)
        macro = int(actions[0])
        if not candidate_legal(obs, macro):
            macro = MACRO_HOLD
        quantity_bin = max(0, min(len(QUANTITIES) - 1, int(actions[1])))
        target_bin = max(0, min(len(TARGETS) - 1, int(actions[2])))
        quantity = QUANTITIES[quantity_bin]
        target = TARGETS[target_bin]
        if target and not (_unlocked_mask(obs) & target):
            target = 0
        entry.update(intent=macro, quantity=quantity, target=target, ticks=0)
        return execute_macro(obs, macro, quantity, target)
