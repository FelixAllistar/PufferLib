#ifndef WEBNAV_FORMS_PUBLIC_CONTROLLER_H
#define WEBNAV_FORMS_PUBLIC_CONTROLLER_H

#include "../common/family_api.h"
#include <ctype.h>
#include <string.h>

/* One scratch buffer is enough: each decision emits at most one insertion,
 * and it remains valid until the runner consumes the action. */
static char forms_public_payload[256];

static const char *forms_public_name(const WFView *view, const WFNode *node) {
    return node ? wf_text_get(view, node->name) : NULL;
}

static const char *forms_public_value(const WFView *view, const WFNode *node) {
    return node ? wf_text_get(view, node->value) : NULL;
}

static const WFNode *forms_public_find(const WFView *view, uint32_t role,
                                       const char *name) {
    if (!view || !name || view->count > WF_MAX_NODES) return NULL;
    for (uint32_t i = 0; i < view->count; ++i) {
        const WFNode *node = &view->nodes[i];
        const char *candidate = forms_public_name(view, node);
        if ((node->flags & WF_VISIBLE) && node->role == role && candidate &&
            strcmp(candidate, name) == 0) return node;
    }
    return NULL;
}

static const char *forms_public_quoted(const char *query, unsigned wanted,
                                       size_t *length) {
    const char *cursor = query;
    unsigned found = 0;
    if (!query || !length) return NULL;
    while ((cursor = strchr(cursor, '"')) != NULL) {
        const char *start = ++cursor;
        const char *end = strchr(start, '"');
        if (!end) return NULL;
        if (found++ == wanted) {
            *length = (size_t)(end - start);
            return start;
        }
        cursor = end + 1;
    }
    return NULL;
}

static int forms_public_copy_payload(const char *source, size_t length,
                                     int upper, int lower,
                                     const char **out, size_t *out_length) {
    if (!source || !out || !out_length || length > sizeof(forms_public_payload) - 1u)
        return -1;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)source[i];
        forms_public_payload[i] = upper ? (char)toupper(c) :
                                  lower ? (char)tolower(c) : (char)c;
    }
    forms_public_payload[length] = '\0';
    *out = forms_public_payload;
    *out_length = length;
    return 0;
}

/* Task identity comes from public wording and node labels; there is no row,
 * seed, private goal, or family-state access in this controller. */
static int forms_public_task(const WFView *view, const char *query,
                             unsigned *field_count) {
    unsigned inputs = 0;
    int captcha = forms_public_find(view, WF_TEXT, "Captcha") != NULL;
    for (uint32_t i = 0; i < view->count; ++i)
        if (view->nodes[i].role == WF_INPUT || view->nodes[i].role == WF_TEXTAREA)
            ++inputs;
    *field_count = inputs;
    if (strstr(query, "username") && strstr(query, "password")) return 7;
    if (inputs == 4u) return 5;
    if (inputs == 2u) {
        if (forms_public_find(view, WF_INPUT, "Verify password")) return 2;
        if (forms_public_find(view, WF_INPUT, "Answer")) return 4;
        return 6;
    }
    if (captcha) return 3;
    if (strstr(query, "all upper case")) return 1;
    if (strstr(query, "all lower case")) return 1;
    if (inputs == 1u) return 0;
    return -1;
}

static int forms_public_table_value(const WFView *view, const char *label,
                                   const char **value, size_t *length) {
    size_t n;
    if (!label || !*label || (n = strlen(label)) < 2u || label[n - 1u] != ':')
        return -1;
    for (uint32_t i = 0; i + 1u < view->count; ++i) {
        const WFNode *key = &view->nodes[i];
        const char *name = forms_public_name(view, key);
        if ((key->flags & WF_VISIBLE) && key->role == WF_CELL && name &&
            strlen(name) == n - 1u && !strncmp(name, label, n - 1u)) {
            const WFNode *cell = &view->nodes[i + 1u];
            const char *cell_value = forms_public_value(view, cell);
            if (cell->role != WF_CELL || !cell_value) return -1;
            *value = cell_value;
            *length = strlen(cell_value);
            return 0;
        }
    }
    return -1;
}

