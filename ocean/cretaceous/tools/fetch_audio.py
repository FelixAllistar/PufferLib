#!/usr/bin/env python3
"""Download the game's CC0 sound sources from Freesound and build the
in-game sounds in ocean/cretaceous/assets/audio/*.ogg.

Every source is CC0 (public domain): the script re-checks the licence on
each sound's page and refuses anything else. It uses the public preview
files (128 kbps MP3), which needs no account or API key.

Each download lands in build/cretaceous/dl_audio/<id>/ and is only ever
decoded as audio (ffmpeg) and edited (sox); nothing in it is executed.
Run from the repository root:
    python3 -I ocean/cretaceous/tools/fetch_audio.py
Requires ffmpeg and sox on PATH. The script also writes assets/audio/SOURCES.md.

What it builds:
  amb_*    ambience loops, cross-faded so they repeat without a seam
  step_*   footsteps per ground material, cut from walking recordings at their
           onsets (the game pitches them per species and layers a thud for
           big animals)
  call_*   dinosaur calls: animal and creature recordings slowed (pitch and
           formants drop together, which reads as a bigger body), filtered and
           layered; see RECIPES
  sfx_*    attacks, hits, eating, drinking, splashes, body falls
  ui_*     menu sounds
"""
import array, html, os, re, shutil, subprocess, sys, urllib.request

OUT = "ocean/cretaceous/assets/audio"
DL = "build/cretaceous/dl_audio"
TMP = "build/cretaceous/dl_audio/_work"
UA = {"User-Agent": "Mozilla/5.0 (cretaceous-fetch-audio)"}
RATE = 44100

# ---------------------------------------------------------------- recipes
# Each output: (name, source id, kind, options). Kinds:
#   loop   - a seamless loop of `len` s from the most even stretch (or `start`)
#   steps  - `n` single footsteps of `len` s cut at onsets
#   shot   - one-shot of `len` s from the loudest window (or `start`)
#   shots  - `n` one-shots of `len` s from the loudest distinct windows
# Common options: speed (sox speed: <1 lower and longer), lowpass, highpass,
# gain (dB), mono, layer (another (id, start, gain dB, speed) mixed in), reverb.
RECIPES = [
    # Ambience.
    ("amb_day",     423711, "loop", dict(len=36)),                      # tropical birds and insects
    ("amb_insects", 272169, "loop", dict(len=40)),                      # cicadas
    ("amb_night",   328293, "loop", dict(len=75)),                      # night forest: crickets, cicadas
    ("amb_frogs",   462889, "loop", dict(len=40)),                      # frogs and crickets at a swamp
    ("amb_wind",    117136, "loop", dict(len=50)),                      # open wind
    ("amb_surf",    531015, "loop", dict(len=60)),                      # waves on a beach
    ("amb_river",   433589, "loop", dict(len=55)),                      # stream close up
    ("amb_rain",    678435, "loop", dict(len=70)),                      # rain on leaves
    # Footsteps (the game picks by the ground under the foot).
    ("step_grass",  389625, "steps", dict(n=6, len=0.42)),
    ("step_dirt",   264469, "steps", dict(n=6, len=0.40)),
    ("step_rock",   530589, "steps", dict(n=6, len=0.38)),
    ("step_sand",   383490, "steps", dict(n=6, len=0.45)),
    ("step_water",  376809, "steps", dict(n=6, len=0.50)),
    ("step_heavy",  504626, "shots", dict(n=1, len=1.2, speed=0.75, lowpass=900)),   # body fall on dirt as a stomp
    ("step_thud",   332668, "shots", dict(n=1, len=0.6, speed=0.8, lowpass=1200)),
    # Calls. Small animals: higher, harsher; large: slowed and darkened.
    ("call_raptor", 320345, "shot", dict(len=2.2, speed=1.15, highpass=250)),                  # dino hiss
    ("call_raptor2", 233277, "shot", dict(len=1.6, speed=1.05, highpass=200)),
    ("call_stego",  527844, "shot", dict(len=4.0, speed=0.62, lowpass=1800)),                  # alligator growl
    ("call_trex",   529462, "shot", dict(len=4.5, speed=0.8, lowpass=3000, reverb=20,
                                         layer=(527844, None, -6, 0.5))),                      # T. rex calls + deep growl
    ("call_pachy",  233279, "shot", dict(len=2.4, speed=0.72, lowpass=2600)),
    ("call_sauropelta", 437933, "shot", dict(len=3.5, speed=0.55, lowpass=1400)),              # alligator growl, slower
    ("call_galli",  233278, "shot", dict(len=1.8, speed=1.25, highpass=300)),
    ("growl",       20208, "shot", dict(len=1.8, speed=0.8, lowpass=2500)),                    # attack vocal
    # Combat and body sounds.
    ("sfx_bite",    467701, "shot", dict(len=0.9)),
    ("sfx_swing",   475135, "shot", dict(len=1.0, speed=0.85)),
    ("sfx_hit",     257928, "shot", dict(len=0.9, layer=(192146, None, -4, 1.0))),             # body thud + bone crunch
    ("sfx_crunch",  249811, "shots", dict(n=2, len=1.4)),                                       # eating meat
    ("sfx_chew",    244370, "shots", dict(n=2, len=1.6)),                                       # grazing
    ("sfx_drink",   506294, "shots", dict(n=2, len=1.8)),
    ("sfx_fall",    457531, "shot", dict(len=3.5)),                                             # dinosaur falls to the ground
    ("sfx_splash",  442773, "shot", dict(len=1.8)),
    # Menu.
    ("ui_click",    253168, "shot", dict(len=0.25)),
    ("ui_move",     200469, "shot", dict(len=0.2, gain=-6)),
]

