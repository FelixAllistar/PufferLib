#define PUFFER_TUI_CAPTURE
#include "puffer_tui.h"
#include <assert.h>
#include <stdio.h>

static unsigned char pixels[] = {1, 2, 3, 255, 4, 5, 6, 255};
static int loads, unloads;

Image LoadImageFromScreen(void) {
    loads++;
    return (Image){pixels, 2, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

void UnloadImage(Image image) {
    assert(image.data == pixels);
    unloads++;
}

int main(void) {
    int fds[2];
    assert(pipe(fds) == 0);
    char descriptor[32];
    snprintf(descriptor, sizeof(descriptor), "%d", fds[1]);
    setenv("PUFFER_TUI_OUT", descriptor, 1);
    setenv("PUFFER_TUI_EVERY", "2", 1);
    for (int i = 0; i < 4; i++) ptui_capture(2, 1);
    close(fds[1]);
    unsigned char bytes[64] = {0};
    assert(read(fds[0], bytes, sizeof(bytes)) == 48);
    assert(read(fds[0], bytes + 48, 1) == 0);
    close(fds[0]);
    const unsigned char header[] = {'P', 'F', 'R', 'M', 2, 0, 0, 0,
        1, 0, 0, 0, 8, 0, 0, 0};
    for (int i = 0; i < 2; i++) {
        assert(memcmp(bytes + 24 * i, header, 16) == 0);
        assert(memcmp(bytes + 24 * i + 16, pixels, 8) == 0);
    }
    assert(loads == 2 && unloads == 2);
    ptui_frames = 0;
    setenv("PUFFER_TUI_OUT", "-1", 1);
    ptui_capture(2, 1);
    assert(loads == 2);
    puts("TUI capture initialization, frame wire format, decimation and disable passed");
}
