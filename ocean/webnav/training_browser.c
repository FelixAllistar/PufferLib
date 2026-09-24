/*
 * Browser training/evaluation lane for the first five public MiniWoB tasks.
 *
 * This executable deliberately has a small scope: it loads the pinned original
 * MiniWoB pages, observes only the visible DOM-v2 projection and instruction,
 * dispatches real CDP mouse events, and advances a controlled 250 ms clock.
 * It does not use the Bend training row, private task goals, or simulator
 * policy state.  The browser's terminal raw/timed rewards are used only for
 * reporting the result of the browser episode.
 */
#define _GNU_SOURCE

#include "cdp.h"
#include "dom.h"
#include "training.h"
#include "training_policy.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define MINIWOB_REVISION "33c3b4ddef8c6eb67c57a29663d844b1eda7e614"
#define MINIWOB_ROOT \
    "build/webnav/reference/MiniWoB-plusplus-" MINIWOB_REVISION \
    "/miniwob/html/miniwob"
#define STEP_MS 250u
#define MAX_STEPS 40u

typedef enum {
    TASK_BUTTON,
    TASK_LINK,
    TASK_FOCUS,
    TASK_CHECKBOXES,
    TASK_OPTION,
} BrowserTask;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    char *data = calloc((size_t)size + 1, 1);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static int js_discard(WebCdp *cdp, const char *expression) {
    cJSON *value = web_cdp_eval(cdp, expression);
    if (!value) return -1;
    cJSON_Delete(value);
    return 0;
}

static int js_bool(WebCdp *cdp, const char *expression, int *out) {
    cJSON *value = web_cdp_eval(cdp, expression);
    if (!value) return -1;
    if (!cJSON_IsBool(value)) {
        cJSON_Delete(value);
        return -1;
    }
    *out = cJSON_IsTrue(value);
    cJSON_Delete(value);
    return 0;
}

static const WebDomNode *dom_node(const WebDom *dom, unsigned ref) {
    for (unsigned i = 0; i < dom->count; i++) {
        if (dom->nodes[i].ref == ref) return dom->nodes + i;
    }
    return NULL;
}

static const char *dom_text_or_empty(const WebDom *dom, WebText span) {
    const char *text = web_dom_text(dom, span);
    return text ? text : "";
}

static int copy_text(char *dst, size_t capacity, const char *src, int *clipped) {
    if (!src || capacity == 0) return -1;
    size_t length = strlen(src);
    if (length >= capacity) {
        length = capacity - 1;
        if (clipped) *clipped = 1;
    }
    memcpy(dst, src, length);
    dst[length] = 0;
    return 0;
}

static void add_span(WTView *view, const char *text) {
    if (!text || !*text || strlen(text) >= sizeof(view->copy[0])) return;
    for (unsigned i = 0; i < view->spans; i++) {
        if (!strcmp(view->copy[i], text)) return;
    }
    if (view->spans >= WT_SPANS) return;
    snprintf(view->copy[view->spans], sizeof(view->copy[0]), "%s", text);
    view->copy_node[view->spans++] = 0;
}

/* Preserve the same quoted public text candidates as the native projection. */
static void quoted_spans(WTView *view) {
    const char *p = view->query;
    while ((p = strchr(p, '"')) != NULL) {
        const char *start = ++p;
        const char *end = strchr(start, '"');
        if (!end) break;
        size_t length = (size_t)(end - start);
        if (length && length < sizeof(view->copy[0])) {
            char text[sizeof(view->copy[0])];
            memcpy(text, start, length);
            text[length] = 0;
            add_span(view, text);
        }
        p = end + 1;
    }
}

static int relevant_node(BrowserTask task, const WebDomNode *node) {
    switch (task) {
        case TASK_BUTTON:
            return node->role == WEB_ROLE_BUTTON;
        case TASK_LINK:
            /* MiniWoB's links are styled spans with role=other. */
            return node->role == WEB_ROLE_LINK ||
                   (node->role == WEB_ROLE_OTHER &&
                    (node->flags & WEB_ACTIONABLE));
        case TASK_FOCUS:
            return node->role == WEB_ROLE_TEXTBOX;
        case TASK_CHECKBOXES:
            return node->role == WEB_ROLE_CHECKBOX ||
                   node->role == WEB_ROLE_BUTTON;
        case TASK_OPTION:
            return node->role == WEB_ROLE_RADIO ||
                   node->role == WEB_ROLE_BUTTON;
    }
    return 0;
}

