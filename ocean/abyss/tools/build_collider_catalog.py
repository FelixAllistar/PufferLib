#!/usr/bin/env python3
"""Extract measured overlapping giant-rock sphere templates from a registry dump."""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

BALL = re.compile(r"radius=([0-9.]+).*?xyz=([-0-9.]+),([-0-9.]+),([-0-9.]+)")


def fmt(value: float) -> str:
    return f"{value:.8g}f"


def build(source: Path, output: Path) -> None:
    spheres = []
    for line in source.read_text(encoding="utf-8").splitlines():
        match = BALL.search(line)
        if not match:
            continue
        radius, x, y, z = map(float, match.groups())
        if radius >= 3000 and not math.isclose(radius, 5000, abs_tol=1) and not math.isclose(radius, 7500, abs_tol=1):
            spheres.append((x, y, z, radius))

    # Connected components under overlapping-sphere distance.
    remaining = set(range(len(spheres)))
    components = []
    while remaining:
        todo = [remaining.pop()]
        component = []
        while todo:
            index = todo.pop(); component.append(index)
            x, y, z, r = spheres[index]
            linked = []
            for other in remaining:
                ox, oy, oz, radius = spheres[other]
                if math.dist((x, y, z), (ox, oy, oz)) <= r + radius:
                    linked.append(other)
            for other in linked:
                remaining.remove(other); todo.append(other)
        components.append(component)
    components = sorted(components, key=len, reverse=True)[:2]
    if len(components) != 2 or any(len(component) != 30 for component in components):
        raise ValueError(f"expected two measured 30-sphere components, got {[len(c) for c in components]}")

    lines = [
        "// Generated from the 2026-07-13 giant-rock native-ball capture.",
        "typedef struct { float center[3], radius; } GeneratedColliderSphere;",
        "typedef struct { unsigned short offset, count; } GeneratedColliderTemplate;",
        "#define GENERATED_COLLIDER_TEMPLATE_COUNT 2",
        "#define GENERATED_COLLIDER_SPHERE_COUNT 60",
        "static const GeneratedColliderTemplate GENERATED_COLLIDER_TEMPLATES[2] = {{0,30},{30,30}};",
        "static const GeneratedColliderSphere GENERATED_COLLIDER_SPHERES[60] = {",
    ]
    for component in components:
        # Radius-weighted center preserves the captured union while making placement reusable.
        weight = sum(spheres[index][3] for index in component)
        center = tuple(sum(spheres[index][axis] * spheres[index][3] for index in component) / weight for axis in range(3))
        for index in component:
            x, y, z, radius = spheres[index]
            lines.append(f"    {{{{{fmt(x-center[0])},{fmt(y-center[1])},{fmt(z-center[2])}}},{fmt(radius)}}},")
    lines.extend(["};", ""])
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {output}: {[len(c) for c in components]} spheres")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path("ocean/abyss/generated_colliders.h"))
    args = parser.parse_args()
    build(args.source, args.output)


if __name__ == "__main__":
    main()
