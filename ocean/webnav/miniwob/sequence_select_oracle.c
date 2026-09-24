#define _GNU_SOURCE

#include "bridge.h"
#include "cdp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This is a differential controller for the two small, original MiniWoB
 * pages represented by sequence_select/Wire.bend.  It intentionally imports
 * only the private values needed by that wire: button rectangles and the
 * visible option strings.  Every browser action below is a CDP mouse or key
 * event; the DOM is queried for observation and for the event target, never
 * mutated to perform an action.
 */

enum { LANES = 32, WORDS = 256, ROW_WORDS = LANES * WORDS };
enum { TAG_SEQUENCE = 7, TAG_LIST = 8 };

static int eval_ok(WebCdp *c, const char *expression) {
    cJSON *r = web_cdp_eval(c, expression);
    if (!r) {
        fprintf(stderr, "Runtime.evaluate returned no value\n");
        return -1;
    }
    cJSON_Delete(r);
    return 0;
}

static int call_ok(WebCdp *c, const char *method, cJSON *params) {
    cJSON *r = web_cdp_call(c, method, params);
    if (!r) return -1;
    cJSON_Delete(r);
    return 0;
}

static unsigned jnum(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return item && cJSON_IsNumber(item) ? (unsigned)item->valuedouble : 0;
}

static double jdouble(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return item && cJSON_IsNumber(item) ? item->valuedouble : 0.0;
}

static int jbool(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsTrue(item);
}

static const char *jstring(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return item && cJSON_IsString(item) ? item->valuestring : NULL;
}

static void batch(uint32_t *words) {
    for (unsigned lane = 1; lane < LANES; lane++)
        memcpy(words + lane * WORDS, words, WORDS * sizeof *words);
    webnav_batch(words);
}

static unsigned round_u32(double value) {
    if (!(value >= 0.0) || value > 4294967295.0) return 0;
    return (unsigned)floor(value + 0.5);
}

static int ascii64(const char *text) {
    size_t n = strlen(text);
    if (n > 64) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126)
            return 0;
    return 1;
}

static void pack_option(uint32_t *row, unsigned index, const char *text) {
    unsigned base = 32 + 16 * index;
    size_t length = strlen(text);
    row[17 + index] = (unsigned)length;
    for (size_t i = 0; i < length; i++)
        row[base + i / 4] |= (uint32_t)(unsigned char)text[i] << (8 * (i % 4));
}

static void unpack_option(const uint32_t *row, unsigned index, char text[65]) {
    unsigned length = row[17 + index];
    if (length > 64) length = 64;
    unsigned base = 32 + 16 * index;
    for (unsigned i = 0; i < length; i++)
        text[i] = (char)((row[base + i / 4] >> (8 * (i % 4))) & 255u);
    text[length] = 0;
}

static int same_float(float expected, double actual) {
    return fabs((double)expected - actual) <= 1e-6;
}

static float row_float(const uint32_t *row, unsigned at) {
    float value;
    memcpy(&value, row + at, sizeof value);
    return value;
}

static int key_event(WebCdp *c, const char *name, unsigned code) {
    for (unsigned release = 0; release < 2; release++) {
        cJSON *params = cJSON_CreateObject();
        if (!params) return -1;
        cJSON_AddStringToObject(params, "type", release ? "keyUp" : "keyDown");
        cJSON_AddStringToObject(params, "key", name);
        cJSON_AddNumberToObject(params, "windowsVirtualKeyCode", code);
        if (call_ok(c, "Input.dispatchKeyEvent", params)) return -1;
    }
    return 0;
}

static int click_id(WebCdp *c, const char *id) {
    char expression[384];
    snprintf(expression, sizeof expression,
             "(()=>{const e=document.getElementById('%s');if(!e)return null;"
             "const r=e.getBoundingClientRect();return{x:r.left+r.width/2,y:r.top+r.height/2}})()",
             id);
    cJSON *point = web_cdp_eval(c, expression);
    if (!point || !cJSON_IsObject(point)) {
        cJSON_Delete(point);
        return -1;
    }
    double x = jdouble(point, "x");
    double y = jdouble(point, "y");
    cJSON_Delete(point);
    return web_cdp_click(c, x, y);
}

