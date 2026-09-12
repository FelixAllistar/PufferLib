// Hearthwild viewer. Manual keeper + model pet tasks, or full policy autoplay.
// Build, controls, model contract, and test commands: README.md.
#include "arpg.h"
#include "puffercpu.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const double AR_FAST_SIM_DT = 1.0 / 60.0;
static const int AR_FAST_MAX_CATCHUP_STEPS = 5;

// ---------------------------------------------------------------------------
// Configurable key bindings.
// ---------------------------------------------------------------------------

typedef struct {
    int keys[4];
    int count;
} ARBinding;

typedef struct {
    const char* name;
    int key;
} ARKeyName;

static const ARKeyName AR_KEY_NAMES[] = {
    {"SPACE", KEY_SPACE},
    {"ESCAPE", KEY_ESCAPE},
    {"ENTER", KEY_ENTER},
    {"KP_ENTER", KEY_KP_ENTER},
    {"TAB", KEY_TAB},
    {"LEFT_SHIFT", KEY_LEFT_SHIFT},
    {"RIGHT_SHIFT", KEY_RIGHT_SHIFT},
    {"LEFT_CONTROL", KEY_LEFT_CONTROL},
    {"LEFT_ALT", KEY_LEFT_ALT},
    {"UP", KEY_UP},
    {"DOWN", KEY_DOWN},
    {"LEFT", KEY_LEFT},
    {"RIGHT", KEY_RIGHT},
    {"ONE", KEY_ONE},
    {"TWO", KEY_TWO},
    {"THREE", KEY_THREE},
    {"FOUR", KEY_FOUR},
};

static int ar_lookup_key(const char* name) {
    for (size_t i = 0; i < sizeof(AR_KEY_NAMES) / sizeof(AR_KEY_NAMES[0]); i++) {
        if (strcmp(AR_KEY_NAMES[i].name, name) == 0) return AR_KEY_NAMES[i].key;
    }
    if (name[0] >= 'A' && name[0] <= 'Z' && name[1] == '\0') {
        return KEY_A + (name[0] - 'A');
    }
    if (name[0] >= '0' && name[0] <= '9' && name[1] == '\0') {
        return KEY_ZERO + (name[0] - '0');
    }
    if (name[0] == 'K' && name[1] == 'P' && name[2] >= '0' && name[2] <= '9'
            && name[3] == '\0') {
        return KEY_KP_0 + (name[2] - '0');
    }
    if (name[0] == 'F' && name[0] != '\0' && name[1] >= '1' && name[1] <= '9'
            && name[2] == '\0') {
        return KEY_F1 + (name[1] - '1');
    }
    return KEY_NULL;
}

// Missing [keys] entries fall back instead of exiting, so older configs
// without the autoplay row still work.
static ARBinding ar_binding_from_ini_opt(Ini* ini, const char* section,
        const char* name, int fallback) {
    ARBinding binding = {0};
    const char* value = NULL;
    for (int i = 0; i < ini->num_sections; i++) {
        if (strcmp(ini->sections[i].name, section) == 0) {
            DictItem* item = dict_find(&ini->sections[i], name);
            if (item && item->str) value = item->str;
            break;
        }
    }
    if (value == NULL) {
        binding.keys[binding.count++] = fallback;
        return binding;
    }

    // Comma-separated key names with whitespace trimmed, e.g. "up = W, UP".
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", value);
    size_t i = 0;
    while (buffer[i] != '\0' && binding.count < 4) {
        while (buffer[i] == ' ' || buffer[i] == '\t' || buffer[i] == ',') i++;
        size_t start = i;
        while (buffer[i] != '\0' && buffer[i] != ',') i++;
        size_t end = i;
        while (end > start && (buffer[end - 1] == ' ' || buffer[end - 1] == '\t')) {
            end--;
        }
        char token[32];
        size_t len = end - start < sizeof(token) - 1 ? end - start : sizeof(token) - 1;
        memcpy(token, buffer + start, len);
        token[len] = '\0';
        int key = ar_lookup_key(token);
        if (key != KEY_NULL) binding.keys[binding.count++] = key;
    }
    if (binding.count == 0) binding.keys[binding.count++] = fallback;
    return binding;
}

static int ar_binding_down(ARBinding binding) {
    for (int i = 0; i < binding.count; i++) {
        if (IsKeyDown(binding.keys[i])) return 1;
    }
    return 0;
}

