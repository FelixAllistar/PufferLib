from __future__ import annotations

import copy
import json
import pathlib
import sys
import tempfile
import unittest
import zipfile

import numpy as np


KAG_DIR = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

import macro_bc_labels as labels
import import_elite_replays as importer
import scan_replay_identities as identities


def _observation(*, money=5000, step=24, day=1, hour=0,
                 hands=(), unlocked=("NW",), shed=None, seeds=None):
    board = [["LOCKED" for _ in range(10)] for _ in range(10)]
    for name in unlocked:
        x0 = 5 if name in ("NE", "SE") else 0
        y0 = 5 if name in ("SW", "SE") else 0
        for y in range(y0, y0 + 5):
            for x in range(x0, x0 + 5):
                board[y][x] = None
    own = {
        "money": money, "farmer": [4, 4],
        "hands": [list(position) for position in hands],
        "hires_today": 0, "unlocked_quadrants": list(unlocked),
        "tiles": board,
    }
    opponent = {
        "money": 1, "farmer": [4, 4], "hands": [], "hires_today": 0,
        "unlocked_quadrants": ["NW"],
        "tiles": [[None if x < 5 and y < 5 else "LOCKED"
                   for x in range(10)] for y in range(10)],
    }
    prices = {item: 100 for item in labels._PRODUCTS}
    return {
        "step": step, "day": day, "hour": hour, "player": 0,
        "farms": [own, opponent],
        "private": {
            "shed": dict(shed or {}), "seeds": dict(seeds or {}),
            "inventories": [{} for _ in range(1 + len(hands))],
        },
        "market": {
            "prices": prices,
            "inventory": {item: 10000 for item in labels._PRODUCTS},
        },
        "town": {"unlocked_shops": []},
    }


