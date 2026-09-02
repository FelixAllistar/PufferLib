// Shenanigans 3D — FPS navigation gym ON BOX3D PHYSICS.
//
// The sim IS the deployment stack: pd64/character.{h,c} (s&box-style
// controller) runs inside a real box3d world, exactly like the game will.
// No hand-rolled collision, no sim gap for locomotion, no reward hacks from
// kinematic edge cases.
//
// Course: legacy obstacle route at difficulties 1-2; difficulty 3 generates a
// self-avoiding hallway route with randomized doors, turns, jumps, and crouch
// clearances.
//
// UNITS: meters, dt = 1/60 fixed, y-up. Character: s&box dims (r=16u,
// h=72u stand / 40u crouch), Source-style wish-velocity movement.
//
// OBSERVATIONS (float32[OBS_SIZE]): flat transport, structured sensor regions
//   [0:11] scalar state: velocity, yaw, stance, ground, goal, vertical speed
//   [11:71] depth map [pitch][azimuth], normalized ray distances
//   [71:391] occupancy [height][lateral][forward], ray-sampled free/blocked/unknown
//
// ACTIONS (NUM_ATNS = 5): turn{5} fwd{3} strafe{3} jump{2} crouch{2}
//
// REWARD: configurable horizontal potential progress, goal, time, fall, and
// one-time jump/crouch action penalties.

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "raylib.h"
#include "pufferenv.h"

#include "box3d/box3d.h"
#include "character.h"

// ---------------------------------------------------------------------------
// Contract constants
// ---------------------------------------------------------------------------
#define DT 60.0f
#define SIM_SUBSTEPS 1        // training speed; game uses 4 (note: minor gap)
#define MAX_SPEED 5.84f       // 230 u/s walk (matches controller walkSpeed)
#define GROUND_RAY 5.0f
#define RAY_RANGE 24.0f
#define DEPTH_MAP_WIDTH 12
#define DEPTH_MAP_HEIGHT 5
#define DEPTH_MAP_SIZE (DEPTH_MAP_WIDTH * DEPTH_MAP_HEIGHT)
#define OBS_SCALAR_SIZE 11

#define OCCUPANCY_FORWARD_BINS 16
#define OCCUPANCY_LATERAL_BINS 5
#define OCCUPANCY_VERTICAL_BINS 4
#define OCCUPANCY_SIZE (OCCUPANCY_FORWARD_BINS * OCCUPANCY_LATERAL_BINS * OCCUPANCY_VERTICAL_BINS)
#define OCCUPANCY_RANGE 12.0f
// Sensor maps are expressed in the agent's current frame, so retaining them
// across a turn or a moving obstacle makes the policy observe the wrong scene.
#define DEPTH_MAP_UPDATE_INTERVAL 1
#define OCCUPANCY_UPDATE_INTERVAL 1
#define OCCUPANCY_LATERAL_MIN (-2.0f)
#define OCCUPANCY_LATERAL_STEP 0.8f
#define OCCUPANCY_VERTICAL_MIN 0.25f
#define OCCUPANCY_VERTICAL_STEP 0.55f

#define NUM_ATNS 5
#define ACT_SIZES {5, 3, 3, 2, 2}
#define DEPTH_MAP_OFFSET OBS_SCALAR_SIZE
#define OCCUPANCY_OFFSET (DEPTH_MAP_OFFSET + DEPTH_MAP_SIZE)
#define OBS_SIZE (OCCUPANCY_OFFSET + OCCUPANCY_SIZE)

#define MAX_BOXES 128
#define COURSE_MAX_COLUMNS 24
#define COURSE_DOORS 3
#define COURSE_MAX_ROOMS 12
#define HALL_ROOM_HALF_SIZE 2.0f
#define HALL_ROOM_SPACING 4.0f
#define HALL_WALL_HEIGHT 3.4f
#define HALL_WALL_THICKNESS 0.2f
#define HALL_DOOR_LOW 0
#define HALL_DOOR_JUMP 1
#define HALL_DOOR_NORMAL 2
#define COURSE_LENGTH 36.0f
#define KILL_Y -9.0f
#define GOAL_X 30.0f
#define GOAL_Z 0.0f
#define GOAL_Y (-3.0f)
#define GOAL_BONUS 10.0f
#define TIME_COST 0.0005f
#define JUMP_PENALTY_DEFAULT (-TIME_COST)
#define CROUCH_PENALTY_DEFAULT (-TIME_COST)
#define CROUCH_ENTER_COMMIT_TICKS_DEFAULT 1
#define CROUCH_EXIT_COMMIT_TICKS_DEFAULT 1
#define FALL_PENALTY 10.0f
#define PROGRESS_SHAPING_DEFAULT 0.05f
#define HEAD_HIT_REWARD_DEFAULT (-0.005f)
#define HEAD_HIT_RANGE 0.75f // forward hull-cast lookahead in meters
#define MAX_TICKS_DEFAULT 3600
#define COURSE_MODE_FIXED 0
#define COURSE_MODE_RANDOM 1
#define COURSE_MODE_RANDOM_EVERY_RESET 2
#define COURSE_DIFFICULTY_DEFAULT 1
#define COURSE_STAGE_COLUMNS 0
#define COURSE_STAGE_ONE_TURN 1
#define COURSE_STAGE_TWO_TURNS 2
#define COURSE_STAGE_JUMP 3
#define COURSE_STAGE_CROUCH 4
#define COURSE_STAGE_STRESS 5
#define COURSE_FLOOR_END 34.0f
#define COURSE_WALL_TOP 4.2f

typedef struct Log Log;
struct Log {
    float perf;
    float episode_return;
    float episode_length;
    float score;
    float n;
};

typedef struct Client Client;
typedef float obs_t;
typedef Env Shenanigans3D;

typedef struct AABB {
    float cx, cy, cz, hx, hy, hz;
} AABB;

typedef struct CourseDoor {
    float x;
    float z;
    float width;
    float height;
    int axis; // 0 = wall normal X, 1 = wall normal Z
    int kind; // HALL_DOOR_* for generated hallway doors
} CourseDoor;

typedef struct CourseRoom {
    int gx;
    int gz;
    float x;
    float z;
} CourseRoom;

typedef struct CourseColumn {
    float x;
    float z;
    float radius;
    float height;
} CourseColumn;

typedef struct CourseParams {
    CourseDoor doors[COURSE_DOORS];
    float jump_x;
    float jump_width;
    float pit_depth;
    float tunnel_start;
    float tunnel_end;
    float tunnel_clearance;
    float hole_start;
    float hole_end;
    CourseRoom rooms[COURSE_MAX_ROOMS];
    CourseDoor route_doors[COURSE_MAX_ROOMS - 1];
    CourseColumn columns[COURSE_MAX_COLUMNS];
    int column_count;
    int room_count;
    int ceiling_room;
    float ceiling_clearance;
    float goal_x;
    float goal_z;
    float goal_y;
    float route_length;
} CourseParams;

struct Env {
    // GPU environments are reduced through this field directly from device
    // memory. Keep it first, matching the GPU environment ABI.
    Log log;
    Client* client;
    Agent agents[1];

    int num_agents;
    int tick, max_ticks;

    b3WorldId world;
    PDCharacter ch;
    float yaw;

    // render-only mirror of the course geometry
    AABB boxes[MAX_BOXES];
    int numBoxes;
    CourseParams course;
    int course_mode;
    int course_difficulty;
    int course_stage;
    bool course_stage_enabled;
    float depth[DEPTH_MAP_SIZE];
    bool depth_valid;
    float occupancy[OCCUPANCY_SIZE];
    bool occupancy_valid;
    int depth_update_interval;
    int occupancy_update_interval;

    float prevProgressDist;
    float epReturn;
    float progressSum;
    bool reachedGoal;
    bool lastGoal;

    // reward knobs (configurable via [env] ini, see puf_init)
    float reward_progress; // small telescoping potential-delta scale
    float time_cost;       // - per tick (0.0005 default)
    float reward_goal;     // + on goal
    float reward_fall;     // - on kill-plane (negative)
    float reward_head_hit; // - while pushing forward into a nearby obstruction
    float jump_penalty;    // fixed cost per successful on-ground jump
    float crouch_penalty;  // - on each successful crouch entry; defaults to -time_cost
    int crouch_enter_commit_ticks; // lock stance after entering crouch
    int crouch_exit_commit_ticks;  // lock stance after standing up
    int crouch_commit_ticks;       // internal remaining stance lock

