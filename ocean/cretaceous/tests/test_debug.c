// Developer tooling: the console's command parser and completion (cheats.c),
// the sim cheats and debug actors, the scenario format and a headless
// scenario run (scenario.c), and the population director (population.c).
// No window or GPU. Runs from the repository root on the tiny test world.
//   make -C ocean/cretaceous test     (also under ASan + UBSan in make asan)
#include "test_util.h"
#include "../cheats.h"
#include "../scenario.h"
#include "../population.h"

// ---------------------------------------------------------------- parser
static void test_parser(void) {
    CretCmd c; char err[160];
    CHECK(cret_cmd_parse("spawn stego passive 3", &c, err, sizeof err) && c.id == CMD_SPAWN && c.species == CRET_SP_STEGO && c.actor == CRET_ACTOR_PASSIVE && c.count == 3, "spawn stego passive 3: %s", err);
    CHECK(cret_cmd_parse("SPAWN Raptor", &c, err, sizeof err) && c.species == CRET_SP_RAPTOR && c.actor == CRET_ACTOR_DUMMY && c.count == 1, "spawn defaults: %s", err);
    CHECK(cret_cmd_parse("spawn random 2 hostile", &c, err, sizeof err) && c.species == -2 && c.actor == CRET_ACTOR_HOSTILE && c.count == 2, "spawn random 2 hostile: %s", err);
    CHECK(cret_cmd_parse("spawn t-rex bot", &c, err, sizeof err) && c.species == CRET_SP_TREX && c.actor == CRET_ACTOR_BOT, "t-rex alias: %s", err);
    CHECK(cret_cmd_parse("  tp   100 -20.5 ", &c, err, sizeof err) && c.id == CMD_TP && c.num[0] == 100 && c.num[1] == -20.5f && c.species == -1, "tp x z: %s", err);
    CHECK(cret_cmd_parse("tp nearest gallimimus", &c, err, sizeof err) && c.species == CRET_SP_GALLI, "tp nearest: %s", err);
    CHECK(cret_cmd_parse("time 13.5", &c, err, sizeof err) && c.num[0] == 13.5f, "time: %s", err);
    CHECK(cret_cmd_parse("speed 2x", &c, err, sizeof err) && c.num[0] == 2, "speed 2x: %s", err);
    CHECK(cret_cmd_parse("grow 0.5", &c, err, sizeof err) && c.num[0] == 0.5f, "grow: %s", err);
    CHECK(cret_cmd_parse("weather rain", &c, err, sizeof err) && c.id == CMD_WEATHER, "weather: %s", err);
    CHECK(cret_cmd_parse("god", &c, err, sizeof err) && c.toggle == -1, "god toggles: %s", err);
    CHECK(cret_cmd_parse("god on", &c, err, sizeof err) && c.toggle == 1, "god on: %s", err);
    CHECK(cret_cmd_parse("fog off", &c, err, sizeof err) && c.id == CMD_FOG && c.toggle == 0, "fog off: %s", err);
    CHECK(cret_cmd_parse("shadows", &c, err, sizeof err) && c.id == CMD_SHADOWS && c.argc == 0, "shadows cycle: %s", err);
    CHECK(cret_cmd_parse("preset low", &c, err, sizeof err) && c.id == CMD_PRESET, "preset: %s", err);
    CHECK(cret_cmd_parse("help spawn", &c, err, sizeof err) && c.id == CMD_HELP, "help spawn: %s", err);
    CHECK(cret_cmd_parse("kill all", &c, err, sizeof err) && c.id == CMD_KILL, "kill all: %s", err);
    CHECK(CRET_CMDS[CMD_SPAWN].cheat && !CRET_CMDS[CMD_FOG].cheat && CRET_CMDS[CMD_FOG].view && !CRET_CMDS[CMD_SPAWN].view, "cheat and view flags");
    for (int k = 0; k < CMD_COUNT; k++) CHECK(CRET_CMDS[k].name && CRET_CMDS[k].usage && CRET_CMDS[k].help, "command %d has a name, usage and help", k);
    // Rejected: unknown words, out of range, wrong arity, overlong input.
    static const char* bad[] = {"", "   ", "fly", "spawn", "spawn dodo", "spawn stego 17", "spawn stego 0", "spawn stego 2.5", "spawn stego dummy 2 extra",
                                "tp", "tp 1", "tp nearest", "tp nearest dodo", "tp a b", "time 25", "time -1", "time noon", "speed 0", "speed 9", "speed fast",
                                "grow 1.5", "weather snow", "god maybe", "preset ultra", "shadows max", "kill everyone", "heal now", "help fly",
                                "spawn aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "a b c d e f g"};
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) { err[0] = 0; CHECK(!cret_cmd_parse(bad[k], &c, err, sizeof err) && err[0], "'%s' should be rejected with a message", bad[k]); }
    CHECK(!cret_cmd_parse(NULL, &c, NULL, 0), "NULL line, NULL err");
    // Species names.
    CHECK(cret_species_by_name("Velociraptor") == CRET_SP_RAPTOR && cret_species_by_name("rex") == CRET_SP_TREX && cret_species_by_name("RANDOM") == -2 &&
          cret_species_by_name("dodo") == -1 && cret_species_by_name("") == -1 && cret_species_by_name(NULL) == -1, "species by name");
}