# ---------------------------------------------------------------- download
def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120).read()

def fetch(sid):
    """Returns (path, meta) for a CC0 sound; exits if it is not CC0."""
    d = os.path.join(DL, str(sid)); os.makedirs(d, exist_ok=True)
    meta_p, mp3 = os.path.join(d, "meta.txt"), os.path.join(d, "preview.mp3")
    if os.path.exists(meta_p) and os.path.exists(mp3):
        f = open(meta_p).read().split("\n"); return mp3, dict(title=f[0], user=f[1], url=f[2])
    page = get("https://freesound.org/s/%d/" % sid).decode("utf8", "replace")
    lic = set(re.findall(r"creativecommons\.org/(?:licenses|publicdomain)/[a-z\-]+", page))
    if lic != {"creativecommons.org/publicdomain/zero"}:
        sys.exit("sound %d is not CC0 (%s); refusing" % (sid, sorted(lic)))
    m = re.search(r"<title>Freesound - (.*) by ([^<]+)</title>", page)
    title, user = (html.unescape(m.group(1)), html.unescape(m.group(2))) if m else ("?", "?")
    pv = re.search(r"https://cdn\.freesound\.org/previews/\d+/%d_\d+-hq\.mp3" % sid, page)
    if not pv: sys.exit("no preview for %d" % sid)
    open(mp3, "wb").write(get(pv.group(0)))
    url = "https://freesound.org/people/%s/sounds/%d/" % (user, sid)
    open(meta_p, "w").write("%s\n%s\n%s\n" % (title, user, url))
    return mp3, dict(title=title, user=user, url=url)

# ---------------------------------------------------------------- audio helpers
def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode: sys.exit("failed: %s\n%s" % (" ".join(cmd), r.stderr.decode()[-800:]))
    return r.stdout

def decode(src, ch):
    """PCM s16 at RATE with ch channels (array)."""
    a = array.array("h"); a.frombytes(run(["ffmpeg", "-v", "error", "-i", src, "-f", "s16le", "-ac", str(ch), "-ar", str(RATE), "-"]))
    return a

def envelope(src, hop=0.01):
    """Mono RMS-ish envelope at 1/hop Hz (from an 8 kHz decode)."""
    a = array.array("h"); a.frombytes(run(["ffmpeg", "-v", "error", "-i", src, "-f", "s16le", "-ac", "1", "-ar", "8000", "-"]))
    n = int(8000 * hop); env = []
    for i in range(0, len(a) - n, n):
        s = 0
        for v in a[i:i + n:4]: s += v * v
        env.append((s / (n / 4)) ** 0.5)
    return env

def loudest(env, win, hop=0.01, exclude=()):
    w = max(1, int(win / hop)); best, bi = -1, 0; s = sum(env[:w])
    for i in range(0, max(1, len(env) - w)):
        if i: s += env[i + w - 1] - env[i - 1]
        if any(abs(i - e) < w for e in exclude): continue
        if s > best: best, bi = s, i
    return bi * hop

