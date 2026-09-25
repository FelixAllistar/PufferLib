import gzip
import ctypes
import hashlib
import json
import pathlib
import tempfile
import subprocess
import unittest
from unittest import mock
import zipfile

from ocean.kaggriculture import prepare_bc_replays as prep


def episode(eid=12, names=("teacher", "opponent"), version="1.32.7"):
    value = {
        "configuration": {"episodeSteps": 3, "seed": 123},
        "name": "kaggriculture", "module_version": version,
        "info": {"EpisodeId": eid, "seed": 123, "TeamNames": list(names),
                 "Agents": [{"Name": name} for name in names]},
        "rewards": [90000.0, 80000.0], "statuses": ["DONE", "DONE"],
    }
    value["steps"] = [[{
        "observation": {"step": turn},
        "action": {"farmer": ["PASS" if turn != 1 else "NORTH"], "hands": [], "market": []},
    } for _ in range(2)] for turn in range(3)]
    return value


def archive(path, episodes):
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as stream:
        for index, value in enumerate(episodes):
            stream.writestr(f"{index}.json", json.dumps(value))


class ReplayPrepTests(unittest.TestCase):
    def test_current_native_core_cache_roundtrip(self):
        # Generated native frames test the ABI/cache path, not official rule parity.
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            library = root / "core.so"
            core = pathlib.Path(prep.__file__).with_name("core.h")
            subprocess.run(["cc", "-x", "c", "-O2", "-shared", "-fPIC",
                str(core), "-lm", "-o", str(library)], check=True, capture_output=True)
            lib = prep.native.load_core(library)
            value = episode()
            cfg = prep.native.replay_config(value)
            state = lib.kg_create(ctypes.byref(cfg))
            self.assertTrue(state)
            try:
                for turn, frame in enumerate(value["steps"]):
                    if turn:
                        actions = (prep.native.CAction * 2)(
                            *(prep.native.c_action(record["action"]) for record in frame))
                        lib.kg_step(state, actions)
                    snapshot = prep.native.c_snapshot(lib, state)
                    for player, record in enumerate(frame):
                        record["observation"] = {
                            key: snapshot[key] for key in
                            ("step", "day", "hour", "farms", "market", "town")}
                        record["observation"]["private"] = snapshot["privates"][player]
                        record["status"] = "DONE" if snapshot["done"] else "ACTIVE"
                value["rewards"] = [lib.kg_player_money(state, i) for i in range(2)]
            finally:
                lib.kg_destroy(state)
            source = root / "generated.zip"
            archive(source, [value])
            row = prep.catalog([source])[0][0]
            digest = hashlib.sha256(library.read_bytes()).hexdigest()
            path, reused, frames = prep.cache_one(lib, digest, row, root / "cache")
            self.assertFalse(reused)
            self.assertEqual(frames, 3)
            self.assertTrue(prep.cache_one(lib, digest, row, root / "cache")[1])
            with gzip.open(path, "rt") as stream:
                tape = json.load(stream)
            tape["rewards"][0] += 1
            with self.assertRaisesRegex(ValueError, "terminal mismatch"):
                prep.verify_tape(lib, tape)

    def test_inventory_deduplicates_games_and_keeps_seats_together(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            a, b = root / "2026-09-17.zip", root / "2026-09-18.zip"
            archive(a, [episode(), episode(13, version="1.32.6")])
            archive(b, [episode()])
            rows, counts = prep.catalog([b, a, a])
            self.assertEqual(len(rows), 2)
            self.assertEqual(counts["duplicate_player_streams"], 2)
            self.assertEqual(counts["other_module_version"], 1)
            self.assertEqual(rows[0]["split"], rows[1]["split"])
            self.assertEqual(rows[0]["archive"], str(a))

    def test_conflicting_duplicate_is_not_silently_merged(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            a, b = root / "a.zip", root / "b.zip"
            archive(a, [episode()])
            archive(b, [episode(names=("changed", "opponent"))])
            with self.assertRaisesRegex(ValueError, "conflicting metadata"):
                prep.catalog([a, b])

    def test_self_play_is_one_game_two_streams_and_exact_names_stay_separate(self):
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "2026-09-19.zip"
            archive(path, [episode(names=("teacher", "teacher")),
                           episode(13, names=("teacher-v2", "opponent"))])
            rows, _ = prep.catalog([path])
            summary = prep.summarize(rows)
            teacher = next(r for r in summary if r["display_name"] == "teacher")
            self.assertEqual(teacher["player_streams"], 2)
            self.assertEqual(teacher["unique_episodes"], 1)
            self.assertEqual(len(summary), 3)

    def test_bad_outcomes_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "bad.zip"
            value = episode()
            value["rewards"] = [None, 123]
            archive(path, [value])
            rows, counts = prep.catalog([path])
            self.assertEqual(rows, [])
            self.assertEqual(counts["bad_rewards"], 1)

    def test_compact_tape_uses_next_frame_actions(self):
        value = episode()
        for frame in value["steps"]:
            del frame[1]["observation"]["step"]
        tape = prep.compact_tape(value)
        self.assertEqual(len(tape["actions"]), 2)
        self.assertEqual(tape["actions"][0][0]["farmer"], ["NORTH"])
        self.assertEqual(tape["actions"][1][0]["farmer"], ["PASS"])
        self.assertNotIn("steps", tape)
        self.assertFalse(tape["bc_ready"])

    def test_compact_tape_rejects_incomplete_or_missing_actions(self):
        value = episode()
        value["steps"].pop()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            prep.compact_tape(value)
        value = episode()
        value["steps"][1][0]["action"] = None
        with self.assertRaisesRegex(ValueError, "missing primitive"):
            prep.compact_tape(value)

    def test_cache_publishes_only_after_parity_and_reuses_without_full_parse(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "source.zip"
            archive(source, [episode()])
            row = prep.catalog([source])[0][0]
            with mock.patch.object(prep, "verify_tape", side_effect=ValueError("parity failure")):
                with self.assertRaisesRegex(ValueError, "parity failure"):
                    prep.cache_one(None, "a" * 64, row, root / "cache")
            self.assertFalse((root / "cache").exists())
            with mock.patch.object(prep, "verify_tape") as verify:
                path, reused, frames = prep.cache_one(None, "a" * 64, row, root / "cache")
                self.assertEqual((reused, frames), (False, 3))
                self.assertIsNotNone(verify.call_args.args[2])
                with gzip.open(path, "rt") as stream:
                    self.assertEqual(json.load(stream)["parity_frames"], 3)
                with mock.patch.object(zipfile.ZipFile, "read", side_effect=AssertionError("reparsed")):
                    self.assertTrue(prep.cache_one(None, "a" * 64, row, root / "cache")[1])
                self.assertEqual(len(verify.call_args.args), 2)
                # A different core gets a separate cache, not a silent reuse.
                other = prep.cache_one(None, "b" * 64, row, root / "cache")[0]
                self.assertNotEqual(path, other)


if __name__ == "__main__":
    unittest.main()
