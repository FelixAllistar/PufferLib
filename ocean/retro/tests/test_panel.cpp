#include <cassert>
#include "../retro_sweep_config.h"

int main() {
    float logits[65] = {};
    logits[64] = 1000; // Value is not an action.
    assert(retro_panel_action(logits, 0, true) == 0);
    for (unsigned i = 0; i < 64; i++) {
        assert(retro_panel_action(logits, i*67108864u, false) == (int)i);
    }
    logits[17] = 2;
    assert(retro_panel_action(logits, 0, true) == 17);
    assert(retro_panel_objective("speed", 0, 4, 1, 3400, 0, 1800) == -1);
    double last = retro_panel_objective("speed", 1, 4, 0, 0, 1800, 1800, 1800);
    assert(last > -1);
    double faster = retro_panel_objective("speed", 1, 4, 0, 0, 1700, 1800, 1600);
    double slower = retro_panel_objective("speed", 4, 4, 1, 3400, 1601, 1800, 1601);
    assert(faster > slower); // One-frame best beats mean-time/clear-count ties.
    assert(retro_panel_score(1, 4, 0) > retro_panel_score(0, 4, 1));
    assert(retro_panel_distance(200, 100) == 0);
    assert(retro_panel_progress(200, 3600) == 1);
    puts("PASS panel action layout, seeded sampling and objective ordering");
}
