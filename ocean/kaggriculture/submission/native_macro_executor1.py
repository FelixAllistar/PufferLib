"""Public-state port of explicit_executor.h; preserve native ordering/tie breaks.

No strategy substitutions: PPO owns purchases, sales and expansion. The executor
only schedules operations and procures mandatory feed, exactly as in training.
"""

import native_macro_runtime as r

RECLAIM = 35
FERTILIZE = 36


def reclaimable(tile):
    return r._kind(tile) == "WEED" or (
        r._kind(tile) in ("COOP", "PASTURE") and not r._is_animal(tile))


def reserved(obs, tile):
    return sum(r._item_stock(obs, species) for s, species in enumerate(r.ANIMALS)
               if r._kind(tile) == r.ANIMAL_DEF[s][1])


def legal(obs, macro):
    if macro in (r.MACRO_DIVERSIFY, r.MACRO_MAINTAIN):
        return False
    tiles = [tile for row in r._tiles(obs) for tile in row]
    if macro == RECLAIM:
        return any(reclaimable(tile) and not reserved(obs, tile) for tile in tiles)
    if macro == FERTILIZE:
        return r._item_stock(obs, "FERTILIZER") > 0 and any(
            r._kind(t) == "PLANT" and r._get(t, "fertilized_until_day", 0)
            < r._get(obs, "day", 0) for t in tiles)
    if 1 <= macro <= 5:
        c = macro - 1
        return r._reclaimable_tiles(obs) > 0 and (
            r._get(r._seeds(obs), r.CROPS[c], 0) > 0
            or r._can_invest_after_feed(obs, r.SEED_COST[c], 0))
    if 6 <= macro <= 8:
        s = macro - 6
        return (r._animal_room(obs, s) > 0 or r._empty_tiles(obs) > 0) and (
            r._item_stock(obs, r.ANIMALS[s]) > 0
            or r._can_invest_after_feed(obs, r.ANIMAL_COST[s], 1))
    if macro == r.MACRO_HIRE:
        return r._market_buy_legal(obs, "HIRE") and r._can_invest_after_feed(
            obs, r._fib(r._get(r._farm(obs), "hires_today", 0)), 0)
    if macro == r.MACRO_EXPAND:
        return r._market_buy_legal(obs, "BUY_LAND") and r._can_invest_after_feed(
            obs, r._land_price(obs), 0)
    if 20 <= macro <= 24:
        c = macro - 20
        return r._seed_purchase_room(obs) > 0 and r._market_buy_legal(
            obs, "BUY_SEED", r.CROPS[c]) and r._can_invest_after_feed(obs, r.SEED_COST[c], 0)
    if 25 <= macro <= 27:
        s = macro - 25
        return r._animal_purchase_room(obs, s) > 0 and r._market_buy_legal(
            obs, "BUY_ANIMAL", r.ANIMALS[s]) and r._can_invest_after_feed(obs, r.ANIMAL_COST[s], 1)
    if macro == r.MACRO_BUY_FERTILIZER:
        price = r._get(r._get(r._get(obs, "market", {}), "prices", {}), "FERTILIZER", 0)
        return r._market_buy_legal(obs, "BUY_PRODUCT", "FERTILIZER") and r._can_invest_after_feed(obs, price, 1)
    return r.candidate_legal(obs, macro)