static int ar_binding_pressed(ARBinding binding) {
    for (int i = 0; i < binding.count; i++) {
        if (IsKeyPressed(binding.keys[i])) return 1;
    }
    return 0;
}

typedef struct {
    ARBinding up, down, left, right;
    ARBinding summon;
    ARBinding dash, nova, frost;
    ARBinding build_totem, build_wall, build_harvester;
    ARBinding class_wisp, class_fang, class_aegis, class_mule;
    ARBinding order_follow, order_attack, order_guard, order_focus;
    ARBinding reset;
    ARBinding hitboxes;
    ARBinding autoplay;
} ARControls;

// Keep the ini alive for the lifetime of the bindings; strings point into it.
static Ini g_controls_ini;

static void ar_load_config(ARPG* env, ARControls* controls) {
    if (!FileExists("config/arpg.ini")) {
        fprintf(stderr,
            "missing config/arpg.ini; run ./arpg from the repository root\n");
        exit(1);
    }
    g_controls_ini = (Ini){0};
    puf_ini_load_env(&g_controls_ini, "arpg", 0, NULL);
    Dict* env_kwargs = puf_ini_section(&g_controls_ini, "env", 0);
    env->cfg = ar_config_from_kwargs(env_kwargs);
    env->show_hitboxes = (int)dict_get(env_kwargs, "show_hitboxes");

    *controls = (ARControls){
        .up = ar_binding_from_ini_opt(&g_controls_ini, "keys", "up", KEY_W),
        .down = ar_binding_from_ini_opt(&g_controls_ini, "keys", "down", KEY_S),
        .left = ar_binding_from_ini_opt(&g_controls_ini, "keys", "left", KEY_A),
        .right = ar_binding_from_ini_opt(&g_controls_ini, "keys", "right", KEY_D),
        .summon = ar_binding_from_ini_opt(&g_controls_ini, "keys", "summon", KEY_SPACE),
        .dash = ar_binding_from_ini_opt(&g_controls_ini, "keys", "dash", KEY_Q),
        .nova = ar_binding_from_ini_opt(&g_controls_ini, "keys", "nova", KEY_E),
        .frost = ar_binding_from_ini_opt(&g_controls_ini, "keys", "frost", KEY_F),
        .build_totem = ar_binding_from_ini_opt(&g_controls_ini, "keys", "build_totem", KEY_G),
        .build_wall = ar_binding_from_ini_opt(&g_controls_ini, "keys", "build_wall", KEY_V),
        .build_harvester = ar_binding_from_ini_opt(&g_controls_ini, "keys", "build_harvester", KEY_B),
        .class_wisp = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_wisp", KEY_Z),
        .class_fang = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_fang", KEY_X),
        .class_aegis = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_aegis", KEY_C),
        .class_mule = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_mule", KEY_M),
        .order_follow = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_follow", KEY_ONE),
        .order_attack = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_attack", KEY_TWO),
        .order_guard = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_guard", KEY_THREE),
        .order_focus = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_focus", KEY_FOUR),
        .reset = ar_binding_from_ini_opt(&g_controls_ini, "keys", "reset", KEY_R),
        .hitboxes = ar_binding_from_ini_opt(&g_controls_ini, "keys", "hitboxes", KEY_H),
        .autoplay = ar_binding_from_ini_opt(&g_controls_ini, "keys", "autoplay", KEY_T),
    };
}

// ---------------------------------------------------------------------------
// Input -> actions.
// ---------------------------------------------------------------------------

static int ar_read_move_mask(ARControls* controls) {
    int mask = 0;
    if (ar_binding_down(controls->up)) mask |= 1;
    if (ar_binding_down(controls->down)) mask |= 2;
    if (ar_binding_down(controls->left)) mask |= 4;
    if (ar_binding_down(controls->right)) mask |= 8;
    return mask;
}