class MacroBCLabelTests(unittest.TestCase):
    def test_buy_seed_uses_mode2_intent_quantity_and_legal_mask(self):
        obs = _observation()
        action = {"farmer": ["PASS"], "hands": [],
                  "market": [["BUY_SEED", "WHEAT", 7]]}
        label = labels.translate_action(obs, action)
        self.assertIsNotNone(label)
        assert label is not None
        self.assertEqual(label.macro_id, labels.RUNTIME.MACRO_BUY_SEED_BASE)
        self.assertEqual(label.quantity, 7)
        self.assertEqual(label.quantity_bin, 2)  # native bins floor 7 -> 4
        self.assertFalse(label.quantity_exact)
        expert, packed, diagnostic, mask = labels.build_macro_row(obs, action)
        self.assertIsNotNone(diagnostic)
        self.assertEqual(expert[:3].tolist(), [20.0, 2.0, 0.0])
        self.assertEqual(packed.shape, (labels.MASK_BYTES,))
        self.assertTrue(mask[20])
        self.assertTrue(mask[44 + 2])

    def test_plant_target_is_inferred_from_acting_worker_positions(self):
        obs = _observation(unlocked=("NW", "NE"), hands=((5, 4),), seeds={"MELON": 2})
        action = {"farmer": ["PASS"], "hands": [["PLANT", "MELON"]], "market": []}
        label = labels.translate_action(obs, action)
        self.assertIsNotNone(label)
        assert label is not None
        self.assertEqual(label.macro_id, labels.RUNTIME.MACRO_PLANT_BASE + 4)
        self.assertEqual(label.target, 2)  # NE
        self.assertEqual(label.target_bin, 2)
        expert, _packed, diagnostic, _mask = labels.build_macro_row(obs, action)
        self.assertFalse(diagnostic.ambiguous)
        self.assertEqual(expert[:3].tolist(), [5.0, 0.0, 2.0])

    def test_conflicting_intents_are_filtered_not_falsely_labeled(self):
        obs = _observation()
        action = {"farmer": ["PASS"], "hands": [], "market": [
            ["BUY_SEED", "WHEAT", 1], ["BUY_ANIMAL", "COW", 1],
        ]}
        self.assertIsNone(labels.translate_action(obs, action, strict=True))
        diagnostic = labels.translate_action(obs, action, strict=False)
        self.assertTrue(diagnostic.ambiguous)
        expert, _packed, _diagnostic, _mask = labels.build_macro_row(obs, action)
        self.assertTrue(np.all(expert < 0))

    def test_terminal_investment_is_rejected_by_current_mode2_legality(self):
        obs = _observation(step=700, day=29, hour=4)
        action = {"farmer": ["PASS"], "hands": [],
                  "market": [["BUY_PRODUCT", "FERTILIZER", 1]]}
        self.assertIsNone(labels.translate_action(obs, action, strict=True))
        diagnostic = labels.translate_action(obs, action, strict=False)
        self.assertTrue(diagnostic.ambiguous)
        self.assertIn("legality mask", diagnostic.reason)
        expert, _packed, diagnostic, _mask = labels.build_macro_row(obs, action)
        self.assertTrue(diagnostic.ambiguous)
        self.assertIn("legality mask", diagnostic.reason)
        self.assertTrue(np.all(expert < 0))

    def test_final_two_day_multi_product_sale_is_cash_out(self):
        obs = _observation(step=680, day=28, hour=8,
                           shed={"WHEAT": 2, "MILK": 1})
        action = {"farmer": ["PASS"], "hands": [], "market": [
            ["SELL", "WHEAT", 2], ["SELL", "MILK", 1],
        ]}
        label = labels.translate_action(obs, action)
        self.assertIsNotNone(label)
        assert label is not None
        self.assertEqual(label.macro_id, labels.RUNTIME.MACRO_CASH_OUT)

    def test_opponent_private_or_board_changes_cannot_change_label_or_row(self):
        obs = _observation(shed={"MILK": 1})
        action = {"farmer": ["PASS"], "hands": [],
                  "market": [["SELL", "MILK", 1]]}
        before = labels.build_macro_row(obs, action)
        changed = copy.deepcopy(obs)
        changed["farms"][1]["money"] = 999999
        changed["farms"][1]["unlocked_quadrants"] = ["NW", "NE", "SW", "SE"]
        changed["farms"][1]["tiles"][0][0] = {"kind": "PLANT", "crop": "MELON"}
        changed["private"]["opponent"] = {
            "shed": {"MILK": 999999}, "inventories": [{"MILK": 999999}],
        }
        after = labels.build_macro_row(changed, action)
        self.assertEqual(before[0].tolist(), after[0].tolist())
        self.assertEqual(before[1].tolist(), after[1].tolist())
        self.assertEqual(before[2].as_dict(), after[2].as_dict())

    def test_identity_scan_keeps_same_display_name_and_agent_name_distinct(self):
        def episode(number, team, agent):
            return {
                "name": "kaggriculture", "module_version": "1.32.7",
                "info": {"EpisodeId": number, "TeamNames": [team, "bot"],
                         "Agents": [{"Name": agent}, {"Name": "bot"}]},
                "rewards": [100, 1], "steps": [],
            }

        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "kaggriculture-episodes-2026-09-01.zip"
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("one.json", json.dumps(episode(1, "Yuan800", "sub-a")))
                archive.writestr("two.json", json.dumps(episode(2, "Yuan800", "sub-b")))
            report = identities.scan([path], agent_names={"sub-a"})
        rows = [row for row in report["records"] if row["display_name"] == "Yuan800"]
        self.assertEqual({(row["display_name"], row["agent_name"])
                          for row in rows}, {("Yuan800", "sub-a"), ("Yuan800", "sub-b")})
        selected = [row for row in rows if row["agent_name"] == "sub-a"][0]
        self.assertTrue(selected["full_parse"])
        self.assertEqual(selected["episodes"], 1)

    def test_import_identity_filters_match_the_same_exact_seat(self):
        episode = {
            "info": {
                "TeamNames": ["Yuan800", "Yuan800"],
                "Agents": [{"Name": "sub-a"}, {"Name": "Yuan800"}],
            }
        }
        self.assertEqual(
            importer._identity_players(
                episode, display_names={"Yuan800"}, submission_names=set()
            ),
            [0, 1],
        )
        self.assertEqual(
            importer._identity_players(
                episode, display_names=set(), submission_names={"Yuan800"}
            ),
            [1],
        )
        self.assertEqual(
            importer._identity_players(
                episode, display_names={"Yuan800"}, submission_names={"sub-a"}
            ),
            [0],
        )
        self.assertEqual(
            importer._identity_players(
                episode, display_names={"Yuan800"}, submission_names={"missing"}
            ),
            [],
        )


if __name__ == "__main__":
    unittest.main()