static int click_selector(WebCdp *c, const char *selector) {
    char expression[384];
    snprintf(expression, sizeof expression,
             "(()=>{const e=document.querySelector('%s');if(!e)return null;"
             "const r=e.getBoundingClientRect();return{x:r.left+r.width/2,y:r.top+r.height/2}})()",
             selector);
    cJSON *point = web_cdp_eval(c, expression);
    if (!point || !cJSON_IsObject(point)) {
        cJSON_Delete(point);
        return -1;
    }
    double x = jdouble(point, "x");
    double y = jdouble(point, "y");
    cJSON_Delete(point);
    return web_cdp_click(c, x, y);
}

static int click_point(WebCdp *c, unsigned x, unsigned y) {
    return web_cdp_click(c, (double)x, (double)y);
}

static int sequence_point_hit(WebCdp *c, unsigned x, unsigned y) {
    char expression[640];
    snprintf(expression, sizeof expression,
             "(()=>{const e=document.elementFromPoint(%u,%u);"
             "const b=e&&e.closest?e.closest('button'):null;"
             "return b&&b.id==='subbtn2'?2:b&&b.id==='subbtn'?1:0})()",
             x, y);
    cJSON *result = web_cdp_eval(c, expression);
    if (!result || !cJSON_IsNumber(result)) {
        cJSON_Delete(result);
        return -1;
    }
    int hit = result->valueint;
    cJSON_Delete(result);
    return hit == 1 || hit == 2 ? hit : 0;
}

static int find_sequence_point(WebCdp *c, unsigned wanted, unsigned left,
                               unsigned top, unsigned *x, unsigned *y) {
    char expression[1024];
    snprintf(expression, sizeof expression,
             "(()=>{const want='subbtn%s';"
             "for(const dx of [1,20,39])for(const dy of [1,20,39]){"
             "const x=%u+dx,y=%u+dy,e=document.elementFromPoint(x,y),"
             "b=e&&e.closest?e.closest('button'):null;"
             "if(b&&b.id===want)return{x,y}}return null})()",
             wanted == 2 ? "2" : "", left, top);
    cJSON *point = web_cdp_eval(c, expression);
    if (!point || !cJSON_IsObject(point)) {
        cJSON_Delete(point);
        return 0;
    }
    *x = jnum(point, "x");
    *y = jnum(point, "y");
    cJSON_Delete(point);
    return 1;
}

static int select_index(WebCdp *c, unsigned index) {
    if (click_id(c, "options")) return -1;
    if (key_event(c, "Home", 36)) return -1;
    for (unsigned i = 0; i < index; i++)
        if (key_event(c, "ArrowDown", 40)) return -1;
    return key_event(c, "Enter", 13);
}

