"""Exact native/submission observation and mask parity for task mode."""

from __future__ import annotations

import ctypes
import os
from pathlib import Path
import subprocess
import sys
import unittest

import numpy as np


KAG_DIR = Path(__file__).resolve().parents[1]
SUBMISSION = KAG_DIR / "submission"
LIB = KAG_DIR / "build" / "libkaggriculture.so"
sys.path.insert(0, str(KAG_DIR))
sys.path.insert(0, str(SUBMISSION))
os.environ["PUFFERLIB_MODEL_PATH"] = "/nonexistent/task-parity.bin"

import main as submission_main  # noqa: E402
from native_macro_runtime import NativeMacroRuntime  # noqa: E402
from replay_native import CAction, CConfig, c_action, c_snapshot, load_core  # noqa: E402


class NativeTaskParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", str(KAG_DIR), "lib"], check=True)
        cls.lib = load_core(LIB)

    def test_mode2_automatic_operation_native_portable_parity(self):
        for driver in ("rule", "cow"):
            with self.subTest(driver=driver):
                self._check_mode2_operation_parity(driver)

    def test_observation_versions_both_seats(self):
        fn = self.lib.kg_policy_observation_version
        fn.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                       ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
        fn.restype = None
        cfg = CConfig()
        self.lib.kg_config_default(ctypes.byref(cfg))
        cfg.seed = 709
        state = self.lib.kg_create(ctypes.byref(cfg))
        runtime = NativeMacroRuntime(mode=2)
        different = False
        try:
            for turn in range(720):
                if turn % 24 == 0:
                    snapshot = c_snapshot(self.lib, state)
                    for player in range(2):
                        obs = dict(snapshot, player=player, private=snapshot['privates'][player])
                        obs.pop('privates', None)
                        versions = []
                        for version in (0, 1):
                            portable = submission_main.encode_observation(obs, version)
                            runtime.fill_observation(obs, portable)
                            native = np.zeros(1280, dtype=np.uint8)
                            fn(state, player, 2, version, native.ctypes.data, native.nbytes)
                            np.testing.assert_array_equal(portable, native,
                                err_msg=f'turn={turn} seat={player} version={version}')
                            versions.append(portable)
                        if player == 0:
                            np.testing.assert_array_equal(*versions)
                        else:
                            different |= not np.array_equal(*versions)
                actions = (CAction * 2)()
                self.lib.kg_rule_action(state, 0, ctypes.byref(actions[0]))
                # Asymmetric rule-versus-pass farms make the ownership check non-vacuous.
                self.lib.kg_step(state, actions)
            self.assertTrue(different)
        finally:
            self.lib.kg_destroy(state)

    def _check_mode2_operation_parity(self, driver):
        self.lib.kg_policy_macro_action.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(CAction)]
        self.lib.kg_policy_macro_action.restype = ctypes.c_int
        cfg = CConfig()
        self.lib.kg_config_default(ctypes.byref(cfg))
        cfg.seed = 709
        state = self.lib.kg_create(ctypes.byref(cfg))
        runtime = NativeMacroRuntime(mode=2)
        try:
            for turn in range(361):
                if turn % 12 == 0:
                    snapshot = c_snapshot(self.lib, state)
                    for player in range(2):
                        obs = dict(snapshot, player=player,
                                   private=snapshot["privates"][player])
                        mask = runtime.action_mask(obs)
                        for macro in np.flatnonzero(mask[:44]):
                            for quantity in (0, 2):
                                requests = np.zeros(47, dtype=np.float32)
                                requests[0] = macro
                                requests[1] = quantity
                                native = CAction()
                                self.lib.kg_policy_macro_action(state, player,
                                    requests.ctypes.data_as(ctypes.c_void_p),
                                    ctypes.byref(native))
                                portable = c_action(runtime.decode(obs, requests))
                                def signature(action):
                                    units = [action.farmer, *action.hands[:action.hand_count]]
                                    return ([(u.op, u.arg if u.op in (5, 7, 14) else -1,
                                              u.n if u.op == 5 else 0) for u in units],
                                            [(m.op, m.item, m.n if m.op < 4 else 0)
                                             for m in action.market[:action.market_count]])
                                self.assertEqual(signature(native), signature(portable),
                                    f"mode2 turn={turn} player={player} macro={macro} quantity={quantity}")
                actions = (CAction * 2)()
                for player in range(2):
                    if driver == "cow" and player == 0:
                        requests = np.zeros(47, dtype=np.float32)
                        requests[0] = 7 if turn < 12 else 0
                        self.lib.kg_policy_macro_action(state, player,
                            requests.ctypes.data_as(ctypes.c_void_p),
                            ctypes.byref(actions[player]))
                    else:
                        self.lib.kg_rule_action(state, player, ctypes.byref(actions[player]))
                self.lib.kg_step(state, actions)
        finally:
            self.lib.kg_destroy(state)

    def test_public_runtime_matches_native_across_rule_episode(self):
        cfg = CConfig()
        self.lib.kg_config_default(ctypes.byref(cfg))
        cfg.episode_steps = 720
        cfg.seed = 709
        state = self.lib.kg_create(ctypes.byref(cfg))
        self.assertTrue(state)
        runtime = NativeMacroRuntime(mode=3)
        try:
            for turn in range(97):
                if turn % 4 == 0:
                    snapshot = c_snapshot(self.lib, state)
                    for player in range(2):
                        observation = dict(snapshot)
                        observation["player"] = player
                        observation["private"] = snapshot["privates"][player]
                        observation.pop("privates", None)

                        portable_obs = submission_main.encode_observation(observation)
                        runtime.fill_observation(observation, portable_obs)
                        native_obs = np.zeros(1280, dtype=np.uint8)
                        self.lib.kg_policy_observation_mode(
                            state, player, 3,
                            native_obs.ctypes.data_as(ctypes.c_void_p),
                            native_obs.nbytes,
                        )
                        np.testing.assert_array_equal(
                            portable_obs, native_obs,
                            err_msg=f"observation mismatch turn={turn} player={player}",
                        )

                        portable_mask = runtime.action_mask(observation)
                        native_mask = np.zeros(1058, dtype=np.uint8)
                        self.lib.kg_policy_action_mask_mode(
                            state, player, 3,
                            native_mask.ctypes.data_as(ctypes.c_void_p),
                            native_mask.nbytes,
                        )
                        np.testing.assert_array_equal(
                            portable_mask, native_mask.astype(bool),
                            err_msg=f"mask mismatch turn={turn} player={player}",
                        )
                        # Repeated requests exercise worker/tile conflicts and
                        # prerequisite pickups. Compare committed action fields;
                        # ignored args/counts on PASS/movement need not match.
                        for token in np.flatnonzero(portable_mask[:44]):
                            requests = np.zeros(47, dtype=np.float32)
                            requests[:17] = token
                            native_action = CAction()
                            self.assertEqual(self.lib.kg_policy_task_action(
                                state, player, requests.ctypes.data_as(ctypes.c_void_p),
                                ctypes.byref(native_action)), 1)
                            portable_action = c_action(runtime.decode(observation, requests))
                            def signature(action):
                                units = [action.farmer, *action.hands[:action.hand_count]]
                                return ([(u.op, u.arg if u.op in (5, 7, 14) else -1,
                                          u.n if u.op == 5 else 0) for u in units],
                                        [(m.op, m.item, m.n) for m in action.market[:action.market_count]])
                            self.assertEqual(signature(native_action), signature(portable_action),
                                f"action mismatch turn={turn} player={player} token={token}")
                if turn == 96:
                    break
                actions = (CAction * 2)()
                for player in range(2):
                    self.lib.kg_rule_action(
                        state, player, ctypes.byref(actions[player])
                    )
                self.lib.kg_step(state, actions)
        finally:
            self.lib.kg_destroy(state)


if __name__ == "__main__":
    unittest.main()
