#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include <sys/wait.h>

static void invalid(const char* team) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        PKGame game = {0};
        pk_parse_fixed_team(&game, 0, team);
        _exit(0);
    }
    int status;
    waitpid(child, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
}

int main(void) {
    int expected[6] = {422,445,515,397,366,389};
    PKGame game = {0};
    game.rng = 42;
    game.max_updates = 512;
    pk_parse_fixed_team(&game, 0, "422,445,515,397,366,389");
    for (int mode = 0; mode < 2; mode++) {
        game.draft = mode;
        for (int episode = 0; episode < 32; episode++) {
            pk_game_reset(&game);
            while (game.picks < 6) {
                int legal = 0;
                for (int a = 0; a < PK_ACTIONS; a++) legal += game.masks[0][a];
                assert(legal == 1);
                assert(!memcmp(game.obs[0]+480, game.masks[0], PK_ACTIONS));
                int a = pk_random_action(game.masks[0], &game.rng);
                int b = pk_random_action(game.masks[1], &game.rng);
                pk_game_step(&game, a, b);
            }
            for (int i = 0; i < 6; i++) assert(game.teams[0][i] == expected[i]);
            assert(game.invalid_actions == 0);
        }
    }
    // Same fixed policy in the other seat; unrestricted side remains free.
    pk_parse_fixed_team(&game, 0, "None");
    pk_parse_fixed_team(&game, 1, "422,445,515,397,366,389");
    game.draft = 1;
    pk_game_reset(&game);
    assert(game.masks[0][0] && !game.masks[1][0]);
    invalid("0,0,0,0,0,0");
    invalid("0,1,2");
    invalid("422,445,515,397,366,9999");
    invalid("422,445,515,397,366,389,0");
    invalid("required:");
    invalid("required:445,445");
    invalid("required:445,");
    // Adversarial drafts spend all open slots first. Remaining masks must
    // force every required species AND its exact set, without fixing the lead.
    for (int mode = 0; mode < 2; mode++) for (int count = 1; count <= 6; count++) {
        PKGame partial = {0};
        partial.rng = 73; partial.max_updates = 512; partial.draft = mode;
        char spec[128] = "required:";
        for (int i = 0; i < count; i++) {
            char id[16]; snprintf(id,sizeof(id),"%s%d",i?",":"",expected[i]);
            strcat(spec,id);
        }
        pk_parse_fixed_team(&partial,0,spec);
        pk_parse_fixed_team(&partial,1,"required:445,512,392,522");
        for (int trial = 0; trial < 32; trial++) {
            pk_game_reset(&partial);
            while (partial.picks < 6) {
                int a = pk_random_action(partial.masks[0],&partial.rng);
                if (!partial.selecting_set) for (int j = 0; j < 149; j++)
                    if (partial.masks[0][j] && pk_required_set(&partial,0,j+1) < 0) { a=j; break; }
                pk_game_step(&partial,a,pk_random_action(partial.masks[1],&partial.rng));
            }
            assert(!partial.invalid_actions);
            for (int i = 0; i < count; i++) {
                int found = 0;
                for (int j = 0; j < 6; j++) found += partial.teams[0][j] == expected[i];
                assert(found == 1);
            }
            for (int i = 0; i < 4; i++) {
                int found = 0;
                for (int j = 0; j < 6; j++) found += partial.teams[1][j] == partial.fixed_team[1][i];
                assert(found == 1);
            }
        }
    }

    // Opt-in logging stops at its cap without truncating existing content.
    Env env = {0};
    env.game = game;
    for (int p = 0; p < 2; p++) for (int i = 0; i < 6; i++) env.game.teams[p][i] = expected[i];
    char path[] = "/tmp/pokemon-team-log-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    snprintf(env.team_log_path, sizeof(env.team_log_path), "%s", path);
    env.team_log_max_bytes = 4096;
    env.team_log_interval = 1;
    for (int i = 0; i < 20; i++) pk_record_teams(&env);
    struct stat st;
    assert(stat(path, &st) == 0 && st.st_size > 0 && st.st_size <= 4096);
    assert(env.team_log_interval == 0);
    off_t previous = st.st_size;
    env.team_log_max_bytes = 1;
    env.team_log_interval = 1;
    pk_record_teams(&env);
    assert(stat(path, &st) == 0 && st.st_size == previous);
    unlink(path);
    puts("Fixed-team masks, both draft modes, validation, and log cap passed");
}
