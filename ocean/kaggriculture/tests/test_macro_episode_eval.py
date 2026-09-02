import ctypes
import json
import sys
import tempfile
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

from macro_actions import merge_macro_action
from macro_episode_eval import evaluate, parse_args


class MacroEpisodeEvalTests(unittest.TestCase):
    def test_macro_overlay_preserves_unspecified_fallback_work(self):
        fallback = {
            "farmer": ["WATER"],
            "hands": [["HARVEST"], ["FEED"]],
            "market": [["SELL", "MILK", 2]],
        }
        direct = {
            "farmer": ["PASS"],
            "hands": [["PASS"], ["PASS"]],
            "market": [["BUY_SEED", "WHEAT", 5]],
        }
        merged = merge_macro_action(direct, fallback)
        self.assertEqual(merged["farmer"], ["WATER"])
        self.assertEqual(merged["hands"], [["HARVEST"], ["FEED"]])
        self.assertEqual(merged["market"], [["BUY_SEED", "WHEAT", 5]])

        farmer_macro = {
            "farmer": ["EAST"],
            "hands": [["PASS"], ["PASS"]],
            "market": [],
        }
        merged = merge_macro_action(farmer_macro, fallback)
        self.assertEqual(merged["farmer"], ["EAST"])
        self.assertEqual(merged["hands"], [["HARVEST"], ["FEED"]])

        # HOLD is the exact no-intervention baseline, including its market
        # queue.  This protects offline labels from turning HOLD into an
        # accidental market cancellation.
        self.assertEqual(
            merge_macro_action(
                {"farmer": ["PASS"], "hands": [], "market": []}, fallback,
            ),
            fallback,
        )

    def test_complete_episode_runs_against_submission_policy(self):
        import numpy as np

        hidden = 32
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint = root / "zero_32x2.bin"
            np.zeros(float_count, dtype=np.float32).tofile(checkpoint)
            macro_model = root / "macro.npz"
            names = np.asarray(["feature_step", "candidate_kind_code"])
            np.savez_compressed(
                macro_model, weights=np.zeros(3), mean=np.zeros(2), scale=np.ones(2),
                feature_names=names, target=np.asarray(["delta_money"]),
            )
            output = root / "episode.json"
            summary = evaluate(parse_args([
                "--macro-model", str(macro_model),
                "--opponent-model", str(checkpoint),
                "--output", str(output),
                "--episodes", "1", "--episode-steps", "8",
                "--decision-interval", "2",
            ]))
            self.assertEqual(len(summary["episodes"]), 1)
            self.assertEqual(summary["episodes"][0]["macro"]["steps"], 7)
            self.assertIn("summaries", summary)
            loaded = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(loaded["format"], "kaggriculture_macro_episode_eval_v1")


if __name__ == "__main__":
    unittest.main()
