import ctypes as C
import pathlib
import sys
import unittest
import os
import subprocess
import tempfile

import numpy as np

HERE = pathlib.Path(__file__).resolve().parents[1]
BUILD = HERE / os.environ.get("KAG_BC_TEST_BUILD", "build/bc_entity")
sys.path.insert(0, str(HERE))
import build_entity_bc_dataset as dataset
import entity_bc_labels as labels
import multi_intent_labels
import replay_native as native


class LabelTests(unittest.TestCase):
    def test_multi_intent_labels_preserve_every_action_and_order(self):
        action = {"farmer": ["PLANT", "WHEAT"],
                  "hands": [["PLANT", "MELON"], ["FEED"], ["EAST"], ["PLACE", "COW", 1]],
                  "market": [["SELL", "MILK", 50], ["HIRE"], ["BUY_LAND"], ["BUY_SEED", "WHEAT", 12]]}
        annotation = multi_intent_labels.annotate(action, [[0, 0], [5, 0], [1, 1], [4, 4], [5, 5]],
                      episode_id="test", player=0, step=7, single_label=[-1, -1, -1], reason="conflict")
        self.assertEqual(annotation["primitive_action"], action)
        self.assertEqual([r["command"] for r in annotation["market_queue"]], action["market"])
        self.assertEqual([r["item"] for r in annotation["production_requests"]], ["WHEAT", "MELON"])
        self.assertEqual(annotation["strategic_request_count"], 6)
        self.assertEqual(len(annotation["chore_trace"]), 2)
        self.assertEqual(len(annotation["movement_trace"]), 1)
        annotation["primitive_action"]["farmer"][1] = "CARROT"
        self.assertEqual(action["farmer"][1], "WHEAT")

    def test_discounted_returns_and_terminal_mask(self):
        np.testing.assert_allclose(dataset.discounted_returns([1, 2, 3], .5),
                                   [2.75, 3.5, 3, np.nan], equal_nan=True)

    def test_quantityless_market_orders_are_not_dropped(self):
        action = native.c_action({"market": [["HIRE"], ["BUY_LAND"]]})
        self.assertEqual(labels.markets(action), {(4, -1): 1, (5, -1): 1})
        self.assertEqual(labels.candidates(action), [])  # cannot do both in one macro
        self.assertEqual(labels.candidates(native.c_action({"market": [["HIRE"]]})), [28])
        self.assertEqual(labels.candidates(native.c_action({"market": [["BUY_LAND"]]})), [9])

    def test_returning_cow_does_not_mean_buy_cow(self):
        action = native.c_action({"farmer": ["PLACE", "COW", 1]})
        self.assertEqual(labels.strategy(action), set())
        self.assertEqual(labels.candidates(action), [])

    def test_maintenance_is_not_disabled_macro_30(self):
        for command in ("WATER", "CARE", "FEED", "HARVEST"):
            self.assertEqual(labels.candidates(native.c_action({"farmer": [command]})), [])
        self.assertEqual(labels.candidates(native.c_action({"farmer": ["FERTILIZE"]})), [36])
        self.assertEqual(labels.candidates(native.c_action({"farmer": ["DIG"]})), [35])

    def test_conflicting_growth_is_not_guessed(self):
        action = native.c_action({"farmer": ["PLANT", "WHEAT"], "hands": [["PLANT", "MELON"]]})
        self.assertEqual(labels.candidates(action), [])
        self.assertEqual(labels.candidates(native.c_action({"farmer": ["BUILD_PASTURE"]})), [])

    def test_only_unique_arguments_are_supervised(self):
        action = native.c_action({"market": [["SELL", "MILK", 4]]})
        def decode(heads):
            return native.c_action({"market": [["SELL", "MILK", 4 if heads[1] == 2 else 1]]})
        label, history, reason = labels.project(action, np.ones(labels.MASK), decode)
        np.testing.assert_array_equal(label[:3], [16, 2, -1])
        np.testing.assert_array_equal(history[:3], [16, 2, 0])
        self.assertEqual(reason, "projected_strategy")

    def test_masked_macro_never_becomes_label(self):
        action = native.c_action({"market": [["HIRE"]]})
        label, _, reason = labels.project(action, np.zeros(labels.MASK), lambda _: action)
        self.assertTrue(np.all(label == -1))
        self.assertEqual(reason, "executor_cannot_reproduce_strategy")