static unsigned wt_role(BrowserTask task, const WebDomNode *node) {
    if (task == TASK_LINK && node->role == WEB_ROLE_OTHER) return WT_LINK;
    switch (node->role) {
        case WEB_ROLE_BUTTON: return WT_BUTTON;
        case WEB_ROLE_CHECKBOX: return WT_CHECKBOX;
        case WEB_ROLE_TEXTBOX: return WT_INPUT;
        case WEB_ROLE_LINK: return WT_LINK;
        case WEB_ROLE_RADIO: return WT_RADIO;
        default: return WT_OTHER;
    }
}

/* Convert only public DOM data into the policy-facing, bounded WTView. */
static int make_view(const WebDom *dom, BrowserTask task, unsigned elapsed,
                     WTView *view) {
    memset(view, 0, sizeof *view);
    view->elapsed = elapsed;
    view->omitted = dom->omitted;
    view->text_clipped = dom->truncated;
    int clipped = view->text_clipped != 0;

    const char *instruction = web_dom_text(dom, dom->instruction);
    if (!instruction || copy_text(view->query, sizeof view->query, instruction,
                                  &clipped)) {
        return -1;
    }
    quoted_spans(view);

    for (unsigned i = 0; i < dom->count; i++) {
        const WebDomNode *source = dom->nodes + i;
        if (!(source->flags & WEB_VISIBLE) || !relevant_node(task, source)) {
            continue;
        }
        if (view->count >= WT_NODES) {
            view->omitted++;
            continue;
        }

        WTNode *node = view->nodes + view->count;
        memset(node, 0, sizeof *node);
        node->ref = source->ref; /* opaque DOM occurrence, never a semantic ID */
        node->role = wt_role(task, source);
        node->flags = WT_VISIBLE;
        if (source->flags & WEB_ENABLED) node->flags |= WT_ENABLED;
        if (source->flags & WEB_ACTIONABLE) node->flags |= WT_CLICKABLE;
        if (source->flags & WEB_CHECKED) node->flags |= WT_CHECKED;
        if (source->flags & WEB_FOCUSED) node->flags |= WT_FOCUSED;
        node->x = 0.0f;
        node->y = (float)view->count / (float)WT_NODES;
        if (copy_text(node->name, sizeof node->name,
                      dom_text_or_empty(dom, source->name),
                      &clipped) ||
            copy_text(node->value, sizeof node->value,
                      dom_text_or_empty(dom, source->value),
                      &clipped)) {
            return -1;
        }
        if (node->role == WT_INPUT) {
            node->capacity = 64;
            node->insert_limit = 64;
        }
        view->count++;
    }
    view->text_clipped = (unsigned)clipped;
    return 0;
}

static int snapshot(WebCdp *cdp, WebDom *dom, int *done, float *raw,
                    float *reward) {
    cJSON *value = web_cdp_eval(cdp, "__mw.snapshot()");
    if (!value) return -1;
    const cJSON *observation = cJSON_GetObjectItemCaseSensitive(value, "obs");
    const cJSON *done_value = cJSON_GetObjectItemCaseSensitive(value, "done");
    const cJSON *raw_value = cJSON_GetObjectItemCaseSensitive(value, "raw");
    const cJSON *reward_value = cJSON_GetObjectItemCaseSensitive(value, "reward");
    int bad = !cJSON_IsBool(done_value) || !cJSON_IsNumber(raw_value) ||
              !cJSON_IsNumber(reward_value) || !observation ||
              web_dom_parse(dom, observation);
    if (!bad) {
        *done = cJSON_IsTrue(done_value);
        *raw = (float)raw_value->valuedouble;
        *reward = (float)reward_value->valuedouble;
    }
    cJSON_Delete(value);
    return bad ? -1 : 0;
}

static int task_parse(const char *name, BrowserTask *task) {
    if (!strcmp(name, "click-button")) *task = TASK_BUTTON;
    else if (!strcmp(name, "click-link")) *task = TASK_LINK;
    else if (!strcmp(name, "focus-text")) *task = TASK_FOCUS;
    else if (!strcmp(name, "click-checkboxes")) *task = TASK_CHECKBOXES;
    else if (!strcmp(name, "click-option")) *task = TASK_OPTION;
    else return -1;
    return 0;
}

