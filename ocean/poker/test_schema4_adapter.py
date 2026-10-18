"""Synthetic terminal/decision schema cases; no match data."""
import json
import unittest

from export_schema4 import schema4_loads


class SchemaAdapterTests(unittest.TestCase):
    def test_only_terminal_metadata_alias_changes(self):
        source = {"hand": 0, "position": 1, "winnings_chips": "opaque-unused",
                  "decisions": "opaque-unused", "slumbot_hole_cards": "opaque-unused"}
        text = json.dumps(source)
        result = schema4_loads(text)
        self.assertEqual(result, {**source, "client_pos": 1})
        self.assertEqual(json.loads(text), source)
        decision = {"client_pos": 0, "action": "opaque-unused", "board": "opaque-unused"}
        self.assertEqual(schema4_loads(json.dumps(decision)), decision)
        self.assertEqual(schema4_loads(json.dumps({"hand_reserved": {"game": 0}})), {"hand_reserved": {"game": 0}})

    def test_invalid_or_conflicting_terminal_metadata_fails(self):
        for position in (-1, 2, "0", True):
            with self.assertRaises(ValueError):
                schema4_loads(json.dumps({"winnings_chips": None, "position": position}))
        with self.assertRaises(ValueError):
            schema4_loads(json.dumps({"winnings_chips": None, "position": 0, "client_pos": 1}))
        with self.assertRaises(KeyError):
            schema4_loads(json.dumps({"winnings_chips": None}))


if __name__ == "__main__":
    unittest.main()