static int sequence_compare(const cJSON *snapshot, const uint32_t *row,
                            int expected_hit, const char *where) {
    const cJSON *pushes = cJSON_GetObjectItemCaseSensitive(snapshot, "pushes");
    const cJSON *one = cJSON_GetObjectItemCaseSensitive(snapshot, "one");
    const cJSON *two = cJSON_GetObjectItemCaseSensitive(snapshot, "two");
    if (!cJSON_IsArray(pushes) || !cJSON_IsObject(one) || !cJSON_IsObject(two)) {
        fprintf(stderr, "sequence snapshot schema mismatch at %s\n", where);
        return -1;
    }
    int count = cJSON_GetArraySize(pushes);
    unsigned first = 0;
    if (count > 0) {
        const cJSON *item = cJSON_GetArrayItem(pushes, 0);
        if (item && cJSON_IsString(item))
            first = !strcmp(item->valuestring, "subbtn") ? 1u :
                    !strcmp(item->valuestring, "subbtn2") ? 2u : 0u;
    }
    int done = jbool(snapshot, "done");
    if (row[10] != (unsigned)count || row[11] != first || row[12] != (unsigned)expected_hit ||
        done != (row[1] != 0) || ((!done || row[1] == 2) && row[2] != 0)) {
        fprintf(stderr, "sequence state mismatch at %s: row clicks/first/hit=%u/%u/%u "
                "browser=%d/%u/%d; status/success=%u/%u\n", where,
                row[10], row[11], row[12], count, first, done, row[1], row[2]);
        return -1;
    }
    if (round_u32(jdouble(one, "width")) != 40 ||
        round_u32(jdouble(one, "height")) != 40 ||
        round_u32(jdouble(two, "width")) != 40 ||
        round_u32(jdouble(two, "height")) != 40 ||
        round_u32(jdouble(one, "left")) != row[13] ||
        round_u32(jdouble(one, "top")) != row[14] ||
        round_u32(jdouble(two, "left")) != row[15] ||
        round_u32(jdouble(two, "top")) != row[16]) {
        fprintf(stderr, "sequence rectangle mismatch at %s\n", where);
        return -1;
    }
    if (done) {
        int browser_success = jdouble(snapshot, "raw") > 0.0;
        if (row[2] != (unsigned)browser_success) {
            fprintf(stderr, "sequence success mismatch at %s\n", where);
            return -1;
        }
        if (!same_float(row_float(row, 8), jdouble(snapshot, "raw")) ||
            !same_float(row_float(row, 9), jdouble(snapshot, "reward")) ||
            row[7] != jnum(snapshot, "elapsed")) {
            fprintf(stderr, "sequence reward/time mismatch at %s\n", where);
            return -1;
        }
    }
    return 0;
}

static int sequence_import(WebCdp *c, uint32_t *words) {
    cJSON *snapshot = web_cdp_eval(c, "__seq.snapshot()");
    if (!snapshot) return -1;
    const cJSON *one = cJSON_GetObjectItemCaseSensitive(snapshot, "one");
    const cJSON *two = cJSON_GetObjectItemCaseSensitive(snapshot, "two");
    if (!cJSON_IsObject(one) || !cJSON_IsObject(two)) {
        cJSON_Delete(snapshot);
        return -1;
    }
    memset(words, 0, ROW_WORDS * sizeof *words);
    words[0] = TAG_SEQUENCE;
    words[13] = round_u32(jdouble(one, "left"));
    words[14] = round_u32(jdouble(one, "top"));
    words[15] = round_u32(jdouble(two, "left"));
    words[16] = round_u32(jdouble(two, "top"));
    words[17] = 1;
    batch(words);
    int result = sequence_compare(snapshot, words, 0, "sequence import");
    cJSON_Delete(snapshot);
    return result;
}

static int sequence_step(WebCdp *c, uint32_t *words, unsigned command,
                         unsigned x, unsigned y, unsigned *actions) {
    unsigned delta = command == 3 ? 10000u : 137u;
    char expression[128];
    snprintf(expression, sizeof expression, "__seq.advance(%u);true", delta);
    if (eval_ok(c, expression)) return -1;

    int expected_hit = (int)words[12];
    int active = words[1] == 0 && words[6] + delta < 10000u;
    if (command == 1 && active) {
        int hit = sequence_point_hit(c, x, y);
        if (hit < 0) return -1;
        expected_hit = hit;
        if (click_point(c, x, y)) return -1;
    }
    words[3] = command;
    words[4] = x;
    words[5] = y;
    words[6] += delta;
    batch(words);
    cJSON *snapshot = web_cdp_eval(c, "__seq.snapshot()");
    if (!snapshot) return -1;
    int result = sequence_compare(snapshot, words, expected_hit, "sequence step");
    cJSON_Delete(snapshot);
    (*actions)++;
    return result;
}

static int option_target(const cJSON *snapshot, unsigned *target) {
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(snapshot, "options");
    const char *text = jstring(snapshot, "targetText");
    if (!cJSON_IsArray(options) || !text || !ascii64(text)) return -1;
    int count = cJSON_GetArraySize(options);
    for (int i = 0; i < count; i++) {
        const cJSON *item = cJSON_GetArrayItem(options, i);
        if (cJSON_IsString(item) && !strcmp(item->valuestring, text)) {
            *target = (unsigned)i;
            return 0;
        }
    }
    return -1;
}

