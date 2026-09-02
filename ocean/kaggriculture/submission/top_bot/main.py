"""Pure-Python export of PufferLib's native Kaggriculture top hybrid bot."""

CROPS = ("WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON")
PRODUCTS = CROPS + ("EGG", "MILK", "WOOL", "FERTILIZER")
CROP_DEF = {
    "WHEAT": (10, 2, 4, 0, 6, False),
    "CARROT": (20, 2, 3, 0, 4, False),
    "TOMATO": (50, 8, 8, 1, 4, True),
    "STRAWBERRY": (100, 10, 10, 2, 4, True),
    "MELON": (80, 10, 12, 0, 6, False),
}
SHOP_PRODUCTS = {
    "BAKERY": ("EGG", "WHEAT"),
    "BRUNCH_SPOT": ("EGG", "WHEAT", "STRAWBERRY"),
    "FARMERS_MARKET": ("WHEAT", "CARROT", "TOMATO", "STRAWBERRY"),
    "ICE_CREAM_SHOP": ("STRAWBERRY", "MILK", "WHEAT"),
    "PET_CAFE": ("CARROT",),
    "PIZZA_SHOP": ("MILK", "TOMATO", "WHEAT"),
    "SMOOTHIE_SHOP": ("STRAWBERRY", "MILK"),
    "YARN_STORE": ("WOOL",),
}

P = ("PASS",)


def _u(op, arg=None, n=None):
    out = [op]
    if arg is not None:
        out.append(arg)
    if n is not None:
        out.append(n)
    return out


def _m(op, arg=None, n=None):
    return _u(op, arg, n)


# Exact first 26 frames decoded from KG_TAPE_TOP_B64. Missing hand commands
# are PASS, which matters because the number of hired hands is state-dependent.
OPENING = (
    (P, (), ()),
    (P, (), ((_m("HIRE"),) * 5) + (_m("BUY_ANIMAL", "COW", 2), _m("BUY_ANIMAL", "SHEEP", 2), _m("BUY_SEED", "WHEAT", 7), _m("BUY_SEED", "MELON", 12), _m("BUY_PRODUCT", "WHEAT", 5))),
    ((_u("PICKUP", "SHEEP", 1)), (_u("WEST"), _u("NORTH"), _u("WEST"), _u("WEST"), _u("WEST")), (_m("SELL", "WHEAT", 1), _m("SELL", "WHEAT", 1))),
    ((_u("PICKUP", "WHEAT", 1)), (_u("PICKUP", "COW", 2), _u("BUILD_PASTURE"), _u("WEST"), _u("WEST"), _u("WEST")), (_m("BUY_PRODUCT", "WHEAT", 1),)),
    ((_u("WEST")), (_u("PICKUP", "SHEEP", 1), _u("WEST"), _u("WEST"), _u("WEST"), _u("WEST")), ()),
    ((_u("WEST")), (_u("PICKUP", "WHEAT", 3), _u("BUILD_PASTURE"), _u("WEST"), _u("WEST"), _u("WEST")), (_m("BUY_PRODUCT", "WHEAT", 1),)),
    ((_u("BUILD_PASTURE")), (_u("NORTH"), _u("NORTH"), _u("WEST"), _u("NORTH"), _u("WEST")), ()),
    ((_u("PLACE", "SHEEP", 1)), (_u("BUILD_PASTURE"), _u("NORTH"), _u("NORTH"), _u("NORTH"), _u("NORTH")), ()),
    ((_u("FEED")), (_u("PLACE", "COW", 1), _u("PLANT", "MELON"), _u("PLANT", "MELON"), _u("NORTH"), _u("NORTH")), ()),
    ((_u("CARE")), (_u("FEED"), _u("WATER"), _u("WATER"), _u("PLANT", "WHEAT"), _u("PLANT", "MELON")), ()),
    (P, (_u("CARE"), _u("NORTH"), _u("EAST"), _u("WATER"), _u("WATER")), ()),
    (P, (_u("NORTH"), _u("PLANT", "MELON"), _u("EAST"), _u("EAST"), _u("SOUTH")), ()),
    (P, (_u("BUILD_PASTURE"), _u("WATER"), _u("EAST"), _u("PLANT", "WHEAT"), _u("PLANT", "MELON")), ()),
    (P, (_u("PLACE", "COW", 1), _u("NORTH"), _u("EAST"), _u("WATER"), _u("WATER")), ()),
    (P, (_u("FEED"), _u("PLANT", "WHEAT"), _u("NORTH"), _u("EAST"), _u("EAST")), ()),
    (P, (_u("CARE"), _u("WATER"), _u("NORTH"), _u("PLANT", "MELON"), _u("PLANT", "MELON")), ()),
    (P, (_u("WEST"), _u("WEST"), _u("NORTH"), _u("WATER"), _u("WATER")), ()),
    (P, (_u("SOUTH"), _u("PLANT", "WHEAT"), _u("PLANT", "MELON"), _u("SOUTH"), _u("EAST")), ()),
    (P, (_u("BUILD_PASTURE"), _u("WATER"), _u("WATER"), _u("PLANT", "MELON"), _u("PLANT", "MELON")), ()),
    (P, (_u("PLACE", "SHEEP", 1), _u("WEST"), _u("NORTH"), _u("WATER"), _u("WATER")), ()),
    (P, (_u("FEED"), _u("PLANT", "WHEAT"), _u("PLANT", "WHEAT"), _u("WEST"), _u("WEST")), ()),
    (P, (_u("CARE"), _u("WATER"), _u("WATER"), _u("PLANT", "MELON"), _u("SOUTH")), ()),
    (P, (_u("PASS"), _u("WEST"), _u("PASS"), _u("WATER"), _u("PLANT", "MELON")), ()),
    (P, (_u("PASS"), _u("PLANT", "WHEAT"), _u("PASS"), _u("PASS"), _u("WATER")), ()),
    (P, (_u("PASS"), _u("WATER"), _u("PASS"), _u("PASS"), _u("PASS")), ()),
    (P, (), ()),
)


