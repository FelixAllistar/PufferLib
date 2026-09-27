"""Regression gates for executor-version dispatch and explicit operations."""
import unittest
import numpy as np
import native_macro_runtime as r
from test_native_macro_runtime import observation, tile, commands


class Executor1Fixtures(unittest.TestCase):
    def setUp(self):
        self.runtime = r.NativeMacroRuntime(mode=2, executor_version=1)

    def decode(self, obs, macro, quantity_bin=0, target_bin=0):
        request = np.zeros(47, dtype=np.int32)
        request[:3] = macro, quantity_bin, target_bin
        return self.runtime.decode(obs, request)

    def test_versions_are_explicit_and_legacy_remains_default(self):
        obs = observation()
        legacy = r.NativeMacroRuntime()
        self.assertEqual(legacy.executor_version, 0)
        self.assertTrue(legacy.action_mask(obs)[31])
        self.assertFalse(self.runtime.action_mask(obs)[31])
        self.assertFalse(self.runtime.action_mask(obs)[30])
        with self.assertRaises(ValueError): r.NativeMacroRuntime(mode=3, executor_version=1)
        with self.assertRaises(ValueError): r.NativeMacroRuntime(executor_version=2)

    def test_full_quadrant_remains_targetable_for_operations(self):
        obs = observation()
        for y in range(5):
            for x in range(5): obs['farms'][0]['tiles'][y][x] = tile('PASTURE', animal=None)
        self.assertTrue(self.runtime.action_mask(obs)[89])
        self.assertFalse(r.NativeMacroRuntime().action_mask(obs)[89])
        self.assertTrue(self.runtime.action_mask(obs)[35])
        self.assertEqual(self.decode(obs, 35, target_bin=1)['farmer'], ['DIG'])

    def test_reserved_housing_is_not_reclaimed_and_masked_diversify_places_cow(self):
        obs = observation(inventories=({'COW': 1},))
        obs['farms'][0]['tiles'][4][4] = tile('PASTURE', animal=None)
        self.assertFalse(self.runtime.action_mask(obs)[35])
        action = self.decode(obs, 31)
        self.assertEqual(action['farmer'], ['PLACE', 'COW', 1])
        self.assertEqual(action['market'], [])

    def test_no_strategy_under_hold(self):
        obs = observation(money=100000, day=9, step=216)
        for macro in (0, 30, 31):
            self.assertEqual(self.decode(obs, macro)['market'], [])

    def test_more_than_sixteen_hands_are_operated(self):
        positions = [(i % 5, i // 5) for i in range(18)]
        obs = observation(hands=positions[1:], seeds={'MELON': 18})
        obs['farms'][0]['farmer'] = positions[0]
        action = self.decode(obs, 5, 6)
        self.assertEqual(len(action['hands']), 17)
        self.assertEqual(sum(cmd == ['PLANT', 'MELON'] for cmd in commands(action)), 18)

    def test_fertilization_is_explicit(self):
        obs = observation(inventories=({'FERTILIZER': 1},))
        obs['farms'][0]['tiles'][4][4] = tile('PLANT', crop='MELON', planted_day=0,
            watered_today=True, fertilized_until_day=0, yield_units=0)
        self.assertTrue(self.runtime.action_mask(obs)[36])
        self.assertEqual(self.decode(obs, 36)['farmer'], ['FERTILIZE'])

    def test_unaffordable_animals_do_not_construct_empty_housing(self):
        obs = observation(money=0)
        action = self.decode(obs, 7, 7)
        self.assertEqual(action['farmer'], ['PASS'])
        self.assertEqual(action['market'], [])

    def test_hire_is_not_limited_by_old_workload_or_endgame(self):
        obs = observation(money=100000, hands=[(4,4)] * 16, day=29, hour=23, step=719)
        self.assertTrue(self.runtime.action_mask(obs)[28])
        self.assertEqual(self.decode(obs, 28, 7)['market'], [['HIRE']] * 10)


if __name__ == '__main__': unittest.main()