    int tag;
    int boundary_reached;
    unsigned int rng;
};

// ---------------------------------------------------------------------------
// Course
// ---------------------------------------------------------------------------

static void add_box(Shenanigans3D* env, float cx, float cy, float cz,
                    float hx, float hy, float hz) {
    assert(env->numBoxes < MAX_BOXES);
    AABB* b = &env->boxes[env->numBoxes++];
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->hx = hx; b->hy = hy; b->hz = hz;

    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.position = (b3Pos){ cx, cy, cz };
    b3BodyId body = b3CreateBody(env->world, &bodyDef);
    b3ShapeDef shapeDef = b3DefaultShapeDef();
    b3BoxHull hull = b3MakeBoxHull(hx, hy, hz);
    b3CreateHullShape(body, &shapeDef, &hull.base);
}

static void add_box_bounds(Shenanigans3D* env,
                           float x0, float x1, float y0, float y1,
                           float z0, float z1) {
    add_box(env, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f,
             (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, (z1 - z0) * 0.5f);
}

static float course_rand01(Shenanigans3D* env) {
    return (float)rand_r(&env->rng) / ((float)RAND_MAX + 1.0f);
}

static float course_rand_range(Shenanigans3D* env, float lo, float hi) {
    return lo + (hi - lo) * course_rand01(env);
}

static void set_fixed_course(CourseParams* p) {
    *p = (CourseParams){
        .doors = {
            { .x = 6.0f,  .z = 0.0f, .width = 1.6f, .height = 2.2f },
            { .x = 9.0f,  .z = 0.0f, .width = 1.4f, .height = 2.1f },
            { .x = 18.5f, .z = 0.0f, .width = 1.4f, .height = 2.1f },
        },
        .jump_x = 10.5f,
        .jump_width = 0.4f,
        .pit_depth = 0.9f,
        .tunnel_start = 12.0f,
        .tunnel_end = 16.0f,
        .tunnel_clearance = 1.15f,
        .hole_start = 22.0f,
        .hole_end = 26.0f,
        .column_count = 0,
        .room_count = 0,
        .ceiling_room = -1,
        .ceiling_clearance = 0.0f,
        .goal_x = GOAL_X,
        .goal_z = GOAL_Z,
        .goal_y = GOAL_Y,
        .route_length = COURSE_LENGTH,
    };
}

static bool column_position_clear(const CourseParams* p, int used,
                                  float x, float z, float radius) {
    // Keep columns visibly separate so this stage tests steering around
    // obstacles rather than discovering accidental wall-like formations.
    for (int i = 0; i < used; i++) {
        const CourseColumn* column = &p->columns[i];
        float dx = x - column->x;
        float dz = z - column->z;
        float minDistance = radius + column->radius + 0.75f;
        if (fabsf(dx) < minDistance && fabsf(dz) < minDistance)
            return false;
    }
    return true;
}

static void randomize_column_course(Shenanigans3D* env) {
    CourseParams* p = &env->course;
    p->column_count = 0;
    p->room_count = 0;
    p->ceiling_room = -1;
    p->ceiling_clearance = 0.0f;
    p->goal_x = 28.0f;
    p->goal_z = 0.0f;
    p->goal_y = 0.0f;
    p->route_length = p->goal_x;

    const int target = 12;
    for (int i = 0; i < target; i++) {
        bool placed = false;
        for (int attempt = 0; attempt < 128 && !placed; attempt++) {
            float radius = course_rand_range(env, 0.35f, 0.55f);
            // Guarantee one genuine avoidance decision while keeping its
            // position and side random for every reset.
            float x = i == 0 ? course_rand_range(env, 6.0f, 18.0f) :
                               course_rand_range(env, 4.0f, 25.5f);
            float z = i == 0 ? course_rand_range(env, -0.55f, 0.55f) :
                               course_rand_range(env, -5.8f, 5.8f);
            float spawnClearance = 2.75f + radius;
            float goalDx = x - p->goal_x;
            float goalDz = z - p->goal_z;
            float goalClearance = 2.25f + radius;
            if (x * x + z * z < spawnClearance * spawnClearance ||
                    goalDx * goalDx + goalDz * goalDz <
                        goalClearance * goalClearance ||
                    !column_position_clear(p, p->column_count, x, z, radius))
                continue;

            p->columns[p->column_count++] = (CourseColumn){
                .x = x,
                .z = z,
                .radius = radius,
                .height = course_rand_range(env, 1.0f, 2.4f),
            };
            placed = true;
        }
    }
}

static void set_hallway_room(CourseParams* p, int index, int gx, int gz) {
    p->rooms[index] = (CourseRoom){
        .gx = gx,
        .gz = gz,
        .x = gx * HALL_ROOM_SPACING,
        .z = gz * HALL_ROOM_SPACING,
    };
}

static void curriculum_hallway_course(Shenanigans3D* env, int stage) {
    CourseParams* p = &env->course;
    int side = course_rand01(env) < 0.5f ? 1 : -1;
    int room_count = 0;
    int route[6][2] = { { 0, 0 } };

    // Each stage teaches one new skill while leaving the later skill
    // combinations for the full randomized stress course.
    if (stage == COURSE_STAGE_ONE_TURN) {
        int points[][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 } };
        room_count = 3;
        memcpy(route, points, sizeof(points));
        if (side < 0)
            for (int i = 0; i < room_count; i++) route[i][1] = -route[i][1];
    } else if (stage == COURSE_STAGE_TWO_TURNS) {
        int points[][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } };
        room_count = 4;
        memcpy(route, points, sizeof(points));
        if (side < 0)
            for (int i = 0; i < room_count; i++) route[i][1] = -route[i][1];
    } else if (stage == COURSE_STAGE_JUMP) {
        int points[][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 } };
        room_count = 4;
        memcpy(route, points, sizeof(points));
        if (side < 0)
            for (int i = 0; i < room_count; i++) route[i][1] = -route[i][1];
    } else {
        int points[][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 },
                            { 3, 0 }, { 3, 1 } };
        room_count = 5;
        memcpy(route, points, sizeof(points));
        if (side < 0)
            for (int i = 0; i < room_count; i++) route[i][1] = -route[i][1];
    }

    p->column_count = 0;
    p->room_count = room_count;
    p->ceiling_room = -1;
    p->ceiling_clearance = 0.0f;
    for (int i = 0; i < room_count; i++)
        set_hallway_room(p, i, route[i][0], route[i][1]);

    p->route_length = 0.0f;
    for (int i = 0; i + 1 < room_count; i++) {
        CourseRoom* a = &p->rooms[i];
        CourseRoom* b = &p->rooms[i + 1];
        int axis = a->gx != b->gx ? 0 : 1;
        float width = stage <= COURSE_STAGE_TWO_TURNS ?
            course_rand_range(env, 2.35f, 2.85f) :
            course_rand_range(env, 2.05f, 2.55f);
        p->route_doors[i] = (CourseDoor){
            .x = (a->x + b->x) * 0.5f,
            .z = (a->z + b->z) * 0.5f,
            .width = width,
            .height = course_rand_range(env, 2.25f, 2.55f),
            .axis = axis,
            .kind = HALL_DOOR_NORMAL,
        };
        float dx = b->x - a->x;
        float dz = b->z - a->z;
        p->route_length += sqrtf(dx * dx + dz * dz);
    }

    if (stage == COURSE_STAGE_JUMP) {
        // Put the lip on a straight segment. The turn comes after it, so the
        // learner does not have to discover jumping and turning simultaneously.
        p->route_doors[1].kind = HALL_DOOR_JUMP;
        p->route_doors[1].width = course_rand_range(env, 2.0f, 2.4f);
    } else if (stage == COURSE_STAGE_CROUCH) {
        // Two repetitions teach the action before the final turn.
        for (int i = 1; i <= 2; i++) {
            p->route_doors[i].kind = HALL_DOOR_LOW;
            p->route_doors[i].height = course_rand_range(env, 1.18f, 1.28f);
        }
    }

    p->goal_x = p->rooms[room_count - 1].x;
    p->goal_z = p->rooms[room_count - 1].z;
    p->goal_y = 0.0f;
}