static int choose_import(WebCdp *c, uint32_t *words, unsigned *target_out) {
    cJSON *snapshot = web_cdp_eval(c, "__sel.snapshot()");
    if (!snapshot) return -1;
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(snapshot, "options");
    int count = cJSON_GetArraySize(options);
    unsigned target = 0;
    if (!cJSON_IsArray(options) || count < 3 || count > 10 ||
        option_target(snapshot, &target)) {
        fprintf(stderr, "Unsupported choose-list instance (count/target/options)\n");
        cJSON_Delete(snapshot);
        return -1;
    }
    memset(words, 0, ROW_WORDS * sizeof *words);
    words[0] = TAG_LIST;
    words[10] = jnum(snapshot, "selected");
    words[11] = target;
    words[12] = 1;
    words[13] = (unsigned)count;
    for (int i = 0; i < count; i++) {
        const cJSON *item = cJSON_GetArrayItem(options, i);
        if (!cJSON_IsString(item) || !ascii64(item->valuestring)) {
            fprintf(stderr, "Unsupported choose-list option at %d\n", i);
            cJSON_Delete(snapshot);
            return -1;
        }
        pack_option(words, (unsigned)i, item->valuestring);
    }
    batch(words);
    *target_out = target;
    int result = 0;
    if (words[10] != jnum(snapshot, "selected")) result = -1;
    if (!result) {
        for (int i = 0; i < count; i++) {
            char text[65];
            unpack_option(words, (unsigned)i, text);
            const cJSON *item = cJSON_GetArrayItem(options, i);
            if (strcmp(text, item->valuestring)) {
                result = -1;
                break;
            }
        }
    }
    cJSON_Delete(snapshot);
    return result;
}

static int choose_compare(const cJSON *snapshot, const uint32_t *row,
                          const char *where) {
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(snapshot, "options");
    if (!cJSON_IsArray(options) || cJSON_GetArraySize(options) != (int)row[13]) {
        fprintf(stderr, "choose option count mismatch at %s\n", where);
        return -1;
    }
    if (row[10] != jnum(snapshot, "selected") || jbool(snapshot, "done") != (row[1] != 0)) {
        fprintf(stderr, "choose state mismatch at %s: selected/status row=%u/%u browser=%u/%d\n",
                where, row[10], row[1], jnum(snapshot, "selected"), jbool(snapshot, "done"));
        return -1;
    }
    for (unsigned i = 0; i < row[13]; i++) {
        const cJSON *item = cJSON_GetArrayItem(options, (int)i);
        char text[65];
        unpack_option(row, i, text);
        if (!cJSON_IsString(item) || strcmp(text, item->valuestring)) {
            fprintf(stderr, "choose option mismatch at %s index=%u\n", where, i);
            return -1;
        }
    }
    if (jbool(snapshot, "done")) {
        int browser_success = jdouble(snapshot, "raw") > 0.0;
        if (row[2] != (unsigned)browser_success) {
            fprintf(stderr, "choose success mismatch at %s\n", where);
            return -1;
        }
        if (!same_float(row_float(row, 8), jdouble(snapshot, "raw")) ||
            !same_float(row_float(row, 9), jdouble(snapshot, "reward")) ||
            row[7] != jnum(snapshot, "elapsed")) {
            fprintf(stderr, "choose reward/time mismatch at %s\n", where);
            return -1;
        }
    }
    return 0;
}

static int choose_step(WebCdp *c, uint32_t *words, unsigned command,
                       unsigned index, unsigned *actions) {
    unsigned delta = command == 3 ? 10000u : 137u;
    char expression[128];
    snprintf(expression, sizeof expression, "__sel.advance(%u);true", delta);
    if (eval_ok(c, expression)) return -1;
    int active = words[1] == 0 && words[6] + delta < 10000u;
    if (command == 1 && active && index < words[13]) {
        if (select_index(c, index)) return -1;
    } else if (command == 2 && active) {
        if (click_selector(c, "#area button.secondary-action")) return -1;
    }
    words[3] = command;
    words[4] = index;
    words[6] += delta;
    batch(words);
    cJSON *snapshot = web_cdp_eval(c, "__sel.snapshot()");
    if (!snapshot) return -1;
    int result = choose_compare(snapshot, words, "choose step");
    cJSON_Delete(snapshot);
    (*actions)++;
    return result;
}