def most_even(env, win, hop=0.01):
    """Start of the window whose loudness varies least (a steady bed for a loop)."""
    w = int(win / hop); blk = int(1.0 / hop); best, bi = 1e30, 0
    secs = [sum(env[i:i + blk]) / blk for i in range(0, len(env) - blk, blk)]
    k = int(win)
    for i in range(0, max(1, len(secs) - k)):
        seg = secs[i:i + k]; mean = sum(seg) / len(seg)
        if mean <= 0: continue
        var = (sum((x - mean) ** 2 for x in seg) / len(seg)) ** 0.5 / mean
        if var < best: best, bi = var, i
    return float(bi)

def onsets(env, hop=0.01, gap=0.22):
    peak = max(env) if env else 1; thr = peak * 0.18; out = []; last = -1e9
    for i in range(1, len(env)):
        if env[i] > thr and env[i] > env[i - 1] * 1.6 and (i - last) * hop > gap:
            out.append(i); last = i
    return [(i, max(env[i:i + int(0.15 / hop)] or [0])) for i in out]

def write_pcm(a, ch, path):
    raw = path + ".raw"; open(raw, "wb").write(a.tobytes())
    return raw

def sox_chain(o):
    fx = []
    if o.get("speed"): fx += ["speed", str(o["speed"])]
    if o.get("highpass"): fx += ["highpass", str(o["highpass"])]
    if o.get("lowpass"): fx += ["lowpass", str(o["lowpass"])]
    if o.get("reverb"): fx += ["reverb", str(o["reverb"]), "50", "60"]
    return fx

def finish(inp, out, o, ch, fade_in=0.004, fade_out=0.05, norm=-1.0, length=None):
    """Effects, fades and peak-normalise into an ogg."""
    fx = sox_chain(o)
    if length: fx += ["trim", "0", str(length)]
    fx += ["fade", "t", str(fade_in), "0" if not length else str(length), str(fade_out)]
    fx += ["norm", str(norm + o.get("gain", 0))]
    run(["sox", "-t", "raw", "-r", str(RATE), "-e", "signed", "-b", "16", "-c", str(ch), inp, "-C", "4", out] + fx + ["rate", str(RATE)])

def cut(a, ch, t0, length):
    i0 = max(0, int(t0 * RATE)) * ch; i1 = min(len(a), i0 + int(length * RATE) * ch)
    return a[i0:i1]

def mix(a, b, gain_db):
    g = 10 ** (gain_db / 20); out = array.array("h", a)
    for i in range(min(len(a), len(b))):
        v = int(a[i] + b[i] * g); out[i] = 32767 if v > 32767 else -32768 if v < -32768 else v
    return out

def layer_source(o, length, main_speed):
    """The layer, cut and resampled so that after the main sound's speed change
    it plays at its own speed: (pcm, gain dB, id)."""
    sid, start, gain, speed = o["layer"]
    src, _ = fetch(sid); a = decode(src, 1)
    t = start if start is not None else loudest(envelope(src), length * speed)
    seg = cut(a, 1, t, length * speed); r = speed / main_speed
    if abs(r - 1) > 1e-3:
        raw = os.path.join(TMP, "layer.raw"); open(raw, "wb").write(seg.tobytes())
        b = array.array("h"); b.frombytes(run(["sox", "-t", "raw", "-r", str(RATE), "-e", "signed", "-b", "16", "-c", "1", raw,
                                               "-t", "raw", "-", "speed", str(r), "rate", str(RATE)]))
        seg = b
    return seg, gain, sid

# ---------------------------------------------------------------- builders
def build_loop(name, src, o):
    ch = 2; a = decode(src, ch); env = envelope(src); L = o["len"]; X = 3.0
    dur = len(a) / ch / RATE
    if dur < L + X + 0.5: L = max(5.0, dur - X - 0.5)
    t0 = o.get("start", most_even(env, L + X))
    seg = cut(a, ch, t0, L + X)
    n, nx = int(L * RATE) * ch, int(X * RATE) * ch
    out = array.array("h", seg[:n])
    for i in range(0, nx, ch):   # equal-power cross-fade of the tail into the head
        f = i / nx; gi, go = (f ** 0.5), ((1 - f) ** 0.5)
        for c in range(ch):
            v = int(seg[i + c] * gi + seg[n + i + c] * go); out[i + c] = 32767 if v > 32767 else -32768 if v < -32768 else v
    raw = write_pcm(out, ch, os.path.join(TMP, name))
    fx = sox_chain(o) + ["norm", str(-3.0 + o.get("gain", 0))]
    run(["sox", "-t", "raw", "-r", str(RATE), "-e", "signed", "-b", "16", "-c", str(ch), raw, "-C", "3", os.path.join(OUT, name + ".ogg")] + fx)
    return "%.0f s loop from %.0f s" % (L, t0)

