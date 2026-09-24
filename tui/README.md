# Terminal rendering over SSH

This optional tool preserves the old fork's Breakout frame-capture workflow
without changing the trainer or root build script. It captures Raylib RGBA
frames and streams them to an ANSI terminal viewer. Gameplay stays upstream.

```bash
make -C tui test breakout
```

Run from the repository root so renderer resources resolve. A headless server
needs Xvfb and a working OpenGL implementation. Send frames through a dedicated
descriptor so Raylib/inference logging cannot corrupt the frame stream:

```bash
(PUFFER_TUI_OUT=3 xvfb-run -a ./tui/build/breakout MODEL.bin 3>&1 1>&2) \
    | ./tui/build/tui_viewer --sink=ansi
```

Omit `MODEL.bin` for an untrained policy. Do not use `--headless` here: that
skips rendering and therefore produces no frames. Ordinary CPU/GPU builds do
not enable `PUFFER_TUI_CAPTURE` and are unaffected.

`PUFFER_TUI_EVERY=3` captures every third frame. The viewer accepts `--fps=N`,
`--swaprb` and `--flipv`. It reads `q` from the controlling terminal to exit.
The optional legacy `--sink=opentui --lib=PATH` dynamically loads OpenTUI;
that external-library ABI has not been qualified during this conversion.

Tests cover capture initialization, decimation, disabling and exact PFRM
header/payload bytes using a stub framebuffer. The viewer and capture-enabled
Breakout executable compile. Real Xvfb/OpenGL capture and interactive terminal
presentation still need visual qualification; no Xvfb is installed locally.

The old capture returned before initialization because its descriptor started
at -1. Initialization now happens before checking whether capture is disabled.
