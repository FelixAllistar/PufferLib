#pragma once
#include "ml_sim.h"

static double ml_option(Dict* d, const char* key, double fallback) {
    DictItem* i = dict_find(d, key); return i ? i->value : fallback;
}
static MLConfig ml_config(Dict* d) {
    MLConfig c = ml_default_config();
#define ML_CFG_INT(name) do { double v = ml_option(d, #name, c.name); \
    if (!isfinite(v) || v != floor(v) || v < 0 || v > 2147483647) { \
        fprintf(stderr, "mario_lab: invalid integer %s\n", #name); abort(); } c.name = (int)v; } while (0)
    ML_CFG_INT(course); ML_CFG_INT(difficulty); ML_CFG_INT(length);
    ML_CFG_INT(max_frames); ML_CFG_INT(require_pipe); ML_CFG_INT(seed); ML_CFG_INT(split);
    ML_CFG_INT(accel); ML_CFG_INT(friction); ML_CFG_INT(walk_speed); ML_CFG_INT(run_speed);
    ML_CFG_INT(jump_speed); ML_CFG_INT(gravity_hold); ML_CFG_INT(gravity_release);
    ML_CFG_INT(max_fall); ML_CFG_INT(hold_frames);
    ML_CFG_INT(practice_gap); ML_CFG_INT(practice_entry); ML_CFG_INT(practice_exit);
    ML_CFG_INT(practice_route); ML_CFG_INT(practice_frames);
    ML_CFG_INT(practice_short_goals);
    ML_CFG_INT(mix_flat); ML_CFG_INT(mix_gaps); ML_CFG_INT(mix_pipes); ML_CFG_INT(mix_stairs);
    ML_CFG_INT(mix_walkers); ML_CFG_INT(mix_underground); ML_CFG_INT(mix_composite);
    ML_CFG_INT(physics_mode); ML_CFG_INT(walk_accel); ML_CFG_INT(brake_accel);
    ML_CFG_INT(player_width); ML_CFG_INT(player_height); ML_CFG_INT(jump_fast_speed);
    ML_CFG_INT(jump_fast_threshold); ML_CFG_INT(gravity_fast_hold); ML_CFG_INT(gravity_fast_release);
    ML_CFG_INT(pipe_min_height); ML_CFG_INT(pipe_max_height); ML_CFG_INT(stair_height);
    ML_CFG_INT(generator_mode); ML_CFG_INT(length_min); ML_CFG_INT(section_min); ML_CFG_INT(section_max);
    ML_CFG_INT(spacing_min); ML_CFG_INT(spacing_max);
#undef ML_CFG_INT
#define ML_CFG_FLOAT(name) do { double v = ml_option(d, #name, c.name); \
    if (!isfinite(v) || v < 0 || v > 100) { fprintf(stderr, "mario_lab: invalid %s\n", #name); abort(); } c.name = (float)v; } while (0)
    ML_CFG_FLOAT(completion_reward); ML_CFG_FLOAT(death_penalty);
    ML_CFG_FLOAT(speed_bonus); ML_CFG_FLOAT(progress_reward);
    ML_CFG_FLOAT(practice_prob);
#undef ML_CFG_FLOAT
    if (c.course > ML_COMPOSITE || c.difficulty > 2 || c.length < 48 || c.length > ML_MAX_WIDTH
        || c.generator_mode > 1 || (!c.generator_mode && (c.length > ML_WIDTH || c.length_min))
        || (c.length_min && (c.length_min < 48 || c.length_min > c.length))
        || c.section_min < 24 || c.section_max < c.section_min || c.section_max > ML_MAX_WIDTH
        || c.spacing_min < 6 || c.spacing_max < c.spacing_min || c.spacing_max > 64
        || c.max_frames < 1 || c.max_frames > 100000 || c.require_pipe > 1 || c.split > 2
        || c.accel < 1 || c.accel > 256 || c.friction < 1 || c.friction > 256
        || c.walk_speed < 1 || c.walk_speed > c.run_speed || c.run_speed > 4 * ML_FP
        || c.jump_speed < ML_FP || c.jump_speed > 8 * ML_FP
        || c.gravity_hold < 1 || c.gravity_hold > c.gravity_release || c.gravity_release > 2 * ML_FP
        || c.max_fall < ML_FP || c.max_fall > 8 * ML_FP || c.hold_frames > 60
        || c.practice_prob > 1 || c.practice_frames < 25 || c.practice_frames > 100000
        || c.practice_gap > 100 || c.practice_entry > 100 || c.practice_exit > 100 || c.practice_route > 100
        || c.practice_short_goals > 1
        || c.physics_mode > 2 || c.walk_accel < 1 || c.walk_accel > 256 || c.brake_accel < 1 || c.brake_accel > 256
        || c.player_width < 8 || c.player_width > 16 || c.player_height < 8 || c.player_height > 32
        || c.jump_fast_speed < ML_FP || c.jump_fast_speed > 8 * ML_FP || c.jump_fast_threshold > 4 * ML_FP
        || c.gravity_fast_hold < 1 || c.gravity_fast_hold > c.gravity_fast_release || c.gravity_fast_release > 2 * ML_FP
        || c.pipe_min_height > 5 || c.pipe_max_height > 5
        || (c.pipe_min_height && !c.pipe_max_height) || c.pipe_min_height > c.pipe_max_height
        || c.stair_height == 1 || c.stair_height > 8
        || c.mix_flat > 100 || c.mix_gaps > 100 || c.mix_pipes > 100 || c.mix_stairs > 100
        || c.mix_walkers > 100 || c.mix_underground > 100 || c.mix_composite > 100
        || (!c.course && !(c.mix_flat + c.mix_gaps + c.mix_pipes + c.mix_stairs + c.mix_walkers + c.mix_underground + c.mix_composite))
        || (c.practice_prob > 0 && !(c.practice_gap + c.practice_entry + c.practice_exit + c.practice_route))) {
        fprintf(stderr, "mario_lab: configuration outside supported controller/generator bounds\n"); abort();
    }
    return c;
}
