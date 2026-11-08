// The binding's per-tick reward (puf_step in cretaceous.h) split into its
// terms, for the eval harness and the reward audit. Include after
// cretaceous.h. test_binding checks every tick that the terms sum to the
// reward the binding hands the trainer, so a change to puf_step that is not
// mirrored here fails `make test`.
#ifndef CRET_REWARD_TERMS_H
#define CRET_REWARD_TERMS_H

enum { RT_ALIVE, RT_FOOD, RT_WATER, RT_DEALT, RT_TAKEN, RT_KILL, RT_DEATH, RT_GROWTH, RT_COUNT };
static const char* const RT_NAMES[RT_COUNT] = {"alive", "food", "water", "dealt", "taken", "kill", "death", "growth"};

// Terms after a cret_sim_step; growth0 is the agent's growth before the step.
static inline void cret_reward_terms(const RewardW* w, const CretAgent* a, float growth0, float* t) {
    bool grew = a->alive && !a->ev_died && a->growth > growth0;
    t[RT_ALIVE] = a->alive ? w->alive : 0;
    t[RT_FOOD] = w->food * a->ev_food;
    t[RT_WATER] = w->water * a->ev_water;
    t[RT_DEALT] = w->dealt * a->ev_dealt;
    t[RT_TAKEN] = -w->taken * a->ev_taken;
    t[RT_KILL] = w->kill * a->ev_kills;
    t[RT_DEATH] = a->ev_died ? -w->death : 0;
    t[RT_GROWTH] = grew ? w->growth * (a->growth - growth0) : 0;
}
static inline float cret_reward_sum(const float* t) { float r = 0; for (int k = 0; k < RT_COUNT; k++) r += t[k]; return r; }
#endif
