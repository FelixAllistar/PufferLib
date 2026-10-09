#include "capabilities.h"
#include <string.h>

/* These protocols have no text/key editing actions.  In particular, focus-text
 * belongs to the click family: its adapter only transports WAIT and CLICK. */
enum {
    BASIC_CLICK, BASIC_PANELS, BASIC_TREE, BASIC_SOCIAL,
    BASIC_BOARD, BASIC_MARKET, BASIC_GEOMETRY
};

static int add_click(WUCapabilities *caps, uint32_t ref) {
    WUCapability cap = {0};
    cap.kind = WF_CLICK;
    cap.ref = cap.wire_target = ref;
    cap.step0 = cap.step1 = 1u;
    return wu_cap_add(caps, cap);
}

static int add_canvas(WUCapabilities *caps, uint32_t ref) {
    WUCapability cap = {0};
    cap.kind = WF_CLICK;
    cap.ref = cap.wire_target = ref;
    cap.flags = WU_CAP_RANGE;
    /* DrawWire.fixed_point divides both transport coordinates by 256.
     * Bounds are inclusive, and the smallest transport step is 1/256 px. */
    cap.max0 = 150u * 256u;
    cap.max1 = 130u * 256u;
    cap.step0 = cap.step1 = 1u;
    cap.unit0 = cap.unit1 = WU_UNIT_FIXED_PIXEL;
    return wu_cap_add(caps, cap);
}

static void require_node(const WFView *view, WUCapabilities *caps,
                         uint32_t ref) {
    if (!wu_node(view, ref)) caps->incomplete = 1u;
}

int wu_caps_basic(const WFFamily *family, uint32_t task,
                  const WFView *view, WUCapabilities *caps) {
    unsigned group, limit;
    if (!family || !family->family) return -1;
    if (!strcmp(family->family, "click")) {
        group = BASIC_CLICK; limit = 16u;
    } else if (!strcmp(family->family, "panels")) {
        group = BASIC_PANELS; limit = 9u;
    } else if (!strcmp(family->family, "tree")) {
        group = BASIC_TREE; limit = 1u;
    } else if (!strcmp(family->family, "social")) {
        group = BASIC_SOCIAL; limit = 3u;
    } else if (!strcmp(family->family, "board")) {
        group = BASIC_BOARD; limit = 1u;
    } else if (!strcmp(family->family, "market")) {
        group = BASIC_MARKET; limit = 1u;
    } else if (!strcmp(family->family, "geometry")) {
        group = BASIC_GEOMETRY; limit = 5u;
    } else {
        return 0;
    }
    if (!view || !caps || family->abi_version != WF_ABI_VERSION ||
        view->version != WF_ABI_VERSION || view->count > WF_MAX_NODES ||
        view->text_bytes > WF_TEXT_BYTES || task >= limit ||
        task >= family->task_count) return -1;

    if (view->omitted || view->text_truncated || !view->count)
        caps->incomplete = 1u;
    /* The shared dispatcher supplies the global WAIT, target zero. */

    if (group == BASIC_BOARD)
        for (uint32_t ref = 1u; ref <= 9u; ref++) require_node(view, caps, ref);
    if (group == BASIC_MARKET) require_node(view, caps, 1u);
    if (group == BASIC_GEOMETRY) {
        uint32_t last = task == 3u ? 25u : 2u;
        for (uint32_t ref = 1u; ref <= last; ref++) require_node(view, caps, ref);
    }
    if (group == BASIC_SOCIAL && task != 0u) require_node(view, caps, 1u);

    for (uint32_t i = 0u; i < view->count; i++) {
        const WFNode *node = &view->nodes[i];
        if (group == BASIC_SOCIAL && node->role == WF_TEXT &&
            node->ref >= 26u && node->ref % 16u == 10u) {
            /* A public post header promises four public controls.  Open More
             * promises six additional menu choices; never inspect its goal. */
            uint32_t base = node->ref - 10u;
            for (uint32_t slot = 0u; slot < 4u; slot++)
                require_node(view, caps, base + slot);
            const WFNode *more = wu_node(view, base + 3u);
            if (task == 0u && more && (more->flags & WF_EXPANDED))
                for (uint32_t slot = 4u; slot < 10u; slot++)
                    require_node(view, caps, base + slot);
        }
        if (!wu_available(node) || !(node->flags & WF_CLICKABLE)) continue;
        int supported = 0;
        switch (group) {
            case BASIC_CLICK:
                supported = node->ref >= 1u && node->ref <= 16u;
                break;
            case BASIC_PANELS:
                supported = node->ref >= 1u && node->ref <= 128u &&
                    (node->role == WF_TAB || node->role == WF_LINK ||
                     node->role == WF_BUTTON);
                break;
            case BASIC_TREE:
                supported = node->ref >= 1u && node->ref <= 8u &&
                    (node->role == WF_FOLDER || node->role == WF_FILE);
                break;
            case BASIC_SOCIAL: {
                uint32_t post = node->ref / 16u, slot = node->ref % 16u;
                supported = node->role == WF_BUTTON &&
                    ((task != 0u && node->ref == 1u) ||
                     (post >= 1u && post <= (task == 0u ? 9u : 11u) &&
                      slot < (task == 0u ? 10u : 4u)));
                break;
            }
            case BASIC_BOARD: {
                if (node->ref < 1u || node->ref > 9u || node->role != WF_CELL)
                    break;
                const char *value = wf_text_get(view, node->value);
                if (!value) { caps->incomplete = 1u; continue; }
                /* Occupied-cell clicks are accepted by C but leave the Bend
                 * board unchanged.  Empty cells include every losing move. */
                if (node->value.length == 1u && (*value == 'X' || *value == 'O'))
                    continue;
                if (node->value.length) { caps->incomplete = 1u; continue; }
                supported = 1;
                break;
            }
            case BASIC_MARKET:
                supported = node->ref == 1u && node->role == WF_BUTTON;
                break;
            case BASIC_GEOMETRY:
                if (task == 3u) {
                    supported = node->ref >= 1u && node->ref <= 25u &&
                        node->role == WF_BUTTON;
                } else if (node->ref == 1u && node->role == WF_CANVAS) {
                    if (add_canvas(caps, node->ref) < 0) return -1;
                    continue;
                } else {
                    supported = node->ref == 2u && node->role == WF_BUTTON;
                }
                break;
        }
        if (!supported) { caps->incomplete = 1u; continue; }
        if (add_click(caps, node->ref) < 0) return -1;
    }
    return 1;
}
