"""Public-observation regression fixtures for the native macro export.

The native ``Env`` is not part of the submission ABI, so these tests pin the
portable executor to the observable mode-2 semantics in kaggriculture.h.
"""

from __future__ import annotations

import unittest

import numpy as np

import native_macro_runtime as macro


def tile(kind, **values):
    return {"kind": kind, **values}


def observation(*, money=5000, step=24, day=1, hour=0, hands=(),
                unlocked=("NW",), shed=None, seeds=None, inventories=None,
                prices=None):
    board = [["LOCKED" for _ in range(10)] for _ in range(10)]
    for name in unlocked:
        x0 = 5 if name in ("NE", "SE") else 0
        y0 = 5 if name in ("SW", "SE") else 0
        for y in range(y0, y0 + 5):
            for x in range(x0, x0 + 5):
                board[y][x] = None
    own = {
        "money": money,
        "farmer": [4, 4],
        "hands": [list(position) for position in hands],
        "hires_today": 0,
        "unlocked_quadrants": list(unlocked),
        "tiles": board,
    }
    opponent = {
        "money": 1,
        "farmer": [4, 4],
        "hands": [],
        "hires_today": 0,
        "unlocked_quadrants": ["NW"],
        "tiles": [[None if x < 5 and y < 5 else "LOCKED"
                   for x in range(10)] for y in range(10)],
    }
    default_prices = {item: 100 for item in macro.PRODUCTS}
    if prices:
        default_prices.update(prices)
    own_inventories = inventories or [{} for _ in range(1 + len(hands))]
    return {
        "step": step,
        "day": day,
        "hour": hour,
        "player": 0,
        "farms": [own, opponent],
        # Competition observations expose only the acting player's private
        # state. There is intentionally no opponent-private fixture.
        "private": {
            "shed": dict(shed or {}),
            "seeds": dict(seeds or {}),
            "inventories": [dict(value) for value in own_inventories],
        },
        "market": {
            "prices": default_prices,
            "inventory": {item: 10000 for item in macro.PRODUCTS},
        },
        "town": {"unlocked_shops": []},
    }


def commands(action):
    return [action["farmer"], *action.get("hands", ())]


