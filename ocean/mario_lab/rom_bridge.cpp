// Reference measurement and frozen-policy evaluation. Never trains on ROM frames.
#include "../retro/retro.h"
#include "../retro/nes_emu/abstract_file.h"
#include "ml_config.h"
#include "ml_rom_obs.h"
#include "ml_policy_api.h"
#include <filesystem>
#include <fstream>

struct Options {
    std::string mode = "evaluate", model, config, output;
    int episodes = 32, max_frames = 4000, seed = 901, trace_episodes = 4;
    bool deterministic = false;
};
static void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static Options options(int argc, char** argv) {
    Options a;
    for (int i = 1; i < argc; i++) {
        std::string key = argv[i];
        if (key == "--deterministic") { a.deterministic = true; continue; }
        require(i + 1 < argc, "missing option value"); std::string value = argv[++i];
        if (key == "--mode") a.mode = value;
        else if (key == "--model") a.model = value;
        else if (key == "--config") a.config = value;
        else if (key == "--output") a.output = value;
        else if (key == "--episodes") a.episodes = std::stoi(value);
        else if (key == "--max-frames") a.max_frames = std::stoi(value);
        else if (key == "--seed") a.seed = std::stoi(value);
        else if (key == "--trace-episodes") a.trace_episodes = std::stoi(value);
        else throw std::runtime_error("unknown option " + key);
    }
    require(a.mode == "evaluate" || a.mode == "calibrate", "mode must be evaluate or calibrate");
    require(!a.output.empty(), "output directory is required");
    require(a.episodes >= 1 && a.episodes <= 10000 && a.max_frames >= 100 && a.max_frames <= 100000,
        "unsupported episode/frame budget");
    require(a.trace_episodes >= 0 && a.trace_episodes <= a.episodes && a.seed >= 0, "invalid trace count/seed");
    if (a.mode == "evaluate") require(!a.model.empty() && !a.config.empty(), "model and its config are required");
    std::filesystem::create_directories(a.output);
    return a;
}
static std::vector<char> snapshot(Nes_Emu& emu) {
    Nes_State state; emu.save_state(&state); Mem_Writer writer;
    retro_check(state.write(writer)); return {writer.data(), writer.data() + writer.size()};
}
static void reset_rom(Nes_Emu& emu) {
    retro_check(emu.set_cart(&retro_rom().cart, &retro_rom().seed));
    retro_bind_pixels(&emu); emu.load_state(retro_rom().starts[0]->state);
}
static void advance(Nes_Emu& emu, int action) {
    retro_bind_pixels(&emu); retro_check(emu.emulate_frame(retro_action_mask(action)));
    require(emu.error_count() == 0, "unsupported emulator opcode");
}
static FILE* new_file(const std::string& path, const char* mode = "w") {
    require(!std::filesystem::exists(path), "refusing to overwrite an artifact");
    FILE* f = fopen(path.c_str(), mode); require(f != nullptr, "cannot create artifact"); return f;
}
static void save_frame(Nes_Emu& emu, const std::string& path) {
    FILE* f = new_file(path, "wb"); const auto& frame = emu.frame();
    fprintf(f, "P6\n256 240\n255\n");
    for (int y = 0; y < 240; y++) for (int x = 0; x < 256; x++) {
        const auto& c = Nes_Emu::nes_colors[frame.palette[frame.pixels[y * frame.pitch + x]] & (Nes_Emu::color_table_size - 1)];
        unsigned char rgb[] = {c.red, c.green, c.blue}; fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void ram_row(FILE* f, const unsigned char* m, int tick, int action) {
    fprintf(f, "{\"type\":\"frame\",\"tick\":%d,\"action\":%d,\"mask\":%d,"
        "\"x_fp\":%d,\"feet_fp\":%d,\"vx_fp\":%d,\"vy_fp\":%d,"
        "\"vx_fine\":%d,\"grounded\":%d,\"move_state\":%d,\"routine\":%d,"
        "\"gravity\":%d,\"fall_gravity\":%d,\"area_type\":%d,\"size\":%d,"
        "\"bbox\":[%d,%d,%d,%d]}", tick, action, retro_action_mask(action),
        (robs_x(m) + 3) * ML_FP + m[0x400], (((int)m[0xb5] - 1) * 256 + m[0xce] + 32) * ML_FP + m[0x416],
        robs_s8(m[0x57]) * 16, robs_s8(m[0x9f]) * ML_FP + m[0x433], m[0x705], m[0x1d] == 0,
        m[0x1d], m[0xe], m[0x709], m[0x70a], m[0x74e], m[0x754],
        m[0x4ac], m[0x4ad], m[0x4ae], m[0x4af]);
}
static void calibrate(const Options& a) {
    const char* cases[] = {"idle", "run", "walk", "brake", "reverse", "jump_hold", "jump_tap", "run_jump",
        "down_run", "up_run", "both_run"};
    for (const char* name : cases) {
        Nes_Emu emu; reset_rom(emu); FILE* f = new_file(a.output + "/" + name + ".jsonl");
        fprintf(f, "{\"type\":\"header\",\"case\":\"%s\",\"frames\":100,\"start\":\"natural playable 1-1\"}\n", name);
        for (int t = 0; t <= 100; t++) {
            int action = 0;
            if (!strcmp(name, "run")) action = ML_RIGHT | ML_B;
            if (!strcmp(name, "walk")) action = ML_RIGHT;
            if (!strcmp(name, "brake") && t < 32) action = ML_RIGHT | ML_B;
            if (!strcmp(name, "reverse")) action = (t < 32 ? ML_RIGHT : ML_LEFT) | ML_B;
            if (!strcmp(name, "jump_hold") && t < 40) action = ML_A;
            if (!strcmp(name, "jump_tap") && t < 1) action = ML_A;
            if (!strcmp(name, "run_jump")) action = ML_RIGHT | ML_B | (t >= 48 && t < 88 ? ML_A : 0);
            if (!strcmp(name, "down_run")) action = ML_RIGHT | ML_B | (t >= 32 ? ML_DOWN : 0);
            if (!strcmp(name, "up_run")) action = ML_RIGHT | ML_B | (t >= 32 ? 4 : 0);
            if (!strcmp(name, "both_run")) action = ML_RIGHT | ML_B | (t >= 32 ? ML_LEFT : 0);
            ram_row(f, emu.low_mem(), t, action); fputs("\n", f);
            if (t < 100) advance(emu, action);
        }
        fclose(f);
    }
    printf("{\"type\":\"calibration\",\"cases\":11,\"frames_per_case\":100,\"rom_fingerprint\":\"%016llx\"}\n", retro_rom().fingerprint);
}
static void evaluate(const Options& a) {
    Ini ini = {}; puf_ini_load_file(&ini, a.config.c_str());
    MLConfig cfg = ml_config(puf_ini_section(&ini, "env", 0)); cfg.practice_prob = 0; cfg.require_pipe = 0;
    int hidden = (int)puf_ini_get(&ini, "policy", "hidden_size"), layers = (int)puf_ini_get(&ini, "policy", "num_layers");
    void* policy = ml_cpu_load(a.model.c_str(), hidden, layers); require(policy != nullptr, "invalid Mario Lab checkpoint/architecture");
    FILE* results = new_file(a.output + "/episodes.jsonl");
    fprintf(results, "{\"type\":\"header\",\"format\":\"mario_lab_rom_eval_v1\",\"adapter\":4,\"physics_mode\":%d,"
        "\"episodes\":%d,\"max_frames\":%d,\"seed\":%d,\"deterministic\":%s,"
        "\"initial_idle\":\"episode index modulo 32\",\"frames_per_decision\":1,"
        "\"endpoint\":\"first death or 1-1 black-screen RTA split or timeout\"}\n",
        cfg.physics_mode, a.episodes, a.max_frames, a.seed, a.deterministic ? "true" : "false");
    int clears = 0, deaths = 0, timeouts = 0, clear_frames = 0, total_frames = 0, furthest = 0;
    for (int episode = 0; episode < a.episodes; episode++) {
        Nes_Emu emu; reset_rom(emu);
        int idle = episode % 32, max_x = robs_x(emu.low_mem()), flag_frame = -1, visits = 0, returns = 0;
        int unknown_frames = 0, unknown_cell_frames = 0, big_frames = 0;
        unsigned int action_seed = ml_hash((unsigned)a.seed ^ ml_hash((unsigned)episode + 1)); srand(action_seed);
        ml_cpu_reset(policy); MLRomContext context = {}; context.frame_limit = a.max_frames;
        RetroRtaClock clock; retro_rta_reset(&clock, 1, 1, 0);
        std::vector<int> actions;
        for (int t = 0; t < idle; t++) { advance(emu, 0); actions.push_back(0); }
        context.tick = idle;
        std::string prefix = a.output + "/episode_" + std::to_string(episode);
        FILE* trace = nullptr; FILE* observations = nullptr;
        if (episode < a.trace_episodes) { trace = new_file(prefix + ".jsonl"); observations = new_file(prefix + ".obs.f32", "wb"); }
        const char* status = "timeout";
        for (; context.tick < a.max_frames; ) {
            const unsigned char* m = emu.low_mem(); float obs[ML_OBS_SIZE], probabilities[6], entropy;
            MLRomView view; ml_rom_observe(m, &context, &cfg, &view, obs);
            obs[23] = view.big_player ? 0.0f : 1.0f;
            for (float x : obs) require(std::isfinite(x), "nonfinite semantic observation");
            int action = ml_cpu_action(policy, obs, a.deterministic, probabilities, &entropy);
            int policy_action = action;
            if (cfg.physics_mode >= 2 && (action & (ML_LEFT | ML_RIGHT)) == (ML_LEFT | ML_RIGHT))
                action &= ~(ML_LEFT | ML_RIGHT);
            if (view.unknown_enemies) unknown_frames++;
            if (view.unknown_cells) unknown_cell_frames++;
            if (view.big_player) big_frames++;
            if (trace) {
                ram_row(trace, m, context.tick, action); // append diagnostics to the same JSON object
                fseek(trace, -1, SEEK_CUR);
                fprintf(trace, ",\"policy_action\":%d,\"unknown_cells\":%d,\"unknown_enemies\":%d,\"parsed_col\":%d,\"entropy\":%.9g,"
                    "\"button_probs\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],\"raw_grid\":[",
                    policy_action, view.unknown_cells, view.unknown_enemies, view.parsed_col, entropy,
                    probabilities[0], probabilities[1], probabilities[2], probabilities[3], probabilities[4], probabilities[5]);
                for (int j = 0; j < ML_GRID_H * ML_GRID_W; j++) fprintf(trace, "%s%d", j ? "," : "", view.raw_grid[j]);
                fputs("],\"enemies\":[", trace);
                bool first_enemy = true;
                for (int j = 0; j < 5; j++) if (m[0x0f + j]) {
                    fprintf(trace, "%s{\"id\":%d,\"state\":%d,\"x\":%d,\"feet\":%d}", first_enemy ? "" : ",",
                        m[0x16 + j], m[0x1e + j], m[0x6e + j] * 256 + m[0x87 + j] + 2,
                        ((int)m[0xb6 + j] - 1) * 256 + m[0xcf + j] + 24);
                    first_enemy = false;
                }
                fputs("]}\n", trace); fwrite(obs, sizeof(float), ML_OBS_SIZE, observations);
                if (context.tick == idle + 1) save_frame(emu, prefix + "_start.ppm");
                if (context.tick % 120 == 0 && context.tick > idle)
                    save_frame(emu, prefix + "_frame_" + std::to_string(context.tick) + ".ppm");
            }
            int old_routine = m[0xe], old_timer = m[0x7a0], old_mode = m[0x770], old_type = m[0x74e];
            int old_grounded = m[0x1d] == 0, old_vy = robs_s8(m[0x9f]);
            actions.push_back(action); advance(emu, action); context.tick++; context.previous_action = action;
            m = emu.low_mem(); max_x = std::max(max_x, robs_x(m));
            if (m[0x74e] == 2 && old_type != 2) visits++;
            if (m[0x74e] != 2 && old_type == 2) returns++;
            context.pipe_visits = visits; context.pipe_returns = returns;
            if (old_grounded && m[0x1d] != 0 && robs_s8(m[0x9f]) < 0) context.jump_frames = 1;
            else if (old_vy < 0) context.jump_frames++;
            if (m[0xe] < 4) context.jump_frames = 0;
            if (robs_flagget(m) && flag_frame < 0) flag_frame = context.tick;
            bool split = retro_rta_update(&clock, context.tick, m, old_routine, old_timer, old_mode);
            if (split && clock.split_level == 0) { status = "clear"; break; }
            if (robs_dying(m) || robs_dead(m) || robs_gameover(m)) { status = "death"; break; }
        }
        if (trace) { fclose(trace); fclose(observations); save_frame(emu, prefix + "_end.ppm"); }
        auto final = snapshot(emu);
        Nes_Emu replay; reset_rom(replay);
        for (int action : actions) advance(replay, action);
        require(final == snapshot(replay), "action tape does not reproduce the complete emulator state");
        bool clear = !strcmp(status, "clear"); clears += clear; deaths += !strcmp(status, "death"); timeouts += !strcmp(status, "timeout");
        if (clear) clear_frames += context.tick;
        total_frames += context.tick; furthest = std::max(furthest, max_x);
        fprintf(results, "{\"type\":\"episode\",\"episode\":%d,\"status\":\"%s\",\"frames\":%d,"
            "\"policy_decisions\":%d,\"idle_frames\":%d,\"action_seed\":%u,\"max_x\":%d,\"final_x\":%d,"
            "\"final_feet\":%d,\"routine\":%d,\"flag_frame\":%d,\"pipe_visits\":%d,\"pipe_returns\":%d,"
            "\"unknown_enemy_frames\":%d,\"unknown_cell_frames\":%d,\"big_player_frames\":%d,\"replay_verified\":true,\"actions\":[",
            episode, status, context.tick, context.tick - idle, idle, action_seed, max_x, robs_x(emu.low_mem()),
            (((int)emu.low_mem()[0xb5] - 1) * 256 + emu.low_mem()[0xce] + 32), emu.low_mem()[0xe], flag_frame, visits, returns,
            unknown_frames, unknown_cell_frames, big_frames);
        for (size_t j = 0; j < actions.size(); j++) fprintf(results, "%s%d", j ? "," : "", actions[j]);
        fputs("]}\n", results); fflush(results);
        printf("episode %d: %s, %d frames, max X %d, pipes %d/%d\n", episode, status, context.tick, max_x, visits, returns); fflush(stdout);
    }
    fprintf(results, "{\"type\":\"summary\",\"episodes\":%d,\"clears\":%d,\"deaths\":%d,\"timeouts\":%d,"
        "\"mean_clear_frames\":%.9g,\"total_frames\":%d,\"furthest_x\":%d}\n",
        a.episodes, clears, deaths, timeouts, clears ? (double)clear_frames / clears : 0, total_frames, furthest);
    fclose(results); ml_cpu_free(policy); puf_ini_free(&ini);
}
int main(int argc, char** argv) {
    try {
        Options a = options(argc, argv); RetroRom& rom = retro_rom();
        retro_load_rom_locked(rom, RETRO_SMB1_ROM_PATH); retro_prepare_start_locked(rom, 1, 1);
        if (a.mode == "calibrate") calibrate(a); else evaluate(a);
        return 0;
    } catch (const std::exception& error) { fprintf(stderr, "rom_bridge: %s\n", error.what()); return 1; }
}
