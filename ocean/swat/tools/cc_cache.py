#!/usr/bin/env python3
# Drop-in compiler for the SWAT Makefile's one-line builds
# ("clang FLAGS a.c b.c ... LIBS -o OUT"). Every target used to recompile the
# same ~35 sources serially; this compiles each source once per distinct
# (flags, preprocessed text) into a shared object cache, in parallel, then
# links. The key covers every header a source includes, so a header edit
# rebuilds exactly the sources that see it. Linking goes to a temporary name
# and is renamed into place, so parallel targets that build the same binary
# (several tests share test_encounter) never see a half-written file.
# Anything that is not a plain compile-and-link (-c, -E, no -o, no .c) is
# passed straight to the real compiler. SWAT_REAL_CC overrides clang;
# SWAT_CC_CACHE overrides the cache directory (default OUT_DIR/cc_cache).
import concurrent.futures, hashlib, os, subprocess, sys

real = os.environ.get("SWAT_REAL_CC", "clang")
args = sys.argv[1:]
if "-o" not in args or any(a in ("-c", "-E", "-S", "-M", "-MM") for a in args):
    os.execvp(real, [real] + args)
out = args[args.index("-o") + 1]
sources = [a for i, a in enumerate(args) if a.endswith(".c") and not a.startswith("-") and (i == 0 or args[i - 1] != "-o")]
if not sources:
    os.execvp(real, [real] + args)

# Compile flags: every option except linker inputs and the output.
flags = []
skip = False
for i, a in enumerate(args):
    if skip: skip = False; continue
    if a == "-o": skip = True; continue
    if a.startswith("-") and not a.startswith(("-l", "-L", "-Wl,")): flags.append(a)
cache = os.environ.get("SWAT_CC_CACHE") or os.path.join(os.path.dirname(out) or ".", "cc_cache")
os.makedirs(cache, exist_ok=True)

def build(src):
    pre = subprocess.run([real] + flags + ["-E", src], capture_output=True)
    if pre.returncode:
        sys.stderr.buffer.write(pre.stderr); return None
    key = hashlib.sha256(("\0".join(flags) + "\0" + src + "\0").encode() + pre.stdout).hexdigest()[:24]
    obj = os.path.join(cache, os.path.basename(src)[:-2] + "-" + key + ".o")
    if os.path.exists(obj): return obj
    tmp = f"{obj}.{os.getpid()}.tmp"
    r = subprocess.run([real] + flags + ["-c", src, "-o", tmp], capture_output=True)
    sys.stderr.buffer.write(r.stderr)
    if r.returncode:
        try: os.remove(tmp)
        except OSError: pass
        return None
    os.replace(tmp, obj); return obj

jobs = int(os.environ.get("SWAT_CC_JOBS", os.cpu_count() or 4))
with concurrent.futures.ThreadPoolExecutor(min(jobs, 8)) as pool:
    objects = dict(zip(sources, pool.map(build, sources)))
if any(o is None for o in objects.values()): sys.exit(1)

# Link in the original argument order, sources replaced by their objects.
link, skip = [], False
for i, a in enumerate(args):
    if skip: skip = False; continue
    if a == "-o": skip = True; continue
    link.append(objects.get(a, a) if a in objects else a)
tmp = f"{out}.{os.getpid()}.tmp"
r = subprocess.run([real] + link + ["-Wno-unused-command-line-argument", "-o", tmp])
if r.returncode:
    try: os.remove(tmp)
    except OSError: pass
    sys.exit(r.returncode)
os.replace(tmp, out)