class NativeMacroRuntimeFixtures(unittest.TestCase):
    def test_weeds_are_reclaimable_and_target_filter_precedes_plant_slots(self):
        obs = observation(unlocked=("NW", "NE"), seeds={"WHEAT": 1},
                          prices={"WHEAT": 0})
        board = obs["farms"][0]["tiles"]
        for y in range(5):
            for x in range(10):
                board[y][x] = tile("PLANT", crop="WHEAT", planted_day=0,
                                   watered_today=True, yield_units=0)
        board[0][0] = None                 # Outside the selected quadrant.
        board[0][5] = None                 # Selected planting slot.
        board[0][6] = tile("WEED")         # Selected reclaimable tile.
        obs["farms"][0]["farmer"] = [5, 0]

        self.assertEqual(macro._reclaimable_tiles(obs), 3)
        self.assertEqual(macro._reclaimable_tiles_in_target(obs, 2), 2)
        self.assertTrue(macro.candidate_legal(obs, macro.MACRO_PLANT_BASE))
        action = macro.execute_macro(obs, macro.MACRO_PLANT_BASE, 1, 2)
        self.assertEqual(action["farmer"], ["PLANT", "WHEAT"])
        self.assertNotIn(["BUY_SEED", "WHEAT", 1], action["market"])

        obs["private"]["seeds"]["WHEAT"] = 0
        board[0][5] = tile("WEED")
        self.assertTrue(macro.candidate_legal(obs, macro.MACRO_PLANT_BASE))

    def test_harvest_and_maintain_have_disjoint_job_filters(self):
        obs = observation(step=72, day=3, seeds={})
        board = obs["farms"][0]["tiles"]
        board[4][4] = tile("PLANT", crop="TOMATO", planted_day=0,
                           watered_today=False, consecutive_unwatered=1,
                           yield_units=0)
        board[4][3] = tile("PLANT", crop="WHEAT", planted_day=0,
                           watered_today=True, consecutive_unwatered=0,
                           yield_units=2)

        harvest = macro.execute_macro(obs, macro.MACRO_HARVEST, 1, 0)
        maintain = macro.execute_macro(obs, macro.MACRO_MAINTAIN, 1, 0)
        self.assertEqual(harvest["farmer"], ["WEST"])
        self.assertEqual(maintain["farmer"], ["WATER"])
        self.assertNotEqual(harvest["farmer"][0], "WATER")
        self.assertNotEqual(maintain["farmer"][0], "HARVEST")

    def test_compatible_animal_stock_reduces_buy_room(self):
        obs = observation(shed={"SHEEP": 1})
        board = obs["farms"][0]["tiles"]
        board[0][0] = tile("PASTURE", animal=None)
        board[0][1] = tile("PASTURE", animal=None)
        self.assertEqual(macro._animal_purchase_room(obs, 1), 1)
        action = macro.execute_macro(obs, macro.MACRO_BUY_ANIMAL_BASE + 1, 8, 0)
        self.assertIn(["BUY_ANIMAL", "COW", 1], action["market"])

    def test_selected_animal_builds_only_selected_species_and_batch(self):
        obs = observation(shed={"SHEEP": 7}, hands=((4, 3),))
        obs["farms"][0]["farmer"] = [3, 4]
        action = macro.execute_macro(obs, macro.MACRO_ANIMAL_BASE + 1, 2, 0)

        self.assertEqual(action["farmer"], ["BUILD_PASTURE"])
        self.assertEqual(action["hands"], [["BUILD_PASTURE"]])
        self.assertEqual([order for order in action["market"]
                          if order[0] == "BUY_ANIMAL"],
                         [["BUY_ANIMAL", "COW", 2]])
        flat = [part for command in commands(action) for part in command]
        self.assertNotIn("SHEEP", flat)
        self.assertFalse(any(order[0] in ("BUY_SEED", "HIRE", "BUY_LAND")
                             for order in action["market"]))

    def test_same_turn_pickup_frees_full_shed_for_selected_purchase(self):
        filler = {product: 0 for product in macro.PRODUCTS}
        filler.update({"FERTILIZER": 99, "COW": 1})
        obs = observation(money=5000, shed=filler)
        board = obs["farms"][0]["tiles"]
        board[0][0] = tile("PASTURE", animal=None)
        board[0][1] = tile("PASTURE", animal=None)
        action = macro.execute_macro(obs, macro.MACRO_ANIMAL_BASE + 1, 2, 0)

        self.assertEqual(action["farmer"], ["PICKUP", "COW", 1])
        self.assertIn(["BUY_ANIMAL", "COW", 1], action["market"])

    def test_feed_reserves_cash_and_shed_room_for_new_animals(self):
        shed = {"FERTILIZER": 99}
        obs = observation(money=5000, shed=shed)
        obs["farms"][0]["tiles"][0][0] = tile(
            "PASTURE", animal="COW", fed_today=False, cared_today=True,
            yield_units=0, fertilizer_available=False,
        )
        obs["farms"][0]["tiles"][0][1] = tile("PASTURE", animal=None)
        self.assertEqual(macro._feed_shortfall(obs), 1)
        self.assertFalse(macro.candidate_legal(
            obs, macro.MACRO_BUY_ANIMAL_BASE + 1))

        obs = observation(money=424)
        obs["farms"][0]["tiles"][0][0] = tile(
            "PASTURE", animal="COW", fed_today=False, cared_today=True,
            yield_units=0, fertilizer_available=False,
        )
        obs["farms"][0]["tiles"][0][1] = tile("PASTURE", animal=None)
        self.assertGreater(macro._feed_cost(obs), 0)
        self.assertFalse(macro.candidate_legal(
            obs, macro.MACRO_BUY_ANIMAL_BASE + 1))

    def test_sell_all_consumes_quantity_by_visible_unit_value(self):
        obs = observation(shed={"WHEAT": 3, "MILK": 2, "WOOL": 1},
                          prices={"WHEAT": 10, "MILK": 200, "WOOL": 300})
        action = macro.execute_macro(obs, macro.MACRO_SELL_ALL, 2, 0)
        sales = [order for order in action["market"] if order[0] == "SELL"]
        self.assertEqual(sales, [["SELL", "WOOL", 1],
                                 ["SELL", "MILK", 1]])

    def test_terminal_hold_keeps_mandatory_feed_and_wheat_is_not_duplicated(self):
        obs = observation(step=700, day=29, hour=4)
        obs["farms"][0]["tiles"][0][0] = tile(
            "COOP", animal="GOOSE", fed_today=False, cared_today=True,
            yield_units=0, fertilizer_available=False,
        )
        hold = macro.execute_macro(obs, macro.MACRO_HOLD, 1, 0)
        buy = macro.execute_macro(obs, macro.MACRO_BUY_WHEAT, 1, 0)
        self.assertEqual(hold["market"], [["BUY_PRODUCT", "WHEAT", 1]])
        self.assertEqual(buy["market"], [["BUY_PRODUCT", "WHEAT", 1]])

    def test_locked_southeast_shed_spawn_escapes_to_owned_root(self):
        obs = observation()
        farm = obs["farms"][0]
        self.assertEqual(macro._route(farm, (5, 5), 0, 0), ["NORTH"])
        self.assertEqual(macro._route(farm, (5, 4), 0, 0), ["WEST"])

    def test_maintain_is_illegal_when_feed_cannot_commit(self):
        obs = observation(money=0, shed={"FERTILIZER": 100})
        obs["farms"][0]["tiles"][0][0] = tile(
            "PASTURE", animal="COW", fed_today=False, cared_today=True,
            yield_units=0, fertilizer_available=False,
        )
        self.assertFalse(macro.candidate_legal(obs, macro.MACRO_MAINTAIN))
        obs["private"]["shed"] = {"FERTILIZER": 99, "WHEAT": 1}
        self.assertTrue(macro.candidate_legal(obs, macro.MACRO_MAINTAIN))

    def test_product_buy_uses_next_marginal_quote(self):
        obs = observation(money=25)
        self.assertEqual(obs["market"]["prices"]["WHEAT"], 100)
        # Official defaults at equilibrium display $25, while decrementing
        # inventory first makes the committed marginal quote $26.
        obs["market"]["prices"]["WHEAT"] = 25
        self.assertEqual(macro._product_buy_cost(obs, "WHEAT", 1), 26)
        self.assertFalse(macro._market_buy_legal(obs, "BUY_PRODUCT", "WHEAT"))

    def test_target_mask_requires_reclaimable_tile(self):
        obs = observation(unlocked=("NW", "NE"))
        for y in range(5):
            for x in range(5, 10):
                obs["farms"][0]["tiles"][y][x] = tile(
                    "PLANT", crop="WHEAT", planted_day=0,
                    watered_today=True, yield_units=0,
                )
        mask = macro.NativeMacroRuntime().action_mask(obs)
        target_base = 2 * macro.UNIT_COMMANDS
        self.assertTrue(mask[target_base])      # AUTO
        self.assertTrue(mask[target_base + 1])  # NW
        self.assertFalse(mask[target_base + 2]) # saturated NE

    def test_diversify_feed_is_first_and_no_progress_is_illegal(self):
        action = {"market": [["BUY_ANIMAL", "COW", 1],
                             ["BUY_PRODUCT", "WHEAT", 1],
                             ["BUY_SEED", "WHEAT", 1]]}
        macro._prioritize_feed(action)
        self.assertEqual(action["market"][0], ["BUY_PRODUCT", "WHEAT", 1])

        obs = observation(money=0)
        obs["farms"][0]["tiles"][0][0] = tile("PASTURE", animal=None)
        self.assertFalse(macro.candidate_legal(obs, macro.MACRO_DIVERSIFY))
        obs["private"]["shed"]["COW"] = 1
        self.assertFalse(macro.candidate_legal(obs, macro.MACRO_DIVERSIFY))

        obs["private"]["shed"] = {}
        for y in range(5):
            for x in range(5):
                obs["farms"][0]["tiles"][y][x] = tile(
                    "PASTURE", animal=None)
        obs["farms"][0]["tiles"][0][1] = None
        obs["private"]["seeds"] = {"TOMATO": 1}
        self.assertFalse(macro.candidate_legal(obs, macro.MACRO_DIVERSIFY))

        obs["private"]["seeds"] = {}
        obs["farms"][0]["tiles"][0][1] = tile(
            "PLANT", crop="TOMATO", planted_day=0,
            watered_today=True, yield_units=1,
        )
        self.assertFalse(macro.candidate_legal(obs, macro.MACRO_DIVERSIFY))

    def test_structured_runtime_decides_each_turn_without_opponent_private(self):
        runtime = macro.NativeMacroRuntime()
        obs = observation(shed={"MILK": 1})
        encoded = np.zeros(1280, dtype=np.uint8)
        runtime.fill_observation(obs, encoded)
        selected = macro.MACRO_SELL_BASE + 6
        first = runtime.decode(obs, [selected, 0, 0])
        self.assertIn(["SELL", "MILK", 1], first["market"])

        obs["step"] += 1
        obs["hour"] += 1
        runtime.fill_observation(obs, encoded)
        self.assertEqual(encoded[macro.OBS_OFFSET + macro.MACRO_COUNT], 0)
        self.assertEqual(
            encoded[macro.OBS_OFFSET + macro.MACRO_COUNT + 1],
            macro._u8_scale(selected, macro.MACRO_COUNT - 1),
        )
        second = runtime.decode(obs, [macro.MACRO_HOLD, 0, 0])
        self.assertEqual(second["market"], [])

        mask = runtime.action_mask(obs)
        self.assertTrue(mask[macro.MACRO_HOLD])
        self.assertFalse(mask[macro.MACRO_RESERVED_BASE])


if __name__ == "__main__":
    unittest.main()