static int install_sequence_clock(WebCdp *c) {
    return eval_ok(c,
        "(()=>{window.__seq={clock:0,elapsed:0};const D=Date;"
        "window.Date=class extends D{constructor(...a){super(...(a.length?a:[__seq.clock]))}"
        "static now(){return __seq.clock}};"
        "const end=core.endEpisode;core.endEpisode=function(r,t,why){"
        "if(core.EP_TIMER!==null)__seq.elapsed=Date.now()-core.ept0;"
        "return end(r,t,why)};"
        "__seq.reset=seed=>{__seq.clock=0;__seq.elapsed=0;"
        "Math.seedrandom(String(seed));core.startEpisodeReal();"
        "clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer()};"
        "__seq.advance=ms=>{__seq.clock+=ms;"
        "if(!WOB_DONE_GLOBAL&&__seq.clock-core.ept0>=core.EPISODE_MAX_TIME)"
        "core.endEpisode(-1,false,'timed out')};"
        "__seq.snapshot=()=>{const rect=id=>{const r=document.getElementById(id).getBoundingClientRect();"
        "return{left:r.left,top:r.top,width:r.width,height:r.height}};"
        "return{one:rect('subbtn'),two:rect('subbtn2'),pushes:buttonsPushed.slice(),"
        "done:WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,"
        "elapsed:__seq.elapsed}};return true})()" );
}

static int install_select_clock(WebCdp *c) {
    return eval_ok(c,
        "(()=>{window.__sel={clock:0,elapsed:0};const D=Date;"
        "window.Date=class extends D{constructor(...a){super(...(a.length?a:[__sel.clock]))}"
        "static now(){return __sel.clock}};"
        "const end=core.endEpisode;core.endEpisode=function(r,t,why){"
        "if(core.EP_TIMER!==null)__sel.elapsed=Date.now()-core.ept0;"
        "return end(r,t,why)};"
        "__sel.reset=seed=>{__sel.clock=0;__sel.elapsed=0;"
        "Math.seedrandom(String(seed));core.startEpisodeReal();"
        "clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer()};"
        "__sel.advance=ms=>{__sel.clock+=ms;"
        "if(!WOB_DONE_GLOBAL&&__sel.clock-core.ept0>=core.EPISODE_MAX_TIME)"
        "core.endEpisode(-1,false,'timed out')};"
        "__sel.snapshot=()=>{const s=document.getElementById('options');"
        "const q=document.getElementById('query').textContent;"
        "const m=q.match(/^Select (.*) from the list and click Submit\\.$/);"
        "return{options:[...s.options].map(e=>e.innerHTML),selected:s.selectedIndex,"
        "targetText:m?m[1]:'',done:WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,"
        "reward:WOB_REWARD_GLOBAL,elapsed:__sel.elapsed}};return true})()" );
}

static int reset_page(WebCdp *c, const char *name, int seed) {
    char expression[160];
    snprintf(expression, sizeof expression, "%s.reset(%d);true", name, seed);
    return eval_ok(c, expression);
}

