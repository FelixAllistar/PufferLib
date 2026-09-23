"""Apply an experiment profile as ordinary native CLI overrides.

uv run --no-project python ocean/kaggriculture/run.py train --profile terminal
uv run --no-project python ocean/kaggriculture/run.py train --profile shaped
The default config remains usable directly with ./puffer train.
"""

import argparse
import configparser
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["train", "eval", "match", "sweep"])
    parser.add_argument("--profile", choices=["terminal", "shaped"], default="terminal")
    parser.add_argument("--binary", type=Path, default=ROOT / "puffer")
    parser.add_argument("--dry-run", action="store_true")
    args, overrides = parser.parse_known_args()
    config = configparser.ConfigParser(interpolation=None)
    with Path(__file__).with_name("profiles").joinpath(f"{args.profile}.ini").open() as stream:
        config.read_file(stream)
    command = [str(args.binary.resolve()), args.mode]
    command += [f"--{section}.{key}={value}"
        for section in config.sections() for key, value in config[section].items()]
    if args.mode in ("eval", "match"):
        command.append("--env.reset_state_prob=0")
    command += overrides
    print(shlex.join(command), flush=True)
    if not args.dry_run:
        return subprocess.run(command, cwd=ROOT).returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