@unittest.skipUnless((BUILD / "libbc_replay.so").exists(), "build the isolated native bridge first")
class NativeBridgeTests(unittest.TestCase):
    def setUp(self):
        self.lib = dataset.load_bridge(BUILD / "libbc_replay.so")
        self.cfg = native.CConfig(); self.lib.kg_config_default(C.byref(self.cfg))
        self.ctx = self.lib.kag_bc_create(C.byref(self.cfg), str(HERE / "bc_2_1_pilot.ini").encode())
        self.assertTrue(self.ctx)

    def tearDown(self):
        self.lib.kag_bc_destroy(self.ctx)

    def test_float_view_idempotent_and_decoder_pure(self):
        obs = np.empty(labels.OBS, np.float32); mask = np.empty(labels.MASK, np.uint8)
        self.lib.kag_bc_view(self.ctx, 0, obs.ctypes.data, mask.ctypes.data)
        self.assertTrue(np.isfinite(obs).all())
        self.assertFalse(mask[30]); self.assertFalse(mask[31])
        self.assertTrue(mask[28])
        hold = np.zeros(labels.HEADS, np.float32)
        prefix_mask = np.empty_like(mask)
        self.lib.kag_bc_teacher_mask(self.ctx, 0, hold.ctypes.data, prefix_mask.ctypes.data)
        self.assertEqual(int(prefix_mask[44:88].sum()), 1)
        self.assertEqual(int(prefix_mask[88:132].sum()), 1)
        self.assertTrue(prefix_mask[44] and prefix_mask[88])
        action = native.CAction(); heads = np.zeros(labels.HEADS, np.float32); heads[0] = 28
        before = native.c_snapshot(self.lib, self.lib.kag_bc_state(self.ctx))
        self.lib.kag_bc_decode(self.ctx, 0, heads.ctypes.data, C.byref(action))
        self.assertEqual(labels.markets(action), {(4, -1): 1})
        self.assertEqual(native.c_snapshot(self.lib, self.lib.kag_bc_state(self.ctx)), before)
        obs2 = np.empty_like(obs); mask2 = np.empty_like(mask)
        self.lib.kag_bc_view(self.ctx, 0, obs2.ctypes.data, mask2.ctypes.data)
        np.testing.assert_array_equal(obs, obs2); np.testing.assert_array_equal(mask, mask2)

    def test_full_trajectory_matches_rule_core_and_has_terminal_reward(self):
        core = self.lib.kg_create(C.byref(self.cfg))
        rewards = np.empty(2, np.float32); heads = np.zeros(labels.HEADS, np.float32)
        pair = (native.CAction * 2)(native.c_action({}), native.c_action({}))
        try:
            for _ in range(self.cfg.episode_steps - 1):
                self.assertTrue(self.lib.kag_bc_step(self.ctx, pair, 0, heads.ctypes.data, rewards.ctypes.data))
                self.lib.kg_step(core, pair)
                self.assertTrue(np.isfinite(rewards).all())
            state = self.lib.kag_bc_state(self.ctx)
            self.assertTrue(self.lib.kg_done(state))
            self.assertEqual(native.c_snapshot(self.lib, state), native.c_snapshot(self.lib, core))
            self.assertFalse(self.lib.kag_bc_step(self.ctx, pair, 0, heads.ctypes.data, rewards.ctypes.data))
        finally:
            self.lib.kg_destroy(core)


@unittest.skipUnless(os.environ.get("KAG_BC_TEST_DATA"), "set KAG_BC_TEST_DATA for compiled trainer preflight")
class TrainerPreflightTests(unittest.TestCase):
    def run_check(self, path, *extra):
        binary = BUILD / "kag_bc"
        profile = HERE / os.environ.get("KAG_BC_TEST_PROFILE", "bc_2_1_pilot.ini")
        return subprocess.run([str(binary), "bc.mode=train", f"bc.profile={profile}",
                               f"bc.data={path}", "bc.verify_only=1", *extra],
                              cwd=HERE.parents[1], env={**os.environ, "CUDA_VISIBLE_DEVICES": ""},
                              capture_output=True, text=True, timeout=30)

    def test_valid_joint_loss_preflight_needs_no_gpu(self):
        result = self.run_check(os.environ["KAG_BC_TEST_DATA"], "bc.value_coef=0.1")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("BC v3 preflight", result.stdout)

    def test_old_or_changed_contracts_and_truncation_rejected(self):
        with pathlib.Path(os.environ["KAG_BC_TEST_DATA"]).open("rb") as stream:
            original = list(dataset.HEADER.unpack(stream.read(dataset.HEADER.size)))
        changes = {"legacy": (1, 2), "old_policy": (10, 3), "prototype_policy": (10, 4), "byte_obs": (8, 1), "executor": (12, 0),
                   "source": (16, original[16] ^ 1), "rewards": (17, original[17] ^ 1),
                   "gamma": (18, .9), "no_holdout": (15, 0), "steps": (7, 2),
                   "truncated": None}
        with tempfile.TemporaryDirectory(prefix="kag-bc-preflight-") as directory:
            for name, change in changes.items():
                with self.subTest(name=name):
                    header = original.copy()
                    if change:
                        header[change[0]] = change[1]
                    path = pathlib.Path(directory) / f"{name}.bc"
                    path.write_bytes(dataset.HEADER.pack(*header))
                    self.assertNotEqual(self.run_check(path).returncode, 0)

    def test_cli_cannot_silently_change_returns_or_holdout(self):
        for argument in ("train.gamma=0.9", "env.reward_money_timing=0", "bc.validation_games=1"):
            result = self.run_check(os.environ["KAG_BC_TEST_DATA"], argument)
            self.assertNotEqual(result.returncode, 0, argument)


if __name__ == "__main__":
    unittest.main()
