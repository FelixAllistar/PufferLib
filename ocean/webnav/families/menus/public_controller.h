#ifndef WEBNAV_MENUS_PUBLIC_CONTROLLER_H
#define WEBNAV_MENUS_PUBLIC_CONTROLLER_H

#include "../common/family_api.h"
#include <string.h>

#define MENUS_PATH_PARTS 4u
#define MENUS_PATH_BYTES 256u

static const char *menus_public_text(const WFView *view, WFText text) {
    return wf_text_get(view, text);
}

static const WFNode *menus_public_child(const WFView *view, uint32_t parent,
                                        const char *name) {
    if (!view || !name || view->count > WF_MAX_NODES) return NULL;
    for (uint32_t i = 0; i < view->count; ++i) {
        const WFNode *node = &view->nodes[i];
        const char *node_name;
        if (!(node->flags & WF_VISIBLE) || node->parent != parent) continue;
        node_name = menus_public_text(view, node->name);
        if (node_name && strcmp(node_name, name) == 0) return node;
    }
    return NULL;
}

static int menus_public_path(const char *query, char storage[MENUS_PATH_BYTES],
                             char *parts[MENUS_PATH_PARTS], unsigned *count) {
    static const char prefix[] = "Select ";
    const char *path;
    size_t length;
    unsigned used = 0;

    if (!query || strncmp(query, prefix, sizeof(prefix) - 1u) != 0) return -1;
    path = query + sizeof(prefix) - 1u;
    length = strlen(path);
    if (length < 2u || length >= MENUS_PATH_BYTES || path[length - 1u] != '.') return -1;

    memcpy(storage, path, length - 1u);
    storage[length - 1u] = '\0';
    char *part = storage;
    while (part && used < MENUS_PATH_PARTS) {
        char *separator = strstr(part, " > ");
        if (separator) *separator = '\0';
        if (!*part) return -1;
        parts[used++] = part;
        part = separator ? separator + 3 : NULL;
    }
    if (part || !used) return -1;
    *count = used;
    return 0;
}

static const WFNode *menus_public_target(const WFView *view, const char *needle,
                                         int by_icon) {
    if (!view || !needle || view->count > WF_MAX_NODES) return NULL;
    for (uint32_t i = 0; i < view->count; ++i) {
        const WFNode *node = &view->nodes[i];
        const char *text;
        if (!(node->flags & WF_VISIBLE)) continue;
        text = menus_public_text(view, by_icon ? node->value : node->name);
        if (!text) continue;
        if (!by_icon) {
            if (strcmp(text, needle) == 0) return node;
        } else {
            size_t length = strlen(needle);
            if (strncmp(text, needle, length) == 0 &&
                strcmp(text + length, " icon") == 0) return node;
        }
    }
    return NULL;
}

static int menus_public_emit(WFAction *action, uint32_t kind, uint32_t ref) {
    if (!action || !ref) return -1;
    *action = (WFAction){.kind = kind, .target = ref};
    return 0;
}

/* Returns 0 with a public action, or -1 when the view lacks enough clues. */
static int menus_public_action(const WFView *view, WFAction *action) {
    const char *query;
    if (!view || !action || view->count > WF_MAX_NODES) return -1;
    query = menus_public_text(view, view->instruction);
    if (!query) return -1;

    if (strncmp(query, "Select ", 7u) == 0) {
        char storage[MENUS_PATH_BYTES];
        char *parts[MENUS_PATH_PARTS];
        unsigned count = 0;
        uint32_t parent = 0;
        const WFNode *parent_node = NULL;
        if (menus_public_path(query, storage, parts, &count) < 0) return -1;
        for (unsigned i = 0; i < count; ++i) {
            const WFNode *node = menus_public_child(view, parent, parts[i]);
            if (!node) {
                if (!parent_node) return -1;
                if (parent_node->flags & WF_EXPANDED) return -1;
                return menus_public_emit(action, WF_POINTER_MOVE, parent_node->ref);
            }
            if (i + 1u == count) return menus_public_emit(action, WF_CLICK, node->ref);
            parent_node = node;
            parent = node->ref;
        }
        return -1;
    }

    {
        static const char menu_prefix[] = "Click the \"Menu\" button, and then find and click on the item ";
        const WFNode *menu;
        const WFNode *target;
        const char *label_start;
        const char *icon_start;
        if (strncmp(query, menu_prefix, sizeof(menu_prefix) - 1u) != 0) return -1;
        menu = menus_public_child(view, 0, "Menu");
        if (!menu) return -1;

        unsigned other_visible = 0;
        for (uint32_t i = 0; i < view->count; ++i) {
            const WFNode *node = &view->nodes[i];
            if ((node->flags & WF_VISIBLE) && node->ref != menu->ref) other_visible++;
        }
        if (!other_visible) return menus_public_emit(action, WF_CLICK, menu->ref);

        label_start = strstr(query, "item labeled \"");
        if (label_start) {
            char label[MENUS_PATH_BYTES];
            const char *start = label_start + sizeof("item labeled \"") - 1u;
            const char *end = strchr(start, '"');
            size_t length;
            if (!end || (length = (size_t)(end - start)) == 0u ||
                length >= sizeof(label)) return -1;
            memcpy(label, start, length);
            label[length] = '\0';
            target = menus_public_target(view, label, 0);
        } else {
            icon_start = strstr(query, "item with the ");
            if (!icon_start) return -1;
            icon_start += sizeof("item with the ") - 1u;
            const char *end = strstr(icon_start, " icon.");
            char icon[MENUS_PATH_BYTES];
            size_t length;
            if (!end || (length = (size_t)(end - icon_start)) == 0u ||
                length >= sizeof(icon)) return -1;
            memcpy(icon, icon_start, length);
            icon[length] = '\0';
            target = menus_public_target(view, icon, 1);
        }
        if (target) return menus_public_emit(action, WF_CLICK, target->ref);

        const WFNode *playback = menus_public_child(view, 0, "Playback");
        if (!playback || (playback->flags & WF_EXPANDED)) return -1;
        return menus_public_emit(action, WF_POINTER_MOVE, playback->ref);
    }
}

#endif