static bool hallway_room_clear(const CourseParams* p, int used, int gx, int gz) {
    for (int i = 0; i < used; i++) {
        int dx = abs(gx - p->rooms[i].gx);
        int dz = abs(gz - p->rooms[i].gz);
        if (dx == 0 && dz == 0)
            return false;
        // Do not let non-consecutive rooms touch. That would create an
        // accidental shortcut through a shared wall.
        if (i < used - 1 && dx + dz == 1)
            return false;
    }
    return abs(gz) <= 2;
}

static void randomize_hallway_course(Shenanigans3D* env) {
    CourseParams* p = &env->course;
    int room_count = env->course_difficulty >= 3 ? 11 : 8;
    bool generated = false;

    // The route advances east but may turn north or south. This gives the
    // policy real hallway turns without allowing a random walk to fold back
    // onto itself or make the goal unreachable by a shorter outside path.
    for (int attempt = 0; attempt < 64 && !generated; attempt++) {
        p->rooms[0] = (CourseRoom){ .gx = 0, .gz = 0, .x = 0.0f, .z = 0.0f };
        int heading = 0; // 0 east, 1 north, 3 south
        generated = true;
        for (int i = 1; i < room_count; i++) {
            int candidates[3];
            int candidate_count = 0;
            if (heading == 0) {
                bool turn = course_rand01(env) < (env->course_difficulty >= 3 ? 0.45f : 0.25f);
                int side = course_rand01(env) < 0.5f ? 1 : 3;
                if (turn) {
                    candidates[candidate_count++] = side;
                    candidates[candidate_count++] = 0;
                    candidates[candidate_count++] = side == 1 ? 3 : 1;
                } else {
                    candidates[candidate_count++] = 0;
                    candidates[candidate_count++] = side;
                    candidates[candidate_count++] = side == 1 ? 3 : 1;
                }
            } else {
                candidates[candidate_count++] = heading;
                candidates[candidate_count++] = 0;
            }

            int chosen = -1;
            for (int c = 0; c < candidate_count; c++) {
                int direction = candidates[c];
                int dx = direction == 0 ? 1 : 0;
                int dz = direction == 1 ? 1 : (direction == 3 ? -1 : 0);
                int gx = p->rooms[i - 1].gx + dx;
                int gz = p->rooms[i - 1].gz + dz;
                if (hallway_room_clear(p, i, gx, gz)) {
                    chosen = direction;
                    p->rooms[i] = (CourseRoom){
                        .gx = gx, .gz = gz,
                        .x = gx * HALL_ROOM_SPACING,
                        .z = gz * HALL_ROOM_SPACING,
                    };
                    break;
                }
            }
            if (chosen < 0) {
                generated = false;
                break;
            }
            heading = chosen;
        }
    }

    if (!generated) {
        static const int fallback[COURSE_MAX_ROOMS][2] = {
            { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 },
            { 2, 2 }, { 3, 2 }, { 4, 2 }, { 4, 1 },
            { 5, 1 }, { 5, 0 }, { 6, 0 }, { 7, 0 },
        };
        for (int i = 0; i < room_count; i++) {
            p->rooms[i] = (CourseRoom){
                .gx = fallback[i][0], .gz = fallback[i][1],
                .x = fallback[i][0] * HALL_ROOM_SPACING,
                .z = fallback[i][1] * HALL_ROOM_SPACING,
            };
        }
    }

    p->room_count = room_count;
    p->ceiling_room = -1;
    p->ceiling_clearance = 0.0f;
    p->route_length = (room_count - 1) * HALL_ROOM_SPACING;
    p->goal_x = p->rooms[room_count - 1].x;
    p->goal_z = p->rooms[room_count - 1].z;
    p->goal_y = 0.0f;

    int low_count = 0, jump_count = 0;
    for (int i = 0; i < room_count - 1; i++) {
        CourseRoom* a = &p->rooms[i];
        CourseRoom* b = &p->rooms[i + 1];
        int axis = a->gx != b->gx ? 0 : 1;
        float offset = course_rand_range(env, -0.35f, 0.35f);
        CourseDoor door = {
            .x = (a->x + b->x) * 0.5f + (axis == 1 ? offset : 0.0f),
            .z = (a->z + b->z) * 0.5f + (axis == 0 ? offset : 0.0f),
            .width = course_rand_range(env, 1.35f, 1.75f),
            .height = course_rand_range(env, 2.00f, 2.30f),
            .axis = axis,
            .kind = HALL_DOOR_NORMAL,
        };
        float type = course_rand01(env);
        if (i > 0 && type < 0.20f) {
            door.kind = HALL_DOOR_LOW;
            door.height = course_rand_range(env, 1.08f, 1.18f);
            low_count++;
        } else if (i > 0 && type < 0.40f) {
            door.kind = HALL_DOOR_JUMP;
            door.height = course_rand_range(env, 2.00f, 2.25f);
            jump_count++;
        }
        p->route_doors[i] = door;
    }

    // Ensure every generated layout teaches both non-trivial actions while
    // keeping the first door a predictable onboarding section.
    if (low_count == 0 && room_count > 3) {
        p->route_doors[room_count / 2].kind = HALL_DOOR_LOW;
        p->route_doors[room_count / 2].height = 1.12f;
    }
    if (jump_count == 0 && room_count > 4) {
        p->route_doors[room_count / 2 + 1].kind = HALL_DOOR_JUMP;
        p->route_doors[room_count / 2 + 1].height = 2.10f;
    }
    if (course_rand01(env) < 0.65f && room_count > 3) {
        p->ceiling_room = 1 + rand_r(&env->rng) % (room_count - 2);
        p->ceiling_clearance = course_rand_range(env, 1.08f, 1.22f);
    }

    // A jump gate cannot be placed inside or immediately before a low
    // ceiling. Re-roll that combination as a normal doorway and keep one
    // jump gate elsewhere in the route.
    for (int i = 0; i < room_count - 1; i++) {
        if (p->route_doors[i].kind == HALL_DOOR_JUMP &&
            (i == p->ceiling_room || i + 1 == p->ceiling_room)) {
            p->route_doors[i].kind = HALL_DOOR_NORMAL;
        }
    }
    bool haveJump = false;
    for (int i = 1; i < room_count - 1; i++) {
        if (p->route_doors[i].kind == HALL_DOOR_JUMP) {
            haveJump = true;
            break;
        }
    }
    if (!haveJump) {
        for (int i = 1; i < room_count - 1; i++) {
            if (i != p->ceiling_room && i + 1 != p->ceiling_room) {
                p->route_doors[i].kind = HALL_DOOR_JUMP;
                p->route_doors[i].height = 2.10f;
                break;
            }
        }
    }
}

