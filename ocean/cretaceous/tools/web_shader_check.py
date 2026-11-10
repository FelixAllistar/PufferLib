#!/usr/bin/env python3
"""Checks that every shader in ocean/cretaceous/shaders compiles as GLSL ES 3.00
(WebGL2), the way the web build loads it: `#include "file"` lines expanded
relative to the including file, and the `#version` line replaced by
`#version 300 es` plus shaders/web_es.glsl (render/gfx.c does the same under
__EMSCRIPTEN__). Each .vs is checked as a vertex shader and each .fs as a
fragment shader with glslangValidator (Debian/Ubuntu: glslang-tools).
Run from anywhere:  python3 -I ocean/cretaceous/tools/web_shader_check.py [NAME ...]
Exit status 1 if any shader fails.
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHADERS = os.path.join(HERE, "..", "shaders")
INC = re.compile(r'#include "([^"]+)"')


def expand(path):
    out = []
    d = os.path.dirname(path)
    for line in open(path).read().splitlines(True):
        m = INC.match(line)
        if m:
            out.append(expand(os.path.join(d, m.group(1))) + "\n")
        elif line.startswith("#version"):
            out.append("#version 300 es\n" + open(os.path.join(d, "web_es.glsl")).read())
        else:
            out.append(line)
    return "".join(out)


def main(names):
    files = sorted(f for f in os.listdir(SHADERS) if f.endswith((".vs", ".fs")))
    if names:
        files = [f for f in files if f in names or os.path.splitext(f)[0] in names]
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for f in files:
            stage = "vert" if f.endswith(".vs") else "frag"
            src = os.path.join(tmp, f + "." + stage)
            open(src, "w").write(expand(os.path.join(SHADERS, f)))
            r = subprocess.run(["glslangValidator", src], capture_output=True, text=True)
            ok = r.returncode == 0
            bad += not ok
            print(("ok    " if ok else "FAIL  ") + f)
            if not ok:
                for line in r.stdout.splitlines():
                    if "ERROR" in line:
                        print("      " + line.replace(src, f))
    print("%d of %d shaders failed" % (bad, len(files)) if bad else "all %d shaders compile as GLSL ES 3.00" % len(files))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
