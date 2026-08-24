/*
 * tui_viewer - render PufferLib RGBA frame streams in a terminal.
 *
 * Reads "PFRM" framed RGBA (see puffer_tui.h) from stdin and presents it:
 *
 *   --sink=ansi     (default) half-block truecolor cells. Zero dependencies,
 *                   works over plain SSH in any truecolor terminal.
 *   --sink=opentui  dlopens OpenTUI's native core and drives its C ABI
 *                   (createRenderer / imageCreateFromRgba / bufferDrawImage /
 *                   render). Supports kitty graphics, sixel, and blocks
 *                   protocols depending on your terminal.
 *
 * Usage:
 *   xvfb-run ./breakout | ./tui_viewer [--sink=ansi] [options]
 *
 * Options:
 *   --sink=ansi|opentui  presentation backend (default: ansi)
 *   --lib=PATH           path to libopentui.so for --sink=opentui
 *                        (default: $OPENTUI_LIB or ./libopentui.so)
 *   --proto=auto|kitty|sixel|blocks  image protocol for opentui sink
 *                                    (default: auto)
 *   --fps=N              cap viewer redraw rate (default 30)
 *   --swaprb             swap R/B channels (in case a driver gives BGRA)
 *   --flipv              flip vertically (in case a driver doesn't pre-flip)
 *
 * Keys: q or Ctrl-C quits.
 */
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Framed RGBA stream reader                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t* pixels; // w*h*4 RGBA, owned
} Frame;

static int read_full(uint8_t* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(STDIN_FILENO, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0; // EOF
        off += (size_t)n;
    }
    return 1;
}

static uint32_t le_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Returns 1 on frame, 0 on clean EOF, -1 on protocol error.
static int read_frame(Frame* f) {
    uint8_t hdr[16];
    int rc = read_full(hdr, sizeof(hdr));
    if (rc <= 0) return rc;

    static const uint8_t MAGIC[4] = {'P', 'F', 'R', 'M'};
    if (memcmp(hdr, MAGIC, 4) != 0) return -1;

    uint32_t w = le_u32(hdr + 4);
    uint32_t h = le_u32(hdr + 8);
    uint32_t len = le_u32(hdr + 12);
    if (w == 0 || h == 0 || len != w * h * 4 || w > 16384 || h > 16384) return -1;

    if (f->width != w || f->height != h) {
        free(f->pixels);
        f->pixels = (uint8_t*)malloc(len);
        if (f->pixels == NULL) return -1;
        f->width = w;
        f->height = h;
    }

    rc = read_full(f->pixels, len);
    if (rc <= 0) return (rc == 0) ? -1 : -1; // truncated frame is an error too
    return 1;
}

/* ------------------------------------------------------------------ */
/* Terminal helpers                                                   */
/* ------------------------------------------------------------------ */

static struct termios g_orig_termios;
static int g_raw_enabled = 0;
static int g_tty_fd = -1; // /dev/tty: keys + raw mode (stdin carries frames)

static void term_restore(void) {
    if (g_raw_enabled) {
        tcsetattr(g_tty_fd, TCSAFLUSH, &g_orig_termios);
        g_raw_enabled = 0;
    }
    if (g_tty_fd >= 0) {
        dprintf(g_tty_fd, "\x1b[?1049l\x1b[?25h\x1b[0m\n");
    } else {
        dprintf(STDOUT_FILENO, "\x1b[0m\n");
    }
}

static void on_sigint(int sig) {
    (void)sig;
    term_restore();
    _exit(130);
}

static int term_enter(void) {
    g_tty_fd = open("/dev/tty", O_RDWR);
    if (g_tty_fd >= 0 && tcgetattr(g_tty_fd, &g_orig_termios) == 0) {
        atexit(term_restore);
        signal(SIGINT, on_sigint);
        signal(SIGTERM, on_sigint);

        struct termios raw = g_orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT);
        tcsetattr(g_tty_fd, TCSAFLUSH, &raw);
        g_raw_enabled = 1;

        // Enter alt screen, hide cursor, clear.
        dprintf(g_tty_fd, "\x1b[?1049h\x1b[?25l\x1b[2J");
    }
    // Headless/no-tty (e.g. piping to a file): still run, just no keys.
    return 0;
}