def build_steps(name, src, o):
    a = decode(src, 1); env = envelope(src); hits = onsets(env)
    hits = sorted(hits, key=lambda h: -h[1])[: o["n"] * 2]
    hits = sorted(hits[: o["n"]]) if len(hits) >= o["n"] else sorted(hits)
    if not hits: sys.exit("no onsets in %s" % name)
    for k, (i, _) in enumerate(hits):
        seg = cut(a, 1, i * 0.01 - 0.02, o["len"])
        raw = write_pcm(seg, 1, os.path.join(TMP, "%s_%d" % (name, k)))
        finish(raw, os.path.join(OUT, "%s_%d.ogg" % (name, k)), o, 1, fade_out=o["len"] * 0.45, norm=-1.5)
    return "%d steps cut at onsets" % len(hits)

def build_shots(name, src, o, single):
    a = decode(src, 1); env = envelope(src); L = o["len"]; sp = o.get("speed", 1.0)
    n = 1 if single else o.get("n", 1); used = []
    for k in range(n):
        t = o.get("start") if (single and o.get("start") is not None) else loudest(env, L * sp, exclude=[int(u / 0.01) for u in used])
        used.append(t)
        seg = cut(a, 1, max(0.0, t - 0.03), L * sp + 0.03)
        extra = ""
        if o.get("layer"):
            lay, g, lid = layer_source(o, L, sp)
            seg = mix(seg, lay, g); extra = " + layer %d" % lid
        out = os.path.join(OUT, ("%s.ogg" % name) if single or n == 1 else "%s_%d.ogg" % (name, k))
        raw = write_pcm(seg, 1, os.path.join(TMP, "%s_%d" % (name, k)))
        finish(raw, out, o, 1, fade_out=min(0.4, L * 0.3), length=L)
    return "%d x %.1f s from the loudest part%s" % (n, L, extra if o.get("layer") else "")

def main():
    for tool in ("ffmpeg", "sox"):
        if not shutil.which(tool): sys.exit("%s is required" % tool)
    os.makedirs(OUT, exist_ok=True); os.makedirs(TMP, exist_ok=True)
    only = set(sys.argv[1:])
    used = {}
    rows = []
    for name, sid, kind, o in RECIPES:
        if only and name not in only: continue
        src, meta = fetch(sid); used[sid] = meta
        if o.get("layer"): lsrc, lmeta = fetch(o["layer"][0]); used[o["layer"][0]] = lmeta
        if kind == "loop": how = build_loop(name, src, o)
        elif kind == "steps": how = build_steps(name, src, o)
        else: how = build_shots(name, src, o, kind == "shot")
        rows.append((name, sid, how))
        print("%-16s %-7d %s" % (name, sid, how))
    if only: return
    lines = ["# Sounds", "",
             "All sources are CC0 (public domain) recordings from [Freesound](https://freesound.org).",
             "`tools/fetch_audio.py` downloads them and builds the game's sounds: loops, cut footsteps,",
             "and dinosaur calls made by slowing, filtering and layering animal and creature recordings.",
             "Thank you to every recordist below for releasing their work into the public domain.", "",
             "| Sound | Built from | How |", "|---|---|---|"]
    for name, sid, how in rows:
        m = used[sid]; lay = ""
        o = dict(RECIPES[[r[0] for r in RECIPES].index(name)][3])
        if o.get("layer"): lm = used[o["layer"][0]]; lay = " + [%s](%s) by %s" % (lm["title"].replace("|", "\\|"), lm["url"], lm["user"])
        lines.append("| `%s` | [%s](%s) by %s%s | %s |" % (name, m["title"].replace("|", "\\|"), m["url"], m["user"], lay, how))
    lines += ["", "Licence of every source: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)."]
    open(os.path.join(OUT, "SOURCES.md"), "w").write("\n".join(lines) + "\n")
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT) if f.endswith(".ogg"))
    print("wrote %d files, %.1f MB" % (len([f for f in os.listdir(OUT) if f.endswith('.ogg')]), total / 1e6))

if __name__ == "__main__":
    main()