static void randomize_course(Shenanigans3D* env) {
    CourseParams* p = &env->course;
    int difficulty = env->course_difficulty;
    if (difficulty < 1) difficulty = 1;
    if (difficulty > 3) difficulty = 3;

    if (difficulty == 3) {
        randomize_hallway_course(env);
        return;
    }

    if (difficulty == 1) {
        p->doors[0] = (CourseDoor){
            .x = 6.0f, .z = course_rand_range(env, -0.65f, 0.65f),
            .width = course_rand_range(env, 1.25f, 1.55f),
            .height = course_rand_range(env, 2.00f, 2.20f),
        };
        p->doors[1] = (CourseDoor){
            .x = course_rand_range(env, 8.6f, 9.4f),
            .z = course_rand_range(env, -0.75f, 0.75f),
            .width = course_rand_range(env, 1.20f, 1.50f),
            .height = course_rand_range(env, 2.00f, 2.15f),
        };
        p->doors[2] = (CourseDoor){
            .x = course_rand_range(env, 18.0f, 19.0f),
            .z = course_rand_range(env, -0.75f, 0.75f),
            .width = course_rand_range(env, 1.20f, 1.50f),
            .height = course_rand_range(env, 2.00f, 2.15f),
        };
        p->jump_x = course_rand_range(env, 10.2f, 10.9f);
        p->jump_width = course_rand_range(env, 0.35f, 0.55f);
        p->pit_depth = course_rand_range(env, 0.65f, 0.90f);
        p->tunnel_start = course_rand_range(env, 12.0f, 12.6f);
        p->tunnel_end = p->tunnel_start + course_rand_range(env, 3.8f, 4.8f);
        p->tunnel_clearance = course_rand_range(env, 1.08f, 1.22f);
        p->hole_start = course_rand_range(env, 20.5f, 22.0f);
        p->hole_end = p->hole_start + course_rand_range(env, 3.0f, 4.0f);
    } else {
        p->doors[0] = (CourseDoor){
            .x = 6.0f, .z = course_rand_range(env, -0.90f, 0.90f),
            .width = course_rand_range(env, 1.10f, 1.35f),
            .height = course_rand_range(env, 1.95f, 2.10f),
        };
        p->doors[1] = (CourseDoor){
            .x = course_rand_range(env, 8.5f, 9.5f),
            .z = course_rand_range(env, -0.95f, 0.95f),
            .width = course_rand_range(env, 1.05f, 1.30f),
            .height = course_rand_range(env, 1.95f, 2.05f),
        };
        p->doors[2] = (CourseDoor){
            .x = course_rand_range(env, 18.0f, 19.3f),
            .z = course_rand_range(env, -0.95f, 0.95f),
            .width = course_rand_range(env, 1.05f, 1.30f),
            .height = course_rand_range(env, 1.95f, 2.05f),
        };
        p->jump_x = course_rand_range(env, 10.1f, 11.0f);
        p->jump_width = course_rand_range(env, 0.40f, 0.65f);
        p->pit_depth = course_rand_range(env, 0.75f, 1.05f);
        p->tunnel_start = course_rand_range(env, 11.8f, 12.8f);
        p->tunnel_end = p->tunnel_start + course_rand_range(env, 4.5f, 5.8f);
        p->tunnel_clearance = course_rand_range(env, 1.06f, 1.14f);
        p->hole_start = course_rand_range(env, 20.0f, 22.0f);
        p->hole_end = p->hole_start + course_rand_range(env, 3.0f, 4.0f);
    }

    // Leave a straight recovery section between the tunnel and the drop, and
    // enough lower corridor to approach the fixed goal.
    if (p->hole_start < p->tunnel_end + 3.0f)
        p->hole_start = p->tunnel_end + 3.0f;
    if (p->doors[2].x < p->tunnel_end + 0.8f)
        p->doors[2].x = p->tunnel_end + 0.8f;
    if (p->hole_start < p->doors[2].x + 1.0f)
        p->hole_start = p->doors[2].x + 1.0f;
    if (p->hole_end < p->hole_start + 3.0f)
        p->hole_end = p->hole_start + 3.0f;
    if (p->hole_end > 28.5f)
        p->hole_end = 28.5f;
}

static void add_course_door(Shenanigans3D* env, const CourseDoor* door) {
    float door_lo = door->z - door->width * 0.5f;
    float door_hi = door->z + door->width * 0.5f;
    add_box_bounds(env, door->x - 0.2f, door->x + 0.2f,
                   0.0f, COURSE_WALL_TOP, -2.0f, door_lo);
    add_box_bounds(env, door->x - 0.2f, door->x + 0.2f,
                   0.0f, COURSE_WALL_TOP, door_hi, 2.0f);
    add_box_bounds(env, door->x - 0.2f, door->x + 0.2f,
                   door->height, COURSE_WALL_TOP, door_lo, door_hi);
}

static int hallway_route_neighbor(const CourseParams* p, int room_index,
                                  int dx, int dz) {
    int gx = p->rooms[room_index].gx + dx;
    int gz = p->rooms[room_index].gz + dz;
    for (int i = 0; i < p->room_count; i++) {
        if (p->rooms[i].gx == gx && p->rooms[i].gz == gz &&
            abs(i - room_index) == 1)
            return i;
    }
    return -1;
}

static void add_hallway_full_wall(Shenanigans3D* env, float x, float z,
                                  int axis) {
    if (axis == 0) {
        add_box_bounds(env, x - HALL_WALL_THICKNESS, x + HALL_WALL_THICKNESS,
                       0.0f, HALL_WALL_HEIGHT,
                       z - HALL_ROOM_HALF_SIZE, z + HALL_ROOM_HALF_SIZE);
    } else {
        add_box_bounds(env, x - HALL_ROOM_HALF_SIZE, x + HALL_ROOM_HALF_SIZE,
                       0.0f, HALL_WALL_HEIGHT,
                       z - HALL_WALL_THICKNESS, z + HALL_WALL_THICKNESS);
    }
}

static void add_hallway_door(Shenanigans3D* env, const CourseDoor* door) {
    float lo, hi;
    if (door->axis == 0) {
        lo = door->z - door->width * 0.5f;
        hi = door->z + door->width * 0.5f;
        add_box_bounds(env, door->x - HALL_WALL_THICKNESS,
                       door->x + HALL_WALL_THICKNESS, 0.0f, HALL_WALL_HEIGHT,
                       door->z - HALL_ROOM_HALF_SIZE, lo);
        add_box_bounds(env, door->x - HALL_WALL_THICKNESS,
                       door->x + HALL_WALL_THICKNESS, 0.0f, HALL_WALL_HEIGHT,
                       hi, door->z + HALL_ROOM_HALF_SIZE);
    } else {
        lo = door->x - door->width * 0.5f;
        hi = door->x + door->width * 0.5f;
        add_box_bounds(env, door->x - HALL_ROOM_HALF_SIZE, lo,
                       0.0f, HALL_WALL_HEIGHT,
                       door->z - HALL_WALL_THICKNESS,
                       door->z + HALL_WALL_THICKNESS);
        add_box_bounds(env, hi, door->x + HALL_ROOM_HALF_SIZE,
                       0.0f, HALL_WALL_HEIGHT,
                       door->z - HALL_WALL_THICKNESS,
                       door->z + HALL_WALL_THICKNESS);
    }

    if (door->kind == HALL_DOOR_LOW) {
        // A low doorway is a real overhead obstruction. The character must
        // crouch to reduce its hull height before crossing this opening.
        if (door->axis == 0) {
            add_box_bounds(env, door->x - HALL_WALL_THICKNESS,
                           door->x + HALL_WALL_THICKNESS,
                           door->height, HALL_WALL_HEIGHT, lo, hi);
        } else {
            add_box_bounds(env, lo, hi, door->height, HALL_WALL_HEIGHT,
                           door->z - HALL_WALL_THICKNESS,
                           door->z + HALL_WALL_THICKNESS);
        }
    } else if (door->kind == HALL_DOOR_JUMP) {
        if (door->axis == 0) {
            add_box(env, door->x, 0.32f, door->z,
                    HALL_WALL_THICKNESS, 0.32f, door->width * 0.5f);
        } else {
            add_box(env, door->x, 0.32f, door->z,
                    door->width * 0.5f, 0.32f, HALL_WALL_THICKNESS);
        }
    }
}

static void build_hallway_course(Shenanigans3D* env) {
    CourseParams* p = &env->course;
    env->numBoxes = 0;

    for (int i = 0; i < p->room_count; i++) {
        CourseRoom* room = &p->rooms[i];
        add_box(env, room->x, -0.5f, room->z,
                HALL_ROOM_HALF_SIZE, 0.5f, HALL_ROOM_HALF_SIZE);

        const int directions[4][2] = {
            { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0], dz = directions[d][1];
            int neighbor = hallway_route_neighbor(p, i, dx, dz);
            int axis = dx != 0 ? 0 : 1;
            float wallX = room->x + dx * HALL_ROOM_HALF_SIZE;
            float wallZ = room->z + dz * HALL_ROOM_HALF_SIZE;
            if (neighbor >= 0) {
                if (i < neighbor)
                    add_hallway_door(env, &p->route_doors[i]);
            } else {
                add_hallway_full_wall(env, wallX, wallZ, axis);
            }
        }
    }

    if (p->ceiling_room >= 0 && p->ceiling_room < p->room_count) {
        CourseRoom* room = &p->rooms[p->ceiling_room];
        add_box_bounds(env, room->x - HALL_ROOM_HALF_SIZE,
                       room->x + HALL_ROOM_HALF_SIZE,
                       p->ceiling_clearance, HALL_WALL_HEIGHT,
                       room->z - HALL_ROOM_HALF_SIZE,
                       room->z + HALL_ROOM_HALF_SIZE);
    }

    add_box(env, p->goal_x, p->goal_y + 0.25f, p->goal_z,
            0.35f, 0.25f, 0.35f);
}

