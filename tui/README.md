# PufferLib TUI eval over SSH

Render native env evals in your terminal, from a headless server, over plain
SSH. No X forwarding, no VNC: the TUI runs server-side and emits ANSI /
graphics-protocol bytes; your local terminal rasterizes them.

```
┌─ server (headless) ─────────────────────────────┐      ┌─ laptop ──────────────────┐
│  xvfb-run ./breakout | ./tui_viewer             │ ssh  │                           │
│    ├─ env sim + puffernet inference (native C)  │─────▶│  terminal renders         │
│    ├─ raylib draws frame (software GL)          │ ANSI │  half-blocks / sixel /    │
│    ├─ ptui_capture() grabs RGBA (puffer_tui.h)  │+sixel│  kitty graphics locally   │
│    └─ tui_viewer presents to the pty            │      │                           │
└─────────────────────────────────────────────────┘      └───────────────────────────┘
```

## Files

- `tui/puffer_tui.h` — header-only capture shim. Included by env headers;
  compiles to nothing unless `-DPUFFER_TUI_CAPTURE` is set. `ptui_capture()`
  calls raylib's `LoadImageFromScreen()` just before `EndDrawing()` and
  streams framed RGBA ("PFRM" wire format, see header docs).
- `tui/tui_viewer.c` — reads framed RGBA on stdin, presents it:
  - `--sink=ansi` (default): half-block truecolor cells, zero dependencies,
    works in any truecolor terminal over plain SSH.
  - `--sink=opentui`: dlopens OpenTUI's native core (`libopentui.so`) and
    drives its C ABI (`createRenderer` → `imageCreateFromRgba` →
    `bufferDrawImage` → `render`). Supports kitty graphics / sixel / blocks.
- `build.sh <env> --tui` — builds the standalone env binary with capture
  enabled plus `tui_viewer`.

## Setup (server, once)

```bash
sudo apt-get install -y xvfb libgl1          # software GL via Mesa llvmpipe

# Optional, for --sink=opentui:
git clone https://github.com/anomalyco/opentui
cd opentui && bun install && zig build -Doptimize=ReleaseFast
# locate the built lib (packages/native/zig-out/lib or similar) for --lib=
```

## Run

From the `pufferlib/` dir (breakout loads `resources/...` relative to CWD):

```bash
./build.sh breakout --tui
xvfb-run -a ./breakout | ./tui_viewer --sink=ansi
```

Over SSH this is it — everything streams through the session. Quit with `q`.

### Useful flags

Producer (env binary):
- `PUFFER_TUI_EVERY=3` — emit every Nth frame; use on slow links.
- `PUFFER_TUI_OUT=<fd>` — write frames to another fd instead of stdout.

Viewer:
- `--fps=N` cap redraw rate (default 30).
- `--proto=kitty|sixel|blocks|auto` image protocol for the opentui sink.
- `--swaprb` / `--flipv` fix channel order / orientation if a driver
  misbehaves (raylib 5.5 already returns RGBA top-down).

## Adding capture to another env

Two guarded lines per env header:

```c
// with the other includes:
#ifdef PUFFER_TUI_CAPTURE
#include "puffer_tui.h"
#endif

// at the end of c_render/puf_render, before EndDrawing():
#ifdef PUFFER_TUI_CAPTURE
    ptui_capture(env->width, env->height);
#endif
```

GPU/CUDA builds never define the macro, so they're untouched. Window-mode
(`--fast`) builds are also unaffected.

## Roadmap

1. Single-process variant: link `libopentui.so` directly into the eval binary
   instead of piping (drops xvfb-run pipe hop; same ABI calls).
2. Stats panes via OpenTUI primitives (`bufferDrawText`, `bufferDrawBox`,
   reward curves via `bufferDrawGrayscaleBuffer`).
3. Key-override input: viewer already reads `/dev/tty`; forward mapped keys
   (arrows/WASD) to the env's action buffer for interactive policy testing.
4. `@opentui/ssh` server mode so multiple people can attach to one eval.