static int run_sequence(WebCdp *c, int episodes, unsigned *actions,
                        unsigned *successes, unsigned *wrong, unsigned *timeouts) {
    uint32_t words[ROW_WORDS];
    for (int episode = 0; episode < episodes; episode++) {
        int seed = 300000 + episode;
        if (reset_page(c, "__seq", seed) || sequence_import(c, words)) return -1;
        unsigned left_one = words[13], top_one = words[14];
        unsigned left_two = words[15], top_two = words[16];
        unsigned one_x = left_one + 20, one_y = top_one + 20;
        unsigned two_x = left_two + 20, two_y = top_two + 20;
        unsigned one_point_x = 0, one_point_y = 0;
        unsigned two_point_x = 0, two_point_y = 0;
        int have_one = find_sequence_point(c, 1, left_one, top_one,
                                            &one_point_x, &one_point_y);
        int have_two = find_sequence_point(c, 2, left_two, top_two,
                                            &two_point_x, &two_point_y);
        if (have_one < 0 || have_two < 0) return -1;
        unsigned schedule = (unsigned)episode % 6;
#define SEQ_STEP(command, x, y) do { \
            if (sequence_step(c, words, (command), (x), (y), actions)) { \
                fprintf(stderr, "sequence mismatch seed=%d schedule=%u command=%u\\n", \
                        seed, schedule, (unsigned)(command)); \
                return -1; \
            } \
        } while (0)
        if (schedule == 0) {
            if (!have_one) SEQ_STEP(3, 0, 0);
            else {
                SEQ_STEP(1, one_point_x, one_point_y);
                if (words[1] == 0) {
                    if (!have_two) SEQ_STEP(3, 0, 0);
                    else SEQ_STEP(1, two_point_x, two_point_y);
                }
            }
        } else if (schedule == 1) {
            SEQ_STEP(1, one_x, one_y);
            if (words[1] == 0) SEQ_STEP(1, two_x, two_y);
            if (words[1] == 0) SEQ_STEP(3, 0, 0);
        } else if (schedule == 2) {
            SEQ_STEP(1, two_x, two_y);
            if (words[1] == 0) SEQ_STEP(1, one_x, one_y);
            if (words[1] == 0) SEQ_STEP(3, 0, 0);
        } else if (schedule == 3) {
            SEQ_STEP(1, 0, 0);
            if (words[1] == 0 && have_one) SEQ_STEP(1, one_point_x, one_point_y);
            if (words[1] == 0 && have_two) SEQ_STEP(1, two_point_x, two_point_y);
            if (words[1] == 0) SEQ_STEP(3, 0, 0);
        } else if (schedule == 4) {
            SEQ_STEP(3, 0, 0);
        } else {
            if (have_one) SEQ_STEP(1, one_point_x, one_point_y);
            if (words[1] == 0) SEQ_STEP(0, 0, 0);
            if (words[1] == 0 && have_two) SEQ_STEP(1, two_point_x, two_point_y);
            if (words[1] == 0) SEQ_STEP(3, 0, 0);
        }
        if (words[1] == 0) {
            fprintf(stderr, "sequence did not terminate seed=%d\n", seed);
            return -1;
        }
        if (words[1] == 2) (*timeouts)++;
        else if (words[2]) (*successes)++;
        else (*wrong)++;
        SEQ_STEP(0, 0, 0); /* terminal absorption and frozen reward */
#undef SEQ_STEP
    }
    return 0;
}

static int run_sequence_overlap_fixture(WebCdp *c, unsigned *actions) {
    if (reset_page(c, "__seq", 399999) ||
        eval_ok(c,
            "(()=>{const a=document.getElementById('subbtn'),b=document.getElementById('subbtn2');"
            "a.style.left='60px';a.style.top='100px';b.style.left='60px';b.style.top='100px';"
            "buttonsPushed=[];return true})()")) return -1;
    uint32_t words[ROW_WORDS];
    if (sequence_import(c, words)) return -1;
    unsigned x = words[13] + 20, y = words[14] + 20;
    /* The point is named as ONE by the intended trace, but the later DOM
     * button is the actual event target at this complete overlap. */
    if (sequence_step(c, words, 1, x, y, actions) ||
        sequence_step(c, words, 1, x, y, actions) || words[1] != 1 ||
        words[2] != 0 || words[11] != 2 || words[12] != 2) {
        fprintf(stderr, "sequence overlap fixture failed (expected TWO/TWO wrong)\n");
        return -1;
    }
    if (sequence_step(c, words, 0, 0, 0, actions)) return -1;
    return 0;
}