static void build_column_course(Shenanigans3D* env) {
    CourseParams* p = &env->course;
    env->numBoxes = 0;

    // Open arena with shallow boundaries. Columns remain isolated so there
    // is always room to steer around them rather than a hidden wall puzzle.
    add_box(env, 13.0f, -1.0f, 0.0f, 17.0f, 1.0f, 8.0f);
    add_box_bounds(env, -4.0f, 30.0f, 0.0f, HALL_WALL_HEIGHT,
                   -8.4f, -8.0f);
    add_box_bounds(env, -4.0f, 30.0f, 0.0f, HALL_WALL_HEIGHT,
                   8.0f, 8.4f);
    add_box_bounds(env, -4.4f, -4.0f, 0.0f, HALL_WALL_HEIGHT,
                   -8.0f, 8.0f);
    add_box_bounds(env, 30.0f, 30.4f, 0.0f, HALL_WALL_HEIGHT,
                   -8.0f, 8.0f);

    for (int i = 0; i < p->column_count; i++) {
        CourseColumn* column = &p->columns[i];
        add_box(env, column->x, column->height * 0.5f, column->z,
                column->radius, column->height * 0.5f, column->radius);
    }

    add_box(env, p->goal_x, p->goal_y + 0.25f, p->goal_z,
            0.35f, 0.25f, 0.35f);
}

static void build_course(Shenanigans3D* env) {
    if (env->course.column_count > 0) {
        build_column_course(env);
        return;
    }
    if (env->course.room_count > 0) {
        build_hallway_course(env);
        return;
    }
    env->numBoxes = 0;

    // A: floor x[-4,6] top 0
    add_box(env, 1.0f, -1.0f, 0.0f, 5.0f, 1.0f, 2.0f);

    // Three sequential doors force lateral alignment but never create a
    // choice of route.
    for (int i = 0; i < COURSE_DOORS; i++)
        add_course_door(env, &env->course.doors[i]);

    // B: upper floor with a shallow jump pit before the tunnel.
    float pit_start = env->course.jump_x - env->course.jump_width * 0.5f;
    float pit_end = env->course.jump_x + env->course.jump_width * 0.5f;
    add_box_bounds(env, 6.0f, pit_start, -2.0f, 0.0f, -2.0f, 2.0f);
    add_box_bounds(env, pit_end, env->course.hole_start,
                   -2.0f, 0.0f, -2.0f, 2.0f);
    add_box_bounds(env, pit_start, pit_end,
                   -env->course.pit_depth - 0.2f, -env->course.pit_depth,
                   -2.0f, 2.0f);

    // Crouch tunnel ceiling, with randomized start, length, and clearance.
    add_box_bounds(env, env->course.tunnel_start, env->course.tunnel_end,
                   env->course.tunnel_clearance, COURSE_WALL_TOP, -2.0f, 2.0f);

    // Hole: no floor spans [hole_start, hole_end]. The lower slab catches the
    // player at -3m after the gap, creating a real randomized drop.
    add_box_bounds(env, env->course.hole_end, COURSE_FLOOR_END,
                   -4.0f, -3.0f, -2.0f, 2.0f);

    // Keep the route linear. These walls prevent a lateral drift from leaving
    // the playable strip while still exposing the randomized door openings.
    add_box_bounds(env, -4.0f, COURSE_FLOOR_END,
                   -4.0f, COURSE_WALL_TOP, -2.25f, -2.0f);
    add_box_bounds(env, -4.0f, COURSE_FLOOR_END,
                   -4.0f, COURSE_WALL_TOP, 2.0f, 2.25f);

    // goal pedestal
    add_box(env, GOAL_X, GOAL_Y + 0.25f, GOAL_Z, 0.35f, 0.25f, 0.35f);
}

static void create_course_world(Shenanigans3D* env) {
    set_fixed_course(&env->course);
    if (env->course_stage_enabled) {
        if (env->course_stage == COURSE_STAGE_COLUMNS) {
            randomize_column_course(env);
        } else if (env->course_stage >= COURSE_STAGE_ONE_TURN &&
                   env->course_stage <= COURSE_STAGE_CROUCH) {
            curriculum_hallway_course(env, env->course_stage);
        } else if (env->course_stage == COURSE_STAGE_STRESS) {
            // Stage 5 is the explicit final stress course; do not let a
            // leftover legacy difficulty setting silently shorten it.
            int difficulty = env->course_difficulty;
            env->course_difficulty = 3;
            randomize_hallway_course(env);
            env->course_difficulty = difficulty;
        }
    } else if (env->course_mode == COURSE_MODE_RANDOM ||
        env->course_mode == COURSE_MODE_RANDOM_EVERY_RESET) {
        randomize_course(env);
    }

    b3WorldDef worldDef = b3DefaultWorldDef();
    env->world = b3CreateWorld(&worldDef);
    build_course(env);
    pd_char_init(&env->ch, env->world, (b3Pos){ 0.0f, 1.0f, 0.0f });
}

// ---------------------------------------------------------------------------
// Senses (real engine rays)
// ---------------------------------------------------------------------------

typedef struct S3DRayCastContext {
    const PDCharacter* character;
    float closest_fraction;
    bool hit;
} S3DRayCastContext;

static float s3d_ray_cast_callback(b3ShapeId shapeId, b3Pos point,
                                   b3Vec3 normal, float fraction,
                                   uint64_t userMaterialId, int triangleIndex,
                                   int childIndex, void* context) {
    (void)point;
    (void)normal;
    (void)userMaterialId;
    (void)triangleIndex;
    (void)childIndex;
    S3DRayCastContext* ctx = (S3DRayCastContext*)context;
    for (int i = 0; i < ctx->character->ownShapeCount; i++) {
        if (B3_ID_EQUALS(shapeId, ctx->character->ownShapes[i]))
            return -1.0f;
    }
    if (fraction <= 0.0f) {
        ctx->closest_fraction = 0.0f;
        ctx->hit = true;
        return 0.0f;
    }
    if (fraction < ctx->closest_fraction) {
        ctx->closest_fraction = fraction;
        ctx->hit = true;
    }
    return ctx->closest_fraction;
}

static float cast_ray_dist(Shenanigans3D* env, float ox, float oy, float oz,
                           float dx, float dy, float dz, float maxT) {
    b3Vec3 trans = { dx * maxT, dy * maxT, dz * maxT };
    S3DRayCastContext context = {
        .character = &env->ch,
        .closest_fraction = 1.0f,
        .hit = false,
    };
    b3World_CastRay(env->world, (b3Pos){ ox, oy, oz }, trans,
                    b3DefaultQueryFilter(), s3d_ray_cast_callback, &context);
    return context.hit ? context.closest_fraction * maxT : maxT;
}

static float feet_x(Shenanigans3D* env) { return (float)pd_char_feet_position(&env->ch).x; }
static float feet_y(Shenanigans3D* env) { return (float)pd_char_feet_position(&env->ch).y; }
static float feet_z(Shenanigans3D* env) { return (float)pd_char_feet_position(&env->ch).z; }

static int s3d_occupancy_index(int vertical, int lateral, int forward) {
    return (vertical * OCCUPANCY_LATERAL_BINS + lateral) *
           OCCUPANCY_FORWARD_BINS + forward;
}

static float s3d_occupancy_distance(Shenanigans3D* env, b3Pos origin,
                                     b3Vec3 forward) {
    // Perception samples geometry; the character hull remains the authority
    // for whether the agent can actually fit through the opening.
    return cast_ray_dist(env, origin.x, origin.y, origin.z,
                         forward.x, forward.y, forward.z, OCCUPANCY_RANGE);
}

