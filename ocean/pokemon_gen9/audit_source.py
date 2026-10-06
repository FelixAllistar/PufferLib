#!/usr/bin/env python3
"""Index the pinned Showdown source for the Gen 9 Random Battle port plan.

This is a lexical source inventory, not a reachability proof or simulator test.
No dependencies, downloads, builds, or source mutations are performed.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


REVISION = "9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e"
REPOSITORY = "https://github.com/smogon/pokemon-showdown"
CORE_FILES = [
    "config/formats.ts", "data/scripts.ts", "data/random-battles/gen9/teams.ts",
    "data/random-battles/gen9/sets.json", "sim/teams.ts", "sim/prng.ts",
    "sim/battle.ts", "sim/battle-queue.ts", "sim/battle-actions.ts",
    "sim/pokemon.ts", "sim/side.ts", "sim/field.ts", "sim/dex.ts",
    "sim/dex-data.ts", "sim/dex-moves.ts", "sim/dex-species.ts",
    "sim/dex-abilities.ts", "sim/dex-items.ts", "sim/dex-conditions.ts",
    "sim/dex-formats.ts", "sim/team-validator.ts", "sim/state.ts",
    "sim/battle-stream.ts", "sim/SIM-PROTOCOL.md", "sim/global-types.ts",
    "sim/tools/random-player-ai.ts", "sim/tools/runner.ts",
    "sim/tools/exhaustive-runner.ts", "lib/utils.ts",
    "data/moves.ts", "data/abilities.ts", "data/items.ts", "data/conditions.ts",
    "data/rulesets.ts", "data/pokedex.ts", "data/typechart.ts", "data/natures.ts",
    "data/aliases.ts", "data/formats-data.ts",
    "test/random-battles/gen9.js", "test/random-battles/all-gens.js",
    "test/random-battles/tools.js", "test/common.js", "test/assert.js",
    "test/sim/misc/prng.js", "test/sim/misc/terastal.js",
    "test/sim/misc/terastellar.js", "test/sim/misc/terapagos.js",
    "test/sim/rulesets/sleepclausemod.js", "test/sim/abilities/illusion.js",
]
METHOD = re.compile(
    r"^\t(?:(?:public|private|protected|static|override|async|get|set) )*"
    r"([A-Za-z_]\w*)(?:<[^\n]*>)?\("
)
ARROW = re.compile(r"^\t([A-Za-z_]\w*) = \([^\n]*\) =>")
ENTRY = re.compile(r"^\t([a-z0-9]+): \{")
HOOK = re.compile(r"^\t{2,}((?:on\w+|\w+Callback))\s*(?:\(|:)")
DECL = re.compile(r"^(?:export )?(?:abstract )?(?:class|function|const) (\w+)")
SINGLES_METHODS = [
    "constructor", "getTeam", "setSeed", "random", "randomChance", "sample",
    "sampleIfArray", "fastPop", "sampleNoReplace", "multipleSamplesNoReplace",
    "queryMoves", "cullMovePool", "incompatibleMoves", "addMove", "getMoveType",
    "randomMoveset", "shouldCullAbility", "getAbility", "getPriorityItem",
    "getItem", "getLevel", "getForme", "randomSet", "getPokemonPool",
    "getPokemonCompatibility", "randomTeam", "hasDirectCustomBanlistChanges",
    "enforceNoDirectCustomBanlistChanges",
]


def to_id(value):
    return re.sub(r"[^a-z0-9]", "", value.lower())


def source_url(path, line=None):
    return f"{REPOSITORY}/blob/{REVISION}/{path}" + (f"#L{line}" if line else "")


def index_file(root, relative):
    raw = (root / relative).read_bytes()
    lines = raw.decode().splitlines()
    methods, declarations = [], []
    for line, value in enumerate(lines, 1):
        match = METHOD.match(value) or ARROW.match(value)
        if match:
            methods.append({"name": match[1], "line": line})
        match = DECL.match(value)
        if match:
            declarations.append({"name": match[1], "line": line})
    return {
        "sha256": hashlib.sha256(raw).hexdigest(), "line_count": len(lines),
        "url": source_url(relative), "declarations": declarations, "methods": methods,
    }


def index_entries(root, relative, test_kind=None):
    lines = (root / relative).read_text().splitlines()
    starts = [(i, ENTRY.match(value)[1]) for i, value in enumerate(lines) if ENTRY.match(value)]
    result = {}
    for entry_number, (start, name) in enumerate(starts):
        end = starts[entry_number + 1][0] if entry_number + 1 < len(starts) else len(lines)
        callbacks, metadata, conditions = [], [], []
        for offset in range(start + 1, end):
            value = lines[offset]
            match = HOOK.match(value)
            if match:
                hook = {"name": match[1], "line": offset + 1}
                (metadata if match[1].endswith(("Priority", "SubOrder", "Order")) else callbacks).append(hook)
            if re.match(r"^\t{2,}condition:\s*\{", value):
                conditions.append(offset + 1)
        entry = {
            "line": start + 1, "url": source_url(relative, start + 1),
            "callbacks": callbacks, "ordering_metadata": metadata,
            "nested_condition_lines": conditions,
        }
        test = f"test/sim/{test_kind}/{name}.js" if test_kind else None
        if test and (root / test).is_file():
            entry["upstream_test"] = test
        result[name] = entry
    return result


def audit(root):
    actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if actual != REVISION:
        raise SystemExit(f"Expected {REVISION}, found {actual}")
    dirty = subprocess.check_output(["git", "-C", str(root), "status", "--porcelain", "--untracked-files=no"], text=True)
    if dirty:
        raise SystemExit("Reference checkout has modified tracked files")
    sets = json.loads((root / "data/random-battles/gen9/sets.json").read_text())
    templates = [template for species in sets.values() for template in species["sets"]]
    moves = sorted({to_id(move) for template in templates for move in template["movepool"]})
    abilities = sorted({to_id(ability) for template in templates for ability in template["abilities"]})
    files = {path: index_file(root, path) for path in CORE_FILES}
    registries = {
        "moves": index_entries(root, "data/moves.ts", "moves"),
        "abilities": index_entries(root, "data/abilities.ts", "abilities"),
        "items": index_entries(root, "data/items.ts", "items"),
        "conditions": index_entries(root, "data/conditions.ts", "statuses"),
        "rulesets": index_entries(root, "data/rulesets.ts", "rulesets"),
    }
    for key, required in [("moves", moves), ("abilities", abilities)]:
        missing = set(required) - set(registries[key])
        if missing:
            raise SystemExit(f"Missing {key} definitions: {sorted(missing)}")
    generator_path = "data/random-battles/gen9/teams.ts"
    generator_lines = (root / generator_path).read_text().splitlines()
    methods = files[generator_path]["methods"]
    method_names = {method["name"] for method in methods}
    generator = []
    for i, method in enumerate(methods):
        if method["name"] not in SINGLES_METHODS:
            continue
        end = methods[i + 1]["line"] - 1 if i + 1 < len(methods) else len(generator_lines)
        body = "\n".join(generator_lines[method["line"] - 1:end])
        calls = sorted(set(re.findall(r"\bthis\.(\w+)\(", body)) & method_names)
        generator.append({**method, "url": source_url(generator_path, method["line"]), "lexical_method_calls": calls})
    return {
        "schema_version": 1, "repository": REPOSITORY, "revision": REVISION,
        "format": "gen9randombattle",
        "inventory_limits": [
            "Lexical method and handler index; not a complete static call graph.",
            "Direct movepool/ability lists are generator roots, not full mechanic reachability.",
            "Full base registries include entries outside Gen 9 Random Battle.",
            "Items are selected by generator code and species.requiredItems, not by sets.json.",
            "Nested conditions and dynamically acquired abilities/forms require closure analysis during the port.",
            "A matching test filename locates an upstream test; it does not establish coverage or test results.",
        ],
        "generator_inventory": {
            "species_or_form_count": len(sets), "template_count": len(templates),
            "direct_move_count": len(moves), "listed_ability_count": len(abilities),
            "roles": sorted({template["role"] for template in templates}),
            "species_or_form_ids": sorted(sets), "direct_move_ids": moves,
            "listed_ability_ids": abilities,
            "tera_types": sorted({kind for template in templates for kind in template["teraTypes"]}),
            "level_min": min(species["level"] for species in sets.values()),
            "level_max": max(species["level"] for species in sets.values()),
        },
        "generator_methods": generator, "files": files, "registries": registries,
        "upstream_test_files": sorted(str(path.relative_to(root)) for path in (root / "test/sim").rglob("*.js")),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("build/pokemon_gen9/showdown"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path, help="Compare an existing inventory without writing")
    args = parser.parse_args()
    if args.output and args.check:
        parser.error("--output and --check are mutually exclusive")
    result = audit(args.source)
    if args.check:
        if json.loads(args.check.read_text()) != result:
            raise SystemExit(f"Source map differs: {args.check}")
        print(f"Source map matches {REVISION}: {args.check}")
    elif args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(f"Wrote {args.output}")
    else:
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