static const char *task_page(BrowserTask task) {
    switch (task) {
        case TASK_BUTTON: return "click-button.html";
        case TASK_LINK: return "click-link.html";
        case TASK_FOCUS: return "focus-text.html";
        case TASK_CHECKBOXES: return "click-checkboxes.html";
        case TASK_OPTION: return "click-option.html";
    }
    return NULL;
}

static int install_public_runtime(WebCdp *cdp) {
    char *script = read_file("ocean/webnav/web/dom_snapshot.js");
    if (!script) {
        fprintf(stderr, "cannot read ocean/webnav/web/dom_snapshot.js: %s\n",
                strerror(errno));
        return -1;
    }
    int bad = js_discard(cdp, script);
    free(script);
    if (bad) return -1;

    /* Date is controlled only to make the original page's timed reward
     * deterministic.  No task closure or private generator value is read. */
    const char *runtime =
        "(()=>{window.__mw={clock:0,elapsed:0};const D=Date;"
        "window.Date=class extends D{constructor(...a){super(...(a.length?a:[__mw.clock]))}"
        "static now(){return __mw.clock}};"
        "const end=core.endEpisode;core.endEpisode=function(r,t,why){"
        "if(core.EP_TIMER!==null)__mw.elapsed=Date.now()-core.ept0;"
        "return end(r,t,why)};"
        "__mw.reset=seed=>{__mw.clock=0;__mw.elapsed=0;"
        "Math.seedrandom(String(seed));core.startEpisodeReal();"
        "clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer();return true};"
        "__mw.advance=ms=>{__mw.clock+=ms;"
        "if(!WOB_DONE_GLOBAL&&__mw.clock-core.ept0>=core.EPISODE_MAX_TIME)"
        "core.endEpisode(-1,false,'timed out');return !!WOB_DONE_GLOBAL};"
        "__mw.snapshot=()=>({obs:webnavDOM(),done:WOB_DONE_GLOBAL,"
        "raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,elapsed:__mw.elapsed});"
        "return true})()";
    return js_discard(cdp, runtime);
}

static int wait_for_browser(WebCdp *cdp) {
    /* A zero-delay promise lets page handlers and layout updates settle before
     * the next public snapshot.  The optional headed delay is wall time only;
     * logical reward time advances in exactly STEP_MS increments below. */
    return js_discard(cdp,
                      "new Promise(resolve=>setTimeout(()=>resolve(true),0))");
}

static int advance_clock(WebCdp *cdp, unsigned elapsed, int *done) {
    char expression[128];
    snprintf(expression, sizeof expression, "__mw.advance(%u)", elapsed);
    return js_bool(cdp, expression, done);
}