static void s3d_update_occupancy(Shenanigans3D* env) {
    int updateInterval = env->occupancy_update_interval > 0 ?
                         env->occupancy_update_interval : OCCUPANCY_UPDATE_INTERVAL;
    if (env->occupancy_valid && env->tick % updateInterval != 0)
        return;

    // 0 = unknown, 0.5 = known free, 1 = occupied. Each ray certifies its
    // centerline up to its first hit, but says nothing beyond that hit.
    memset(env->occupancy, 0, sizeof(env->occupancy));
    float px = feet_x(env), py = feet_y(env), pz = feet_z(env);
    float cosYaw = cosf(env->yaw), sinYaw = sinf(env->yaw);
    b3Vec3 forward = { cosYaw, 0.0f, sinYaw };
    b3Vec3 right = { -sinYaw, 0.0f, cosYaw };
    float forwardStep = OCCUPANCY_RANGE / (float)OCCUPANCY_FORWARD_BINS;

    for (int v = 0; v < OCCUPANCY_VERTICAL_BINS; v++) {
        float height = OCCUPANCY_VERTICAL_MIN + v * OCCUPANCY_VERTICAL_STEP;
        for (int l = 0; l < OCCUPANCY_LATERAL_BINS; l++) {
            float lateral = OCCUPANCY_LATERAL_MIN +
                           (l + 0.5f) * OCCUPANCY_LATERAL_STEP;
            b3Pos origin = {
                px + right.x * lateral,
                py + height,
                pz + right.z * lateral,
            };
            float hitDistance = s3d_occupancy_distance(env, origin, forward);
            int hitBin = OCCUPANCY_FORWARD_BINS;
            if (hitDistance < OCCUPANCY_RANGE) {
                hitBin = (int)(hitDistance / forwardStep);
                if (hitBin < 0) hitBin = 0;
                if (hitBin >= OCCUPANCY_FORWARD_BINS)
                    hitBin = OCCUPANCY_FORWARD_BINS - 1;
            }
            for (int f = 0; f < hitBin; f++)
                env->occupancy[s3d_occupancy_index(v, l, f)] = 0.5f;
            if (hitBin < OCCUPANCY_FORWARD_BINS)
                env->occupancy[s3d_occupancy_index(v, l, hitBin)] = 1.0f;
        }
    }
    env->occupancy_valid = true;
}

static void s3d_update_depth(Shenanigans3D* env) {
    int updateInterval = env->depth_update_interval > 0 ?
                         env->depth_update_interval : DEPTH_MAP_UPDATE_INTERVAL;
    if (env->depth_valid && env->tick % updateInterval != 0)
        return;

    float px = feet_x(env), py = feet_y(env), pz = feet_z(env);
    PDCharacter* c = &env->ch;
    float eyeY = py + c->totalHeight - 0.2032f;
    static const float pitches[DEPTH_MAP_HEIGHT] = {
        -60.0f, -35.0f, -10.0f, 15.0f, 40.0f,
    };
    for (int p = 0; p < DEPTH_MAP_HEIGHT; p++) {
        float pitch = pitches[p] * (float)M_PI / 180.0f;
        float cp = cosf(pitch);
        for (int a = 0; a < DEPTH_MAP_WIDTH; a++) {
            float angle = env->yaw +
                          (float)a / (float)DEPTH_MAP_WIDTH * 2.0f * (float)M_PI;
            float distance = cast_ray_dist(env, px, eyeY, pz,
                                           cosf(angle) * cp, sinf(pitch),
                                           sinf(angle) * cp, RAY_RANGE);
            env->depth[p * DEPTH_MAP_WIDTH + a] = distance / RAY_RANGE;
        }
    }
    env->depth_valid = true;
}

