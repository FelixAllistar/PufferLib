from __future__ import annotations

import pathlib
import sys
import unittest

import numpy as np


KAG_DIR = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

import import_elite_replays as importer
import macro_bc_labels
import task_bc_labels as labels


def observation(*, step=0, day=0, farmer=(4, 4), hands=(),
                unlocked=("NW",), seeds=None, shed=None, inventories=None):
    board = [["LOCKED" for _ in range(10)] for _ in range(10)]
    for name in unlocked:
        x0 = 5 if name in ("NE", "SE") else 0
        y0 = 5 if name in ("SW", "SE") else 0
        for y in range(y0, y0 + 5):
            for x in range(x0, x0 + 5):
                board[y][x] = None
    own = {
        "money": 5000,
        "farmer": list(farmer),
        "hands": [list(value) for value in hands],
        "hires_today": 0,
        "unlocked_quadrants": list(unlocked),
        "tiles": board,
    }
    opponent = {
        "money": 3000, "farmer": [4, 4], "hands": [], "hires_today": 0,
        "unlocked_quadrants": ["NW"],
        "tiles": [[None if x < 5 and y < 5 else "LOCKED"
                   for x in range(10)] for y in range(10)],
    }
    products = macro_bc_labels.RUNTIME.PRODUCTS
    return {
        "step": step, "day": day, "hour": step % 24, "player": 0,
        "farms": [own, opponent],
        "private": {
            "shed": dict(shed or {}), "seeds": dict(seeds or {}),
            "inventories": [dict(value) for value in
                            (inventories or [{} for _ in range(1 + len(hands))])],
        },
        "market": {
            "prices": {item: 100 for item in products},
            "inventory": {item: 10000 for item in products},
        },
        "town": {"unlocked_shops": []},
    }


def action(farmer=("PASS",), hands=(), market=()):
    return {
        "farmer": list(farmer),
        "hands": [list(value) for value in hands],
        "market": [list(value) for value in market],
    }


class TaskBCLabelTests(unittest.TestCase):
    def test_movement_traces_to_eventual_work_but_pass_stays_idle(self):
        observations = [
            observation(step=0, farmer=(4, 4), seeds={"WHEAT": 1}),
            observation(step=1, farmer=(3, 4), seeds={"WHEAT": 1}),
            observation(step=2, farmer=(2, 4), seeds={"WHEAT": 1}),
        ]
        actions = [
            action(("WEST",)),
            action(("WEST",)),
            action(("PLANT", "WHEAT")),
        ]
        tokens, audit = labels.trajectory_task_tokens(
            observations, actions, lookahead=8,
        )
        wheat_nw = macro_bc_labels.RUNTIME.TASK_PLANT_BASE
        self.assertEqual([row[0] for row in tokens],
                         [wheat_nw, wheat_nw, wheat_nw])
        self.assertEqual(audit["task_routed"], 2)

        actions[0] = action(("PASS",))
        tokens, _ = labels.trajectory_task_tokens(observations, actions)
        self.assertEqual(tokens[0][0], macro_bc_labels.RUNTIME.TASK_IDLE)

    def test_task_multiset_packs_active_hand_work_before_idle(self):
        obs = observation(step=48, day=2, farmer=(4, 4), hands=((0, 0),))
        obs["farms"][0]["tiles"][0][0] = {
            "kind": "PLANT", "crop": "WHEAT", "planted_day": 0,
            "watered_today": False, "yield_units": 0,
        }
        tokens, _ = labels.trajectory_task_tokens(
            [obs], [action(("PASS",), (("WATER",),))],
        )
        self.assertEqual(tokens[0], [macro_bc_labels.RUNTIME.TASK_WATER, 0])

    def test_mode3_row_keeps_task_and_market_decisions(self):
        obs = observation(seeds={"TOMATO": 1})
        token = macro_bc_labels.RUNTIME.TASK_PLANT_BASE + 4 * 2
        runtime = macro_bc_labels.RUNTIME.NativeMacroRuntime(mode=3)
        row_obs, expert, packed, audit = importer._build_task_row(
            obs,
            action(market=(("BUY_LAND",),)),
            [token], runtime=runtime,
        )
        self.assertEqual(row_obs.shape, (1280,))
        self.assertEqual(packed.shape, (importer.MASK_BYTES,))
        self.assertEqual(expert[0], token)
        self.assertEqual(expert[17:20].tolist(), [1.0, 20.0, 0.0])
        self.assertEqual(expert[20], 0.0)
        self.assertEqual(audit.macro_mode, "tasks")

    def test_trajectory_alignment_uses_next_record_action(self):
        observations = [
            observation(step=turn, farmer=(4 - min(turn, 2), 4),
                        seeds={"WHEAT": 1})
            for turn in range(4)
        ]
        decisions = [
            action(),
            action(("WEST",)),
            action(("WEST",)),
            action(("PLANT", "WHEAT")),
        ]
        episode = {
            "steps": [[{"observation": observations[turn],
                         "action": decisions[turn]},
                        {"observation": observations[turn],
                         "action": action()}]
                       for turn in range(4)],
            "rewards": [9000, 3000],
        }
        _obs, experts, _masks, audit = importer._build_trajectory(
            episode, 0, 4, "tasks", task_lookahead=8,
        )
        wheat_nw = macro_bc_labels.RUNTIME.TASK_PLANT_BASE
        self.assertEqual(experts[:3, 0].tolist(),
                         [float(wheat_nw)] * 3)
        self.assertLess(experts[3, 0], 0)
        self.assertEqual(audit.macro_mode, "tasks")


if __name__ == "__main__":
    unittest.main()