static void term_size(int* cols, int* rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 ||
        ws.ws_row == 0) {
        *cols = 80;
        *rows = 24;
        return;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
}

static int stdin_has_key(void) {
    if (g_tty_fd < 0) return 0;
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(g_tty_fd, &fds);
    return select(g_tty_fd + 1, &fds, NULL, NULL, &tv) > 0;
}

/* ------------------------------------------------------------------ */
/* ANSI half-block sink                                               */
/* ------------------------------------------------------------------ */

// Each terminal cell renders two stacked frame pixels via U+2580 (upper
// half block): top pixel -> foreground color, bottom pixel -> background.
// Terminal cells are ~2x taller than wide, so this is roughly square.

static long long last_emit_ns = 0;

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void render_ansi(const Frame* f, int swaprb, int fps_cap) {
    int cols, rows;
    term_size(&cols, &rows);

    // Reserve one row for the status line at the bottom.
    int view_rows = rows - 1;
    if (view_rows < 1) view_rows = 1;

    // Frame pacing.
    long long interval_ns = 1000000000LL / (fps_cap > 0 ? fps_cap : 30);
    long long t = now_ns();
    if (t - last_emit_ns < interval_ns) return;
    last_emit_ns = t;

    // Pixel grid: cols wide x (view_rows*2) tall, nearest-neighbor sample.
    int pix_h = view_rows * 2;
    int fw = (int)f->width;
    int fh = (int)f->height;

    printf("\x1b[H");
    int last_r = -1, last_g = -1, last_b = -1;
    int last_br = -1, last_bg = -1, last_bb = -1;

    for (int cy = 0; cy < view_rows; cy++) {
        int sy0 = (int)((long long)cy * fh / pix_h);
        int sy1 = (int)((long long)(cy * 2 + 1) * fh / pix_h);
        if (sy0 >= fh) sy0 = fh - 1;
        if (sy1 >= fh) sy1 = fh - 1;

        last_r = last_g = last_b = -1;
        last_br = last_bg = last_bb = -1;

        const uint8_t* row0 = f->pixels + (size_t)sy0 * fw * 4;
        const uint8_t* row1 = f->pixels + (size_t)sy1 * fw * 4;

        for (int cx = 0; cx < cols; cx++) {
            int sx = (int)((long long)cx * fw / cols);
            if (sx >= fw) sx = fw - 1;

            int r0 = row0[sx * 4 + 0], g0 = row0[sx * 4 + 1], b0 = row0[sx * 4 + 2];
            int r1 = row1[sx * 4 + 0], g1 = row1[sx * 4 + 1], b1 = row1[sx * 4 + 2];
            if (swaprb) {
                int tmp;
                tmp = r0, r0 = b0, b0 = tmp;
                tmp = r1, r1 = b1, b1 = tmp;
            }

            // Emit SGR only when colors change; big bandwidth saver over SSH.
            if (r0 != last_r || g0 != last_g || b0 != last_b || r1 != last_br ||
                g1 != last_bg || b1 != last_bb) {
                printf("\x1b[38;2;%d;%d;%d;48;2;%d;%d;%dm", r0, g0, b0, r1, g1, b1);
                last_r = r0; last_g = g0; last_b = b0;
                last_br = r1; last_bg = g1; last_bb = b1;
            }
            fputs("\xe2\x96\x80", stdout); // U+2580 UPPER HALF BLOCK
        }
        fputs("\x1b[0m\x1b[K\r\n", stdout);
    }

    // Status line.
    printf("\x1b[%d;1H\x1b[2m%dx%d -> %dx%d cells | q: quit\x1b[0m",
           rows, fw, fh, cols, view_rows);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* OpenTUI sink                                                       */
/* ------------------------------------------------------------------ */

// C ABI types mirrored from packages/native/src/lib.zig (NativeHandle=u32).
typedef uint32_t ot_handle;

typedef struct {
    int32_t x, y;
    uint32_t width, height;
    uint32_t pixel_width, pixel_height;
    uint32_t source_x, source_y, source_width, source_height;
    uint32_t protocol; // RenderProtocol: auto=0 kitty=1 sixel=2 blocks=3
} ot_image_draw_options;

typedef ot_handle (*fn_createRenderer)(uint32_t, uint32_t, uint8_t, uint8_t,
                                       void*);
typedef ot_handle (*fn_getNextBuffer)(ot_handle);
typedef uint32_t (*fn_imageCreateFromRgba)(const uint8_t*, uint64_t, uint32_t,
                                           uint32_t, uint32_t, ot_handle*);
typedef uint8_t (*fn_bufferDrawImage)(ot_handle, ot_handle,
                                      const ot_image_draw_options*);
typedef uint8_t (*fn_render)(ot_handle, uint8_t);
typedef void (*fn_imageDestroy)(ot_handle);
typedef void (*fn_destroyRenderer)(ot_handle, uint8_t);

typedef struct {
    void* lib;
    fn_createRenderer createRenderer;
    fn_getNextBuffer getNextBuffer;
    fn_imageCreateFromRgba imageCreateFromRgba;
    fn_bufferDrawImage bufferDrawImage;
    fn_render render;
    fn_imageDestroy imageDestroy;
    fn_destroyRenderer destroyRenderer;
    ot_handle renderer;
    ot_handle buffer;
} Opentui;

static int ot_load(Opentui* ot, const char* path) {
    memset(ot, 0, sizeof(*ot));
    ot->lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (ot->lib == NULL) {
        fprintf(stderr, "tui_viewer: cannot open %s\n  dlerror: %s\n", path,
                dlerror());
        fprintf(stderr,
                "Build it with:\n"
                "  git clone https://github.com/anomalyco/opentui\n"
                "  cd opentui && bun install && zig build -Doptimize=ReleaseFast\n"
                "  # then pass --lib=<path to libopentui .so>\n");
        return -1;
    }
#define OT_SYM(field, name)                                                  \
    do {                                                                     \
        *(void**)(&ot->field) = dlsym(ot->lib, name);                         \
        if (ot->field == NULL) {                                              \
            fprintf(stderr, "tui_viewer: missing symbol %s in %s\n", name,    \
                    path);                                                    \
            return -1;                                                        \
        }                                                                     \
    } while (0)
    OT_SYM(createRenderer, "createRenderer");
    OT_SYM(getNextBuffer, "getNextBuffer");
    OT_SYM(imageCreateFromRgba, "imageCreateFromRgba");
    OT_SYM(bufferDrawImage, "bufferDrawImage");
    OT_SYM(render, "render");
    OT_SYM(imageDestroy, "imageDestroy");
    OT_SYM(destroyRenderer, "destroyRenderer");
#undef OT_SYM
    return 0;
}

static int ot_init(Opentui* ot, uint32_t protocol) {
    int cols, rows;
    term_size(&cols, &rows);
    // destKind: 0 = process stdout (OpenTUI owns ANSI output).
    // remoteMode: 0 = auto (handles SSH-style terminals).
    ot->renderer = ot->createRenderer((uint32_t)cols, (uint32_t)rows, 0, 0,
                                      NULL);
    if (ot->renderer == 0) {
        fprintf(stderr, "tui_viewer: createRenderer failed\n");
        return -1;
    }
    ot->buffer = ot->getNextBuffer(ot->renderer);
    if (ot->buffer == 0) {
        fprintf(stderr, "tui_viewer: getNextBuffer failed\n");
        return -1;
    }
    (void)protocol;
    return 0;
}

static void render_opentui(Opentui* ot, const Frame* f, uint32_t protocol,
                           int swaprb) {
    ot_handle img = 0;
    uint32_t status =
        ot->imageCreateFromRgba(f->pixels, (uint64_t)f->width * f->height * 4,
                                f->width, f->height, f->width * 4, &img);
    if (status != 0 || img == 0) {
        fprintf(stderr, "tui_viewer: imageCreateFromRgba status=%u\n", status);
        return;
    }

    int cols, rows;
    term_size(&cols, &rows);

    ot_image_draw_options opt = {
        .x = 0,
        .y = 0,
        .width = (uint32_t)cols,
        .height = (uint32_t)rows - 1 > 0 ? (uint32_t)rows - 1 : 1,
        .pixel_width = f->width,
        .pixel_height = f->height,
        .source_x = 0,
        .source_y = 0,
        .source_width = f->width,
        .source_height = f->height,
        .protocol = protocol,
    };
    if (!ot->bufferDrawImage(ot->buffer, img, &opt)) {
        fprintf(stderr, "tui_viewer: bufferDrawImage failed\n");
    }
    ot->render(ot->renderer, 0);
    ot->imageDestroy(img);
    (void)swaprb; // handled producer-side or by re-reading if ever needed here
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

static void usage(void) {
    fprintf(stderr,
            "usage: <framed rgba on stdin> tui_viewer "
            "[--sink=ansi|opentui] [--lib=PATH] "
            "[--proto=auto|kitty|sixel|blocks] [--fps=N] [--swaprb] "
            "[--flipv]\n");
}

int main(int argc, char** argv) {
    const char* sink = "ansi";
    const char* lib_path = NULL;
    uint32_t protocol = 0; // auto
    int fps_cap = 30;
    int swaprb = 0;
    int flipv = getenv("PUFFER_TUI_FLIPV") != NULL;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strncmp(a, "--sink=", 7) == 0) sink = a + 7;
        else if (strncmp(a, "--lib=", 6) == 0) lib_path = a + 6;
        else if (strncmp(a, "--proto=", 8) == 0) {
            const char* p = a + 8;
            if (strcmp(p, "kitty") == 0) protocol = 1;
            else if (strcmp(p, "sixel") == 0) protocol = 2;
            else if (strcmp(p, "blocks") == 0) protocol = 3;
            else protocol = 0;
        }
        else if (strncmp(a, "--fps=", 6) == 0) fps_cap = atoi(a + 6);
        else if (strcmp(a, "--swaprb") == 0) swaprb = 1;
        else if (strcmp(a, "--flipv") == 0) flipv = 1;
        else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else {
            usage();
            return 1;
        }
    }

    if (lib_path == NULL) lib_path = getenv("OPENTUI_LIB");
    if (lib_path == NULL) lib_path = "./libopentui.so";

    Opentui ot;
    int use_opentui = strcmp(sink, "opentui") == 0;
    if (use_opentui) {
        if (ot_load(&ot, lib_path) != 0) return 1;
    }

    term_enter();

    if (use_opentui && ot_init(&ot, protocol) != 0) {
        term_restore();
        return 1;
    }

    Frame frame = {0};
    for (;;) {
        if (stdin_has_key()) {
            uint8_t key = 0;
            if (read(g_tty_fd, &key, 1) == 1 &&
                (key == 'q' || key == 3)) { // q or Ctrl-C
                break;
            }
        }

        int rc = read_frame(&frame);
        if (rc == 0) break;      // producer closed: clean exit
        if (rc < 0) {
            fprintf(stderr, "tui_viewer: bad frame stream\n");
            break;
        }

        if (flipv) {
            size_t rw = (size_t)frame.width * 4;
            uint8_t* tmp = (uint8_t*)malloc(rw);
            if (tmp != NULL) {
                for (uint32_t y = 0; y < frame.height / 2; y++) {
                    uint8_t* top = frame.pixels + (size_t)y * rw;
                    uint8_t* bot = frame.pixels +
                                   (size_t)(frame.height - 1 - y) * rw;
                    memcpy(tmp, top, rw);
                    memcpy(top, bot, rw);
                    memcpy(bot, tmp, rw);
                }
                free(tmp);
            }
        }

        if (use_opentui) render_opentui(&ot, &frame, protocol, swaprb);
        else render_ansi(&frame, swaprb, fps_cap);
    }

    if (use_opentui) ot.destroyRenderer(ot.renderer, 1);
    free(frame.pixels);
    term_restore();
    return 0;
}
