import csv
import pathlib
import struct
import tempfile
import unittest

from ocean.kaggriculture import split_macro_dataset


class SplitMacroDatasetTests(unittest.TestCase):
    def test_latest_day_split_preserves_kagb_sections(self):
        header = struct.Struct("<8I")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "full.bc"
            games, steps = 3, 2
            with source.open("wb") as stream:
                stream.write(header.pack(
                    split_macro_dataset.MAGIC, 2, games * steps, 1280, 47,
                    133, games, steps,
                ))
                stream.write(bytes([10]) * games * steps * 1280)
                stream.write(struct.pack(
                    "<" + "f" * (games * steps * 47),
                    *([20.0] * (games * steps * 47)),
                ))
                stream.write(bytes([30]) * games * steps * 133)
            manifest = root / "full.bc.players.tsv"
            with manifest.open("w", newline="") as stream:
                writer = csv.DictWriter(
                    stream, fieldnames=("episode_id", "source"), delimiter="\t"
                )
                writer.writeheader()
                writer.writerows([
                    {"episode_id": "e1", "source": "2026-08-01.zip"},
                    {"episode_id": "e2", "source": "2026-08-02.zip"},
                    {"episode_id": "e3", "source": "2026-08-02.zip"},
                ])
            train = root / "train.bc"
            holdout = root / "holdout.bc"
            split_macro_dataset.split(
                source, manifest, train, train.with_suffix(".bc.players.tsv"),
                holdout, holdout.with_suffix(".bc.players.tsv"),
            )
            values = header.unpack(train.read_bytes()[:header.size])
            self.assertEqual(values[6:], (1, 2))
            body = train.read_bytes()[header.size:]
            self.assertEqual(body[:1280 * steps], bytes([10]) * 1280 * steps)
            expert_offset = 1280 * steps
            self.assertEqual(
                body[expert_offset:expert_offset + 47 * 4 * steps],
                struct.pack("<" + "f" * (47 * steps), *([20.0] * (47 * steps))),
            )
            self.assertEqual(
                body[expert_offset + 47 * 4 * steps:], bytes([30]) * 133 * steps,
            )
            holdout_values = header.unpack(holdout.read_bytes()[:header.size])
            self.assertEqual(holdout_values[6:], (2, 2))


if __name__ == "__main__":
    unittest.main()
