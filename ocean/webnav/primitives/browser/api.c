#include "browser.h"
#include "row.h"
#include <stddef.h>

void primitive_browser_batch(uint32_t *);

int wb_validate(const WBState *state) {
    return state ? wb_row_validate(state->words) : -1;
}
int wb_reset(WBState *state,uint32_t seed) {
    if (!state) return -1;
    state->words[12]=1;state->words[13]=seed;
    primitive_browser_batch(state->words);
    return wb_validate(state);
}
int wb_step(WBState *state,uint32_t command,uint32_t route,uint32_t elapsed_ms) {
    if (wb_validate(state) || command>WB_RELOAD ||
        (command==WB_NAVIGATE ? route>=state->words[1] : route!=0)) return -1;
    state->words[8]=command;state->words[9]=route;
    state->words[10]=elapsed_ms;state->words[12]=2;
    primitive_browser_batch(state->words);
    return wb_validate(state);
}
int wb_observe(const WBState *state,WBView *view) {
    if (!view || wb_validate(state)) return -1;
    const uint32_t *r=state->words;
    *view=(WBView){r[0],r[4],r[6]!=0,r[7]!=0};
    return 0;
}