static int forms_public_goal(const WFView *view, const char *query,
                             unsigned task, const WFNode *field,
                             const char **goal, size_t *goal_length) {
    const char *start;
    size_t length;
    unsigned quoted_index = task == 7u &&
        !strcmp(forms_public_name(view, field), "Password") ? 1u : 0u;

    if (task == 3u) {
        const WFNode *captcha = forms_public_find(view, WF_TEXT, "Captcha");
        *goal = forms_public_value(view, captcha);
        if (!*goal) return -1;
        *goal_length = strlen(*goal);
        return 0;
    }
    if (task == 4u || task == 5u) {
        const char *field_name = forms_public_name(view, field);
        if (!field_name || strcmp(field_name, "Answer")) return -1;
        if (task == 4u) {
            const WFNode *source = forms_public_find(view, WF_TEXTAREA, "Text to copy");
            *goal = forms_public_value(view, source);
        } else {
            unsigned ordinal = strstr(query, "the 2nd text area") ? 1u :
                               strstr(query, "the 3rd text area") ? 2u : 0u;
            static const char *const source_names[] = {
                "1st text area", "2nd text area", "3rd text area"
            };
            const WFNode *source = forms_public_find(view, WF_TEXTAREA,
                                                      source_names[ordinal]);
            *goal = forms_public_value(view, source);
        }
        if (!*goal) return -1;
        *goal_length = strlen(*goal);
        return 0;
    }
    if (task == 6u) {
        const char *label = forms_public_name(view, field);
        return forms_public_table_value(view, label, goal, goal_length);
    }
    start = forms_public_quoted(query, quoted_index, &length);
    if (!start) return -1;
    if (task == 1u) {
        int upper = strstr(query, "all upper case") != NULL;
        return forms_public_copy_payload(start, length, upper, !upper,
                                         goal, goal_length);
    }
    if (task == 7u && quoted_index == 0u)
        return forms_public_copy_payload(start, length, 0, 1,
                                         goal, goal_length);
    return forms_public_copy_payload(start, length, 0, 0,
                                     goal, goal_length);
}

static int forms_public_emit_click(WFAction *action, uint32_t ref) {
    if (!action || !ref) return -1;
    *action = (WFAction){.kind = WF_CLICK, .target = ref};
    return 0;
}

/* The scripted runner supplies elapsed_ms=step*250. This stateless policy
 * reads the task text and visible field/table values, edits one field per
 * action, cancels a visible login popup, and then submits. */
static int forms_public_action(const WFView *view, WFAction *action) {
    const char *query;
    unsigned fields = 0;
    int task;
    if (!view || !action || view->count > WF_MAX_NODES) return -1;
    query = wf_text_get(view, view->instruction);
    if (!query) return -1;
    task = forms_public_task(view, query, &fields);
    if (task < 0) return -1;
    (void)fields;

    const WFNode *cancel = forms_public_find(view, WF_BUTTON, "Popup Cancel");
    if (cancel) return forms_public_emit_click(action, cancel->ref);

    for (uint32_t i = 0; i < view->count; ++i) {
        const WFNode *field = &view->nodes[i];
        const char *field_name, *current, *goal;
        size_t goal_length;
        if (field->role != WF_INPUT && field->role != WF_TEXTAREA) continue;
        field_name = forms_public_name(view, field);
        current = forms_public_value(view, field);
        if (!field_name || !current) return -1;
        if ((task == 4u || task == 5u) && field->role == WF_TEXTAREA) continue;
        if (
            forms_public_goal(view, query, (unsigned)task, field,
                              &goal, &goal_length) < 0 ||
            goal_length > 255u) return -1;
        if (strlen(current) == goal_length &&
            !memcmp(current, goal, goal_length)) continue;
        if (!(field->flags & WF_ENABLED)) return -1;
        if (!(field->flags & WF_FOCUSED))
            return forms_public_emit_click(action, field->ref);
        if (*current && (field->selection_start != 0u ||
                         field->selection_end != strlen(current))) {
            *action = (WFAction){.kind = WF_SELECT_ALL};
            return 0;
        }
        *action = (WFAction){.kind = WF_INSERT, .text = goal,
                             .text_length = goal_length};
        return 0;
    }

    const WFNode *submit = forms_public_find(view, WF_BUTTON,
                                              task == 7 ? "OK" : "Submit");
    if (!submit || !(submit->flags & WF_ENABLED)) return -1;
    return forms_public_emit_click(action, submit->ref);
}

#endif