static int click_action(WebCdp *cdp, const WebDom *dom, const WTView *view,
                        unsigned action) {
    if (!action || action > WT_NODES || action > view->count) return 0;
    const WebDomNode *node = dom_node(dom, view->nodes[action - 1].ref);
    if (!node) return -1;
    return web_cdp_click(cdp, node->x + node->width / 2.0,
                         node->y + node->height / 2.0);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s expert|checkpoint task episodes\n"
            "tasks: click-button click-link focus-text click-checkboxes click-option\n",
            argv0);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        usage(argv[0]);
        return 2;
    }

    const char *mode = argv[1];
    BrowserTask task;
    if (strcmp(mode, "expert") && !*mode) {
        usage(argv[0]);
        return 2;
    }
    if (task_parse(argv[2], &task)) {
        usage(argv[0]);
        return 2;
    }

    char *end = NULL;
    unsigned long requested = strtoul(argv[3], &end, 10);
    if (!*argv[3] || !end || *end || requested < 1 || requested > 1000000UL) {
        usage(argv[0]);
        return 2;
    }
    unsigned episodes = (unsigned)requested;

    WTPolicy *policy = NULL;
    if (strcmp(mode, "expert")) {
        policy = wt_policy_load(mode);
        if (!policy) return 1;
    }

    char fixture[PATH_MAX];
    char resolved[PATH_MAX];
    char page[PATH_MAX + 8];
    const char *filename = task_page(task);
    if (snprintf(fixture, sizeof fixture, "%s/%s", MINIWOB_ROOT, filename) >=
            (int)sizeof fixture || !realpath(fixture, resolved) ||
        snprintf(page, sizeof page, "file://%s", resolved) >= (int)sizeof page) {
        fprintf(stderr, "missing pinned MiniWoB page: %s\n", fixture);
        wt_policy_free(policy);
        return 1;
    }
    snprintf(page, sizeof page, "file://%s", resolved);

    const char *chrome = getenv("WEBNAV_CHROME");
    int headed = getenv("WEBNAV_HEADED") && !strcmp(getenv("WEBNAV_HEADED"), "1");
    int trace = headed || (getenv("WEBNAV_TRACE") && !strcmp(getenv("WEBNAV_TRACE"), "1"));
    if (!chrome || !*chrome) {
        chrome = headed ? "build/webnav/chrome-linux64/chrome"
                        : "build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    }

    WebCdp cdp;
    memset(&cdp, 0, sizeof cdp);
    cdp.input = cdp.output = -1;
    cdp.pid = -1;
    WebDom dom;
    WTView view;
    int status = 1;

    if (web_cdp_start_ready(&cdp, chrome, page,
                            "Boolean(window.core&&core.cover_div)")) {
        fprintf(stderr, "cannot start Chromium with %s\n", chrome);
        goto done;
    }
    if (install_public_runtime(&cdp)) goto done;

    double raw_sum = 0.0;
    double timed_sum = 0.0;
    unsigned long long step_sum = 0;
    unsigned success = 0;
    unsigned timeouts = 0;
    for (unsigned episode = 0; episode < episodes; episode++) {
        char expression[128];
        snprintf(expression, sizeof expression, "__mw.reset(%u);true",
                 100000u + episode);
        if (js_discard(&cdp, expression) || wait_for_browser(&cdp)) goto done;

        int done = 0;
        float raw = 0.0f, timed = 0.0f;
        if (snapshot(&cdp, &dom, &done, &raw, &timed) || done) goto done;
        if (policy) wt_policy_reset(policy);

        unsigned elapsed = 0;
        unsigned steps = 0;
        for (; steps < MAX_STEPS && !done; steps++) {
            if (make_view(&dom, task, elapsed, &view)) {
                fprintf(stderr, "invalid public DOM projection\n");
                goto done;
            }
            unsigned action = policy ? (unsigned)wt_policy_action(policy, &view)
                                     : (unsigned)wt_expert(&view);
            if (trace) {
                fprintf(stderr, "%s episode=%u step=%u query=\"%s\" action=%u target=\"%s\"\n",
                        argv[2], episode + 1, steps + 1, view.query, action,
                        action && action <= view.count ? view.nodes[action - 1].name : "wait");
            }

            elapsed += STEP_MS;
            /* The browser's deadline is checked before the action, matching
             * the native deadline ordering at exactly 10,000 ms. */
            int expired = 0;
            if (advance_clock(&cdp, STEP_MS, &expired)) goto done;
            if (expired) {
                done = 1;
            } else if (click_action(&cdp, &dom, &view, action)) {
                fprintf(stderr, "CDP click failed for action %u\n", action);
                goto done;
            }
            if (wait_for_browser(&cdp)) goto done;
            if (headed) usleep(300000);
            if (snapshot(&cdp, &dom, &done, &raw, &timed)) goto done;
        }

        if (!done) {
            /* A well-behaved controlled clock reaches the deadline on step 40.
             * Keep this guard explicit if a page failed to expose core state. */
            fprintf(stderr, "episode exceeded %u-step deadline\n", MAX_STEPS);
            goto done;
        }
        raw_sum += raw;
        timed_sum += timed;
        step_sum += steps;
        if (raw >= 0.999f) success++;
        if (raw <= -0.999f && elapsed >= 10000u) timeouts++;
        if (trace) fprintf(stderr, "episode=%u success=%s raw=%.3f timed=%.3f steps=%u\n",
                           episode + 1, raw >= 0.999f ? "yes" : "no", raw, timed, steps);
    }

    printf("{\"task\":\"%s\",\"episodes\":%u,\"success\":%u,"
           "\"success_rate\":%.6f,\"raw_reward_mean\":%.6f,"
           "\"timed_reward_mean\":%.6f,\"mean_steps\":%.6f,"
           "\"timeouts\":%u,\"controller\":\"%s\"}\n",
           argv[2], episodes, success, (double)success / episodes,
           raw_sum / episodes, timed_sum / episodes,
           (double)step_sum / episodes, timeouts,
           policy ? "learned-greedy" : "public-expert");
    fflush(stdout);
    status = 0;

done:
    web_cdp_close(&cdp);
    wt_policy_free(policy);
    return status;
}