static float goal_dist(Shenanigans3D* env) {
    float dx = env->course.goal_x - feet_x(env);
    float dz = env->course.goal_z - feet_z(env);
    float dy = env->course.goal_y - feet_y(env);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float hallway_progress(Shenanigans3D* env) {
    CourseParams* p = &env->course;
    float px = feet_x(env), pz = feet_z(env);
    float bestDistance = INFINITY;
    float bestProgress = 0.0f;
    float accumulated = 0.0f;
    for (int i = 0; i + 1 < p->room_count; i++) {
        float ax = p->rooms[i].x, az = p->rooms[i].z;
        float bx = p->rooms[i + 1].x, bz = p->rooms[i + 1].z;
        float dx = bx - ax, dz = bz - az;
        float lengthSquared = dx * dx + dz * dz;
        float t = ((px - ax) * dx + (pz - az) * dz) / lengthSquared;
        t = fmaxf(0.0f, fminf(1.0f, t));
        float nearestX = ax + t * dx, nearestZ = az + t * dz;
        float distanceSquared = (px - nearestX) * (px - nearestX) +
                                (pz - nearestZ) * (pz - nearestZ);
        if (distanceSquared < bestDistance) {
            bestDistance = distanceSquared;
            bestProgress = accumulated + t * sqrtf(lengthSquared);
        }
        accumulated += sqrtf(lengthSquared);
    }
    return bestProgress;
}

static float progress_dist(Shenanigans3D* env) {
    if (env->course.room_count > 1)
        return env->course.route_length - hallway_progress(env);
    float dx = env->course.goal_x - feet_x(env);
    float dz = env->course.goal_z - feet_z(env);
    return sqrtf(dx * dx + dz * dz);
}

static void compute_observations(Shenanigans3D* env) {
    PDCharacter* c = &env->ch;
    obs_t* obs = (obs_t*)env->agents[0].observations;

    float px = feet_x(env), py = feet_y(env), pz = feet_z(env);
    float cy = cosf(env->yaw), sy = sinf(env->yaw);
    s3d_update_occupancy(env);
    s3d_update_depth(env);

    b3Vec3 v = b3Body_GetLinearVelocity(c->body);
    obs[0] = (cy * v.x + sy * v.z) / MAX_SPEED;
    obs[1] = (-sy * v.x + cy * v.z) / MAX_SPEED;
    obs[2] = sy;
    obs[3] = cy;
    obs[4] = c->onGround ? 1.0f : 0.0f;
    obs[5] = pd_char_is_crouched(c) ? 1.0f : 0.0f;
    obs[6] = fminf(cast_ray_dist(env, px, py + 0.05f, pz, 0, -1, 0, GROUND_RAY),
                   GROUND_RAY) / GROUND_RAY;
    float routeLength = env->course.route_length > 0.0f ?
                        env->course.route_length : COURSE_LENGTH;
    obs[7] = fminf(goal_dist(env) / routeLength, 1.5f);
    float bearing = atan2f(env->course.goal_z - pz,
                           env->course.goal_x - px) - env->yaw;
    while (bearing > (float)M_PI) bearing -= 2.0f * (float)M_PI;
    while (bearing < -(float)M_PI) bearing += 2.0f * (float)M_PI;
    obs[8] = sinf(bearing);
    obs[9] = cosf(bearing);
    // Vertical velocity helps distinguish jumping from falling.
    obs[10] = fmaxf(-1.0f, fminf(v.y / 15.0f, 1.0f));

    float* depth = obs + DEPTH_MAP_OFFSET;
    memcpy(depth, env->depth, sizeof(env->depth));
    float* occupancy = obs + OCCUPANCY_OFFSET;
    memcpy(occupancy, env->occupancy, sizeof(env->occupancy));
}

static bool forward_obstacle_collision(Shenanigans3D* env, int fwd,
                                       b3Vec3 forward) {
    if (fwd <= 0)
        return false;

    b3Vec3 velocity = b3Body_GetLinearVelocity(env->ch.body);
    float forwardSpeed = velocity.x * forward.x + velocity.z * forward.z;
    if (forwardSpeed > 0.5f)
        return false;

    b3Pos feet = pd_char_feet_position(&env->ch);
    b3Pos target = {
        feet.x + forward.x * HEAD_HIT_RANGE,
        feet.y,
        feet.z + forward.z * HEAD_HIT_RANGE,
    };
    // Use the controller's feet-anchored hull so a lip that catches the feet
    // is treated the same as an obstruction at chest height. The current
    // stance is used, so a crouched hull can pass under a low ceiling.
    pd_trace_result trace = pd_char_trace_body(&env->ch, feet, target, 1.0f, 1.0f);
    return trace.hit || trace.startedSolid;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void init(Shenanigans3D* env) {
    env->tag = 0;
    env->boundary_reached = 0;
    env->depth_valid = false;
    env->occupancy_valid = false;
    env->depth_update_interval = DEPTH_MAP_UPDATE_INTERVAL;
    env->occupancy_update_interval = OCCUPANCY_UPDATE_INTERVAL;
    env->reward_progress = PROGRESS_SHAPING_DEFAULT;
    env->time_cost = TIME_COST;
    env->reward_goal = GOAL_BONUS;
    env->reward_fall = -FALL_PENALTY;
    env->reward_head_hit = HEAD_HIT_REWARD_DEFAULT;
    env->jump_penalty = JUMP_PENALTY_DEFAULT;
    env->crouch_penalty = CROUCH_PENALTY_DEFAULT;
    env->crouch_enter_commit_ticks = CROUCH_ENTER_COMMIT_TICKS_DEFAULT;
    env->crouch_exit_commit_ticks = CROUCH_EXIT_COMMIT_TICKS_DEFAULT;
    env->crouch_commit_ticks = 0;
    create_course_world(env);
}

void allocate_env(Shenanigans3D* env) {
    init(env);
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->max_ticks = MAX_TICKS_DEFAULT;

    DictItem* it = dict_find(kwargs, "course_mode");
    env->course_mode = it ? (int)it->value : COURSE_MODE_FIXED;
    it = dict_find(kwargs, "course_difficulty");
    env->course_difficulty = it ? (int)it->value : COURSE_DIFFICULTY_DEFAULT;
    it = dict_find(kwargs, "course_stage");
    env->course_stage = it ? (int)it->value : -1;
    env->course_stage_enabled = env->course_stage >= 0;

    init((Shenanigans3D*)env);

    // reward shaping — all tunable from config without recompile
    it = dict_find(kwargs, "reward_progress");
    ((Shenanigans3D*)env)->reward_progress =
        it ? (float)it->value : PROGRESS_SHAPING_DEFAULT;
    it = dict_find(kwargs, "time_cost");
    ((Shenanigans3D*)env)->time_cost = it ? (float)it->value : TIME_COST;
    it = dict_find(kwargs, "jump_penalty");
    ((Shenanigans3D*)env)->jump_penalty =
        it ? (float)it->value : -((Shenanigans3D*)env)->time_cost;
    it = dict_find(kwargs, "crouch_penalty");
    ((Shenanigans3D*)env)->crouch_penalty =
        it ? (float)it->value : -((Shenanigans3D*)env)->time_cost;
    it = dict_find(kwargs, "crouch_enter_commit_ticks");
    ((Shenanigans3D*)env)->crouch_enter_commit_ticks =
        it ? (int)it->value : CROUCH_ENTER_COMMIT_TICKS_DEFAULT;
    if (((Shenanigans3D*)env)->crouch_enter_commit_ticks < 0)
        ((Shenanigans3D*)env)->crouch_enter_commit_ticks = 0;
    it = dict_find(kwargs, "crouch_exit_commit_ticks");
    ((Shenanigans3D*)env)->crouch_exit_commit_ticks =
        it ? (int)it->value : CROUCH_EXIT_COMMIT_TICKS_DEFAULT;
    if (((Shenanigans3D*)env)->crouch_exit_commit_ticks < 0)
        ((Shenanigans3D*)env)->crouch_exit_commit_ticks = 0;
    it = dict_find(kwargs, "reward_goal");
    ((Shenanigans3D*)env)->reward_goal = it ? (float)it->value : GOAL_BONUS;
    it = dict_find(kwargs, "reward_fall");
    ((Shenanigans3D*)env)->reward_fall = it ? (float)it->value : -FALL_PENALTY;
    it = dict_find(kwargs, "reward_head_hit");
    ((Shenanigans3D*)env)->reward_head_hit =
        it ? (float)it->value : HEAD_HIT_REWARD_DEFAULT;
    it = dict_find(kwargs, "max_ticks");
    if (it) env->max_ticks = (int)it->value;
    it = dict_find(kwargs, "sensor_depth_interval");
    if (it) env->depth_update_interval = (int)it->value > 0 ?
        (int)it->value : DEPTH_MAP_UPDATE_INTERVAL;
    it = dict_find(kwargs, "sensor_occupancy_interval");
    if (it) env->occupancy_update_interval = (int)it->value > 0 ?
        (int)it->value : OCCUPANCY_UPDATE_INTERVAL;
}

void puf_reset(Shenanigans3D* env) {
    if (env->course_mode == COURSE_MODE_RANDOM_EVERY_RESET && env->tick > 0) {
#pragma omp critical(s3d_world_rebuild)
        {
        b3DestroyWorld(env->world);
        create_course_world(env);
        }
    }
    env->tick = 0;
    env->epReturn = 0.0f;
    env->progressSum = 0.0f;
    env->reachedGoal = false;
    env->depth_valid = false;
    env->occupancy_valid = false;

    // teleport character back to spawn, zero velocity, force standing
    PDCharacter* c = &env->ch;
    // pd_char_set_crouch() preserves feet when it changes height. Position a
    // crouched body at the corresponding center first, otherwise standing up
    // after the teleport would lift the feet by half the stance delta.
    float reset_y = 1.0f;
    if (c->crouched)
        reset_y -= (c->standHeight - c->crouchHeight) * 0.5f;
    b3Body_SetTransform(c->body, (b3Pos){ 0.0f, reset_y, 0.0f }, b3Quat_identity);
    b3Body_SetLinearVelocity(c->body, (b3Vec3){ 0, 0, 0 });
    c->crouchWish = false;
    c->onGround = false;
    c->jumpCooldown = 0.0f;
    c->groundNormal = b3Vec3_axisY;
    c->groundVelocity = (b3Vec3){ 0.0f, 0.0f, 0.0f };
    c->didStep = false;
    c->lastWishVelocity = (b3Vec3){ 0.0f, 0.0f, 0.0f };
    pd_char_set_crouch(c, false); // clearance is trivially clear at spawn
    b3Body_SetTransform(c->body, (b3Pos){ 0.0f, 1.0f, 0.0f }, b3Quat_identity);
    env->crouch_commit_ticks = 0;
    env->yaw = 0.0f;

    env->prevProgressDist = progress_dist(env);
    compute_observations(env);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "score", log->score);
    dict_set(out, "n", log->n);
}

void puf_close(Shenanigans3D* env) {
    if (B3_IS_NULL(env->world) == false) {
        b3DestroyWorld(env->world);
        env->world = b3_nullWorldId;
    }
#ifndef PUFFER_GPU_ENV
    if (env->client != NULL) {
        if (IsWindowReady()) CloseWindow();
        free(env->client);
        env->client = NULL;
    }
#else
    env->client = NULL;
#endif
}

static void end_episode(Shenanigans3D* env, float perfScore) {
    env->lastGoal = perfScore > 0.5f;
    env->log.perf += perfScore;
    env->log.episode_return += env->epReturn;
    env->log.episode_length += (float)env->tick;
    env->log.score += env->progressSum;
    env->log.n += 1.0f;
    *env->agents[0].terminals = 1.0f;
    puf_reset(env);
}

void puf_step(Shenanigans3D* env) {
    // Keep the terminal flag asserted for the completed transition, then
    // clear it when the caller advances the freshly reset episode. The native
    // trainer also clears this buffer, but standalone watch mode does not.
    if (env->agents[0].terminals != NULL) {
        *env->agents[0].terminals = 0.0f;
    }
    // Clear before the max-tick check too. Otherwise a standalone caller can
    // observe the previous transition's reward on the timeout transition.
    if (env->agents[0].rewards != NULL) {
        *env->agents[0].rewards = 0.0f;
    }
    env->tick++;
    if (env->tick > env->max_ticks) {
        end_episode(env, 0.0f);
        return;
    }

    // --- actions ---
    float* atn = env->agents[0].actions;
    int turn = (int)fmaxf(0.0f, fminf(atn[0], 4.0f));
    static const float turnVals[5] = { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f };
    env->yaw += turnVals[turn] * (float)M_PI / 180.0f;

    int fwd = (int)fmaxf(0.0f, fminf(atn[1], 2.0f)) - 1;
    int strafe = (int)fmaxf(0.0f, fminf(atn[2], 2.0f)) - 1;
    bool jump = atn[3] > 0.5f;
    bool successfulJump = jump && env->ch.onGround &&
                          env->ch.jumpCooldown <= 0.0f;

    bool crouchedBefore = pd_char_is_crouched(&env->ch);
    bool requestCrouch = atn[4] > 0.5f;
    if (env->crouch_commit_ticks > 0) {
        // Ignore the opposite input until the fixed stance commitment ends.
        requestCrouch = crouchedBefore;
        env->crouch_commit_ticks--;
    }
    pd_char_set_crouch(&env->ch, requestCrouch);
    bool crouchedAfter = pd_char_is_crouched(&env->ch);
    bool successfulCrouchEntry = !crouchedBefore && crouchedAfter;
    if (successfulCrouchEntry) {
        env->crouch_commit_ticks = env->crouch_enter_commit_ticks;
    } else if (crouchedBefore && !crouchedAfter) {
        env->crouch_commit_ticks = env->crouch_exit_commit_ticks;
    }

    float cy = cosf(env->yaw), sy = sinf(env->yaw);
    b3Vec3 forward = { cy, 0.0f, sy };
    b3Vec3 right = { -sy, 0.0f, cy };
    b3Vec2 throttle = { (float)fwd, (float)strafe };

    if (jump) pd_char_jump(&env->ch);
    pd_char_pre_step(&env->ch, 1.0f / DT, forward, right, throttle);
    b3World_Step(env->world, 1.0f / DT, SIM_SUBSTEPS);
    pd_char_post_step(&env->ch, 1.0f / DT);

    // --- reward shaping (all weights from config) ---
    float d = progress_dist(env);
    // Horizontal potential delta: vertical jumps and falls cannot farm this.
    float progress = env->prevProgressDist - d;
    env->prevProgressDist = d;
    env->progressSum += progress;
    float stepReward = progress * env->reward_progress - env->time_cost;
    if (successfulJump)
        stepReward += env->jump_penalty;
    if (successfulCrouchEntry)
        stepReward += env->crouch_penalty;
    if (forward_obstacle_collision(env, fwd, forward))
        stepReward += env->reward_head_hit;
    *env->agents[0].rewards += stepReward;
    env->epReturn += stepReward;

    float py = feet_y(env);
    if (fabsf(feet_x(env) - env->course.goal_x) < 0.65f &&
        fabsf(feet_z(env) - env->course.goal_z) < 0.65f &&
        py > env->course.goal_y - 1.0f && py < env->course.goal_y + 1.5f) {
        env->reachedGoal = true;
        *env->agents[0].rewards += env->reward_goal;
        env->epReturn += env->reward_goal;
        end_episode(env, 1.0f);
        return;
    }

    if (py < KILL_Y) {
        *env->agents[0].rewards += env->reward_fall;
        env->epReturn += env->reward_fall;
        end_episode(env, 0.0f);
        return;
    }

    compute_observations(env);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
struct Client {
    Camera3D cam;
    int mode; // 0 = orbit follow, 1 = first person, 2 = chase boom
    float orbitYaw;
    float orbitPitch;
    float orbitDist;
};

#ifdef PUFFER_GPU_ENV
void puf_render(Shenanigans3D* env) {
    (void)env;
}
#else
void puf_render(Shenanigans3D* env) {
    PDCharacter* c = &env->ch;
    float px = feet_x(env), py = feet_y(env), pz = feet_z(env);
    bool crouched = pd_char_is_crouched(c);

    if (env->client == NULL) {
        InitWindow(1280, 720, "PufferLib Shenanigans 3D (box3d)");
        SetTargetFPS(60);
        Client* cl = (Client*)calloc(1, sizeof(Client));
        cl->mode = 0;
        // Match the previous high-angle view while allowing mouse orbit.
        cl->orbitYaw = 2.48f;
        cl->orbitPitch = 0.76f;
        cl->orbitDist = 7.9f;
        cl->cam.up = (Vector3){ 0, 1, 0 };
        cl->cam.fovy = 70.0f;
        cl->cam.projection = CAMERA_PERSPECTIVE;
        cl->cam.position = (Vector3){ px - 5.0f, py + 6.0f, pz + 4.0f };
        cl->cam.target = (Vector3){ px, py, pz };
        env->client = cl;
    }
    Client* cl = env->client;
    if (IsKeyPressed(KEY_F)) cl->mode = (cl->mode + 1) % 3;

    if (cl->mode == 0 &&
        (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT))) {
        Vector2 delta = GetMouseDelta();
        cl->orbitYaw -= delta.x * 0.005f;
        cl->orbitPitch = fmaxf(-1.35f, fminf(1.45f, cl->orbitPitch + delta.y * 0.005f));
    }
    float wheel = GetMouseWheelMove();
    if (cl->mode == 0 && wheel != 0.0f)
        cl->orbitDist = fmaxf(2.5f, fminf(25.0f, cl->orbitDist * (1.0f - 0.1f * wheel)));

    float fx = cosf(env->yaw), fz = sinf(env->yaw);
    float eyeY = py + c->totalHeight - 0.2032f;

    if (cl->mode == 0) {
        float cp = cosf(cl->orbitPitch);
        cl->cam.position = (Vector3){
            px + cl->orbitDist * cp * cosf(cl->orbitYaw),
            py + cl->orbitDist * sinf(cl->orbitPitch),
            pz + cl->orbitDist * cp * sinf(cl->orbitYaw)
        };
        cl->cam.target = (Vector3){ px, py + 0.8f, pz };
    } else if (cl->mode == 1) {
        cl->cam.position = (Vector3){ px, eyeY, pz };
        cl->cam.target = (Vector3){ px + fx, eyeY, pz + fz };
    } else {
        float headY = py + c->totalHeight * 0.85f;
        b3Vec3 boom = { -fx * 4.0f, 1.4f, -fz * 4.0f };
        float len = sqrtf(boom.x * boom.x + boom.y * boom.y + boom.z * boom.z);
        b3RayResult rr = b3World_CastRayClosest(
            env->world, (b3Pos){ px, headY, pz }, boom, b3DefaultQueryFilter());
        if (rr.hit && rr.fraction * len < len) len = fmaxf(rr.fraction * len - 0.3f, 0.8f);
        cl->cam.position = (Vector3){ px + boom.x * len / 4.0f, headY + boom.y * len / 4.0f,
                                      pz + boom.z * len / 4.0f };
        cl->cam.target = (Vector3){ px + fx * 2.0f, py + 1.1f, pz + fz * 2.0f };
    }

    BeginDrawing();
    ClearBackground((Color){ 10, 16, 28, 255 });
    BeginMode3D(cl->cam);

    for (int i = 0; i < env->numBoxes; i++) {
        const AABB* b = &env->boxes[i];
        DrawCube((Vector3){ b->cx, b->cy, b->cz }, b->hx * 2, b->hy * 2, b->hz * 2,
                 (Color){ 30, 90, 110, 255 });
        DrawCubeWires((Vector3){ b->cx, b->cy, b->cz }, b->hx * 2, b->hy * 2, b->hz * 2,
                      (Color){ 0, 187, 187, 255 });
    }

    DrawCube((Vector3){ env->course.goal_x, env->course.goal_y + 1.0f,
                       env->course.goal_z }, 0.4f, 2.0f, 0.4f,
             (Color){ 245, 197, 66, 255 });

    Color body = crouched ? (Color){ 245, 140, 66, 255 } : (Color){ 0, 200, 255, 255 };
    // DrawCylinder takes its position at the base, not the center. The
    // physics feet position is already the character's bottom anchor.
    Vector3 pp = { px, py, pz };
    DrawCylinder(pp, 0.28f, 0.28f, c->totalHeight, 12, body);
    DrawSphere((Vector3){ px, py + c->totalHeight - 0.28f, pz }, 0.28f, body);

    b3Vec3 velocity = b3Body_GetLinearVelocity(c->body);
    float groundDistance = cast_ray_dist(env, px, py + 0.05f, pz, 0, -1, 0, GROUND_RAY);
    Color groundColor = c->onGround ? (Color){ 80, 255, 120, 255 }
                                    : (Color){ 255, 90, 90, 255 };
    DrawSphere((Vector3){ px, py, pz }, 0.06f, groundColor);
    if (groundDistance < GROUND_RAY) {
        DrawLine3D((Vector3){ px, py, pz },
                   (Vector3){ px, py + 0.05f - groundDistance, pz }, groundColor);
    }

    for (int r = 0; r < DEPTH_MAP_WIDTH; r++) {
        float ang = env->yaw + (float)r / (float)DEPTH_MAP_WIDTH * 2.0f * (float)M_PI;
        float t = fminf(cast_ray_dist(env, px, eyeY, pz, cosf(ang), 0, sinf(ang),
                                      RAY_RANGE),
                        RAY_RANGE);
        DrawLine3D((Vector3){ px, eyeY, pz },
                   (Vector3){ px + cosf(ang) * t, eyeY, pz + sinf(ang) * t },
                   (Color){ 0, 255, 180, 60 });
    }

    EndMode3D();
    DrawText(TextFormat("tick %d  goal %.1fm  %s", env->tick, (double)goal_dist(env),
                        crouched ? "CROUCH" : ""),
             10, 10, 20, WHITE);
    DrawText(TextFormat("phys y %.2f  vy %.2f  %s  ground %.2fm", (double)py,
                        (double)velocity.y, c->onGround ? "GROUND" : "AIR",
                        (double)groundDistance),
             10, 35, 18, groundColor);
    EndDrawing();
}
#endif