def execute(obs, macro, quantity, target):
    positions = r._positions(obs)
    inv = [r._inventory(obs, u) for u in range(len(positions))]
    shed = r._shed(obs)
    seeds = r._seeds(obs)
    day = int(r._get(obs, "day", 0))
    quantity = max(1, int(quantity))
    if target and not (r._unlocked_mask(obs) & target):
        target = 0
    crop = macro - 1 if 1 <= macro <= 5 else -1
    animal = macro - 6 if 6 <= macro <= 8 else -1
    feed = r._feed_shortfall(obs)
    free_cash = max(0, int(r._get(r._farm(obs), "money", 0)) - r._feed_cost(obs))
    stock = [r._item_stock(obs, s) for s in r.ANIMALS]
    carried = [sum(r._get(v, s, 0) for v in inv) for s in r.ANIMALS]
    tiles = [(x, y, t) for y, row in enumerate(r._tiles(obs)) for x, t in enumerate(row)]
    unfed = sum(r._is_animal(t) and not r._get(t, "fed_today", False) for _, _, t in tiles)
    budget = {item: 0 for item in r.ITEMS}
    budget["WHEAT"] = min(r._get(shed, "WHEAT", 0), max(0, unfed - sum(r._get(v, "WHEAT", 0) for v in inv)))
    for s, species in enumerate(r.ANIMALS):
        room = r._animal_room(obs, s) - sum(carried[o] for o in range(3)
                    if r.ANIMAL_DEF[o][1] == r.ANIMAL_DEF[s][1])
        budget[species] = min(max(0, room), r._get(shed, species, 0))
    if macro == FERTILIZE:
        budget["FERTILIZER"] = min(r._get(shed, "FERTILIZER", 0), quantity)
    build_budget = 0
    if animal >= 0:
        room = r._animal_room(obs, animal) - sum(stock[s] for s in range(3)
            if s != animal and r.ANIMAL_DEF[s][1] == r.ANIMAL_DEF[animal][1])
        need = min(quantity, stock[animal] + free_cash // r.ANIMAL_COST[animal])
        build_budget = max(0, need - max(0, room))
    plant_budget = min(quantity, r._get(seeds, r.CROPS[crop], 0)) if crop >= 0 else 0
    reclaim_budget = quantity if macro == RECLAIM else 0
    fertilize_budget = quantity if macro == FERTILIZE else 0
    jobs = []

    def add(x, y, priority, op, arg=None):
        if len(jobs) < 320:
            jobs.append((x, y, priority, op, arg))

    for x, y, tile in tiles:
        targeted = not target or r._quadrant(x, y) == target
        kind = r._kind(tile)
        if r._is_animal(tile):
            if not r._get(tile, "fed_today", False):
                add(x, y, 0, "FEED")
            elif r._get(tile, "yield_units", 0) > 0:
                add(x, y, 2, "HARVEST")
            elif not r._get(tile, "cared_today", False):
                add(x, y, 3, "CARE")
            elif r._get(tile, "fertilizer_available", False):
                add(x, y, 4, "COLLECT_FERTILIZER")
        elif kind == "PLANT":
            c = r.CROPS.index(r._get(tile, "crop"))
            age = day - r._get(tile, "planted_day", 0)
            max_day = r.TOP.CROP_DEF[r.CROPS[c]][2]
            if r._plant_needs_water(obs, tile):
                add(x, y, 0 if r._get(tile, "consecutive_unwatered", 0) else 3, "WATER")
            elif r._get(tile, "yield_units", 0) > 0 and age >= r.CROP_DEF[c][1] and (
                    r.CROP_DEF[c][4] or macro == r.MACRO_HARVEST or age >= max_day
                    or r.EPISODE_STEPS - r._step(obs) <= r.TURNS_PER_DAY):
                add(x, y, 2, "HARVEST")
            if fertilize_budget and targeted and r._get(tile, "fertilized_until_day", 0) < day:
                add(x, y, 3, "FERTILIZE", "FERTILIZER")
        else:
            for s, species in enumerate(r.ANIMALS):
                if stock[s] > 0 and kind == r.ANIMAL_DEF[s][1] and r._get(tile, "animal") is None:
                    add(x, y, 1, "PLACE", species)
        if crop >= 0 and targeted:
            if tile is None and plant_budget:
                add(x, y, 4, "PLANT", r.CROPS[crop])
            elif kind == "WEED":
                add(x, y, 4, "DIG")
        if reclaim_budget and targeted and reclaimable(tile) and not reserved(obs, tile):
            add(x, y, 4, "DIG")
        if build_budget and tile is None:
            add(x, y, 4, "BUILD_COOP" if r.ANIMAL_DEF[animal][1] == "COOP" else "BUILD_PASTURE")
    # Native order is NW, NE, SW, SE, including currently locked access points.
    for item in r.ITEMS:
        if budget[item] > 0:
            for x, y in ((4, 4), (5, 4), (4, 5), (5, 5)):
                add(x, y, 0 if item == "WHEAT" else 1, "PICKUP", item)
    commands = [["PASS"] for _ in positions]
    used, claimed, pickup_claimed = set(), set(), set()
    built = picked_animals = 0
    for _ in positions:
        best = None
        best_cost = 2147483647
        for u, (ux, uy) in enumerate(positions):
            if u in used:
                continue
            for j, (x, y, priority, op, arg) in enumerate(jobs):
                if op != "PICKUP" and (x, y) in claimed:
                    continue
                if op == "PLANT" and plant_budget <= 0:
                    continue
                if op in ("BUILD_COOP", "BUILD_PASTURE") and build_budget <= 0:
                    continue
                if op == "DIG" and macro == RECLAIM and reclaim_budget <= 0:
                    continue
                if op == "FERTILIZE" and (fertilize_budget <= 0 or not r._get(inv[u], "FERTILIZER", 0)):
                    continue
                if op == "FEED" and not r._get(inv[u], "WHEAT", 0):
                    continue
                if op == "PLACE" and not r._get(inv[u], arg, 0):
                    continue
                if op == "PICKUP":
                    if arg in pickup_claimed or budget[arg] <= 0 or r._get(inv[u], arg, 0):
                        continue
                    if any(r._get(inv[u], s, 0) for s in r.ANIMALS):
                        continue
                    if arg in r.ANIMALS and unfed and r._get(inv[u], "WHEAT", 0):
                        continue
                dist = abs(ux - x) + abs(uy - y)
                cost = (0 if dist == 0 else 1024) + priority * 32 + dist
                if cost < best_cost:
                    best_cost, best = cost, (u, j, dist == 0)
        if best is None:
            break
        u, j, local = best
        x, y, priority, op, arg = jobs[j]
        used.add(u)
        if op == "PICKUP":
            pickup_claimed.add(arg)
            n = budget[arg] if arg == "WHEAT" else 1
            cmd = [op, arg, n]
            if local and arg in r.ANIMALS:
                picked_animals += n
        else:
            claimed.add((x, y))
            cmd = [op, arg, 1] if op == "PLACE" else [op, arg] if op == "PLANT" else [op]
            if op == "PLANT": plant_budget -= 1
            if op in ("BUILD_COOP", "BUILD_PASTURE"):
                build_budget -= 1
                built += local
            if op == "DIG" and macro == RECLAIM: reclaim_budget -= 1
            if op == "FERTILIZE": fertilize_budget -= 1
        commands[u] = cmd if local else r._route(r._farm(obs), positions[u], x, y)
    action = {"farmer": commands[0], "hands": commands[1:], "market": []}

    def append(op, item=None, n=1, raw=False):
        if n <= 0 or len(action["market"]) >= r.MAX_MARKET_ORDERS:
            return
        allowed = (r._get(shed, item, 0) > 0 if op == "SELL" else r._market_buy_legal(obs, op, item))
        if raw or allowed:
            action["market"].append([op] if item is None else [op, item, n])

    if feed > 0: append("BUY_PRODUCT", "WHEAT", feed)
    if crop >= 0:
        n = min(quantity, r._reclaimable_tiles_in_target(obs, target)) - r._get(seeds, r.CROPS[crop], 0)
        append("BUY_SEED", r.CROPS[crop], min(n, free_cash // r.SEED_COST[crop]))
    elif animal >= 0:
        capacity = r._animal_room(obs, animal) + built - sum(stock[s] for s in range(3)
            if r.ANIMAL_DEF[s][1] == r.ANIMAL_DEF[animal][1])
        n = min(quantity - stock[animal], capacity,
                r.SHED_CAPACITY - r._shed_total(obs) + picked_animals - feed,
                free_cash // r.ANIMAL_COST[animal])
        append("BUY_ANIMAL", r.ANIMALS[animal], n, raw=True)
    elif macro == r.MACRO_EXPAND: append("BUY_LAND")
    elif 10 <= macro <= 18: append("SELL", r.PRODUCTS[macro - 10], quantity)
    elif macro in (r.MACRO_SELL_ALL, r.MACRO_CASH_OUT):
        for item in r.PRODUCTS: append("SELL", item, r._get(shed, item, 0))
        if macro == r.MACRO_SELL_ALL:
            orders = action["market"]
            prices = r._get(r._get(obs, "market", {}), "prices", {})
            # Selection sort (not stable sort): native equal-price tie behavior.
            for i in range(len(orders)):
                if orders[i][0] != "SELL": continue
                best = i
                for j in range(i + 1, len(orders)):
                    if orders[j][0] == "SELL" and r._get(prices, orders[j][1], 0) > r._get(prices, orders[best][1], 0): best = j
                orders[i], orders[best] = orders[best], orders[i]
            r._cap_orders(action, "SELL", None, quantity)
    elif 20 <= macro <= 24: append("BUY_SEED", r.CROPS[macro - 20], min(quantity, r._seed_purchase_room(obs)))
    elif 25 <= macro <= 27: append("BUY_ANIMAL", r.ANIMALS[macro - 25], min(quantity, r._animal_purchase_room(obs, macro - 25)))
    elif macro == r.MACRO_HIRE:
        for _ in range(min(quantity, 240 - len(positions) + 1)): append("HIRE")
    elif macro in (r.MACRO_BUY_WHEAT, r.MACRO_BUY_FERTILIZER):
        item = "WHEAT" if macro == r.MACRO_BUY_WHEAT else "FERTILIZER"
        if item == "WHEAT":
            action["market"] = [o for o in action["market"] if o[:2] != ["BUY_PRODUCT", "WHEAT"]]
            quantity = max(quantity, feed)
        append("BUY_PRODUCT", item, quantity)
    return action