static int run_sequence_occluded_fixture(WebCdp *c, unsigned *actions) {
    if (reset_page(c, "__seq", 399998) ||
        eval_ok(c,
            "(()=>{const a=document.getElementById('subbtn'),b=document.getElementById('subbtn2');"
            "a.style.left='60px';a.style.top='100px';b.style.left='60px';b.style.top='100px';"
            "buttonsPushed=[];return true})()")) return -1;
    uint32_t words[ROW_WORDS];
    if (sequence_import(c, words)) return -1;
    unsigned x = 0, y = 0;
    if (find_sequence_point(c, 1, words[13], words[14], &x, &y)) {
        fprintf(stderr, "sequence occluded fixture unexpectedly exposed ONE\n");
        return -1;
    }
    /* A controller seeking the hidden first button times out this instance;
     * it must not silently omit it from the differential run. */
    if (sequence_step(c, words, 3, 0, 0, actions) || words[1] != 2 ||
        words[12] != 0 || words[10] != 0) return -1;
    return sequence_step(c, words, 0, 0, 0, actions);
}

static int run_choose(WebCdp *c, int episodes, unsigned *actions,
                      unsigned *successes, unsigned *wrong, unsigned *timeouts) {
    uint32_t words[ROW_WORDS];
    for (int episode = 0; episode < episodes; episode++) {
        int seed = 400000 + episode;
        unsigned target;
        if (reset_page(c, "__sel", seed) || choose_import(c, words, &target)) return -1;
        unsigned count = words[13];
        unsigned schedule = (unsigned)episode % 6;
#define SEL_STEP(command, index) do { \
            if (choose_step(c, words, (command), (index), actions)) { \
                fprintf(stderr, "choose mismatch seed=%d schedule=%u command=%u index=%u\\n", \
                        seed, schedule, (unsigned)(command), (unsigned)(index)); \
                return -1; \
            } \
        } while (0)
        if (schedule == 0) {
            SEL_STEP(1, target);
            if (words[1] == 0) SEL_STEP(2, 0);
        } else if (schedule == 1) {
            SEL_STEP(1, count + 1);
            if (words[1] == 0) SEL_STEP(3, 0);
        } else if (schedule == 2) {
            SEL_STEP(1, (target + 1) % count);
            if (words[1] == 0) SEL_STEP(2, 0);
        } else if (schedule == 3) {
            SEL_STEP(1, 0);
            if (words[1] == 0) SEL_STEP(1, target);
            if (words[1] == 0) SEL_STEP(2, 0);
        } else if (schedule == 4) {
            SEL_STEP(2, 0);
        } else {
            SEL_STEP(1, target);
            if (words[1] == 0) SEL_STEP(3, 0);
        }
        if (words[1] == 0) {
            fprintf(stderr, "choose-list did not terminate seed=%d\n", seed);
            return -1;
        }
        if (words[1] == 2) (*timeouts)++;
        else if (words[2]) (*successes)++;
        else (*wrong)++;
        SEL_STEP(0, 0); /* terminal absorption and frozen reward */
#undef SEL_STEP
    }
    return 0;
}

static int run_choose_duplicate_fixture(WebCdp *c, unsigned *actions) {
    if (reset_page(c, "__sel", 499999) ||
        eval_ok(c,
            "(()=>{const s=document.getElementById('options');"
            "const q=document.getElementById('query').textContent.match(/^Select (.*) from the list and click Submit\\.$/);"
            "if(!q)return false;s.innerHTML='';for(let i=0;i<3;i++){const o=document.createElement('option');"
            "o.textContent=i<2?q[1]:'Other';s.appendChild(o)}return true})()")) return -1;
    uint32_t words[ROW_WORDS];
    unsigned target;
    if (choose_import(c, words, &target) || target != 0) return -1;
    if (choose_step(c, words, 1, 1, actions) ||
        choose_step(c, words, 2, 0, actions) || words[1] != 1 || !words[2]) {
        fprintf(stderr, "choose duplicate-label fixture failed\n");
        return -1;
    }
    return choose_step(c, words, 0, 0, actions);
}