static void test_complete(void) {
    char out[160], list[32][24];
    int n = cret_cmd_complete("spa", out, sizeof out, list, 32);
    CHECK(n == 1 && !strcmp(out, "spawn "), "spa -> '%s' (%d)", out, n);
    n = cret_cmd_complete("sp", out, sizeof out, list, 32);
    CHECK(n == 3 && !strcmp(out, "sp"), "sp: spawn, species, speed -> '%s' (%d)", out, n);
    n = cret_cmd_complete("s", out, sizeof out, list, 32);
    CHECK(n >= 4 && !strcmp(out, "s"), "s -> '%s' (%d)", out, n);
    n = cret_cmd_complete("spawn st", out, sizeof out, list, 32);
    CHECK(n == 1 && !strcmp(out, "spawn stego "), "spawn st -> '%s'", out);
    n = cret_cmd_complete("spawn stego p", out, sizeof out, list, 32);
    CHECK(n == 1 && !strcmp(out, "spawn stego passive "), "spawn stego p -> '%s'", out);
    n = cret_cmd_complete("weather ", out, sizeof out, list, 32);
    CHECK(n == 4 && !strcmp(out, "weather "), "weather <tab> lists 4 -> '%s' (%d)", out, n);
    n = cret_cmd_complete("tp nearest t", out, sizeof out, list, 32);
    CHECK(n == 1 && !strcmp(out, "tp nearest trex "), "tp nearest t -> '%s'", out);
    n = cret_cmd_complete("", out, sizeof out, NULL, 0);
    CHECK(n == CMD_COUNT, "empty line completes every command (%d)", n);
    n = cret_cmd_complete("zz", out, sizeof out, list, 32);
    CHECK(n == 0 && !strcmp(out, "zz"), "no match keeps the line");
    n = cret_cmd_complete("time 1", out, sizeof out, list, 32);
    CHECK(n == 0 && !strcmp(out, "time 1"), "numbers are not completed");
}

// ---------------------------------------------------------------- cheats
static int alive_count(const CretSim* s) { int n = 0; for (int i = 0; i < s->cfg.num_agents; i++) n += s->agents[i].alive; return n; }

static void step(CretSim* s, CretCheats* ch, int n) {
    CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < n; t++) {
        for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
        in[0] = (CretInput){0};
        cret_cheats_inputs(ch, s, in, 0); cret_sim_step(s, in); cret_cheats_after_step(ch, s, 0);
    }
}