// Action layout mirrors ar_steer_player (screen-relative): 0 idle, 1 up,
// 2 down, 3 left, 4 right, 5 up-left, 6 up-right, 7 down-left, 8 down-right.
static float ar_read_move_action(int mask) {
    int up = (mask & 1) != 0;
    int down = (mask & 2) != 0;
    int left = (mask & 4) != 0;
    int right = (mask & 8) != 0;

    if (up && left) return 5.0f;
    if (up && right) return 6.0f;
    if (down && left) return 7.0f;
    if (down && right) return 8.0f;
    if (up) return 1.0f;
    if (down) return 2.0f;
    if (left) return 3.0f;
    if (right) return 4.0f;
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Policy control: one avatar and eight independent companion task heads.
// ---------------------------------------------------------------------------

static int ar_has_suffix(const char* s, const char* suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void ar_find_latest(const char* dir, char* out, size_t out_size,
        time_t* best) {
    DIR* dp = opendir(dir);
    if (!dp) return;
    struct dirent* ent = NULL;
    while ((ent = readdir(dp))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            ar_find_latest(path, out, out_size, best);
        } else if (S_ISREG(st.st_mode) && ar_has_suffix(path, ".bin") &&
                st.st_ctime >= *best) {
            *best = st.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }
    closedir(dp);
}

// Returns 0 with out filled, -1 when nothing found (caller falls back).
static int ar_try_latest(char* out, size_t out_size) {
    out[0] = 0;
    time_t best = 0;
    ar_find_latest("checkpoints/arpg", out, out_size, &best);
    return out[0] ? 0 : -1;
}

static void ar_print_usage(const char* argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s                      manual avatar; policy pets when a checkpoint exists\n"
        "  %s play [latest|PATH.bin] [--new] [--no-save] [--save PATH] [--arena]\n"
        "  %s watch [latest|PATH.bin] [--deterministic]\n"
        "Manual play streams and autosaves the frontier. --arena uses the bounded training map.\n",
        argv0, argv0, argv0);
}

static int ar_expected_floats(int hidden, int layers) {
    int sizes[]=ACT_SIZES,sum=0;
    for(int i=0;i<NUM_ATNS;i++)sum+=sizes[i];
    int n=(hidden*AR_OBS_SIZE+7)&~7;
    n+=((sum+1)*hidden+7)&~7;
    for(int i=0;i<layers;i++)n+=(3*hidden*hidden+7)&~7;
    return n;
}

static void ar_reset_policy(PufferNet* net) {
    if(!net || !net->mingru)return;
    memset(net->mingru->state,0,(size_t)net->mingru->num_layers*net->mingru->hidden_size*sizeof(float));
}

static PufferNet* ar_load_policy(const char* path,Weights** out) {
    int hidden=puf_ini_get_int(&g_controls_ini,"policy","hidden_size");
    int layers=puf_ini_get_int(&g_controls_ini,"policy","num_layers");
    if(hidden<1 || hidden>4096 || layers<1 || layers>16)return NULL;
    int expected=ar_expected_floats(hidden,layers);
    struct stat st;
    if(stat(path,&st) || st.st_size!=(off_t)expected*(off_t)sizeof(float)) {
        fprintf(stderr,"ARPG v%d checkpoint mismatch: expected %d floats (%d observations, %d heads). Retrain older policies.\n",
            AR_OBS_VERSION,expected,AR_OBS_SIZE,NUM_ATNS);
        return NULL;
    }
    Weights* weights=load_weights(path);
    if(!weights)return NULL;
    int sizes[]=ACT_SIZES;
    PufferNet* net=make_puffernet(weights,1,AR_OBS_SIZE,hidden,layers,sizes,NUM_ATNS);
    *out=weights;
    fprintf(stderr,"Loaded ARPG v%d policy: %s (avatar + eight pet task heads)\n",AR_OBS_VERSION,path);
    return net;
}

static void ar_policy_step(PufferNet* net,float* obs,float* actions,int deterministic) {
    if(!deterministic){forward_puffernet(net,obs,actions);return;}
    linear(net->encoder,obs);mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    argmax_multidiscrete(net->multidiscrete,net->decoder->output,actions);
}

static int ar_pick_pet(ARPG* e,ARClient* c,Vector2 mouse) {
    int found=-1;float best=1e9f;
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p]) {
        Vector2 at=ar_iso(c,e->pets.x[p],e->pets.y[p],0);
        Rectangle hit={at.x-23*c->zoom,at.y-48*c->zoom,46*c->zoom,55*c->zoom};
        float d=(mouse.x-at.x)*(mouse.x-at.x)+(mouse.y-at.y+20*c->zoom)*(mouse.y-at.y+20*c->zoom);
        if(CheckCollisionPointRec(mouse,hit) && d<best){best=d;found=p;}
    }
    return found;
}

