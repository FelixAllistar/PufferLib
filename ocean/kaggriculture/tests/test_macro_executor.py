import ctypes
import sys
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
LIB = KAG_DIR / "build" / "libkaggriculture.so"
sys.path.insert(0, str(KAG_DIR))

from macro_executor import animal_macro, plant_macro
from replay_native import CAction, CConfig, c_action, c_snapshot, load_core


class MacroExecutorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = load_core(LIB)
        cls.config = CConfig()
        cls.lib.kg_config_default(ctypes.byref(cls.config))
        cls.config.episode_steps = 80

    def _state(self):
        return self.lib.kg_create(ctypes.byref(self.config))

    def _step(self, state, left, right=None):
        actions = (CAction * 2)()
        actions[0] = c_action(left)
        actions[1] = c_action(right or {"farmer": ["PASS"], "hands": [], "market": []})
        self.lib.kg_step(state, actions)

    def test_plant_macro_is_a_real_route_and_plant_sequence(self):
        state = self._state()
        try:
            snapshot = c_snapshot(self.lib, state)
            candidate = plant_macro(snapshot, 0, "WHEAT", 1, episode_steps=80)
            self.assertIsNotNone(candidate)
            assert candidate is not None
            sequence = candidate.action_sequence()
            self.assertTrue(any(action["farmer"][0] == "PLANT" for action in sequence))
            self.assertEqual(sequence[0]["market"][0][:2], ["BUY_SEED", "WHEAT"])
            for action in sequence:
                self._step(state, action)
                if self.lib.kg_done(state):
                    break
            final = c_snapshot(self.lib, state)
            tiles = [tile for row in final["farms"][0]["tiles"] for tile in row]
            self.assertTrue(any(
                isinstance(tile, dict) and tile.get("kind") == "PLANT"
                and tile.get("crop") == "WHEAT" for tile in tiles
            ))
        finally:
            self.lib.kg_destroy(state)

    def test_animal_macro_builds_structure_and_places_animal(self):
        state = self._state()
        try:
            snapshot = c_snapshot(self.lib, state)
            candidate = animal_macro(snapshot, 0, "COW", 1, episode_steps=80)
            self.assertIsNotNone(candidate)
            assert candidate is not None
            sequence = candidate.action_sequence()
            self.assertEqual(sequence[0]["market"][0][:2], ["BUY_ANIMAL", "COW"])
            self.assertTrue(any(action["farmer"][0] == "BUILD_PASTURE" for action in sequence))
            self.assertTrue(any(action["farmer"][0] == "PLACE" for action in sequence))
            for action in sequence:
                self._step(state, action)
                if self.lib.kg_done(state):
                    break
            final = c_snapshot(self.lib, state)
            tiles = [tile for row in final["farms"][0]["tiles"] for tile in row]
            self.assertTrue(any(
                isinstance(tile, dict) and tile.get("kind") == "PASTURE"
                and tile.get("animal") == "COW" for tile in tiles
            ))
        finally:
            self.lib.kg_destroy(state)


if __name__ == "__main__":
    unittest.main()
