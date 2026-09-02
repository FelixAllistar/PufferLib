import ctypes
import sys
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
LIB = KAG_DIR / "build" / "libkaggriculture.so"
sys.path.insert(0, str(KAG_DIR))

from opponent_providers import NativePolicyProvider, RuleProvider, policy_paths_from_league
from replay_native import CAction, CConfig, c_action, c_snapshot, load_core


PASS = {"farmer": ["PASS"], "hands": [], "market": []}


class OpponentProviderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_core(LIB)
        cls.config = CConfig()
        cls.lib.kg_config_default(ctypes.byref(cls.config))
        cls.config.episode_steps = 80

    def test_rule_provider_reacts_to_terminal_inventory(self):
        snapshot = {
            "step": 79,
            "farms": [{"money": 3000, "hands": [], "tiles": [[None]]},
                      {"money": 3000, "hands": [], "tiles": [[None]]}],
            "privates": [{"shed": {"MILK": 4, "WHEAT": 0}, "seeds": {}, "inventories": []},
                         {"shed": {}, "seeds": {}, "inventories": []}],
            "market": {"prices": {"MILK": 200}, "inventory": {}},
            "town": {"unlocked_shops": []},
        }
        action = RuleProvider(episode_steps=80).action(snapshot, 0)
        self.assertEqual(action["market"], [["SELL", "MILK", 4]])

    def test_rule_provider_runs_against_native_core(self):
        provider = RuleProvider(episode_steps=80)
        state = self.lib.kg_create(ctypes.byref(self.config))
        try:
            for _ in range(20):
                snapshot = c_snapshot(self.lib, state)
                actions = (CAction * 2)()
                actions[0] = c_action(provider.action(snapshot, 0))
                actions[1] = c_action(provider.action(snapshot, 1))
                self.lib.kg_step(state, actions)
                if self.lib.kg_done(state):
                    break
            snapshot = c_snapshot(self.lib, state)
            self.assertGreaterEqual(snapshot["step"], 20)
            self.assertIn("market", snapshot)
        finally:
            self.lib.kg_destroy(state)

    def test_provider_preserves_hand_action_shape(self):
        snapshot = {
            "step": 10,
            "farms": [{"money": 5000, "hands": [[0, 0], [1, 1]],
                       "tiles": [[None, None], [None, None]]},
                      {"money": 5000, "hands": [], "tiles": [[None]]}],
            "privates": [{"shed": {}, "seeds": {"WHEAT": 2},
                          "inventories": [{}, {}, {}]},
                         {"shed": {}, "seeds": {}, "inventories": []}],
            "market": {"prices": {"WHEAT": 25}, "inventory": {}},
            "town": {"unlocked_shops": []},
        }
        action = RuleProvider().action(snapshot, 0)
        self.assertEqual(len(action["hands"]), 2)
        self.assertEqual(len(action["market"]) <= 9, True)

    def test_native_rule_action_is_reactive_and_window_configurable(self):
        state = self.lib.kg_create(ctypes.byref(self.config))
        try:
            actions = (CAction * 2)()
            self.assertTrue(hasattr(self.lib, "kg_rule_action_ex"))
            self.lib.kg_rule_action_ex(state, 0, 1, ctypes.byref(actions[0]))
            self.lib.kg_rule_action_ex(state, 1, 1, ctypes.byref(actions[1]))
            self.assertLessEqual(actions[0].hand_count, 240)
            self.assertLessEqual(actions[0].market_count, 32)
            self.lib.kg_step(state, actions)
            self.assertEqual(int(self.lib.kg_state_step(state)), 1)
        finally:
            self.lib.kg_destroy(state)

    def test_learned_provider_uses_submission_abi_and_is_deterministic(self):
        import numpy as np

        # A zero 512x2 checkpoint is enough to exercise the complete policy
        # encoder/mask/decoder path without making the test depend on a user
        # league or a multi-gigabyte checkpoint archive.
        hidden = 512
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "zero_512x2.bin"
            np.zeros(float_count, dtype=np.float32).tofile(model_path)
            # Keep the native state alive only long enough to obtain a complete
            # public/private snapshot for the policy adapter.
            state = self.lib.kg_create(ctypes.byref(self.config))
            try:
                snapshot = c_snapshot(self.lib, state)
            finally:
                self.lib.kg_destroy(state)
            provider = NativePolicyProvider((str(model_path),), deterministic=True)
            provider.begin_episode("fixture", 0)
            first = provider.action(snapshot, 0)
            second = provider.action(snapshot, 0)
            self.assertEqual(first, second)
            self.assertEqual(len(first["hands"]), len(snapshot["farms"][0]["hands"]))
            self.assertIsInstance(c_action(first), CAction)

    def test_live_provider_carries_recurrent_state_and_fork_is_independent(self):
        import numpy as np
        hidden = 32
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "zero_32x2.bin"
            np.zeros(float_count, dtype=np.float32).tofile(model_path)
            state = self.lib.kg_create(ctypes.byref(self.config))
            try:
                snapshot = c_snapshot(self.lib, state)
            finally:
                self.lib.kg_destroy(state)
            provider = NativePolicyProvider(
                (str(model_path),), deterministic=True, snapshot_local=False,
            )
            provider.begin_episode("fixture", 0)
            first = provider.action(snapshot, 0)
            child = provider.fork()
            # A fork must own hidden state rather than aliasing the live model.
            self.assertIsNot(child._models[0].state, provider._models[0].state)
            second = provider.action(snapshot, 0)
            child_second = child.action(snapshot, 0)
            self.assertEqual(second, child_second)

    def test_torch_backend_matches_numpy_on_zero_checkpoint(self):
        try:
            import torch  # noqa: F401
        except ImportError:
            self.skipTest("torch is optional for the accelerated provider")
        import numpy as np
        hidden = 32
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "zero_32x2.bin"
            np.zeros(float_count, dtype=np.float32).tofile(model_path)
            state = self.lib.kg_create(ctypes.byref(self.config))
            try:
                snapshot = c_snapshot(self.lib, state)
            finally:
                self.lib.kg_destroy(state)
            numpy_provider = NativePolicyProvider((str(model_path),), deterministic=True)
            torch_provider = NativePolicyProvider(
                (str(model_path),), deterministic=True,
                backend="torch", device="cuda" if __import__("torch").cuda.is_available() else "cpu",
            )
            numpy_provider.begin_episode("fixture", 0)
            torch_provider.begin_episode("fixture", 0)
            self.assertEqual(numpy_provider.action(snapshot, 0), torch_provider.action(snapshot, 0))

    def test_native_torch_batch_matches_snapshot_adapter(self):
        """The accelerated C observation view must preserve policy actions."""
        try:
            import torch  # noqa: F401
        except ImportError:
            self.skipTest("torch is optional for the accelerated provider")
        import numpy as np
        hidden = 32
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "zero_32x2.bin"
            np.zeros(float_count, dtype=np.float32).tofile(model_path)
            state = self.lib.kg_create(ctypes.byref(self.config))
            try:
                provider = NativePolicyProvider(
                    (str(model_path),), deterministic=True,
                    backend="torch", device="cuda" if torch.cuda.is_available() else "cpu",
                )
                provider.begin_episode("fixture", 0)
                snapshot = c_snapshot(self.lib, state)
                expected = provider.action(snapshot, 0)
                accelerated = provider.native_action_batch(self.lib, [state], [0])[0]
                self.assertEqual(expected, accelerated)
                self.assertIsInstance(c_action(accelerated), CAction)
            finally:
                self.lib.kg_destroy(state)

    def test_native_torch_view_matches_json_on_random_checkpoint(self):
        """Check the native observation/mask shim beyond all-zero argmaxes."""
        try:
            import torch  # noqa: F401
        except ImportError:
            self.skipTest("torch is optional for the accelerated provider")
        import numpy as np
        hidden = 32
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "random_32x2.bin"
            rng = np.random.default_rng(913)
            rng.normal(0.0, 0.03, float_count).astype(np.float32).tofile(model_path)
            # The production policy ABI is defined for the normal 720-turn
            # season.  Use a fresh default config here rather than the short
            # 80-turn fixture used by the rule-provider tests: the C writer
            # intentionally scales its clock from config.episode_steps.
            config = CConfig()
            self.lib.kg_config_default(ctypes.byref(config))
            state = self.lib.kg_create(ctypes.byref(config))
            try:
                provider = NativePolicyProvider(
                    (str(model_path),), deterministic=True,
                    backend="torch", device="cuda" if torch.cuda.is_available() else "cpu",
                )
                provider.begin_episode("fixture", 0)
                for _ in range(30):
                    snapshot = c_snapshot(self.lib, state)
                    expected = provider.action(snapshot, 0)
                    accelerated = provider.native_action(self.lib, state, 0)
                    self.assertEqual(expected, accelerated)
                    self.assertIsInstance(c_action(accelerated), CAction)
                    actions = (CAction * 2)()
                    actions[0] = c_action(PASS)
                    actions[1] = c_action(PASS)
                    self.lib.kg_step(state, actions)
            finally:
                self.lib.kg_destroy(state)

    def test_torch_logits_match_numpy_for_random_checkpoint(self):
        """Check the accelerated MinGRU math, not only all-zero argmaxes."""
        try:
            import torch  # noqa: F401
        except ImportError:
            self.skipTest("torch is optional for the accelerated provider")
        import numpy as np
        hidden = 32
        obs_size = 1280
        unit_heads = 17
        mask_size = unit_heads * 44 + 10 * (2 + 21 + 8)
        float_count = hidden * obs_size + (mask_size + 1) * hidden + 2 * 3 * hidden * hidden
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "random_32x2.bin"
            rng = np.random.default_rng(808)
            rng.normal(0.0, 0.03, float_count).astype(np.float32).tofile(model_path)
            state = self.lib.kg_create(ctypes.byref(self.config))
            try:
                snapshot = c_snapshot(self.lib, state)
            finally:
                self.lib.kg_destroy(state)
            numpy_provider = NativePolicyProvider((str(model_path),), deterministic=True)
            torch_provider = NativePolicyProvider(
                (str(model_path),), deterministic=True,
                backend="torch", device="cuda" if torch.cuda.is_available() else "cpu",
            )
            view = dict(snapshot)
            view["player"] = 0
            view["private"] = snapshot["privates"][0]
            encoded = numpy_provider._module.encode_observation(view)
            numpy_provider.begin_episode("fixture", 0)
            torch_provider.begin_episode("fixture", 0)
            numpy_logits = numpy_provider._models[0].forward(encoded)
            torch_logits = torch_provider._models[0].forward(encoded)
            np.testing.assert_allclose(torch_logits, numpy_logits, rtol=2e-5, atol=2e-5)

    def test_league_paths_resolve_enabled_policies(self):
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first.bin", root / "second.bin"
            first.write_bytes(b"first")
            second.write_bytes(b"second")
            league = root / "league.ini"
            league.write_text(
                "[league]\nmax_active = 2\n"
                "[policy.first]\npath = 'first.bin'\nenabled = 1\n"
                "[policy.disabled]\npath = 'second.bin'\nenabled = 0\n",
                encoding="utf-8",
            )
            paths = policy_paths_from_league(league)
            self.assertEqual(paths, [first.resolve()])


if __name__ == "__main__":
    unittest.main()
