#!/usr/bin/env python3
"""Build only the optional heads-up clone with explicitly supplied model inputs.

This entry reads local Git blobs; it never checks out, fetches or changes Git.
It does not load a native library, initialize a game, sample a policy or issue
HTTP. A successful build records identities and does not qualify a model.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from package_io import (BASE_COMMIT, EVALUATION_COMMIT, PACKAGE, descriptor, digest_string, fresh_output,
                        model_binding, package_inventory, read_object, require,
                        save_new, sha256, verify_descriptor)


BASE_NAMES = {"native.c", "core.h", "rules.h", "equity.h", "fullhouse.h", "poker.h"}
BRIDGE_NAMES = {"native_clone.c", "clone_input_abi.h", "prospective_clone_api.h", "clone_bridge.h",
                "bb_sha256.h", "bridge_public_impl.h", "bridge_native_impl.h",
                "puffer_clone_hooks.h", "clone_public_replay.py", "clone_transport.py"}
SAMPLER_NAMES = {"clone.c", "clone.h", "bc_sha256.h"}
ORIGINAL_FLAGS = ["-O3", "-march=native", "-ffp-contract=off", "-std=c11", "-Wall", "-Wextra"]
SAMPLER_FLAGS = ["-O3", "-march=native", "-fno-fast-math", "-ffp-contract=off",
                 "-std=c11", "-Wall", "-Wextra"]
DISCARDED_ENVIRONMENT = {"CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH",
                         "GCC_EXEC_PREFIX", "COMPILER_PATH", "CFLAGS", "CPPFLAGS",
                         "LDFLAGS", "MAKEFLAGS", "PYTHONPATH", "PYTHONHOME"}
FIXED_ENVIRONMENT = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C",
                     "PYTHONDONTWRITEBYTECODE": "1", "OPENBLAS_NUM_THREADS": "1",
                     "OMP_NUM_THREADS": "1", "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1"}


def now():
    return datetime.now(timezone.utc).isoformat()


def environment():
    result = os.environ.copy()
    discarded = DISCARDED_ENVIRONMENT | {name for name in result if name.startswith("LD_")}
    for name in discarded:
        result.pop(name, None)
    result.update(FIXED_ENVIRONMENT)
    return result


def write_bytes_new(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(raw)


def copy_bound(source, destination, expected):
    row = descriptor(source, expected)
    raw = Path(row["path"]).read_bytes()
    require(hashlib.sha256(raw).hexdigest() == row["sha256"], "source changed during copy")
    write_bytes_new(destination, raw)
    return descriptor(destination, expected)


def execute(command, name, output, commands, cwd):
    stdout = output / "logs" / (name + ".stdout.txt")
    stderr = output / "logs" / (name + ".stderr.txt")
    stdout.parent.mkdir(parents=True, exist_ok=True)
    with stdout.open("xb") as out, stderr.open("xb") as err:
        result = subprocess.run(command, cwd=cwd, env=environment(), stdout=out, stderr=err,
                                timeout=180, check=False)
    row = {"name": name, "argv": command, "cwd": str(cwd), "exit_code": result.returncode,
           "stdout": descriptor(stdout), "stderr": descriptor(stderr)}
    commands.append(row)
    require(result.returncode == 0, name + " failed; output has been preserved")
    return stdout.read_bytes()


def materialize_base(args, output, commands):
    spec = read_object(PACKAGE / "base_sources.json")
    require(set(spec) == {"schema", "commit", "files", "evaluation_support", "generated_puffer_sha256"}
            and spec["schema"] == "poker-clone-base-sources-v1" and spec["commit"] == BASE_COMMIT,
            "the fallback source must be the fixed a4b commit")
    require(type(spec["files"]) is list and len(spec["files"]) == len(BASE_NAMES),
            "fallback source inventory is incomplete")
    require({entry["path"] for entry in spec["files"]} == BASE_NAMES,
            "fallback source filenames differ")
    rows = []
    for entry in spec["files"]:
        require(type(entry) is dict and set(entry) == {"path", "git_path", "bytes", "sha256"}
                and entry["git_path"] == "ocean/poker/" + entry["path"], "invalid base descriptor")
        digest_string(entry["sha256"])
        if args.base_source is not None:
            raw = (args.base_source.resolve() / entry["path"]).read_bytes()
        else:
            raw = execute(["git", "-C", str(args.repository.resolve()), "cat-file", "blob",
                           BASE_COMMIT + ":" + entry["git_path"]],
                          "base_" + entry["path"].replace(".", "_"), output, commands, PACKAGE)
        require(type(entry["bytes"]) is int and len(raw) == entry["bytes"]
                and hashlib.sha256(raw).hexdigest() == entry["sha256"],
                "pinned fallback bytes differ: " + entry["path"])
        target = output / "reference_a4b" / entry["path"]
        write_bytes_new(target, raw)
        rows.append(descriptor(target, entry["sha256"]))
    # The reviewed transport evaluator needs verify_terminal(), which is not
    # present in the older a4b eval.py. Pin this Python-only support separately
    # while retaining the exact a4b C fallback above.
    entry = spec["evaluation_support"]
    require(type(entry) is dict and set(entry) == {"commit", "git_path", "bytes", "sha256"}
            and entry["commit"] == EVALUATION_COMMIT and entry["git_path"] == "ocean/poker/eval.py",
            "evaluation support is not the separately pinned reviewed source")
    digest_string(entry["sha256"])
    if args.base_source is not None:
        raw = (args.base_source.resolve() / "evaluation_support/eval.py").read_bytes()
    else:
        raw = execute(["git", "-C", str(args.repository.resolve()), "cat-file", "blob",
                       EVALUATION_COMMIT + ":" + entry["git_path"]],
                      "evaluation_support", output, commands, PACKAGE)
    require(type(entry["bytes"]) is int and len(raw) == entry["bytes"]
            and hashlib.sha256(raw).hexdigest() == entry["sha256"], "evaluation support bytes differ")
    target = output / "evaluation_support/eval.py"
    write_bytes_new(target, raw)
    rows.append(descriptor(target, entry["sha256"]))
    return spec, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--expected-header-sha256", required=True)
    parser.add_argument("--model-binding", type=Path, required=True)
    parser.add_argument("--expected-model-binding-sha256", required=True)
    parser.add_argument("--module-binary", type=Path, required=True)
    parser.add_argument("--module-source-closure", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repository", type=Path, default=PACKAGE.parents[2])
    parser.add_argument("--base-source", type=Path,
                        help="Local export: six a4b files plus separately pinned evaluation_support/eval.py")
    parser.add_argument("--cc", default="gcc", help="One GCC 13.3.0 executable; no flag string")
    args = parser.parse_args()

    sources = package_inventory()
    source_map = {row["path"]: row["sha256"] for row in sources}
    required = {PACKAGE / "bridge" / name for name in BRIDGE_NAMES}
    required |= {PACKAGE / "sampler" / name for name in SAMPLER_NAMES}
    required |= {PACKAGE / name for name in ("build.py", "package_io.py", "prepare_puffer_source.py", "base_sources.json")}
    require(all(str(path) in source_map for path in required), "required build source is absent from inventory")
    binding = model_binding(read_object(args.model_binding, args.expected_model_binding_sha256))
    inputs = {
        "model_binding": descriptor(args.model_binding, args.expected_model_binding_sha256),
        "header": descriptor(args.header, args.expected_header_sha256),
        "standalone_module": descriptor(args.module_binary, binding["module_binary_sha256"]),
        "standalone_source_closure": descriptor(args.module_source_closure, binding["module_source_closure_sha256"]),
    }
    require(1 <= inputs["header"]["bytes"] <= (16 << 20), "generated header byte count is out of range")
    for row in inputs.values():
        require(not Path(row["path"]).is_relative_to(PACKAGE), "external model inputs must stay outside package source")
    compiler = shutil.which(args.cc, path=FIXED_ENVIRONMENT["PATH"])
    require(compiler is not None, "the explicitly selected compiler is unavailable")
    output = fresh_output(args.output)
    commands, staged = [], []
    save_new(output / "started.json", {"schema": "poker-clone-build-start-v1", "created_utc": now(),
        "inputs": inputs, "base_commit": BASE_COMMIT, "policy_calls": 0, "HTTP_calls": 0})
    try:
        version = execute([compiler, "--version"], "compiler", output, commands, PACKAGE).decode("utf-8")
        require(re.search(r"\b13\.3\.0$", version.splitlines()[0]) is not None
                and "Free Software Foundation" in version,
                "this build profile requires GCC 13.3.0; another compiler needs its own parity qualification")
        spec, base_rows = materialize_base(args, output, commands)
        staged.extend(base_rows)
        for folder, names, destination in (("bridge", BRIDGE_NAMES, "source"),
                                           ("sampler", SAMPLER_NAMES, "sampler")):
            for name in sorted(names):
                source = PACKAGE / folder / name
                staged.append(copy_bound(source, output / destination / name, source_map[str(source)]))
        # The source-only generator contains the exact reviewed substitutions.
        # Its result is checked against the pinned generated-header identity.
        from prepare_puffer_source import render
        generated = render((output / "reference_a4b/poker.h").read_bytes())
        require(hashlib.sha256(generated).hexdigest() == spec["generated_puffer_sha256"],
                "generated Puffer source identity differs")
        write_bytes_new(output / "source/poker_clone.h", generated)
        staged.append(descriptor(output / "source/poker_clone.h", spec["generated_puffer_sha256"]))
        header = copy_bound(inputs["header"]["path"], output / "private/bc_model_generated.h", inputs["header"]["sha256"])
        staged.append(header)
        (output / "objects").mkdir()
        bridge_object = output / "objects/bridge.o"
        clone_object = output / "objects/clone.o"
        library = output / "libpoker2_clone.so"
        execute([compiler, *ORIGINAL_FLAGS, "-DPOKER_PLAYERS=2", "-DPOKER_CLONE_BRIDGE=1", "-fPIC", "-c",
                 str(output / "source/native_clone.c"), "-o", str(bridge_object)],
                "bridge_object", output, commands, output)
        execute([compiler, *SAMPLER_FLAGS, "-fPIC", "-I", str(output / "private"), "-c",
                 str(output / "sampler/clone.c"), "-o", str(clone_object)],
                "sampler_object", output, commands, output)
        execute([compiler, "-shared", str(bridge_object), str(clone_object), "-lm", "-o", str(library)],
                "link", output, commands, output)
        for row in [*sources, *inputs.values(), *staged]:
            verify_descriptor(row)
        save_new(output / "commands.json", commands)
        save_new(output / "model_binding.json", binding)
        manifest = {"schema": "poker-clone-portable-build-v1", "status": "built", "created_utc": now(),
            "base_commit": BASE_COMMIT, "evaluation_commit": EVALUATION_COMMIT,
            "compiler": {"path": compiler, "version": version,
                "environment_policy": {"removed_names": sorted(DISCARDED_ENVIRONMENT),
                    "removed_prefixes": ["LD_"], "fixed": FIXED_ENVIRONMENT}},
            "original_flags": ORIGINAL_FLAGS, "sampler_flags": SAMPLER_FLAGS,
            "players": 2, "batch": 64, "ndebug": False, "river_cfr": False,
            "inputs": inputs, "package_sources": sources, "staged_sources": staged,
            "objects": {"bridge": descriptor(bridge_object), "sampler": descriptor(clone_object)},
            "candidate": descriptor(library), "model_binding": binding,
            "model_binding_file": descriptor(output / "model_binding.json"),
            "commands": descriptor(output / "commands.json"),
            "poker_source": str(output / "evaluation_support"), "context_source": str(output / "source"),
            "native_library_loads": 0, "game_initializations": 0, "policy_calls": 0, "HTTP_calls": 0,
            "play_qualified": False,
            "scope": "Compilation and identity recording only; supplied model qualification is not created or inferred."}
        save_new(output / "build_manifest.json", manifest)
        print(json.dumps({"status": "built", "manifest": descriptor(output / "build_manifest.json"),
                          "candidate": manifest["candidate"], "play_qualified": False}, sort_keys=True))
    except BaseException as error:
        if not (output / "commands.json").exists():
            save_new(output / "commands.json", commands)
        save_new(output / "failure.json", {"schema": "poker-clone-build-failure-v1", "created_utc": now(),
            "status": "failed", "error_type": type(error).__name__, "error": str(error),
            "retry_performed": False, "preserved_output": str(output), "play_qualified": False})
        raise


if __name__ == "__main__":
    main()
