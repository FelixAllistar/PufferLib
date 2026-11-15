#!/usr/bin/env python3
"""Convert an MMDB file between versions without rebuilding from BVH.

Usage:
    mm_convert.py IN.mmdb OUT.mmdb [--format 2] [--drop-joints REGEX] [--auto-stance RATIO]

A version 1 file (float32) converts to version 2 exactly as mm_build.py writes
version 2 from the same clips: both serialize the same stored arrays. With
--auto-stance the stance is recomputed per frame from the stored hips height
(the hips are stored relative to a root on the ground, so their height is
kept). Converting version 2 back to version 1 writes the dequantized values.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mm_build as B  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--format", type=int, choices=(1, 2), default=2)
    ap.add_argument("--drop-joints", metavar="REGEX")
    ap.add_argument("--auto-stance", type=float, metavar="RATIO")
    args = ap.parse_args(argv)
    st = B.read_mmdb(args.input)
    if args.auto_stance:
        B.apply_auto_stance(st, args.auto_stance)
    if args.format == 1:
        if args.drop_joints:
            ap.error("--drop-joints needs version 2")
        size = B.write_v1(st, args.output)
        print(f"{args.output}: version 1, {size / 1e6:.2f} MB")
    else:
        report = {}
        B.write_v2(st, args.output, args.drop_joints, report)
        B.print_report(args.output, report)
    print(f"{args.input}: version {st['version']}, {os.path.getsize(args.input) / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