def _copy(action):
    return list(action)


def _opening(obs, me, private, step):
    farmer, scripted_hands, market = OPENING[step]
    hands = [_copy(scripted_hands[i] if i < len(scripted_hands) else P)
             for i in range(len(me["hands"]))]
    commands = [_copy(farmer)] + hands
    positions = [me["farmer"]] + list(me["hands"])
    inventories = private["inventories"]
    tiles = me["tiles"]
    for i, (x, y) in enumerate(positions):
        tile = tiles[y][x]
        command = commands[i]
        if not isinstance(tile, dict):
            continue
        kind = tile.get("kind")
        if kind == "WEED":
            commands[i] = ["DIG"]
        elif kind == "PLANT" and not tile.get("watered_today", False):
            commands[i] = ["WATER"]
        elif kind in ("COOP", "PASTURE") and tile.get("animal") is not None:
            inv = inventories[i] if i < len(inventories) else {}
            if not tile.get("fed_today", False) and inv.get("WHEAT", 0) > 0:
                commands[i] = ["FEED"]
            elif tile.get("yield_units", 0) > 0 and command[0] == "PASS":
                commands[i] = ["HARVEST"]
            elif tile.get("fertilizer_available", False) and command[0] == "PASS":
                commands[i] = ["COLLECT_FERTILIZER"]
            elif tile.get("fed_today", False) and not tile.get("cared_today", False) and command[0] == "PASS":
                commands[i] = ["CARE"]
    return {"farmer": commands[0], "hands": commands[1:],
            "market": [_copy(order) for order in market]}


def _crop_rank(obs, player):
    own = {crop: 0 for crop in CROPS}
    opponent = {crop: 0 for crop in CROPS}
    demand = {crop: 1 for crop in CROPS}
    for pid, farm in enumerate(obs["farms"]):
        counts = own if pid == player else opponent
        for row in farm["tiles"]:
            for tile in row:
                if isinstance(tile, dict) and tile.get("kind") == "PLANT":
                    counts[tile["crop"]] += 1
    for shop in obs["town"]["unlocked_shops"]:
        products = SHOP_PRODUCTS[shop]
        multiplier = 2 if len(products) == 1 else 1
        for product in products:
            if product in demand:
                demand[product] += multiplier
    scores = {}
    remaining = 30 - obs["day"]
    for crop in CROPS:
        seed_cost, first, _max_day, interval, max_yield, ongoing = CROP_DEF[crop]
        events = 0
        if remaining > first:
            events = 1 + (remaining - first - 1) // interval if ongoing else max_yield
            events = min(events, max_yield)
        gross = max(1.0, events * obs["market"]["prices"][crop] - seed_cost)
        scores[crop] = gross * (1.0 + 0.12 * demand[crop]) / (
            1.0 + 0.12 * own[crop] + 0.06 * opponent[crop])
    # Python's stable sort matches the C insertion sort's tie behavior.
    return sorted(CROPS, key=lambda crop: scores[crop], reverse=True)


