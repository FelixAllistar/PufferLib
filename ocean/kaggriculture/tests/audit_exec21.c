/* Offline exhaustive coverage test. Compile against archived 6abfb8dd4 headers,
 * never link this file into the trainer. Effects ignore worker identity but
 * retain operation, crop/species and quadrant. */
#include "kag_experiment_view.c"

#define AUDIT_EFFECTS (18 * 13 * 4)

static void audit_effects(const KGState* state, int p, const KGAction* action,
        int* out) {
    KGState copy = *state;
    KGPlayer* f = &copy.players[p];
    int blocked[KG_NUM_CROPS];
    kg_validate_plant_atomic(action, f, blocked);
    memset(out, 0, AUDIT_EFFECTS * sizeof(int));
    for (int u = 0; u < f->unit_count && u <= action->hand_count; u++) {
        const KGUnitAction* a = u ? &action->hands[u - 1] : &action->farmer;
        int index = kg_tile_index(f->units[u].x, f->units[u].y);
        KGTile before = f->tiles[index];
        if (a->op == KG_OP_PLANT && (unsigned)a->arg < KG_NUM_CROPS && blocked[a->arg]) {
            continue;
        }
        kg_apply_unit_action(&copy, f, u, a);
        if (a->op != KG_OP_PLANT && a->op != KG_OP_BUILD_COOP &&
                a->op != KG_OP_BUILD_PASTURE && a->op != KG_OP_DIG &&
                a->op != KG_OP_FERTILIZE && a->op != KG_OP_PLACE) {
            continue;
        }
        if (memcmp(&before, &f->tiles[index], sizeof(before)) == 0) {
            continue;
        }
        int arg = (a->op == KG_OP_PLANT || a->op == KG_OP_PLACE) ? a->arg + 1 : 0;
        int q = (index % KG_MAX_BOARD_SIZE >= 5) + 2 * (index / KG_MAX_BOARD_SIZE >= 5);
        out[(a->op * 13 + arg) * 4 + q]++;
    }
}

void audit_measure(const KGState* state, int p, const KGAction* teacher,
        const KGAction* candidate, int* out) {
    int wanted[AUDIT_EFFECTS], got[AUDIT_EFFECTS];
    audit_effects(state, p, teacher, wanted);
    audit_effects(state, p, candidate, got);
    memset(out, 0, 3 * sizeof(int));
    for (int i = 0; i < AUDIT_EFFECTS; i++) {
        out[0] += wanted[i];
        out[1] += got[i] < wanted[i] ? got[i] : wanted[i];
        out[2] += got[i] > wanted[i] ? got[i] - wanted[i] : 0;
    }
}

/* Output: wanted effects, best matched, best extra, candidates, exact-work
 * attainable, exact-work-and-market-commands attainable, chosen macro/q/region.
 * Search all legal single-intent requests, including their auto chores. */
void audit_exec21(const KGState* state, int p, const KGAction* teacher, int* out) {
    Env* env = calloc(1, sizeof(*env));
    env->game_storage = *state;
    env->policy_max_hands = KG_MAX_HANDS;
    int wanted[AUDIT_EFFECTS], got[AUDIT_EFFECTS];
    audit_effects(state, p, teacher, wanted);
    memset(out, 0, 9 * sizeof(int));
    for (int i = 0; i < AUDIT_EFFECTS; i++) out[0] += wanted[i];
    int best = INT_MAX;
    for (int m = 0; m < 37; m++) {
        if (!kag_explicit_legal(env, p, m)) continue;
        for (int q = 0; q < 8; q++) {
            for (int t = 0; t < 5; t++) {
                int target = kag_macro_target_from_bin(t);
                if (target && !(state->players[p].unlocked_mask & target)) continue;
                KGAction action = {0};
                kag_explicit_action(state, p, m, kag_macro_quantity_from_bin(q),
                    target, KG_MAX_HANDS, &action);
                audit_effects(state, p, &action, got);
                int matched = 0, extra = 0;
                for (int i = 0; i < AUDIT_EFFECTS; i++) {
                    matched += got[i] < wanted[i] ? got[i] : wanted[i];
                    extra += got[i] > wanted[i] ? got[i] - wanted[i] : 0;
                }
                out[3]++;
                int exact = matched == out[0] && extra == 0;
                out[4] |= exact;
                out[5] |= exact && action.market_count == teacher->market_count &&
                    !memcmp(action.market, teacher->market,
                        action.market_count * sizeof(KGMarketOrder));
                int loss = (out[0] - matched) * 1000 + extra;
                if (loss < best) {
                    best = loss;
                    out[1] = matched;
                    out[2] = extra;
                    out[6] = m;
                    out[7] = q;
                    out[8] = t;
                }
            }
        }
    }
    free(env);
}
