/*
 * PufferLib TUI capture shim.
 *
 * Include from an env header and call ptui_capture() once per rendered frame
 * (just before EndDrawing, while the frame is still in the GL back buffer).
 * Grabs the framebuffer with raylib and streams it out as framed RGBA so a
 * terminal viewer (tui_viewer) can render it over SSH.
 *
 * Enabled per-env at build time:  ./build.sh breakout --tui
 *   which adds -DPUFFER_TUI_CAPTURE -I./tui
 *
 * Frame wire format (all little-endian):
 *   bytes 0..3   magic "PFRM"
 *   bytes 4..7   u32 width
 *   bytes 8..11  u32 height
 *   bytes 12..15 u32 payload_len (w*h*4)
 *   payload      RGBA8, row-major, top-to-bottom
 *
 * Env vars:
 *   PUFFER_TUI_OUT    output fd (default 1 = stdout; pair via `| tui_viewer`)
 *   PUFFER_TUI_EVERY  emit every Nth frame (default 1; raise for slow links)
 *
 * Notes:
 *   - Logs go to stderr ONLY. Never printf() to the stream fd from an env
 *     built with capture enabled, it corrupts frames.
 *   - x86 little-endian assumed (same as the rest of pufferlib's ocean code).
 *   - raylib 5.5 LoadImageFromScreen() returns RGBA, flipped to top-down,
 *     alpha forced opaque (see rlReadScreenPixels), so no post-processing.
 */
#ifndef PUFFER_TUI_H
#define PUFFER_TUI_H

#ifdef PUFFER_TUI_CAPTURE

#include "raylib.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int ptui_fd = -1;
static int ptui_every = 1;
static long long ptui_frames = 0;

static void ptui_init(void) {
    const char* fd_str = getenv("PUFFER_TUI_OUT");
    ptui_fd = (fd_str != NULL) ? atoi(fd_str) : 1;
    if (ptui_fd < 0) {
        ptui_fd = -1;
        return;
    }

    const char* every_str = getenv("PUFFER_TUI_EVERY");
    if (every_str != NULL) {
        int n = atoi(every_str);
        if (n > 0) {
            ptui_every = n;
        }
    }
}

static void ptui_write_all(const void* buf, size_t len) {
    if (ptui_fd < 0) return;
    const unsigned char* p = (const unsigned char*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(ptui_fd, p + off, len - off);
        if (n <= 0) {
            // Viewer gone or link dead: drop frames rather than die mid-ep.
            ptui_fd = -1;
            return;
        }
        off += (size_t)n;
    }
}

static inline void ptu_put_u32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

/*
 * Grab the current GL back buffer and emit one frame. Call right before
 * EndDrawing(): glReadPixels flushes pending draws, and reading pre-swap is
 * deterministic under software GL (llvmpipe/Xvfb).
 */
static void ptui_capture(int width, int height) {
    if (ptui_fd < 0) return;
    if (ptui_frames++ % ptui_every) return;

    if (ptui_frames == 1) {
        ptui_init();
        // Re-check after env-var config.
        if (ptui_fd < 0) return;
    }

    Image img = LoadImageFromScreen();
    if (img.data == NULL || img.width != width || img.height != height) {
        UnloadImage(img);
        return;
    }

    unsigned char hdr[16];
    hdr[0] = 'P'; hdr[1] = 'F'; hdr[2] = 'R'; hdr[3] = 'M';
    ptu_put_u32(hdr + 4, (uint32_t)img.width);
    ptu_put_u32(hdr + 8, (uint32_t)img.height);
    ptu_put_u32(hdr + 12, (uint32_t)(img.width * img.height * 4));
    ptui_write_all(hdr, sizeof(hdr));
    ptui_write_all(img.data, (size_t)img.width * img.height * 4);

    UnloadImage(img);
}

#else
// Capture disabled: compiles to nothing so env headers need no guards beyond
// this single include.
static void ptui_capture(int width, int height) { (void)width; (void)height; }
#endif // PUFFER_TUI_CAPTURE

#endif // PUFFER_TUI_H
