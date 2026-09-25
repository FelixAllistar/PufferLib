"""Differential tests and a small throughput comparison for Kaggriculture.

The Python side is the installed Kaggle interpreter.  The native side is the
rule-level core in core.h.  Every frame is compared as a
canonical state snapshot, so a mismatch points at a real mechanics/order
difference instead of only comparing final scores.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import random
import time
from pathlib import Path

from kaggle_environments import make


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LIB = Path(__file__).with_name("build") / "libkaggriculture.so"

from replay_native import (
    CAction, CConfig, CROPS, ITEMS, ANIMALS, c_action, c_snapshot,
    canonical_replay_frame, first_difference, load_core, replay_config,
)

PRODUCTS = tuple(ITEMS)[:9]


def canonical_official(env):
    return canonical_replay_frame(env.state)


def official_configuration(cfg: CConfig):
    return {
        "episodeSteps": cfg.episode_steps,
        "boardSize": cfg.board_size,
        "startingMoney": cfg.starting_money,
        "maxMarketOrdersPerTurn": cfg.max_market_orders_per_turn,
        "turnsPerDay": cfg.turns_per_day,
        "shedCapacity": cfg.shed_capacity,
        "weedSpawnChance": cfg.weed_spawn_chance,
        "townShopUnlockInterval": cfg.town_shop_unlock_interval,
        "townShopSellInterval": cfg.town_shop_sell_interval,
        "townCenterSellInterval": cfg.town_center_sell_interval,
        "farmHandCostMult": cfg.farm_hand_cost_mult,
        "seed": cfg.seed,
    }


def run_case(lib, cfg: CConfig, frames, label: str, check=None):
    state = lib.kg_create(ctypes.byref(cfg))
    if not state:
        raise RuntimeError(f"kg_create failed for {label}")
    try:
        env = make(
            "kaggriculture", configuration=official_configuration(cfg), debug=True
        )
        env.reset()
        for frame, pair in enumerate(frames, 1):
            env.step(list(pair))
            array = (CAction * 2)(c_action(pair[0]), c_action(pair[1]))
            lib.kg_step(state, array)
            expected = canonical_official(env)
            actual = c_snapshot(lib, state)
            differences = first_difference(expected, actual)
            if differences:
                raise AssertionError(
                    f"{label} frame {frame} parity mismatch: {differences}"
                )
            if check is not None:
                check(frame, expected)
    finally:
        lib.kg_destroy(state)


def scenario(steps: int):
    """Deterministic actions that exercise market, land, crops, animals, and hands."""
    actions = []
    for step in range(steps):
        a0 = {"farmer": ["PASS"], "hands": [], "market": []}
        a1 = {"farmer": ["PASS"], "hands": [], "market": []}
        if step == 0:
            a0["market"] = [
                ["BUY_SEED", "WHEAT", 2],
                ["BUY_SEED", "CARROT", 1],
                ["BUY_ANIMAL", "GOOSE", 1],
                ["BUY_PRODUCT", "WHEAT", 3],
                ["HIRE"],
                ["HIRE"],
                ["HIRE"],
                ["HIRE"],
            ]
            a1["market"] = [
                ["BUY_SEED", "MELON", 1], ["BUY_LAND"],
                ["HIRE"], ["HIRE"], ["HIRE"],
            ]
        elif step == 1:
            a0["farmer"] = ["PLANT", "WHEAT"]
            a0["hands"] = [["PASS"] for _ in range(4)]
            a1["farmer"] = ["PLANT", "MELON"]
            a1["hands"] = [["PASS"] for _ in range(3)]
        elif step in (2, 3, 4, 5, 6, 7, 8, 9):
            a0["farmer"] = ["WATER"]
            a0["hands"] = [["PASS"] for _ in range(4)]
            a1["farmer"] = ["WATER"]
            a1["hands"] = [["PASS"] for _ in range(3)]
        elif step == 10:
            a0["farmer"] = ["WEST"]
            a1["farmer"] = ["NORTH"]
        elif step == 11:
            a0["farmer"] = ["BUILD_COOP"]
            a1["farmer"] = ["BUILD_PASTURE"]
        elif step == 12:
            a0["farmer"] = ["EAST"]
            a1["farmer"] = ["SOUTH"]
        elif step == 13:
            a0["farmer"] = ["PICKUP", "GOOSE"]
            a1["market"] = [["BUY_PRODUCT", "WHEAT", 2]]
        elif step == 14:
            a0["farmer"] = ["WEST"]
            a1["farmer"] = ["PICKUP", "WHEAT", 2]
        elif step == 15:
            a0["farmer"] = ["PLACE", "GOOSE"]
            a1["farmer"] = ["WEST"]
        elif step == 16:
            a0["farmer"] = ["EAST"]
            a1["farmer"] = ["WEST"]
        elif step == 17:
            a0["farmer"] = ["PICKUP", "WHEAT", 2]
            a1["farmer"] = ["BUILD_COOP"]
        elif step == 18:
            a0["farmer"] = ["WEST"]
            a1["farmer"] = ["EAST"]
        elif step == 19:
            a0["farmer"] = ["FEED"]
            a1["farmer"] = ["PICKUP", "MELON"]
        elif step == 20:
            a0["farmer"] = ["CARE"]
            a1["farmer"] = ["WEST"]
        elif step == 21:
            a0["farmer"] = ["COLLECT_FERTILIZER"]
            a1["farmer"] = ["PLACE", "MELON"]
        elif step == 22:
            a0["farmer"] = ["EAST"]
            a1["farmer"] = ["PASS"]
        elif step == 23:
            a0["farmer"] = ["DIG"]
            a1["farmer"] = ["FERTILIZE"]
        elif step == 24:
            a0["farmer"] = ["DROP"]
            a1["farmer"] = ["DROP"]
        actions.append((a0, a1))
    return actions


UNIT_ACTION_NAMES = (
    "PASS", "NORTH", "SOUTH", "EAST", "WEST", "PICKUP", "DROP",
    "PLANT", "WATER", "HARVEST", "FERTILIZE", "BUILD_COOP",
    "BUILD_PASTURE", "DIG", "PLACE", "FEED", "COLLECT_FERTILIZER",
    "CARE",
)
MARKET_ACTION_NAMES = (
    "BUY_SEED", "BUY_PRODUCT", "BUY_ANIMAL", "SELL", "HIRE", "BUY_LAND",
)


def random_unit_action(rng: random.Random, forced_name: str | None = None):
    op = forced_name or rng.choice(UNIT_ACTION_NAMES)
    if op == "PLANT":
        return [op, rng.choice(tuple(CROPS))]
    if op in ("PICKUP", "PLACE"):
        return [op, rng.choice(tuple(ITEMS)), rng.randint(1, 5)]
    return [op]


def random_market_order(rng: random.Random, forced_name: str | None = None):
    op = forced_name or rng.choice(MARKET_ACTION_NAMES)
    if op == "BUY_SEED":
        return [op, rng.choice(tuple(CROPS)), rng.randint(1, 5)]
    if op == "BUY_PRODUCT":
        return [op, rng.choice(("WHEAT", "FERTILIZER")), rng.randint(1, 5)]
    if op == "BUY_ANIMAL":
        return [op, rng.choice(tuple(ANIMALS)), rng.randint(1, 5)]
    if op == "SELL":
        return [op, rng.choice(tuple(PRODUCTS)), rng.randint(1, 5)]
    return [op]


def randomized_scenario(steps: int, seed: int):
    """Randomized legal-shaped actions, with a forced coverage cycle.

    Most actions are deliberately invalid in the current state.  That is
    useful here: the official engine promises silent no-ops, so this checks
    the native validator as well as successful actions without hand-writing a
    fragile stateful agent.
    """
    rng = random.Random(seed)
    actions = []
    for step in range(steps):
        pair = []
        forced_unit = UNIT_ACTION_NAMES[step % len(UNIT_ACTION_NAMES)]
        forced_market = MARKET_ACTION_NAMES[step % len(MARKET_ACTION_NAMES)]
        for player in range(2):
            hands = []
            for _ in range(8):
                hands.append(random_unit_action(rng))
            orders = [random_market_order(rng) for _ in range(rng.randint(0, 5))]
            if step == 0:
                # Guarantee active hand slots before the random portion starts.
                orders = [["HIRE"] for _ in range(8)] + [
                    ["BUY_SEED", "WHEAT", 2], ["BUY_PRODUCT", "WHEAT", 2],
                ]
            else:
                orders.insert(0, random_market_order(rng, forced_market))
            pair.append({
                "farmer": random_unit_action(rng, forced_unit),
                "hands": hands,
                "market": orders[:10],
            })
        actions.append((pair[0], pair[1]))
    return actions


def animal_scenario(steps: int = 32):
    """Exercise fertilizer, CARE banking, and an unfed production tick."""
    actions = []
    for step in range(steps):
        pair = []
        for player in range(2):
            action = {"farmer": ["PASS"], "hands": [], "market": []}
            if step == 0:
                action["farmer"] = ["BUILD_COOP"]
                action["market"] = [
                    ["BUY_ANIMAL", "GOOSE", 1],
                    ["BUY_PRODUCT", "WHEAT", 8],
                ]
            elif step == 1:
                action["farmer"] = ["PICKUP", "GOOSE", 1]
            elif step == 2:
                action["farmer"] = ["PLACE", "GOOSE", 1]
            elif step in (3, 8, 16):
                action["farmer"] = ["PICKUP", "WHEAT", 1]
            elif step in (4, 9, 17):
                action["farmer"] = ["FEED"]
            elif step == 5 and player == 0:
                action["farmer"] = ["CARE"]
            elif step == 6 and player == 1:
                action["farmer"] = ["DIG"]
            elif step == 10:
                action["farmer"] = ["COLLECT_FERTILIZER"]
            pair.append(action)
        actions.append(tuple(pair))
    return actions


def run_animal_parity(lib):
    cfg = CConfig(
        episode_steps=40,
        board_size=10,
        starting_money=10000,
        max_market_orders_per_turn=10,
        turns_per_day=8,
        shed_capacity=100,
        weed_spawn_chance=0.0,
        town_shop_unlock_interval=100,
        town_shop_sell_interval=100,
        town_center_sell_interval=100,
        farm_hand_cost_mult=1,
        seed=31337,
    )
    state = lib.kg_create(ctypes.byref(cfg))
    if not state:
        raise RuntimeError("kg_create failed for animal parity")
    try:
        env = make(
            "kaggriculture",
            configuration={
                "episodeSteps": cfg.episode_steps,
                "boardSize": cfg.board_size,
                "startingMoney": cfg.starting_money,
                "maxMarketOrdersPerTurn": cfg.max_market_orders_per_turn,
                "turnsPerDay": cfg.turns_per_day,
                "shedCapacity": cfg.shed_capacity,
                "weedSpawnChance": cfg.weed_spawn_chance,
                "townShopUnlockInterval": cfg.town_shop_unlock_interval,
                "townShopSellInterval": cfg.town_shop_sell_interval,
                "townCenterSellInterval": cfg.town_center_sell_interval,
                "farmHandCostMult": cfg.farm_hand_cost_mult,
                "seed": cfg.seed,
            },
            debug=True,
        )
        env.reset()
        frames = animal_scenario()
        for frame, pair in enumerate(frames, 1):
            env.step(list(pair))
            array = (CAction * 2)(c_action(pair[0]), c_action(pair[1]))
            lib.kg_step(state, array)
            expected = canonical_official(env)
            actual = c_snapshot(lib, state)
            differences = first_difference(expected, actual)
            if differences:
                raise AssertionError(
                    f"animal frame {frame} parity mismatch: {differences}"
                )
            tile0 = expected["farms"][0]["tiles"][4][4]
            tile1 = expected["farms"][1]["tiles"][4][4]
            if frame == 8:
                assert tile0["fertilizer_available"] is True
                assert tile1["fertilizer_available"] is True
                assert tile0["pending_care_bonus"] == 1
                assert tile1["pending_care_bonus"] == 0
            elif frame == 11:
                assert tile0["fertilizer_available"] is False
                assert tile1["fertilizer_available"] is False
            elif frame == 16:
                assert tile0["fertilizer_available"] is True
                assert tile1["fertilizer_available"] is True
            elif frame == 32:
                assert tile0["yield_units"] == 1
                assert tile1["yield_units"] == 1
                assert tile0["pending_care_bonus"] == 0
        print("Kaggriculture animal parity: PASS (CARE, fertilizer, unfed yield)")
    finally:
        lib.kg_destroy(state)


def run_documented_edge_parity(lib):
    pass_action = {"farmer": ["PASS"], "hands": [], "market": []}

    # A crop planted on the final turn of a day dies immediately unless it is
    # also watered that day. Planting initializes the first missed watering.
    cfg = CConfig(
        episode_steps=8, board_size=10, starting_money=3000,
        max_market_orders_per_turn=10, turns_per_day=2, shed_capacity=100,
        weed_spawn_chance=0.0, town_shop_unlock_interval=100,
        town_shop_sell_interval=100, town_center_sell_interval=100,
        farm_hand_cost_mult=1, seed=101,
    )
    frames = [
        ({"farmer": ["PASS"], "hands": [],
          "market": [["BUY_SEED", "WHEAT", 1]]}, pass_action),
        ({"farmer": ["PLANT", "WHEAT"], "hands": [], "market": []},
         pass_action),
    ]

    def check_planting_day(frame, snapshot):
        if frame == 2:
            assert snapshot["farms"][0]["tiles"][4][4]["kind"] == "WEED"

    run_case(lib, cfg, frames, "planting-day", check_planting_day)

    # The one-time melon starts with one held unit and reaches its six-unit cap
    # at age 10; watering at ages 11 and 12 is legal but cannot add more.
    # Strawberry fires four times at ages 10/12/14/16, then starts decay.
    cfg = CConfig(
        episode_steps=82, board_size=10, starting_money=10000,
        max_market_orders_per_turn=10, turns_per_day=4, shed_capacity=100,
        weed_spawn_chance=0.0, town_shop_unlock_interval=100,
        town_shop_sell_interval=100, town_center_sell_interval=100,
        farm_hand_cost_mult=1, seed=202,
    )
    frames = []
    for step in range(80):
        pair = []
        for crop in ("MELON", "STRAWBERRY"):
            action = {"farmer": ["PASS"], "hands": [], "market": []}
            if step == 0:
                action["market"] = [["BUY_SEED", crop, 1]]
            elif step == 1:
                action["farmer"] = ["PLANT", crop]
            elif step == 2 or step >= 4 and step % 4 == 0:
                action["farmer"] = ["WATER"]
            pair.append(action)
        frames.append(tuple(pair))

    def check_crop_lifetimes(frame, snapshot):
        melon = snapshot["farms"][0]["tiles"][4][4]
        strawberry = snapshot["farms"][1]["tiles"][4][4]
        if frame == 2:
            assert melon["consecutive_unwatered"] == 1
            assert strawberry["consecutive_unwatered"] == 1
        if frame in (41, 45, 49):
            assert melon["yield_units"] == 6
        if frame in (40, 48, 56, 64):
            assert strawberry["yield_units"] == frame // 8 - 4
        if frame == 63:
            assert melon["kind"] == "WEED"
        if frame == 75:
            assert strawberry["kind"] == "WEED"

    run_case(lib, cfg, frames, "crop-lifetimes", check_crop_lifetimes)

    # BUY_PRODUCT/SELL fertilizer is legal and exactly reversible absent an
    # intervening market change. Hands spawn on shed-access tiles (some LOCKED);
    # kaggle-environments 1.32.3+ allows walking off LOCKED tiles so the third
    # hire at (5,5) can move NORTH to (5,4).
    cfg = CConfig(
        episode_steps=8, board_size=10, starting_money=3000,
        max_market_orders_per_turn=10, turns_per_day=24, shed_capacity=100,
        weed_spawn_chance=0.0, town_shop_unlock_interval=100,
        town_shop_sell_interval=100, town_center_sell_interval=100,
        farm_hand_cost_mult=1, seed=303,
    )
    frames = [
        ({"farmer": ["PASS"], "hands": [], "market": [
            ["HIRE"], ["HIRE"], ["HIRE"], ["HIRE"],
            ["BUY_PRODUCT", "FERTILIZER", 3],
        ]}, pass_action),
        ({"farmer": ["PASS"],
          "hands": [["PASS"], ["PASS"], ["NORTH"], ["PASS"]],
          "market": [["SELL", "FERTILIZER", 3]]}, pass_action),
    ]

    def check_market_and_hands(frame, snapshot):
        farm = snapshot["farms"][0]
        if frame == 1:
            assert farm["hands"] == [[5, 4], [4, 5], [5, 5], [4, 4]]
        if frame == 2:
            assert farm["hands"][2] == [5, 4]
            assert farm["money"] == 2993.0
            assert snapshot["privates"][0]["shed"]["FERTILIZER"] == 0
            assert snapshot["market"]["inventory"]["FERTILIZER"] == 10000

    run_case(lib, cfg, frames, "market-and-hands", check_market_and_hands)
    print("Kaggriculture documented edges: PASS (plants, market, locked move)")


def run_market_parity(lib):
    # Town consumption drives carrot/tomato/egg well below their scarcity knees
    # so the 1.32.7 hinge price curves are exercised against the interpreter.
    cfg = CConfig(
        episode_steps=720, board_size=10, starting_money=3000,
        max_market_orders_per_turn=10, turns_per_day=24, shed_capacity=100,
        weed_spawn_chance=0.0, town_shop_unlock_interval=1,
        town_shop_sell_interval=1, town_center_sell_interval=1,
        farm_hand_cost_mult=1, seed=404,
    )
    pass_action = {"farmer": ["PASS"], "hands": [], "market": []}
    # The interpreter marks DONE after episodeSteps-1 turns, so step until the
    # final legal frame and then assert the knees were actually crossed.
    frames = [(pass_action, pass_action)] * (cfg.episode_steps - 1)
    last_inventory = {}

    def check_knee(frame, snapshot):
        last_inventory.update(snapshot["market"]["inventory"])

    run_case(lib, cfg, frames, "market-hinge", check_knee)
    # I0 - T for each hinge product; the frame diff already verified prices.
    for item, knee in (("CARROT", 9550), ("TOMATO", 9800), ("EGG", 9668)):
        inventory = last_inventory.get(item, 10000)
        if inventory >= knee:
            raise AssertionError(
                f"market-hinge never reached {item} scarcity (inventory={inventory})"
            )
    print("Kaggriculture market hinge parity: PASS")


def run_random_parity(lib):
    cfg = CConfig(
        episode_steps=160,
        board_size=10,
        starting_money=50000,
        max_market_orders_per_turn=10,
        turns_per_day=4,
        shed_capacity=17,
        weed_spawn_chance=0.03,
        town_shop_unlock_interval=2,
        town_shop_sell_interval=3,
        town_center_sell_interval=5,
        farm_hand_cost_mult=2,
        seed=991,
    )
    state = lib.kg_create(ctypes.byref(cfg))
    if not state:
        raise RuntimeError("kg_create failed for randomized parity")
    try:
        env = make(
            "kaggriculture",
            configuration={
                "episodeSteps": cfg.episode_steps,
                "boardSize": cfg.board_size,
                "startingMoney": cfg.starting_money,
                "maxMarketOrdersPerTurn": cfg.max_market_orders_per_turn,
                "turnsPerDay": cfg.turns_per_day,
                "shedCapacity": cfg.shed_capacity,
                "weedSpawnChance": cfg.weed_spawn_chance,
                "townShopUnlockInterval": cfg.town_shop_unlock_interval,
                "townShopSellInterval": cfg.town_shop_sell_interval,
                "townCenterSellInterval": cfg.town_center_sell_interval,
                "farmHandCostMult": cfg.farm_hand_cost_mult,
                "seed": cfg.seed,
            },
            debug=True,
        )
        env.reset()
        expected = canonical_official(env)
        actual = c_snapshot(lib, state)
        differences = first_difference(expected, actual)
        if differences:
            raise AssertionError(f"randomized initial parity mismatch: {differences}")
        frames = randomized_scenario(cfg.episode_steps, seed=12345)
        for frame, pair in enumerate(frames, 1):
            if all(s.get("status") == "DONE" for s in env.state):
                break
            env.step(list(pair))
            array = (CAction * 2)(c_action(pair[0]), c_action(pair[1]))
            lib.kg_step(state, array)
            expected = canonical_official(env)
            actual = c_snapshot(lib, state)
            differences = first_difference(expected, actual)
            if differences:
                raise AssertionError(
                    f"randomized frame {frame} parity mismatch: {differences}"
                )
        print(f"Kaggriculture randomized parity: PASS ({len(frames)} frames, seed={cfg.seed})")
    finally:
        lib.kg_destroy(state)


def run_parity(lib_path: Path):
    lib = load_core(lib_path)
    cfg = CConfig(
        episode_steps=48,
        board_size=10,
        starting_money=3000,
        max_market_orders_per_turn=10,
        turns_per_day=4,
        shed_capacity=100,
        weed_spawn_chance=0.005,
        town_shop_unlock_interval=2,
        town_shop_sell_interval=4,
        town_center_sell_interval=24,
        farm_hand_cost_mult=1,
        seed=7,
    )
    state = lib.kg_create(ctypes.byref(cfg))
    if not state:
        raise RuntimeError("kg_create failed")
    try:
        env = make(
            "kaggriculture",
            configuration={
                "episodeSteps": cfg.episode_steps,
                "boardSize": cfg.board_size,
                "startingMoney": cfg.starting_money,
                "maxMarketOrdersPerTurn": cfg.max_market_orders_per_turn,
                "turnsPerDay": cfg.turns_per_day,
                "shedCapacity": cfg.shed_capacity,
                "weedSpawnChance": cfg.weed_spawn_chance,
                "townShopUnlockInterval": cfg.town_shop_unlock_interval,
                "townShopSellInterval": cfg.town_shop_sell_interval,
                "townCenterSellInterval": cfg.town_center_sell_interval,
                "farmHandCostMult": cfg.farm_hand_cost_mult,
                "seed": cfg.seed,
            },
            debug=True,
        )
        env.reset()
        expected = canonical_official(env)
        actual = c_snapshot(lib, state)
        differences = first_difference(expected, actual)
        if differences:
            raise AssertionError(f"initial parity mismatch: {differences}")
        frames = scenario(cfg.episode_steps)
        for frame, pair in enumerate(frames, 1):
            if all(s.get("status") == "DONE" for s in env.state):
                break
            official_actions = list(pair)
            env.step(official_actions)
            c_pair = (c_action(pair[0]), c_action(pair[1]))
            array = (CAction * 2)(*c_pair)
            lib.kg_step(state, array)
            expected = canonical_official(env)
            actual = c_snapshot(lib, state)
            if os.environ.get("KG_TRACE"):
                print(f"frame {frame}: official market={expected['market']['inventory']} town={expected['town']['unlocked_shops']}; native market={actual['market']['inventory']} town={actual['town']['unlocked_shops']}")
            differences = first_difference(expected, actual)
            if differences:
                raise AssertionError(f"frame {frame} parity mismatch: {differences}")
        run_random_parity(lib)
        run_animal_parity(lib)
        run_documented_edge_parity(lib)
        run_market_parity(lib)
        print(f"Kaggriculture parity: PASS ({len(frames)} frames, seed={cfg.seed})")
    finally:
        lib.kg_destroy(state)


def run_bench(lib_path: Path):
    lib = load_core(lib_path)
    cfg = CConfig(
        episode_steps=720, board_size=10, starting_money=3000,
        max_market_orders_per_turn=10, turns_per_day=24, shed_capacity=100,
        weed_spawn_chance=0.0, town_shop_unlock_interval=1000,
        town_shop_sell_interval=4, town_center_sell_interval=12,
        farm_hand_cost_mult=1, seed=7,
    )
    state = lib.kg_create(ctypes.byref(cfg))
    actions = (CAction * 2)(c_action({"farmer": ["PASS"], "hands": [], "market": []}),
                            c_action({"farmer": ["PASS"], "hands": [], "market": []}))
    episodes = 10
    start = time.perf_counter()
    for episode in range(episodes):
        lib.kg_reset(state)
        for _ in range(cfg.episode_steps):
            lib.kg_step(state, actions)
            if lib.kg_done(state):
                break
    elapsed = time.perf_counter() - start
    lib.kg_destroy(state)
    print(f"Native core: {episodes * cfg.episode_steps / elapsed:,.0f} steps/s ({elapsed:.3f}s)")

    start = time.perf_counter()
    for episode in range(episodes):
        env = make("kaggriculture", configuration={"episodeSteps": cfg.episode_steps, "seed": 7})
        env.run(["pass", "pass"])
    elapsed = time.perf_counter() - start
    print(f"Python oracle: {episodes * cfg.episode_steps / elapsed:,.0f} steps/s ({elapsed:.3f}s)")


def run_replay_parity(lib_path: Path, replay_path: Path):
    replay = json.loads(replay_path.read_text())
    cfg = replay_config(replay)
    lib = load_core(lib_path)
    state = lib.kg_create(ctypes.byref(cfg))
    if not state:
        raise RuntimeError("kg_create failed for replay parity")
    try:
        steps = replay["steps"]
        initial = first_difference(canonical_replay_frame(steps[0]), c_snapshot(lib, state))
        if initial:
            raise AssertionError(f"replay initial parity mismatch: {initial}")
        for index in range(1, len(steps)):
            pair = []
            for player in range(2):
                action = steps[index][player].get("action")
                pair.append(action if isinstance(action, dict) else {})
            array = (CAction * 2)(c_action(pair[0]), c_action(pair[1]))
            lib.kg_step(state, array)
            differences = first_difference(
                canonical_replay_frame(steps[index]), c_snapshot(lib, state)
            )
            if differences:
                raise AssertionError(
                    f"replay frame {index} parity mismatch: {differences}"
                )
        print(
            f"Kaggriculture replay parity: PASS "
            f"({len(steps) - 1} frames, episode={replay.get('id')})"
        )
    finally:
        lib.kg_destroy(state)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib", type=Path, default=DEFAULT_LIB)
    parser.add_argument("--bench", action="store_true")
    parser.add_argument("--replay", type=Path)
    args = parser.parse_args()
    if args.replay:
        run_replay_parity(args.lib, args.replay)
    elif args.bench:
        run_bench(args.lib)
    else:
        run_parity(args.lib)


if __name__ == "__main__":
    main()
