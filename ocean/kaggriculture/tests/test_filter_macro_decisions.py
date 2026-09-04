import pathlib
import struct
import tempfile
import unittest

import numpy as np

from ocean.kaggriculture import filter_macro_decisions as target


class FilterMacroDecisionTests(unittest.TestCase):
    def test_preserves_layout_and_filters_to_opening_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "source.bc"
            games, steps = 2, 6
            count = games * steps
            observations = np.arange(
                count * target.OBS_BYTES, dtype=np.uint8
            ).reshape(count, target.OBS_BYTES)
            experts = np.zeros((games, steps, target.EXPERT_HEADS), dtype="<f4")
            experts[:, :, :] = 0
            # HOLD, strategic decision, unchanged consecutive decision,
            # MAINTAIN, another strategic decision, padding.
            experts[0, :, 0] = (0, 9, 9, 30, 9, -1)
            experts[0, :, 1] = (0, 2, 2, 0, 2, -1)
            experts[0, :, 2] = (0, 1, 1, 0, 1, -1)
            experts[1, :, 0] = (7, 0, 7, 29, 34, -1)
            masks = np.full((count, target.MASK_BYTES), 0xA5, dtype=np.uint8)
            with source.open("wb") as stream:
                stream.write(target.HEADER.pack(
                    target.MAGIC, target.VERSION, count, target.OBS_BYTES,
                    target.EXPERT_HEADS, target.MASK_BYTES, games, steps,
                ))
                stream.write(observations.tobytes())
                stream.write(experts.tobytes())
                stream.write(masks.tobytes())
            output = root / "filtered.bc"
            report = target.filter_dataset(
                source, output, opening_steps=5,
                source_manifest=root / "missing.tsv",
            )
            expert_offset = target.HEADER.size + count * target.OBS_BYTES
            result = np.memmap(
                output, dtype="<f4", mode="r", offset=expert_offset,
                shape=(games, steps, target.EXPERT_HEADS),
            )
            self.assertEqual(result[0, :, 0].tolist(), [-1, 9, -1, -1, 9, -1])
            self.assertEqual(result[1, :, 0].tolist(), [7, -1, 7, -1, 34, -1])
            self.assertEqual(report["kept_decisions"], 5)
            raw = output.read_bytes()
            self.assertEqual(
                raw[target.HEADER.size:expert_offset], observations.tobytes()
            )
            mask_offset = expert_offset + count * target.EXPERT_HEADS * 4
            self.assertEqual(raw[mask_offset:], masks.tobytes())


if __name__ == "__main__":
    unittest.main()