def _crop_for(slot, rank):
    if slot % 8 == 0:
        return "WHEAT"
    bucket = slot % 10
    return rank[0] if bucket < 5 else rank[1] if bucket < 8 else rank[2]


def _quadrant_unlocked(me, x, y):
    quadrant = "SE" if x >= 5 and y >= 5 else "NE" if x >= 5 else "SW" if y >= 5 else "NW"
    return quadrant in me["unlocked_quadrants"]


def _route(me, unit, tx, ty):
    x, y = unit
    unlocked = me["unlocked_quadrants"]
    if not _quadrant_unlocked(me, x, y):
        if x >= 5 and y >= 5:
            if "NE" in unlocked:
                return ["NORTH"]
            if "SW" in unlocked:
                return ["WEST"]
            # A newly hired hand may spawn on the shed's locked SE access
            # cell while only NW is owned. Movement is bounds-only, so escape
            # through locked NE and then route west on the following turn.
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


def _remaining_days(obs):
    step = obs.get("step", obs["day"] * 24 + obs["hour"])
    remaining = 720 - step
    return 0 if remaining <= 0 else (remaining + 23) // 24


def _crop_events(obs, crop):
    _cost, first, _max_day, interval, max_yield, ongoing = CROP_DEF[crop]
    remaining = _remaining_days(obs)
    if remaining <= first:
        return 0
    events = 1 + (remaining - first - 1) // interval if ongoing else max_yield
    return min(events, max_yield)