static void ar_context_order(ARPG* e,ARClient* c,Vector2 mouse,int attack_move) {
    Vector2 goal=ar_unproject(c,mouse);int command=attack_move ? AR_CMD_ATTACK : AR_CMD_MOVE;
    int target=-1;
    for(int n=0;n<AR_MAX_NESTS;n++)if(e->nest_active[n]) {
        Vector2 at=ar_iso(c,e->nest_x[n],e->nest_y[n],0);
        if(CheckCollisionPointRec(mouse,(Rectangle){at.x-42*c->zoom,at.y-99*c->zoom,84*c->zoom,105*c->zoom}) ||
                ar_geometry_dist2(goal.x,goal.y,e->nest_x[n],e->nest_y[n])<3) {
            goal=(Vector2){e->nest_x[n],e->nest_y[n]};command=AR_CMD_ATTACK;target=n;break;
        }
    }
    if(target<0)for(int k=0;k<e->enemy_count;k++) {
        int i=e->enemies.dense[k];Vector2 at=ar_iso(c,e->enemies.x[i],e->enemies.y[i],0);
        if(CheckCollisionPointRec(mouse,(Rectangle){at.x-22*c->zoom,at.y-48*c->zoom,44*c->zoom,52*c->zoom})) {
            goal=(Vector2){e->enemies.x[i],e->enemies.y[i]};command=AR_CMD_ATTACK;target=i;break;
        }
    }
    if(target<0)for(int n=0;n<AR_MAX_SHARDS;n++)if(e->shard_active[n]) {
        Vector2 at=ar_iso(c,e->shard_x[n],e->shard_y[n],0);
        if(CheckCollisionPointRec(mouse,(Rectangle){at.x-20*c->zoom,at.y-37*c->zoom,40*c->zoom,42*c->zoom}) ||
                ar_geometry_dist2(goal.x,goal.y,e->shard_x[n],e->shard_y[n])<1.5f) {
            goal=(Vector2){e->shard_x[n],e->shard_y[n]};command=AR_CMD_GATHER;target=n;break;
        }
    }
    int tile=ar_tile_at(e->dungeon,e->cfg.arena_size,goal.x,goal.y);
    if(e->campaign) {
        int gx=(int)floorf((goal.x+e->cfg.arena_size*0.5f)/ar_world_cell(e));
        int gy=(int)floorf((goal.y+e->cfg.arena_size*0.5f)/ar_world_cell(e));
        tile=ar_world_sample(e,gx,gy);
    }
    if(target<0 && (tile==AR_TILE_ROCK || tile==AR_TILE_FOREST))command=AR_CMD_WORK;
    uint32_t mask=c->selected_mask;
    if(!mask) {
        // Contextual defaults never pull individually assigned workers off a job.
        for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && e->pets.command[p]==AR_CMD_AUTO) {
            if(command==AR_CMD_GATHER && e->pets.kind[p]==AR_PET_MULE){mask=1u<<p;break;}
            if(command==AR_CMD_WORK && e->pets.kind[p]>=AR_PET_BURROWER){mask=1u<<p;break;}
            if((command==AR_CMD_MOVE || command==AR_CMD_ATTACK) && e->pets.kind[p]!=AR_PET_MULE && e->pets.kind[p]!=AR_PET_EMBER)mask|=1u<<p;
        }
    }
    int issued=0;
    for(int p=0;p<AR_MAX_PETS;p++)if((mask&(1u<<p)) && e->pets.active[p]) {
        if(command==AR_CMD_WORK && e->pets.kind[p]<AR_PET_BURROWER)continue;
        if(command==AR_CMD_ATTACK && e->pets.dmg[p]<=0)continue;
        if(command==AR_CMD_MOVE && (tile==AR_TILE_ROCK || tile==AR_TILE_DEEP))continue;
        ar_command_pet(e,0,p,command,goal.x,goal.y);issued++;
    }
    if(issued)ar_notice(c,TextFormat("%s assigned to %d companion%s. P restores automatic assist.",AR_COMMAND_NAMES[command],issued,issued==1 ? "" : "s"));
    else ar_notice(c,command==AR_CMD_WORK ? "Select a Burrower or Ember to work this terrain." : "Select companions first, or choose reachable ground.");
    c->build_kind=-1;c->targeting_nuke=0;ar_compute_observations(e,0);
}