static int start(WebCdp *c, const char *task, int sequence) {
    char path[4096], file[4096], url[4300];
    snprintf(path, sizeof path,
             "build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/"
             "miniwob/html/miniwob/%s.html", task);
    if (!realpath(path, file)) {
        perror(path);
        return -1;
    }
    snprintf(url, sizeof url, "file://%s", file);
    const char *chrome = getenv("WEBNAV_CHROME");
    if (!chrome) chrome = "build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    if (web_cdp_start_ready(c, chrome, url, "Boolean(window.core&&core.cover_div)")) {
        fprintf(stderr, "CDP page readiness failed for %s\n", task);
        return -1;
    }
    int result = sequence ? install_sequence_clock(c) : install_select_clock(c);
    if (result) fprintf(stderr, "clock/snapshot install failed for %s\n", task);
    return result;
}

int main(int argc, char **argv) {
    int episodes = argc > 1 ? atoi(argv[1]) : 200;
    if (episodes < 1) {
        fprintf(stderr, "usage: sequence_select_oracle [episodes=200]\n");
        return 2;
    }
    WebCdp *c = calloc(1, sizeof *c);
    if (!c) return 1;
    c->input = c->output = -1;
    c->pid = -1;
    int status = 1;
    unsigned sequence_actions = 0, sequence_success = 0;
    unsigned sequence_wrong = 0, sequence_timeouts = 0;
    unsigned choose_actions = 0, choose_success = 0;
    unsigned choose_wrong = 0, choose_timeouts = 0;
    unsigned sequence_fixture_actions = 0, choose_fixture_actions = 0;
    if (start(c, "click-button-sequence", 1)) {
        fprintf(stderr, "failed to start click-button-sequence\n");
        goto done;
    }
    if (run_sequence(c, episodes, &sequence_actions, &sequence_success,
                     &sequence_wrong, &sequence_timeouts)) {
        fprintf(stderr, "click-button-sequence differential run failed\n");
        goto done;
    }
    if (run_sequence_overlap_fixture(c, &sequence_fixture_actions) ||
        run_sequence_occluded_fixture(c, &sequence_fixture_actions)) {
        fprintf(stderr, "click-button-sequence overlap fixture failed\n");
        goto done;
    }
    web_cdp_close(c);
    c->input = c->output = -1;
    c->pid = -1;
    if (start(c, "choose-list", 0)) {
        fprintf(stderr, "failed to start choose-list\n");
        goto done;
    }
    if (run_choose(c, episodes, &choose_actions, &choose_success,
                   &choose_wrong, &choose_timeouts)) {
        fprintf(stderr, "choose-list differential run failed\n");
        goto done;
    }
    if (run_choose_duplicate_fixture(c, &choose_fixture_actions)) {
        fprintf(stderr, "choose-list duplicate fixture failed\n");
        goto done;
    }
    printf("{\"tasks\":{\"click-button-sequence\":{\"episodes\":%d,\"actions\":%u,\"success\":%u,\"wrong\":%u,\"timeouts\":%u},\"choose-list\":{\"episodes\":%d,\"actions\":%u,\"success\":%u,\"wrong\":%u,\"timeouts\":%u}},\"fixture_actions\":{\"click-button-sequence\":%u,\"choose-list\":%u},\"directed_fixtures\":{\"overlap\":1,\"fully_occluded_timeout\":1,\"duplicate_labels\":1},\"conformance\":\"PASS\",\"scope\":\"original seeded HTML, real CDP coordinates/mouse/key events, controlled clock, row state and rewards; button event-target overlap, fully occluded timeout, and text-equivalent duplicate options covered; no full DOM, static option DOM policy projection, or unrestricted generator parity\"}\n",
           episodes, sequence_actions, sequence_success, sequence_wrong,
           sequence_timeouts, episodes, choose_actions, choose_success,
           choose_wrong, choose_timeouts, sequence_fixture_actions, choose_fixture_actions);
    status = 0;
done:
    web_cdp_close(c);
    free(c);
    return status;
}