def _dynamic(obs, me, private):
    rank = _crop_rank(obs, obs["player"])
    seed_budget = dict(private["seeds"])
    seed_need = {crop: 0 for crop in CROPS}
    jobs = []
    # Match kag_bot_jobs_ex: occupied animal tiles are a separate row-major
    # pass, so their ordering remains ahead of crop work.
    for y, row in enumerate(me["tiles"]):
        for x, tile in enumerate(row):
            if not (isinstance(tile, dict)
                    and tile.get("kind") in ("COOP", "PASTURE")
                    and tile.get("animal")):
                continue
            if not tile.get("fed_today"):
                jobs.append((0, x, y, "FEED", None))
            elif tile.get("yield_units", 0) > 0:
                jobs.append((1, x, y, "HARVEST", None))
            elif tile.get("fertilizer_available"):
                jobs.append((1, x, y, "COLLECT_FERTILIZER", None))
            elif not tile.get("cared_today"):
                jobs.append((2, x, y, "CARE", None))

    planting_slot = 0
    for y, row in enumerate(me["tiles"]):
        for x, tile in enumerate(row):
            if tile == "LOCKED":
                continue
            if isinstance(tile, dict) and tile.get("kind") == "PLANT":
                planted = tile["planted_day"]
                age = obs["day"] - planted
                _cost, first, max_day, _interval, _yield, ongoing = CROP_DEF[tile["crop"]]
                bonus = not ongoing and (max_day + 1) // 2 <= age <= max_day
                missed = tile.get("consecutive_unwatered", 0)
                must_water = missed > 0 or bonus or ongoing
                if not tile.get("watered_today", False) and must_water:
                    jobs.append((0 if missed else 2, x, y, "WATER", None))
                elif tile.get("yield_units", 0) > 0 and age >= first:
                    jobs.append((1, x, y, "HARVEST", None))
            elif isinstance(tile, dict) and tile.get("kind") == "WEED":
                jobs.append((3, x, y, "DIG", None))
            elif tile is None:
                crop = _crop_for(planting_slot, rank)
                planting_slot += 1
                viable = _crop_events(obs, crop) > 0
                if viable:
                    seed_need[crop] += 1
                if viable and seed_budget.get(crop, 0) > 0:
                    jobs.append((4, x, y, "PLANT", crop))
                    seed_budget[crop] -= 1

    positions = [me["farmer"]] + list(me["hands"])
    commands = [["PASS"] for _ in positions]
    unfed = sum(1 for row in me["tiles"] for tile in row
                if isinstance(tile, dict)
                and tile.get("kind") in ("COOP", "PASTURE")
                and tile.get("animal") and not tile.get("fed_today"))
    pickup_assigned = False
    claimed = set()

    # Native commits a job already under each worker before considering any
    # route. This prevents a worker standing on work from walking away after
    # an earlier worker claimed an unrelated global job.
    for worker, (x, y) in enumerate(positions):
        inv = private["inventories"][worker]
        candidates = [
            (priority, index, op, arg)
            for index, (priority, tx, ty, op, arg) in enumerate(jobs)
            if index not in claimed and (tx, ty) == (x, y)
            and (op != "FEED" or inv.get("WHEAT", 0) > 0)
        ]
        if candidates:
            _priority, index, op, arg = min(candidates)
            commands[worker] = [op, arg] if arg is not None else [op]
            claimed.add(index)

    for worker, (x, y) in enumerate(positions):
        if commands[worker][0] != "PASS":
            continue
        inv = private["inventories"][worker]
        if (unfed and inv.get("WHEAT", 0) == 0 and not pickup_assigned
                and private["shed"].get("WHEAT", 0) > 0):
            if (x, y) in ((4, 4), (5, 4), (4, 5), (5, 5)):
                commands[worker] = ["PICKUP", "WHEAT",
                    min(unfed, private["shed"]["WHEAT"])]
            else:
                commands[worker] = _route(me, (x, y), 4, 4)
            pickup_assigned = True
            continue
        best = None
        best_score = 2 ** 31 - 1
        for j, (priority, tx, ty, op, arg) in enumerate(jobs):
            if j in claimed:
                continue
            if op == "FEED" and inv.get("WHEAT", 0) <= 0:
                continue
            score = priority * 32 + abs(x - tx) + abs(y - ty)
            if score < best_score:
                best, best_score = j, score
        if best is None:
            continue
        claimed.add(best)
        _priority, tx, ty, op, arg = jobs[best]
        commands[worker] = ([op, arg] if arg is not None else [op]) if (x, y) == (tx, ty) else _route(me, (x, y), tx, ty)

    market = []
    for product in PRODUCTS:
        count = private["shed"].get(product, 0)
        if product == "WHEAT" and unfed:
            count -= unfed
        if count > 0 and len(market) < 10:
            market.append(["SELL", product, count])
    carried_wheat = sum(inv.get("WHEAT", 0)
                        for inv in private["inventories"])
    missing_feed = unfed - private["shed"].get("WHEAT", 0) - carried_wheat
    if missing_feed > 0 and len(market) < 10:
        market.append(["BUY_PRODUCT", "WHEAT", missing_feed])

    # The final-two-day freeze suppresses new capital, never feed for existing
    # animals. This return deliberately follows the mandatory feed order.
    if _remaining_days(obs) <= 2:
        return {"farmer": commands[0], "hands": commands[1:], "market": market}

    land = len(me["unlocked_quadrants"])
    if (obs["day"] in (4, 9) and land < 3
            and me["money"] >= (1500 if land == 1 else 3000)
            and len(market) < 10):
        market.append(["BUY_LAND"])
    for crop in CROPS:
        missing = seed_need[crop] - private["seeds"].get(crop, 0)
        if missing > 0 and len(market) < 10:
            market.append(["BUY_SEED", crop, missing])
    desired = 4 if land == 1 else 8 if land == 2 else 12
    for _ in range(len(me["hands"]), desired):
        if len(market) >= 10:
            break
        market.append(["HIRE"])
    return {"farmer": commands[0], "hands": commands[1:], "market": market}


def agent(obs):
    player = obs["player"]
    me = obs["farms"][player]
    step = obs.get("step", obs["day"] * 24 + obs["hour"])
    if step < len(OPENING):
        return _opening(obs, me, obs["private"], step)
    return _dynamic(obs, me, obs["private"])