static int ar_move_toward(ARPG* e,float tx,float ty) {
    float dx=tx-e->px,dy=ty-e->py;
    if(dx*dx+dy*dy<0.5f)return 0;
    if(!ar_nav_visible(e->dungeon,e->cfg.arena_size,e->px,e->py,tx,ty)) {
        float nx,ny;if(!ar_nav_next(e->dungeon,e->cfg.arena_size,e->px,e->py,tx,ty,&nx,&ny))return 0;
        dx=nx-e->px;dy=ny-e->py;if(dx*dx+dy*dy<0.01f)return 0;
    }
    float best=-2;int result=0;
    float length=sqrtf(dx*dx+dy*dy);dx/=length;dy/=length;
    for(int action=1;action<AR_MOVE_ACTION_COUNT;action++) {
        float vx,vy;ar_move_dir(action,&vx,&vy);
        if(!ar_geometry_floor(e->dungeon,e->cfg.arena_size,e->px+vx*0.7f,e->py+vy*0.7f))continue;
        float score=dx*vx+dy*vy;
        if(score>best){best=score;result=action;}
    }
    return result;
}

int main(int argc,char** argv) {
    int watch=0,deterministic=0,bounded=0,no_save=0,new_world=0;
    const char* save_path="saves/arpg/frontier-v3.bin";
    const char* model=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"watch"))watch=1;
        else if(!strcmp(argv[i],"play"))watch=0;
        else if(!strcmp(argv[i],"--deterministic"))deterministic=1;
        else if(!strcmp(argv[i],"--arena"))bounded=1;
        else if(!strcmp(argv[i],"--no-save"))no_save=1;
        else if(!strcmp(argv[i],"--new"))new_world=1;
        else if(!strcmp(argv[i],"--save") && i+1<argc)save_path=argv[++i];
        else if(!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")){ar_print_usage(argv[0]);return 0;}
        else if(argv[i][0]!='-')model=argv[i];
        else {ar_print_usage(argv[0]);return 1;}
    }
    float obs[AR_OBS_SIZE]={0},actions[NUM_ATNS]={0},rewards[1]={0},terminals[1]={0};
    ARPG e={0};e.num_agents=1;e.rng=(uint32_t)time(NULL);
    const char* seed=getenv("ARPG_SEED");if(seed)e.rng=(uint32_t)strtoul(seed,NULL,10);
    e.agents[0].observations=obs;e.agents[0].actions=actions;
    e.agents[0].rewards=rewards;e.agents[0].terminals=terminals;
    ARControls controls;ar_load_config(&e,&controls);
    e.cfg.max_steps=2147483647;
    char path[4096]={0};
    if(model && strcmp(model,"latest"))snprintf(path,sizeof(path),"%s",model);
    else ar_try_latest(path,sizeof(path));
    Weights* weights=NULL;
    PufferNet* net=path[0] ? ar_load_policy(path,&weights) : NULL;
    if((watch || model) && !net) {
        fprintf(stderr,"RL autoplay requires an ARPG v3 checkpoint. Train arpg, then use ./arpg watch PATH.bin.\n");
        puf_ini_free(&g_controls_ini);return 1;
    }
    int save_enabled=!watch && !bounded && !no_save && !getenv("ARPG_SHOT");
    int loaded=0;
    c_reset(&e);
    if(!watch && !bounded) {
        if(save_enabled) {
            mkdir("saves",0755);mkdir("saves/arpg",0755);
            if(new_world) {
                char archive[4096];snprintf(archive,sizeof(archive),"%s.archive-%lld-%ld",save_path,(long long)time(NULL),(long)getpid());
                if(rename(save_path,archive)!=0 && errno!=ENOENT) {
                    fprintf(stderr,"Cannot archive the previous save; leaving it untouched.\n");puf_close(&e);if(net)free_puffernet(net);free(weights);puf_ini_free(&g_controls_ini);return 1;
                }
            } else loaded=ar_world_load(&e,save_path);
            if(loaded<0) {
                fprintf(stderr,"Save is invalid or incompatible: %s. It was not changed. Use --new to archive it, or --no-save for a temporary world.\n",save_path);
                puf_close(&e);if(net)free_puffernet(net);free(weights);puf_ini_free(&g_controls_ini);return 1;
            }
        }
        if(!loaded)ar_world_begin(&e);
    }
    c_render(&e);
    ARClient* client=ar_client(&e);
    client->autoplay=watch;client->pet_policy=net!=NULL;
    if(e.campaign)for(int p=0;p<AR_MAX_PETS;p++)client->task_override[p]=((ARWorld*)e.campaign)->task_override[p];
    if(loaded)ar_notice(client,"Frontier restored. Your outposts, companions, and terrain changes are here.");
    double accumulator=0;
    double reset_armed_until=0;
    int next_save_tick=e.tick+3600;
    int human_order=e.order,shot_frame=0;
    const char* shot=getenv("ARPG_SHOT");
    int shot_at=240;
    if(getenv("ARPG_SHOT_FRAME"))shot_at=atoi(getenv("ARPG_SHOT_FRAME"));
    while(!WindowShouldClose()) {
        float frame_dt=fminf(GetFrameTime(),0.1f);
        if(e.campaign)for(int p=0;p<AR_MAX_PETS;p++)((ARWorld*)e.campaign)->task_override[p]=client->task_override[p];
        if(save_enabled && IsKeyPressed(KEY_F5))ar_notice(client,ar_world_save(&e,save_path) ? "Frontier saved." : "Save failed. Your current world is still in memory.");
        if(ar_binding_pressed(controls.reset)) {
            int reset_ok=!e.campaign || (reset_armed_until>GetTime());
            if(!reset_ok){reset_armed_until=GetTime()+4;ar_notice(client,save_enabled ? "Press R again within four seconds for a new world. Your current world will be archived." : "Press R again within four seconds to replace this unsaved world.");}
            if(reset_ok && save_enabled) {
                char archive[4096];snprintf(archive,sizeof(archive),"%s.archive-%lld-XXXXXX",save_path,(long long)time(NULL));
                int fd=mkstemp(archive);reset_ok=fd>=0;
                if(fd>=0) {
                    close(fd);reset_ok=ar_world_save(&e,archive);
                    if(!reset_ok)unlink(archive);
                }
                if(!reset_ok)ar_notice(client,"Could not archive the current frontier. Restart cancelled.");
            }
            if(reset_ok) {
            ar_world_close(&e);
            c_reset(&e);ar_reset_policy(net);client->paused=0;client->move_target=0;accumulator=0;
            if(!watch && !bounded)ar_world_begin(&e);
            human_order=AR_ORDER_FOLLOW;client->build_kind=-1;client->selected_pet=-1;
            client->camera_free=0;client->cam_x=e.px;client->cam_y=e.py;client->origin_x=client->origin_y=0;
            client->selected_mask=0;client->targeting_nuke=0;client->dragging=0;
            memset(client->groups,0,sizeof(client->groups));
            client->ui_summon=0;client->ui_ability=0;
            memset(actions,0,sizeof(actions));
            for(int p=0;p<AR_MAX_PETS;p++)client->task_override[p]=-1;
            next_save_tick=e.tick+3600;reset_armed_until=0;
            }
        }
        if(IsKeyPressed(KEY_TAB) || client->ui_pause) {
            client->paused=e.hp<=0 ? 1 : !client->paused;client->ui_pause=0;accumulator=0;
        }
        if(ar_binding_pressed(controls.hitboxes))e.show_hitboxes=!e.show_hitboxes;
        if(ar_binding_pressed(controls.autoplay) || client->ui_toggle) {
            client->ui_toggle=0;
            if(net) {
                client->autoplay=!client->autoplay;ar_reset_policy(net);
                memset(actions,0,sizeof(actions));client->move_target=0;
                client->ui_summon=0;client->ui_ability=0;client->build_kind=-1;
            }
            else ar_notice(client,"No RL checkpoint loaded. Scripted pet assist is active; train arpg for RL control.");
        }
        if(IsKeyPressed(KEY_P)) {
            for(int p=0;p<AR_MAX_PETS;p++)if(!client->selected_mask || (client->selected_mask&(1u<<p))) {
                client->task_override[p]=-1;ar_command_pet(&e,0,p,AR_CMD_AUTO,e.pets.x[p],e.pets.y[p]);
            }
            client->pet_policy=net!=NULL;
            ar_notice(client,net ? "Pet policy restored; manual task overrides cleared." : "Scripted companion assist restored. No RL checkpoint loaded.");
        }
        Vector2 mouse=GetMousePosition();
        int in_world=mouse.y>76 && mouse.y<GetScreenHeight()-150 &&
            !CheckCollisionPointRec(mouse,(Rectangle){23,97,298,103}) &&
            !CheckCollisionPointRec(mouse,(Rectangle){GetScreenWidth()-181.0f,88,162,186});
        if(in_world && !client->autoplay && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
            ar_context_order(&e,client,mouse,IsKeyDown(KEY_LEFT_CONTROL));
        if(!client->autoplay) {
            if(ar_binding_pressed(controls.class_wisp))e.pick_class=AR_PET_WISP;
            if(ar_binding_pressed(controls.class_fang))e.pick_class=AR_PET_FANG;
            if(ar_binding_pressed(controls.class_aegis))e.pick_class=AR_PET_AEGIS;
            if(ar_binding_pressed(controls.class_mule))e.pick_class=AR_PET_MULE;
            if(ar_binding_pressed(controls.order_follow)){human_order=AR_ORDER_FOLLOW;e.rally_active=0;}
            if(ar_binding_pressed(controls.order_attack))human_order=AR_ORDER_ATTACK;
            if(ar_binding_pressed(controls.order_guard))human_order=AR_ORDER_GUARD;
            if(ar_binding_pressed(controls.order_focus))human_order=AR_ORDER_FOCUS;
            if(ar_binding_pressed(controls.summon))actions[1]=(float)e.pick_class+1;
            if(client->ui_summon){actions[1]=(float)client->ui_summon;e.pick_class=client->ui_summon-1;client->ui_summon=0;}
            if(client->ui_ability){actions[3]=(float)client->ui_ability;client->ui_ability=0;}
            if(ar_binding_pressed(controls.dash))actions[3]=1;
            if(ar_binding_pressed(controls.nova))actions[3]=2;
            if(ar_binding_pressed(controls.frost))actions[3]=3;
            if(ar_binding_pressed(controls.build_totem))client->build_kind=AR_BUILD_TOTEM;
            if(ar_binding_pressed(controls.build_wall))client->build_kind=AR_BUILD_WALL;
            if(ar_binding_pressed(controls.build_harvester))client->build_kind=AR_BUILD_HARVESTER;
            if(IsKeyPressed(KEY_J))client->build_kind=AR_BUILD_ARTILLERY;
            if(IsKeyPressed(KEY_O))client->build_kind=AR_BUILD_BRIDGE;
            if(IsKeyPressed(KEY_N)){client->targeting_nuke=!client->targeting_nuke;client->build_kind=-1;}
            if(IsKeyPressed(KEY_BACKSPACE)){client->build_kind=-1;client->targeting_nuke=0;client->selected_mask=0;}
            if(IsKeyPressed(KEY_DELETE)) {
                for(int p=0;p<AR_MAX_PETS;p++)if((client->selected_mask&(1u<<p)) && e.pets.active[p]) {
                    e.shards+=e.cfg.summon_cost[e.pets.kind[p]]*0.5f;ar_free_pet(&e,0,p);client->task_override[p]=-1;
                }
                client->selected_mask=0;ar_notice(client,"Selected summons released. Half their aether cost was returned.");
            }
            for(int g=0;g<4;g++)if(IsKeyPressed(KEY_F1+g)) {
                if(IsKeyDown(KEY_LEFT_CONTROL)){client->groups[g]=client->selected_mask;ar_notice(client,"Control group stored.");}
                else {client->selected_mask=client->groups[g];ar_sync_selection(client,&e);}
            }
            if(in_world && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 world=ar_unproject(client,mouse);
                if(client->targeting_nuke) {
                    if(ar_fire_artillery(&e,0,world.x,world.y)) {client->targeting_nuke=0;ar_notice(client,"STARFIRE. A new landscape begins.");}
                    else if(e.campaign && (fabsf(world.x)>e.cfg.arena_size*0.5f-8 || fabsf(world.y)>e.cfg.arena_size*0.5f-8))
                        ar_notice(client,"Move closer to load the whole strike area. No ammunition was spent.");
                    else ar_notice(client,"Need a ready Starfire within 48 units, 8 cores, and 20 aether.");
                } else if(client->build_kind>=0) {
                    if(ar_geometry_dist2(e.px,e.py,world.x,world.y)>144)
                        ar_notice(client,"Move closer to build here (12-unit construction reach).");
                    else if(ar_build_at(&e,0,client->build_kind,world.x,world.y)>=0) {
                        if(!IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT))client->build_kind=-1;
                        ar_compute_observations(&e,0);
                    }
                    else ar_notice(client,"Cannot build here: check aether, footprint, and clear ground.");
                } else {
                    client->drag_start=mouse;client->dragging=1;
                }
            }
            if(client->dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                float dx=mouse.x-client->drag_start.x,dy=mouse.y-client->drag_start.y;
                int additive=IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
                if(dx*dx+dy*dy>64) {
                    if(!additive)client->selected_mask=0;
                    Rectangle box={fminf(mouse.x,client->drag_start.x),fminf(mouse.y,client->drag_start.y),fabsf(dx),fabsf(dy)};
                    for(int p=0;p<AR_MAX_PETS;p++)if(e.pets.active[p]) {
                        Vector2 at=ar_iso(client,e.pets.x[p],e.pets.y[p],0.6f);
                        if(CheckCollisionPointRec(at,box))client->selected_mask|=1u<<p;
                    }
                } else {
                    int p=ar_pick_pet(&e,client,mouse);
                    if(p>=0)ar_select_pet(client,&e,p,additive);
                    else if(in_world) {
                        Vector2 world=ar_unproject(client,mouse);
                        client->selected_mask=0;client->move_target=1;client->move_x=world.x;client->move_y=world.y;
                    }
                }
                client->dragging=0;ar_sync_selection(client,&e);
            }
            int movement=ar_read_move_mask(&controls);
            if(movement)client->move_target=0;
            actions[0]=ar_read_move_action(movement);
            if(client->move_target){actions[0]=(float)ar_move_toward(&e,client->move_x,client->move_y);if(!actions[0])client->move_target=0;}
            actions[2]=(float)human_order;
        }
        if(!client->paused)accumulator+=frame_dt;
        int steps=0;
        while(accumulator>=AR_FAST_SIM_DT && steps<AR_FAST_MAX_CATCHUP_STEPS && !client->paused) {
            for(int p=0;p<AR_MAX_PETS;p++)actions[5+p]=0;
            if(net) {
                float predicted[NUM_ATNS]={0};ar_policy_step(net,obs,predicted,deterministic);
                if(client->autoplay)memcpy(actions,predicted,5*sizeof(float));
                for(int p=0;p<AR_MAX_PETS;p++)actions[5+p]=client->pet_policy ? predicted[5+p] : 0;
            }
            for(int p=0;p<AR_MAX_PETS;p++)if(client->task_override[p]>=0)actions[5+p]=(float)client->task_override[p];
            int respawns=e.campaign ? ((ARWorld*)e.campaign)->respawns : 0;
            c_step(&e);accumulator-=AR_FAST_SIM_DT;steps++;
            if(e.campaign && ((ARWorld*)e.campaign)->respawns>respawns) {
                client->move_target=0;client->camera_free=0;
                ar_notice(client,"Returned to your lodge. Recovery costs 5 aether; your industry remains.");
            }
            if(!client->autoplay){actions[1]=0;actions[3]=0;actions[4]=0;}
            if(terminals[0]>0) {
                if(client->autoplay){c_reset(&e);ar_reset_policy(net);}
                else {client->paused=1;ar_notice(client,"Run complete. R starts a fresh homestead.");}
                accumulator=0;break;
            }
        }
        if(save_enabled && e.tick>=next_save_tick) {
            if(!ar_world_save(&e,save_path))ar_notice(client,"Autosave failed. F5 retries; your world is still in memory.");
            next_save_tick=e.tick+3600;
        }
        c_render(&e);
        if(shot && ++shot_frame==shot_at) {
            // ExportImage preserves absolute paths (raylib TakeScreenshot strips them).
            Image screen=LoadImageFromScreen();ExportImage(screen,shot);UnloadImage(screen);
        }
        if(shot && shot_frame>=shot_at+2)break;
    }
    int save_failed=save_enabled && !ar_world_save(&e,save_path);
    if(save_failed)fprintf(stderr,"WARNING: failed to save frontier to %s\n",save_path);
    puf_close(&e);
    if(net)free_puffernet(net);
    free(weights);puf_ini_free(&g_controls_ini);
    return save_failed ? 1 : 0;
}