static void test_cheats(void) {
    CretConfig cfg = t_config(16); cfg.player_growth = 1; cfg.respawn_ticks = 1 << 20; cfg.seed = 7;
    CretSim* s = calloc(1, sizeof *s); CHECK(cret_sim_init(s, cfg) == 0, "sim init");
    CretPop pop; cret_pop_init(&pop, s, false);
    CretCheats ch; cret_cheats_init(&ch, &pop, 3);
    CretAgent* me = &s->agents[0];
    CretCheatEnv env = {.me = 0, .authority = false, .target = -1, .cam_yaw = me->yaw};
    CretCmd c; char out[200];
    // Clients may not cheat.
    cret_cmd_parse("spawn stego", &c, NULL, 0);
    CHECK(!cret_cheat_exec(&ch, s, &env, &c, out, sizeof out) && strstr(out, "not allowed"), "client spawn refused: %s", out);
    cret_cmd_parse("god", &c, NULL, 0);
    CHECK(!cret_cheat_exec(&ch, s, &env, &c, out, sizeof out) && !ch.god, "client god refused");
    cret_cmd_parse("pop", &c, NULL, 0);
    CHECK(cret_cheat_exec(&ch, s, &env, &c, out, sizeof out), "pop is information, allowed for anyone: %s", out);
    cret_cmd_parse("labels", &c, NULL, 0);
    CHECK(!cret_cheat_exec(&ch, s, &env, &c, out, sizeof out), "view commands are debug.c's");
    env.authority = true;
    // A dummy 8 m ahead (no aim) stands still and never attacks, even next to the player.
    cret_cmd_parse("spawn stego dummy", &c, NULL, 0);
    CHECK(cret_cheat_exec(&ch, s, &env, &c, out, sizeof out), "spawn: %s", out);
    int d = -1; for (int i = 1; i < s->cfg.num_agents; i++) if (ch.actor[i] == CRET_ACTOR_DUMMY) d = i;
    CHECK(d > 0 && s->agents[d].alive && s->agents[d].species == CRET_SP_STEGO && pop.exempt[d], "dummy slot %d", d);
    if (d > 0) {
        float dd = hypotf(s->agents[d].x - me->x, s->agents[d].z - me->z);
        CHECK(dd < 30, "dummy spawned near the player (%.1f m)", dd);
        float x0 = s->agents[d].x, z0 = s->agents[d].z; int dealt = 0;
        uint32_t h0 = s->hit_count;
        step(s, &ch, 150);
        for (uint32_t k = h0; k < s->hit_count; k++) if (s->hits[k % CRET_HIT_LOG].attacker == d) dealt++;
        CHECK(hypotf(s->agents[d].x - x0, s->agents[d].z - z0) < 1.0f, "dummy stood still (moved %.2f m)", hypotf(s->agents[d].x - x0, s->agents[d].z - z0));
        CHECK(dealt == 0, "dummy never attacked (%d hits)", dealt);
        CHECK(s->agents[d].food >= 0.9f, "debug actors stay fed");
    }
    // Passive: wanders slowly; after a blow it runs away from the attacker.
    env.has_aim = true; env.aim[0] = me->x + 15; env.aim[2] = me->z; env.aim[1] = 0;
    cret_cmd_parse("spawn galli passive", &c, NULL, 0);
    CHECK(cret_cheat_exec(&ch, s, &env, &c, out, sizeof out), "spawn passive: %s", out);
    int p = -1; for (int i = 1; i < s->cfg.num_agents; i++) if (ch.actor[i] == CRET_ACTOR_PASSIVE) p = i;
    CHECK(p > 0, "passive slot");
    if (p > 0) {
        CHECK(hypotf(s->agents[p].x - env.aim[0], s->agents[p].z - env.aim[2]) < 12, "spawned at the crosshair (%.1f m off; at %.0f %.0f, aim %.0f %.0f)", hypotf(s->agents[p].x - env.aim[0], s->agents[p].z - env.aim[2]), s->agents[p].x, s->agents[p].z, env.aim[0], env.aim[2]);
        step(s, &ch, 60);
        CHECK(s->agents[p].speed < 4.0f, "passive ambles (%.1f m/s)", s->agents[p].speed);
        float d0 = hypotf(s->agents[p].x - me->x, s->agents[p].z - me->z);
        s->agents[p].hurt_ticks = 12; s->agents[p].last_attacker = 0;   // as if the player had struck it
        step(s, &ch, 90);
        float d1 = hypotf(s->agents[p].x - me->x, s->agents[p].z - me->z);
        CHECK(d1 > d0 + 10, "passive fled after a blow (%.0f -> %.0f m)", d0, d1);
    }
    // Hostile: closes on the player and attacks.
    env.has_aim = false;
    cret_cmd_parse("spawn raptor hostile", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    int hstl = -1; for (int i = 1; i < s->cfg.num_agents; i++) if (ch.actor[i] == CRET_ACTOR_HOSTILE) hstl = i;
    CHECK(hstl > 0, "hostile slot");
    cret_cmd_parse("god on", &c, NULL, 0); CHECK(cret_cheat_exec(&ch, s, &env, &c, out, sizeof out) && ch.god, "god on");
    uint32_t h1 = s->hit_count; step(s, &ch, 300);
    int on_me = 0; for (uint32_t k = h1; k < s->hit_count; k++) if (s->hits[k % CRET_HIT_LOG].attacker == hstl && s->hits[k % CRET_HIT_LOG].victim == 0) on_me++;
    CHECK(on_me > 0, "hostile raptor struck the player (%d hits in 10 s)", on_me);
    CHECK(me->alive && me->health == me->max_health, "god mode kept the player whole");
    // Freeze: animals stop.
    cret_cmd_parse("freeze on", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    step(s, &ch, 30);
    float moving = 0; for (int i = 1; i < s->cfg.num_agents; i++) if (s->agents[i].alive) moving = fmaxf(moving, s->agents[i].speed);
    CHECK(moving < 1.0f, "frozen animals stand (fastest %.2f m/s)", moving);
    cret_cmd_parse("freeze off", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    // tp, time, weather, grow, species, speed.
    float tx = s->world->size * 0.5f + 40, tz = s->world->size * 0.5f - 30; char line[64];
    snprintf(line, sizeof line, "tp %.0f %.0f", tx, tz); cret_cmd_parse(line, &c, NULL, 0);
    env.cut = false; CHECK(cret_cheat_exec(&ch, s, &env, &c, out, sizeof out) && env.cut, "tp: %s", out);
    CHECK(hypotf(me->x - tx, me->z - tz) < 30, "tp landed near the point (%.1f m off)", hypotf(me->x - tx, me->z - tz));
    cret_cmd_parse("time 18", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    CHECK(fabsf(s->time_of_day - 0.75f) < 1e-4f, "time 18");
    cret_cmd_parse("weather rain", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    CHECK(s->cloud_cover > 0.9f && s->cfg.weather == 0, "weather rain fixes the cover");
    cret_cmd_parse("grow 0.5", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    CHECK(fabsf(me->growth - 0.5f) < 1e-4f, "grow 0.5");
    float px = me->x, pz = me->z;
    cret_cmd_parse("species trex", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    CHECK(me->species == CRET_SP_TREX && me->alive && hypotf(me->x - px, me->z - pz) < 15 && pop.keep[0], "species trex where it stood (%.1f m)", hypotf(me->x - px, me->z - pz));
    cret_cmd_parse("speed 4", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    CHECK(ch.speed == 4, "speed");
    // kill: the target, then everything; dead debug actors go back to the normal mix.
    env.target = hstl; cret_cmd_parse("kill", &c, NULL, 0);
    CHECK(cret_cheat_exec(&ch, s, &env, &c, out, sizeof out) && !s->agents[hstl].alive, "kill target: %s", out);
    step(s, &ch, 1);
    CHECK(ch.actor[hstl] == CRET_ACTOR_NONE && s->species_pick[hstl] == 0 && !pop.exempt[hstl], "a dead debug actor is released");
    cret_cmd_parse("kill all", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    CHECK(alive_count(s) == 1 && me->alive, "kill all leaves only the player (%d alive)", alive_count(s));
    // Spawn when every slot is taken by debug actors: no slot, a message.
    cret_cmd_parse("spawn pachy dummy 16", &c, NULL, 0); cret_cheat_exec(&ch, s, &env, &c, out, sizeof out);
    int dummies = 0; for (int i = 0; i < s->cfg.num_agents; i++) dummies += ch.actor[i] == CRET_ACTOR_DUMMY;
    CHECK(dummies == s->cfg.num_agents - 1, "16 asked, %d slots (%d made)", s->cfg.num_agents - 1, dummies);
    CHECK(!cret_cheat_exec(&ch, s, &env, &c, out, sizeof out) && strstr(out, "no free slot"), "full: %s", out);
    cret_sim_close(s); free(s);
}

// ---------------------------------------------------------------- scenarios
static const char* SCN_OK =
    "# comment\n[scenario]\ntitle = A Test Duel\nseconds = 12\nhour = 9.5\nweather = overcast\nplace = open\ncamera = orbit\nend = death target ; trailing\n"
    "[player]\nspecies = raptor\nat = -12 0\nfacing = target\ncontrol = attack target\nabilities = on\n"
    "[actor target]\nspecies = stego\nat = 2 0\nfacing = 180\nbehaviour = dummy\n"
    "[actor herd]\nspecies = galli\ncount = 4\nspread = 10\nat = 40 30\nbehaviour = bot\n"
    "[actor runner]\nspecies = pachy\nat = -30 20\ncontrol = hitrun target 2\n";

static void test_scenario_parse(void) {
    CretScenario sc; char err[200];
    CHECK(cret_scenario_parse(SCN_OK, "t", &sc, err, sizeof err), "parse: %s", err);
    CHECK(!strcmp(sc.title, "a test duel") || !strcmp(sc.title, "A Test Duel"), "title '%s'", sc.title);
    CHECK(sc.seconds == 12 && sc.hour == 9.5f && sc.weather == 1 && sc.open && sc.camera == SCN_CAM_ORBIT && sc.end == SCN_END_DEATH && !strcmp(sc.end_target, "target"), "scenario keys");
    CHECK(sc.player.species == CRET_SP_RAPTOR && sc.player.x == -12 && sc.player.ctrl == SCN_CTRL_ATTACK && !strcmp(sc.player.ctrl_target, "target") && sc.player.abilities, "player keys");
    CHECK(!strcmp(sc.player.face, "target") && isnan(sc.player.facing), "facing an actor");
    CHECK(sc.nactor == 3 && sc.actor[0].behaviour == CRET_ACTOR_DUMMY && sc.actor[0].facing == 180, "dummy actor");
    CHECK(sc.actor[1].count == 4 && sc.actor[1].spread == 10 && sc.actor[1].behaviour == CRET_ACTOR_BOT && sc.actor[1].group, "herd");
    CHECK(sc.actor[2].behaviour == CRET_ACTOR_SCRIPT && sc.actor[2].ctrl == SCN_CTRL_HITRUN && sc.actor[2].p0 == 2, "a control makes an actor scripted");
    CretConfig c = cret_scenario_config(&sc, cret_default_config());
    CHECK(c.num_agents == 1 + 1 + 4 + 1 && c.player_species == CRET_SP_RAPTOR && c.respawn_ticks > 1000000 && c.weather == 0, "config: %d agents", c.num_agents);
    // Errors name the line.
    static const char* bad[][2] = {
        {"seconds = 3\n", "outside a section"}, {"[scenario]\nseconds = 0\n", "seconds"}, {"[scenario]\nhour = 25\n", "hour"},
        {"[scenario]\nweather = snow\n", "weather"}, {"[scenario]\nplace = here\n", "place"}, {"[scenario]\ncamera = fish\n", "camera"},
        {"[scenario]\nend = death nobody\n", "nobody"}, {"[scenario]\nend = sometimes\n", "end"}, {"[scenario]\ncolour = red\n", "unknown key"},
        {"[stage]\n", "unknown section"}, {"[actor]\n", "one-word name"}, {"[actor a b]\n", "one-word"}, {"[actor a]\n[actor a]\n", "twice"},
        {"[actor player]\n", "twice"}, {"[actor a]\nspecies = dodo\n", "species"}, {"[actor a]\ncount = 0\n", "count"}, {"[actor a]\ncount = 17\n", "count"},
        {"[player]\ncount = 2\n", "count"}, {"[actor a]\nat = 1\n", "at X Z"}, {"[actor a]\nat = 1 99999\n", "at X Z"}, {"[actor a]\ngrowth = 2\n", "growth"},
        {"[actor a]\nbehaviour = angry\n", "behaviour"}, {"[actor a]\ncontrol = dance\n", "controller"}, {"[actor a]\ncontrol = walk 1\n", "walk"},
        {"[actor a]\ncontrol = attack\n", "target name"}, {"[actor a]\ncontrol = attack b\n", "not an actor"}, {"[actor a]\ncontrol = circle a 99\n", "radius"},
        {"[actor a]\ncontrol = hitrun a 0\n", "away"}, {"[actor a]\ncontrol = idle now\n", "no arguments"}, {"[actor a]\ntarget = ghost\n", "not an actor"},
        {"[actor a]\nfacing = ghost\n", "neither"}, {"[actor a]\ngod = maybe\n", "god"}, {"[actor a]\nspecies\n", "key = value"}, {"[scenario\n", "bad section"},
    };
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
        err[0] = 0; bool ok = cret_scenario_parse(bad[k][0], "bad", &sc, err, sizeof err);
        CHECK(!ok && strstr(err, bad[k][1]), "'%s' -> '%s' (want '%s')", bad[k][0], err, bad[k][1]);
    }
    // Too many actors and too many animals.
    char big[4096]; int L = 0;
    for (int k = 0; k <= CRET_SCN_MAX_ACTORS; k++) L += snprintf(big + L, sizeof big - L, "[actor a%d]\n", k);
    CHECK(!cret_scenario_parse(big, "big", &sc, err, sizeof err) && strstr(err, "at most"), "too many actors: %s", err);
    L = 0; for (int k = 0; k < 9; k++) L += snprintf(big + L, sizeof big - L, "[actor a%d]\ncount = 16\n", k);
    CHECK(!cret_scenario_parse(big, "big", &sc, err, sizeof err) && strstr(err, "too many animals"), "too many animals: %s", err);
    // A long line, and no text at all (defaults).
    char longl[400]; memset(longl, 'x', sizeof longl - 1); longl[sizeof longl - 1] = 0;
    CHECK(!cret_scenario_parse(longl, "long", &sc, err, sizeof err), "long line rejected");
    CHECK(cret_scenario_parse("", "empty", &sc, err, sizeof err) && sc.nactor == 0 && sc.seconds > 0, "empty text: defaults");
    // Every shipped scenario loads.
    char names[64][48]; int n = cret_scenario_list(names, 64);
    CHECK(n >= 6, "%d scenarios in %s", n, CRET_SCN_DIR);
    for (int k = 0; k < n; k++) { CHECK(cret_scenario_load(names[k], &sc, err, sizeof err), "%s: %s", names[k], err); CHECK(sc.title[0] >= 'A' && sc.title[0] <= 'Z', "%s keeps its title's case ('%s')", names[k], sc.title); }
    CHECK(!cret_scenario_load("no_such_scenario", &sc, err, sizeof err) && strstr(err, "cannot open"), "missing file: %s", err);
}

static void test_scenario_run(void) {
    CretScenario sc; char err[200];
    const char* txt = "[scenario]\nseconds = 25\nend = death target\n[player]\nspecies = raptor\nat = -10 0\nfacing = target\ncontrol = attack target\n"
                      "[actor target]\nspecies = galli\ngrowth = 0.5\nat = 2 0\nbehaviour = dummy\n[actor far]\nspecies = stego\nat = 60 60\nbehaviour = dummy\n";
    CHECK(cret_scenario_parse(txt, "run", &sc, err, sizeof err), "parse: %s", err);
    CretConfig base = t_config(1); base.seed = 5;
    CretConfig c = cret_scenario_config(&sc, base);
    CretSim* s = calloc(1, sizeof *s); CHECK(cret_sim_init(s, c) == 0, "scenario sim");
    CretScnRun* r = calloc(1, sizeof *r);
    CHECK(cret_scenario_start(r, &sc, s), "start");
    CHECK(r->nslot[0] == 1 && r->slot[0][0] == 0 && r->nslot[1] == 1 && r->nslot[2] == 1 && r->group_of[0] == 0, "slots");
    int tgt = r->slot[1][0];
    CHECK(s->agents[0].species == CRET_SP_RAPTOR && s->agents[tgt].species == CRET_SP_GALLI && fabsf(s->agents[tgt].growth - 0.5f) < 1e-3f, "species and growth");
    float gap = hypotf(s->agents[0].x - s->agents[tgt].x, s->agents[0].z - s->agents[tgt].z);
    CHECK(gap > 6 && gap < 20, "placed 12 m apart (%.1f)", gap);
    CretInput in[CRET_MAX_AGENTS]; int guard = 0;
    while (!r->done && guard++ < 40 * CRET_TICK_HZ) { cret_scenario_inputs(r, s, in); cret_sim_step(s, in); cret_scenario_after_step(r, s); }
    CHECK(r->done, "the scenario ended");
    CHECK(r->st[0].hits > 0 && r->st[0].dealt > 0, "the scripted raptor landed blows (%d hits, %.0f hp)", r->st[0].hits, r->st[0].dealt);
    CHECK(r->st[1].hits == 0 && r->st[2].hits == 0, "dummies never struck");
    CHECK(r->st[1].deaths == 1 && !strncmp(r->ended, "target", 6), "ended by the target's death (%s)", r->ended);
    CHECK(r->tick < 25 * CRET_TICK_HZ, "before the time limit (%d ticks)", r->tick);
    char out[2048]; int n = cret_scenario_summary(r, out, sizeof out);
    CHECK(n > 0 && strstr(out, "player") && strstr(out, "target") && strstr(out, "far"), "summary:\n%s", out);
    float cx, cz, rad; CHECK(cret_scenario_centre(r, s, &cx, &cz, &rad) && rad > 10, "centre");
    cret_sim_close(s); free(s); free(r);
}

// ---------------------------------------------------------------- population
// The tiny world is 2 km across; the director's distances are the real ones.
static float nearest_to(const CretSim* s, int i, int diet) { int who; return cret_pop_nearest(s, s->agents[i].x, s->agents[i].z, i, diet, &who); }

static void test_population(void) {
    CretConfig c = t_config(16); c.player_growth = 1; c.respawn_ticks = 300; c.seed = 11;
    cret_pop_config(&c);
    CHECK(c.num_agents == CRET_POP_AGENTS, "solo play has %d animals", c.num_agents);
    CretConfig keep = t_config(64); cret_pop_config(&keep); CHECK(keep.num_agents == 64, "a larger count is kept");
    CretSim* s = calloc(1, sizeof *s); CHECK(cret_sim_init(s, c) == 0, "sim");
    CretPop P; cret_pop_init(&P, s, true);
    CretInput in[CRET_MAX_AGENTS];
    #define RUN(ticks, idle) for (int t_ = 0; t_ < (ticks); t_++) { for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i); if (idle) in[0] = (CretInput){0}; cret_sim_step(s, in); cret_pop_step(&P, s); }
    RUN(30 * CRET_TICK_HZ, false);   // the title screen: everyone a bot
    // Fresh character (Play): placed near prey at once.
    cret_sim_set_species(s, 0, CRET_SP_RAPTOR);
    RUN(2, true);
    float prey = nearest_to(s, 0, CRET_DIET_HERBIVORE), pred = nearest_to(s, 0, CRET_DIET_CARNIVORE);
    CHECK(prey < 160, "fresh raptor: prey %.0f m away", prey);
    CHECK(P.placed >= 1, "the director placed the player");
    // A herbivore lands with its kind and away from predators.
    int placed0 = P.placed;
    cret_sim_set_species(s, 0, CRET_SP_STEGO); RUN(2, true);
    pred = nearest_to(s, 0, CRET_DIET_CARNIVORE);
    int who; float mate = cret_pop_nearest(s, s->agents[0].x, s->agents[0].z, 0, CRET_DIET_HERBIVORE, &who);
    CHECK(P.placed == placed0 + 1, "the stego was placed (%d -> %d)", placed0, P.placed);
    CHECK(mate < 80, "fresh stego: a herbivore %.0f m away", mate);
    CHECK(pred > 40, "fresh stego: no predator within 40 m (%.0f; the 2 km test island is 16x as crowded as the real one)", pred);
    // An idle player keeps company over a few minutes (lonely <= 10% of samples).
    int lonely = 0, samples = 0;
    for (int k = 0; k < 36; k++) { RUN(5 * CRET_TICK_HZ, true); if (!s->agents[0].alive) { cret_sim_respawn(s, 0); continue; } samples++; lonely += nearest_to(s, 0, -1) > CRET_POP_NEAR; }
    CHECK(lonely * 10 <= samples, "idle stego lonely in %d of %d samples", lonely, samples);
    // Restored far from everyone: the position is kept and company is brought (no stranding).
    cret_sim_set_species(s, 0, CRET_SP_RAPTOR);
    float rx = 0, rz = 0, best = -1;
    for (int t = 0; t < 400; t++) {   // the walkable point farthest from every animal
        float x = s->world->size * (0.08f + 0.84f * (float)((t * 7919u) % 1000) / 1000), z = s->world->size * (0.08f + 0.84f * (float)((t * 104729u) % 997) / 997);
        if (!cret_walkable(s->world, x, z)) continue;
        float d = cret_pop_nearest(s, x, z, 0, -1, &who); if (d > best) { best = d; rx = x; rz = z; }
    }
    CHECK(best > 0, "a far restore point (%.0f m from everyone)", best);
    cret_sim_restore(s, 0, 1, 1, 1, 1, true, rx, rz);
    cret_pop_player_placed(&P, s, 0);
    CHECK(hypotf(s->agents[0].x - rx, s->agents[0].z - rz) < 20, "restored position kept");
    RUN(3, true);
    CHECK(hypotf(s->agents[0].x - rx, s->agents[0].z - rz) < 20, "the spawn check does not move a restored player");
    int pulls0 = P.pulls; float d0 = nearest_to(s, 0, CRET_DIET_HERBIVORE);
    RUN((CRET_POP_LONELY_S + 5) * CRET_TICK_HZ, true);
    float d1 = nearest_to(s, 0, CRET_DIET_HERBIVORE);
    CHECK(d1 <= CRET_POP_NEAR || d0 <= CRET_POP_NEAR, "restored raptor has prey within reach (%.0f -> %.0f m)", d0, d1);
    CHECK(d0 <= CRET_POP_NEAR || P.pulls > pulls0, "a herd was brought (%d pulls)", P.pulls - pulls0);
    // Nothing is pulled into view: every pulled group lands >= 120 m from the player.
    CHECK(d1 >= 60 || d0 < 300, "pulled animals arrive out of sight (%.0f m)", d1);
    // The director off: nothing moves.
    CretPop off; cret_pop_init(&off, s, false); int pl = off.placed; cret_pop_step(&off, s); CHECK(off.placed == pl && off.pulls == 0, "off does nothing");
    // move_group keeps offsets.
    int a = -1; for (int i = 1; i < s->cfg.num_agents; i++) if (s->agents[i].alive && s->agents[i].group) { a = i; break; }
    if (a > 0) {
        int mates = 0; for (int j = 1; j < s->cfg.num_agents; j++) mates += j != a && s->agents[j].alive && s->agents[j].group == s->agents[a].group;
        float gx = s->agents[a].x + 6, gz = s->agents[a].z + 4;   // a short hop: the ground there is like the ground here
        int moved = cret_pop_move_group(s, a, gx, gz, true);
        CHECK(moved >= 1 && moved <= mates + 1, "move_group moved %d (group of %d)", moved, mates + 1);
    }
    #undef RUN
    cret_sim_close(s); free(s);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    double t0 = t_now();
    t_hold_world();
    test_parser();
    test_complete();
    test_scenario_parse();
    test_cheats();
    test_scenario_run();
    test_population();
    return t_finish("test_debug", t0);
}
