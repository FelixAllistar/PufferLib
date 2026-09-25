// GPU-resident shenaniguns3d environment.
//
// This is intentionally a specialized Box3D-compatible backend rather than a
// general CUDA rewrite of Box3D. The current environment contains static,
// axis-aligned boxes and one rotation-locked player. The backend keeps that
// exact public action/observation contract and controller ordering, while
// replacing Box3D's per-world allocation, broad phase, and task scheduler with
// fixed-size device data that can be stepped one lane per CUDA thread.

#ifndef PUFFER_SHENANIGUNS3D_GPU_CU
#define PUFFER_SHENANIGUNS3D_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "shenaniguns3d.cu requires build.sh shenaniguns3d --gpu"
#endif

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef S3D_GPU_BLOCK_SIZE
#define S3D_GPU_BLOCK_SIZE 256
#endif

#define S3D_GPU_PI 3.14159265358979323846f
#define S3D_GPU_SRC 0.0254f
#define S3D_GPU_BODY_RADIUS (16.0f * S3D_GPU_SRC)
#define S3D_GPU_STAND_HEIGHT (72.0f * S3D_GPU_SRC)
#define S3D_GPU_CROUCH_HEIGHT (40.0f * S3D_GPU_SRC)
#define S3D_GPU_CAPSULE_RADIUS (S3D_GPU_BODY_RADIUS * 0.707f)
#define S3D_GPU_WALK_SPEED (230.0f * S3D_GPU_SRC)
#define S3D_GPU_CROUCH_SPEED (100.0f * S3D_GPU_SRC)
#define S3D_GPU_JUMP_SPEED (300.0f * S3D_GPU_SRC)
#define S3D_GPU_GRAVITY 15.0f
#define S3D_GPU_MAX_SLOPE_COS 0.7071067811865476f
#define S3D_GPU_TIME_STEP (1.0f / DT)
#define S3D_GPU_LINEAR_SLOP 0.005f

typedef struct S3DGpuConfig {
    int max_ticks;
    int course_mode;
    int course_difficulty;
    int course_stage;
    int course_stage_enabled;
    int depth_interval;
    int occupancy_interval;
    int crouch_enter_commit_ticks;
    int crouch_exit_commit_ticks;
    float reward_progress;
    float time_cost;
    float reward_goal;
    float reward_fall;
    float reward_head_hit;
    float jump_penalty;
    float crouch_penalty;
} S3DGpuConfig;

typedef struct S3DGpuHit {
    int hit;
    int started_solid;
    float fraction;
    float nx, ny, nz;
    float px, py, pz;
} S3DGpuHit;

typedef struct S3DGpuSim {
    int count;
    S3DGpuConfig cfg;
    Env* envs;

    obs_t* observations;
    const float* actions;
    float* rewards;
    float* terminals;

    AABB* boxes;              // [count, MAX_BOXES]
    int* num_boxes;           // [count]
    CourseParams* courses;    // [count]
    uint32_t* rng;            // [count]

    int* tick;
    float* px;
    float* py;
    float* pz;
    float* vx;
    float* vy;
    float* vz;
    float* yaw;
    float* jump_cooldown;
    float* prev_progress_dist;
    float* episode_return;
    float* progress_sum;
    float* ground_nx;
    float* ground_ny;
    float* ground_nz;
    float* step_x;
    float* step_y;
    float* step_z;
    int* crouch_commit_ticks;

    unsigned char* on_ground;
    unsigned char* crouched;
    unsigned char* crouch_wish;
    unsigned char* did_step;
    unsigned char* depth_valid;
    unsigned char* occupancy_valid;
    float* depth;             // [count, DEPTH_MAP_SIZE]
    float* occupancy;         // [count, OCCUPANCY_SIZE]
} S3DGpuSim;

typedef struct S3DNative {
    Env* envs;
    S3DGpuSim sim;
} S3DNative;

static S3DNative s3d_native[8];

static void s3d_gpu_check(cudaError_t error, const char* what) {
    if (error != cudaSuccess) {
        std::fprintf(stderr, "shenaniguns3d CUDA %s: %s\n", what,
                     cudaGetErrorString(error));
        std::exit(1);
    }
}

static void s3d_gpu_check_launch(const char* what) {
    s3d_gpu_check(cudaGetLastError(), what);
}

static int s3d_gpu_grid(int count) {
    return (count + S3D_GPU_BLOCK_SIZE - 1) / S3D_GPU_BLOCK_SIZE;
}

static int s3d_gpu_kwarg_int(Dict* kwargs, const char* name, int fallback) {
    DictItem* item = dict_find(kwargs, name);
    return item ? (int)item->value : fallback;
}

static float s3d_gpu_kwarg_float(Dict* kwargs, const char* name, float fallback) {
    DictItem* item = dict_find(kwargs, name);
    return item ? (float)item->value : fallback;
}

static S3DGpuConfig s3d_gpu_config(Dict* kwargs) {
    S3DGpuConfig cfg = {};
    cfg.max_ticks = s3d_gpu_kwarg_int(kwargs, "max_ticks", MAX_TICKS_DEFAULT);
    cfg.course_mode = s3d_gpu_kwarg_int(kwargs, "course_mode", COURSE_MODE_FIXED);
    cfg.course_difficulty = s3d_gpu_kwarg_int(kwargs, "course_difficulty",
                                               COURSE_DIFFICULTY_DEFAULT);
    cfg.course_stage = s3d_gpu_kwarg_int(kwargs, "course_stage", -1);
    cfg.course_stage_enabled = cfg.course_stage >= 0;
    cfg.depth_interval = s3d_gpu_kwarg_int(kwargs, "sensor_depth_interval",
                                           DEPTH_MAP_UPDATE_INTERVAL);
    cfg.occupancy_interval = s3d_gpu_kwarg_int(kwargs,
                                               "sensor_occupancy_interval",
                                               OCCUPANCY_UPDATE_INTERVAL);
    if (cfg.depth_interval <= 0) cfg.depth_interval = DEPTH_MAP_UPDATE_INTERVAL;
    if (cfg.occupancy_interval <= 0)
        cfg.occupancy_interval = OCCUPANCY_UPDATE_INTERVAL;
    cfg.crouch_enter_commit_ticks = s3d_gpu_kwarg_int(
        kwargs, "crouch_enter_commit_ticks", CROUCH_ENTER_COMMIT_TICKS_DEFAULT);
    cfg.crouch_exit_commit_ticks = s3d_gpu_kwarg_int(
        kwargs, "crouch_exit_commit_ticks", CROUCH_EXIT_COMMIT_TICKS_DEFAULT);
    if (cfg.crouch_enter_commit_ticks < 0) cfg.crouch_enter_commit_ticks = 0;
    if (cfg.crouch_exit_commit_ticks < 0) cfg.crouch_exit_commit_ticks = 0;

    cfg.reward_progress = s3d_gpu_kwarg_float(kwargs, "reward_progress",
                                              PROGRESS_SHAPING_DEFAULT);
    cfg.time_cost = s3d_gpu_kwarg_float(kwargs, "time_cost", TIME_COST);
    cfg.reward_goal = s3d_gpu_kwarg_float(kwargs, "reward_goal", GOAL_BONUS);
    cfg.reward_fall = s3d_gpu_kwarg_float(kwargs, "reward_fall", -FALL_PENALTY);
    cfg.reward_head_hit = s3d_gpu_kwarg_float(kwargs, "reward_head_hit",
                                              HEAD_HIT_REWARD_DEFAULT);
    cfg.jump_penalty = s3d_gpu_kwarg_float(kwargs, "jump_penalty", -cfg.time_cost);
    cfg.crouch_penalty = s3d_gpu_kwarg_float(kwargs, "crouch_penalty", -cfg.time_cost);
    return cfg;
}

static void s3d_gpu_alloc(void** ptr, size_t bytes, const char* what) {
    s3d_gpu_check(cudaMalloc(ptr, bytes), what);
}

static void s3d_gpu_free(void* ptr) {
    if (ptr) s3d_gpu_check(cudaFree(ptr), "free device state");
}

// -----------------------------------------------------------------------------
// Device math and exact reset/course generation
// -----------------------------------------------------------------------------

struct S3DVec {
    float x, y, z;
};

__device__ __forceinline__ S3DVec s3d_v(float x, float y, float z) {
    return {x, y, z};
}

__device__ __forceinline__ S3DVec s3d_add(S3DVec a, S3DVec b) {
    return s3d_v(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ __forceinline__ S3DVec s3d_sub(S3DVec a, S3DVec b) {
    return s3d_v(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ __forceinline__ S3DVec s3d_scale(S3DVec a, float s) {
    return s3d_v(a.x * s, a.y * s, a.z * s);
}

__device__ __forceinline__ float s3d_dot(S3DVec a, S3DVec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ __forceinline__ S3DVec s3d_cross(S3DVec a, S3DVec b) {
    return s3d_v(a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
}

__device__ __forceinline__ float s3d_len2(S3DVec a) {
    return s3d_dot(a, a);
}

__device__ __forceinline__ float s3d_len(S3DVec a) {
    return sqrtf(s3d_len2(a));
}

__device__ __forceinline__ S3DVec s3d_normalize(S3DVec a) {
    float length = s3d_len(a);
    return length > 1.0e-12f ? s3d_scale(a, 1.0f / length) : s3d_v(0, 0, 0);
}

__device__ __forceinline__ float s3d_abs(float x) {
    return fabsf(x);
}

__device__ __forceinline__ float s3d_clamp(float x, float lo, float hi) {
    return fminf(hi, fmaxf(lo, x));
}

// This is glibc's rand_r sequence used by the CPU course generator. Keep the
// integer operations explicit so a reset consumes the same random stream.
__device__ __forceinline__ uint32_t s3d_rand_r(uint32_t* seed) {
    uint32_t next = *seed;
    uint32_t result;
    next = next * 1103515245u + 12345u;
    result = (next / 65536u) % 2048u;
    next = next * 1103515245u + 12345u;
    result <<= 10;
    result ^= (next / 65536u) % 1024u;
    next = next * 1103515245u + 12345u;
    result <<= 10;
    result ^= (next / 65536u) % 1024u;
    *seed = next;
    return result;
}

__device__ __forceinline__ float s3d_rand01(uint32_t* seed) {
    return (float)s3d_rand_r(seed) / 2147483648.0f;
}

__device__ __forceinline__ float s3d_rand_range(uint32_t* seed, float lo,
                                                 float hi) {
    return lo + (hi - lo) * s3d_rand01(seed);
}

__device__ void s3d_zero_course(CourseParams* p) {
    p->jump_x = 0.0f;
    p->jump_width = 0.0f;
    p->pit_depth = 0.0f;
    p->tunnel_start = 0.0f;
    p->tunnel_end = 0.0f;
    p->tunnel_clearance = 0.0f;
    p->hole_start = 0.0f;
    p->hole_end = 0.0f;
    p->column_count = 0;
    p->room_count = 0;
    p->ceiling_room = -1;
    p->ceiling_clearance = 0.0f;
    p->goal_x = 0.0f;
    p->goal_z = 0.0f;
    p->goal_y = 0.0f;
    p->route_length = 0.0f;
    for (int i = 0; i < COURSE_DOORS; i++) p->doors[i] = {};
    for (int i = 0; i < COURSE_MAX_ROOMS; i++) p->rooms[i] = {};
    for (int i = 0; i < COURSE_MAX_ROOMS - 1; i++) p->route_doors[i] = {};
    for (int i = 0; i < COURSE_MAX_COLUMNS; i++) p->columns[i] = {};
}

__device__ void s3d_set_fixed_course(CourseParams* p) {
    s3d_zero_course(p);
    p->doors[0] = {6.0f, 0.0f, 1.6f, 2.2f, 0, 0};
    p->doors[1] = {9.0f, 0.0f, 1.4f, 2.1f, 0, 0};
    p->doors[2] = {18.5f, 0.0f, 1.4f, 2.1f, 0, 0};
    p->jump_x = 10.5f;
    p->jump_width = 0.4f;
    p->pit_depth = 0.9f;
    p->tunnel_start = 12.0f;
    p->tunnel_end = 16.0f;
    p->tunnel_clearance = 1.15f;
    p->hole_start = 22.0f;
    p->hole_end = 26.0f;
    p->goal_x = GOAL_X;
    p->goal_z = GOAL_Z;
    p->goal_y = GOAL_Y;
    p->route_length = COURSE_LENGTH;
}

__device__ bool s3d_column_clear(const CourseParams* p, int used, float x,
                                 float z, float radius) {
    for (int i = 0; i < used; i++) {
        const CourseColumn* c = &p->columns[i];
        float dx = x - c->x;
        float dz = z - c->z;
        float minimum = radius + c->radius + 0.75f;
        if (s3d_abs(dx) < minimum && s3d_abs(dz) < minimum) return false;
    }
    return true;
}

__device__ void s3d_randomize_columns(CourseParams* p, uint32_t* seed) {
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
            float radius = s3d_rand_range(seed, 0.35f, 0.55f);
            float x = i == 0 ? s3d_rand_range(seed, 6.0f, 18.0f) :
                               s3d_rand_range(seed, 4.0f, 25.5f);
            float z = i == 0 ? s3d_rand_range(seed, -0.55f, 0.55f) :
                               s3d_rand_range(seed, -5.8f, 5.8f);
            float spawn_clearance = 2.75f + radius;
            float goal_dx = x - p->goal_x;
            float goal_dz = z - p->goal_z;
            float goal_clearance = 2.25f + radius;
            if (x * x + z * z < spawn_clearance * spawn_clearance ||
                goal_dx * goal_dx + goal_dz * goal_dz <
                    goal_clearance * goal_clearance ||
                !s3d_column_clear(p, p->column_count, x, z, radius)) {
                continue;
            }
            p->columns[p->column_count++] = {x, z, radius,
                                             s3d_rand_range(seed, 1.0f, 2.4f)};
            placed = true;
        }
    }
}

__device__ void s3d_set_room(CourseParams* p, int index, int gx, int gz) {
    p->rooms[index] = {gx, gz, gx * HALL_ROOM_SPACING, gz * HALL_ROOM_SPACING};
}

__device__ void s3d_curriculum_hallway(CourseParams* p, int stage,
                                       uint32_t* seed) {
    int side = s3d_rand01(seed) < 0.5f ? 1 : -1;
    int room_count = 0;
    int route[6][2] = {{0, 0}};
    if (stage == COURSE_STAGE_ONE_TURN) {
        int points[][2] = {{0, 0}, {1, 0}, {1, 1}};
        room_count = 3;
        for (int i = 0; i < room_count; i++) {
            route[i][0] = points[i][0];
            route[i][1] = side < 0 ? -points[i][1] : points[i][1];
        }
    } else if (stage == COURSE_STAGE_TWO_TURNS) {
        int points[][2] = {{0, 0}, {1, 0}, {1, 1}, {2, 1}};
        room_count = 4;
        for (int i = 0; i < room_count; i++) {
            route[i][0] = points[i][0];
            route[i][1] = side < 0 ? -points[i][1] : points[i][1];
        }
    } else if (stage == COURSE_STAGE_JUMP) {
        int points[][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}};
        room_count = 4;
        for (int i = 0; i < room_count; i++) {
            route[i][0] = points[i][0];
            route[i][1] = side < 0 ? -points[i][1] : points[i][1];
        }
    } else {
        int points[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}};
        room_count = 5;
        for (int i = 0; i < room_count; i++) {
            route[i][0] = points[i][0];
            route[i][1] = side < 0 ? -points[i][1] : points[i][1];
        }
    }

    p->column_count = 0;
    p->room_count = room_count;
    p->ceiling_room = -1;
    p->ceiling_clearance = 0.0f;
    for (int i = 0; i < room_count; i++)
        s3d_set_room(p, i, route[i][0], route[i][1]);

    p->route_length = 0.0f;
    for (int i = 0; i + 1 < room_count; i++) {
        CourseRoom* a = &p->rooms[i];
        CourseRoom* b = &p->rooms[i + 1];
        int axis = a->gx != b->gx ? 0 : 1;
        float width = stage <= COURSE_STAGE_TWO_TURNS ?
            s3d_rand_range(seed, 2.35f, 2.85f) :
            s3d_rand_range(seed, 2.05f, 2.55f);
        p->route_doors[i] = {
            (a->x + b->x) * 0.5f, (a->z + b->z) * 0.5f, width,
            s3d_rand_range(seed, 2.25f, 2.55f), axis, HALL_DOOR_NORMAL};
        float dx = b->x - a->x;
        float dz = b->z - a->z;
        p->route_length += sqrtf(dx * dx + dz * dz);
    }
    if (stage == COURSE_STAGE_JUMP) {
        p->route_doors[1].kind = HALL_DOOR_JUMP;
        p->route_doors[1].width = s3d_rand_range(seed, 2.0f, 2.4f);
    } else if (stage == COURSE_STAGE_CROUCH) {
        for (int i = 1; i <= 2; i++) {
            p->route_doors[i].kind = HALL_DOOR_LOW;
            p->route_doors[i].height = s3d_rand_range(seed, 1.18f, 1.28f);
        }
    }
    p->goal_x = p->rooms[room_count - 1].x;
    p->goal_z = p->rooms[room_count - 1].z;
    p->goal_y = 0.0f;
}

__device__ bool s3d_room_clear(const CourseParams* p, int used, int gx, int gz) {
    for (int i = 0; i < used; i++) {
        int dx = gx - p->rooms[i].gx;
        int dz = gz - p->rooms[i].gz;
        if (dx == 0 && dz == 0) return false;
        if (i < used - 1 && s3d_abs((float)dx) + s3d_abs((float)dz) == 1)
            return false;
    }
    return s3d_abs((float)gz) <= 2;
}

__device__ void s3d_randomize_hallway(CourseParams* p, int difficulty,
                                      uint32_t* seed) {
    int room_count = difficulty >= 3 ? 11 : 8;
    bool generated = false;
    for (int attempt = 0; attempt < 64 && !generated; attempt++) {
        p->rooms[0] = {0, 0, 0.0f, 0.0f};
        int heading = 0;
        generated = true;
        for (int i = 1; i < room_count; i++) {
            int candidates[3];
            int candidate_count = 0;
            if (heading == 0) {
                bool turn = s3d_rand01(seed) < (difficulty >= 3 ? 0.45f : 0.25f);
                int side = s3d_rand01(seed) < 0.5f ? 1 : 3;
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
                if (s3d_room_clear(p, i, gx, gz)) {
                    chosen = direction;
                    p->rooms[i] = {gx, gz, gx * HALL_ROOM_SPACING,
                                   gz * HALL_ROOM_SPACING};
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
        const int fallback[COURSE_MAX_ROOMS][2] = {
            {0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 2},
            {4, 2}, {4, 1}, {5, 1}, {5, 0}, {6, 0}, {7, 0},
        };
        for (int i = 0; i < room_count; i++)
            p->rooms[i] = {fallback[i][0], fallback[i][1],
                           fallback[i][0] * HALL_ROOM_SPACING,
                           fallback[i][1] * HALL_ROOM_SPACING};
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
        float offset = s3d_rand_range(seed, -0.35f, 0.35f);
        CourseDoor door = {
            (a->x + b->x) * 0.5f + (axis == 1 ? offset : 0.0f),
            (a->z + b->z) * 0.5f + (axis == 0 ? offset : 0.0f),
            s3d_rand_range(seed, 1.35f, 1.75f),
            s3d_rand_range(seed, 2.00f, 2.30f), axis, HALL_DOOR_NORMAL};
        float type = s3d_rand01(seed);
        if (i > 0 && type < 0.20f) {
            door.kind = HALL_DOOR_LOW;
            door.height = s3d_rand_range(seed, 1.08f, 1.18f);
            low_count++;
        } else if (i > 0 && type < 0.40f) {
            door.kind = HALL_DOOR_JUMP;
            door.height = s3d_rand_range(seed, 2.00f, 2.25f);
            jump_count++;
        }
        p->route_doors[i] = door;
    }
    if (low_count == 0 && room_count > 3) {
        p->route_doors[room_count / 2].kind = HALL_DOOR_LOW;
        p->route_doors[room_count / 2].height = 1.12f;
    }
    if (jump_count == 0 && room_count > 4) {
        p->route_doors[room_count / 2 + 1].kind = HALL_DOOR_JUMP;
        p->route_doors[room_count / 2 + 1].height = 2.10f;
    }
    if (s3d_rand01(seed) < 0.65f && room_count > 3) {
        p->ceiling_room = 1 + (int)(s3d_rand_r(seed) % (uint32_t)(room_count - 2));
        p->ceiling_clearance = s3d_rand_range(seed, 1.08f, 1.22f);
    }
    for (int i = 0; i < room_count - 1; i++) {
        if (p->route_doors[i].kind == HALL_DOOR_JUMP &&
            (i == p->ceiling_room || i + 1 == p->ceiling_room)) {
            p->route_doors[i].kind = HALL_DOOR_NORMAL;
        }
    }
    bool have_jump = false;
    for (int i = 1; i < room_count - 1; i++) {
        if (p->route_doors[i].kind == HALL_DOOR_JUMP) {
            have_jump = true;
            break;
        }
    }
    if (!have_jump) {
        for (int i = 1; i < room_count - 1; i++) {
            if (i != p->ceiling_room && i + 1 != p->ceiling_room) {
                p->route_doors[i].kind = HALL_DOOR_JUMP;
                p->route_doors[i].height = 2.10f;
                break;
            }
        }
    }
}

__device__ void s3d_randomize_legacy(CourseParams* p, int difficulty,
                                     uint32_t* seed) {
    if (difficulty < 1) difficulty = 1;
    if (difficulty > 3) difficulty = 3;
    if (difficulty == 3) {
        s3d_randomize_hallway(p, difficulty, seed);
        return;
    }
    if (difficulty == 1) {
        p->doors[0] = {6.0f, s3d_rand_range(seed, -0.65f, 0.65f),
                       s3d_rand_range(seed, 1.25f, 1.55f),
                       s3d_rand_range(seed, 2.00f, 2.20f), 0, 0};
        p->doors[1] = {s3d_rand_range(seed, 8.6f, 9.4f),
                       s3d_rand_range(seed, -0.75f, 0.75f),
                       s3d_rand_range(seed, 1.20f, 1.50f),
                       s3d_rand_range(seed, 2.00f, 2.15f), 0, 0};
        p->doors[2] = {s3d_rand_range(seed, 18.0f, 19.0f),
                       s3d_rand_range(seed, -0.75f, 0.75f),
                       s3d_rand_range(seed, 1.20f, 1.50f),
                       s3d_rand_range(seed, 2.00f, 2.15f), 0, 0};
        p->jump_x = s3d_rand_range(seed, 10.2f, 10.9f);
        p->jump_width = s3d_rand_range(seed, 0.35f, 0.55f);
        p->pit_depth = s3d_rand_range(seed, 0.65f, 0.90f);
        p->tunnel_start = s3d_rand_range(seed, 12.0f, 12.6f);
        p->tunnel_end = p->tunnel_start + s3d_rand_range(seed, 3.8f, 4.8f);
        p->tunnel_clearance = s3d_rand_range(seed, 1.08f, 1.22f);
        p->hole_start = s3d_rand_range(seed, 20.5f, 22.0f);
        p->hole_end = p->hole_start + s3d_rand_range(seed, 3.0f, 4.0f);
    } else {
        p->doors[0] = {6.0f, s3d_rand_range(seed, -0.90f, 0.90f),
                       s3d_rand_range(seed, 1.10f, 1.35f),
                       s3d_rand_range(seed, 1.95f, 2.10f), 0, 0};
        p->doors[1] = {s3d_rand_range(seed, 8.5f, 9.5f),
                       s3d_rand_range(seed, -0.95f, 0.95f),
                       s3d_rand_range(seed, 1.05f, 1.30f),
                       s3d_rand_range(seed, 1.95f, 2.05f), 0, 0};
        p->doors[2] = {s3d_rand_range(seed, 18.0f, 19.3f),
                       s3d_rand_range(seed, -0.95f, 0.95f),
                       s3d_rand_range(seed, 1.05f, 1.30f),
                       s3d_rand_range(seed, 1.95f, 2.05f), 0, 0};
        p->jump_x = s3d_rand_range(seed, 10.1f, 11.0f);
        p->jump_width = s3d_rand_range(seed, 0.40f, 0.65f);
        p->pit_depth = s3d_rand_range(seed, 0.75f, 1.05f);
        p->tunnel_start = s3d_rand_range(seed, 11.8f, 12.8f);
        p->tunnel_end = p->tunnel_start + s3d_rand_range(seed, 4.5f, 5.8f);
        p->tunnel_clearance = s3d_rand_range(seed, 1.06f, 1.14f);
        p->hole_start = s3d_rand_range(seed, 20.0f, 22.0f);
        p->hole_end = p->hole_start + s3d_rand_range(seed, 3.0f, 4.0f);
    }
    if (p->hole_start < p->tunnel_end + 3.0f)
        p->hole_start = p->tunnel_end + 3.0f;
    if (p->doors[2].x < p->tunnel_end + 0.8f)
        p->doors[2].x = p->tunnel_end + 0.8f;
    if (p->hole_start < p->doors[2].x + 1.0f)
        p->hole_start = p->doors[2].x + 1.0f;
    if (p->hole_end < p->hole_start + 3.0f)
        p->hole_end = p->hole_start + 3.0f;
    if (p->hole_end > 28.5f) p->hole_end = 28.5f;
}

__device__ void s3d_add_box(AABB* boxes, int* count, float cx, float cy,
                            float cz, float hx, float hy, float hz) {
    if (*count >= MAX_BOXES) return;
    boxes[*count] = {cx, cy, cz, hx, hy, hz};
    *count += 1;
}

__device__ void s3d_add_bounds(AABB* boxes, int* count, float x0, float x1,
                               float y0, float y1, float z0, float z1) {
    s3d_add_box(boxes, count, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f,
                (z0 + z1) * 0.5f, (x1 - x0) * 0.5f, (y1 - y0) * 0.5f,
                (z1 - z0) * 0.5f);
}

__device__ void s3d_add_course_door(AABB* boxes, int* count,
                                    const CourseDoor* door) {
    float lo = door->z - door->width * 0.5f;
    float hi = door->z + door->width * 0.5f;
    s3d_add_bounds(boxes, count, door->x - 0.2f, door->x + 0.2f,
                   0.0f, COURSE_WALL_TOP, -2.0f, lo);
    s3d_add_bounds(boxes, count, door->x - 0.2f, door->x + 0.2f,
                   0.0f, COURSE_WALL_TOP, hi, 2.0f);
    s3d_add_bounds(boxes, count, door->x - 0.2f, door->x + 0.2f,
                   door->height, COURSE_WALL_TOP, lo, hi);
}

__device__ int s3d_hall_neighbor(const CourseParams* p, int room_index,
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

__device__ void s3d_add_full_hall_wall(AABB* boxes, int* count, float x,
                                       float z, int axis) {
    if (axis == 0)
        s3d_add_bounds(boxes, count, x - HALL_WALL_THICKNESS,
                       x + HALL_WALL_THICKNESS, 0.0f, HALL_WALL_HEIGHT,
                       z - HALL_ROOM_HALF_SIZE, z + HALL_ROOM_HALF_SIZE);
    else
        s3d_add_bounds(boxes, count, x - HALL_ROOM_HALF_SIZE,
                       x + HALL_ROOM_HALF_SIZE, 0.0f, HALL_WALL_HEIGHT,
                       z - HALL_WALL_THICKNESS, z + HALL_WALL_THICKNESS);
}

__device__ void s3d_add_hall_door(AABB* boxes, int* count,
                                  const CourseDoor* door) {
    float lo, hi;
    if (door->axis == 0) {
        lo = door->z - door->width * 0.5f;
        hi = door->z + door->width * 0.5f;
        s3d_add_bounds(boxes, count, door->x - HALL_WALL_THICKNESS,
                       door->x + HALL_WALL_THICKNESS, 0.0f, HALL_WALL_HEIGHT,
                       door->z - HALL_ROOM_HALF_SIZE, lo);
        s3d_add_bounds(boxes, count, door->x - HALL_WALL_THICKNESS,
                       door->x + HALL_WALL_THICKNESS, 0.0f, HALL_WALL_HEIGHT,
                       hi, door->z + HALL_ROOM_HALF_SIZE);
    } else {
        lo = door->x - door->width * 0.5f;
        hi = door->x + door->width * 0.5f;
        s3d_add_bounds(boxes, count, door->x - HALL_ROOM_HALF_SIZE, lo,
                       0.0f, HALL_WALL_HEIGHT, door->z - HALL_WALL_THICKNESS,
                       door->z + HALL_WALL_THICKNESS);
        s3d_add_bounds(boxes, count, hi, door->x + HALL_ROOM_HALF_SIZE,
                       0.0f, HALL_WALL_HEIGHT, door->z - HALL_WALL_THICKNESS,
                       door->z + HALL_WALL_THICKNESS);
    }
    if (door->kind == HALL_DOOR_LOW) {
        if (door->axis == 0)
            s3d_add_bounds(boxes, count, door->x - HALL_WALL_THICKNESS,
                           door->x + HALL_WALL_THICKNESS, door->height,
                           HALL_WALL_HEIGHT, lo, hi);
        else
            s3d_add_bounds(boxes, count, lo, hi, door->height,
                           HALL_WALL_HEIGHT, door->z - HALL_WALL_THICKNESS,
                           door->z + HALL_WALL_THICKNESS);
    } else if (door->kind == HALL_DOOR_JUMP) {
        if (door->axis == 0)
            s3d_add_box(boxes, count, door->x, 0.32f, door->z,
                        HALL_WALL_THICKNESS, 0.32f, door->width * 0.5f);
        else
            s3d_add_box(boxes, count, door->x, 0.32f, door->z,
                        door->width * 0.5f, 0.32f, HALL_WALL_THICKNESS);
    }
}

__device__ void s3d_build_course(const CourseParams* p, AABB* boxes,
                                 int* count) {
    *count = 0;
    if (p->column_count > 0) {
        s3d_add_box(boxes, count, 13.0f, -1.0f, 0.0f, 17.0f, 1.0f, 8.0f);
        s3d_add_bounds(boxes, count, -4.0f, 30.0f, 0.0f, HALL_WALL_HEIGHT,
                       -8.4f, -8.0f);
        s3d_add_bounds(boxes, count, -4.0f, 30.0f, 0.0f, HALL_WALL_HEIGHT,
                       8.0f, 8.4f);
        s3d_add_bounds(boxes, count, -4.4f, -4.0f, 0.0f, HALL_WALL_HEIGHT,
                       -8.0f, 8.0f);
        s3d_add_bounds(boxes, count, 30.0f, 30.4f, 0.0f, HALL_WALL_HEIGHT,
                       -8.0f, 8.0f);
        for (int i = 0; i < p->column_count; i++) {
            const CourseColumn* c = &p->columns[i];
            s3d_add_box(boxes, count, c->x, c->height * 0.5f, c->z,
                        c->radius, c->height * 0.5f, c->radius);
        }
        s3d_add_box(boxes, count, p->goal_x, p->goal_y + 0.25f, p->goal_z,
                    0.35f, 0.25f, 0.35f);
        return;
    }
    if (p->room_count > 0) {
        for (int i = 0; i < p->room_count; i++) {
            const CourseRoom* room = &p->rooms[i];
            s3d_add_box(boxes, count, room->x, -0.5f, room->z,
                        HALL_ROOM_HALF_SIZE, 0.5f, HALL_ROOM_HALF_SIZE);
            const int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (int d = 0; d < 4; d++) {
                int dx = directions[d][0], dz = directions[d][1];
                int neighbor = s3d_hall_neighbor(p, i, dx, dz);
                int axis = dx != 0 ? 0 : 1;
                float wall_x = room->x + dx * HALL_ROOM_HALF_SIZE;
                float wall_z = room->z + dz * HALL_ROOM_HALF_SIZE;
                if (neighbor >= 0) {
                    if (i < neighbor) s3d_add_hall_door(
                        boxes, count, &p->route_doors[i]);
                } else {
                    s3d_add_full_hall_wall(boxes, count, wall_x, wall_z, axis);
                }
            }
        }
        if (p->ceiling_room >= 0 && p->ceiling_room < p->room_count) {
            const CourseRoom* room = &p->rooms[p->ceiling_room];
            s3d_add_bounds(boxes, count, room->x - HALL_ROOM_HALF_SIZE,
                           room->x + HALL_ROOM_HALF_SIZE, p->ceiling_clearance,
                           HALL_WALL_HEIGHT, room->z - HALL_ROOM_HALF_SIZE,
                           room->z + HALL_ROOM_HALF_SIZE);
        }
        s3d_add_box(boxes, count, p->goal_x, p->goal_y + 0.25f, p->goal_z,
                    0.35f, 0.25f, 0.35f);
        return;
    }

    s3d_add_box(boxes, count, 1.0f, -1.0f, 0.0f, 5.0f, 1.0f, 2.0f);
    for (int i = 0; i < COURSE_DOORS; i++)
        s3d_add_course_door(boxes, count, &p->doors[i]);
    float pit_start = p->jump_x - p->jump_width * 0.5f;
    float pit_end = p->jump_x + p->jump_width * 0.5f;
    s3d_add_bounds(boxes, count, 6.0f, pit_start, -2.0f, 0.0f, -2.0f, 2.0f);
    s3d_add_bounds(boxes, count, pit_end, p->hole_start, -2.0f, 0.0f,
                   -2.0f, 2.0f);
    s3d_add_bounds(boxes, count, pit_start, pit_end,
                   -p->pit_depth - 0.2f, -p->pit_depth, -2.0f, 2.0f);
    s3d_add_bounds(boxes, count, p->tunnel_start, p->tunnel_end,
                   p->tunnel_clearance, COURSE_WALL_TOP, -2.0f, 2.0f);
    s3d_add_bounds(boxes, count, p->hole_end, COURSE_FLOOR_END,
                   -4.0f, -3.0f, -2.0f, 2.0f);
    s3d_add_bounds(boxes, count, -4.0f, COURSE_FLOOR_END, -4.0f,
                   COURSE_WALL_TOP, -2.25f, -2.0f);
    s3d_add_bounds(boxes, count, -4.0f, COURSE_FLOOR_END, -4.0f,
                   COURSE_WALL_TOP, 2.0f, 2.25f);
    s3d_add_box(boxes, count, GOAL_X, GOAL_Y + 0.25f, GOAL_Z,
                0.35f, 0.25f, 0.35f);
}

__device__ void s3d_generate_course(S3DGpuSim sim, int index,
                                    bool force_generate) {
    if (!force_generate && sim.tick[index] == 0) return;
    CourseParams* course = &sim.courses[index];
    s3d_set_fixed_course(course);
    if (sim.cfg.course_stage_enabled) {
        if (sim.cfg.course_stage == COURSE_STAGE_COLUMNS) {
            s3d_randomize_columns(course, &sim.rng[index]);
        } else if (sim.cfg.course_stage >= COURSE_STAGE_ONE_TURN &&
                   sim.cfg.course_stage <= COURSE_STAGE_CROUCH) {
            s3d_curriculum_hallway(course, sim.cfg.course_stage,
                                   &sim.rng[index]);
        } else if (sim.cfg.course_stage == COURSE_STAGE_STRESS) {
            s3d_randomize_hallway(course, 3, &sim.rng[index]);
        }
    } else if (sim.cfg.course_mode == COURSE_MODE_RANDOM ||
               sim.cfg.course_mode == COURSE_MODE_RANDOM_EVERY_RESET) {
        s3d_randomize_legacy(course, sim.cfg.course_difficulty,
                             &sim.rng[index]);
    }
    s3d_build_course(course, sim.boxes + (size_t)index * MAX_BOXES,
                     &sim.num_boxes[index]);
}

__device__ float s3d_goal_dist(S3DGpuSim sim, int i) {
    const CourseParams* p = &sim.courses[i];
    float dx = p->goal_x - sim.px[i];
    float height = sim.crouched[i] ? S3D_GPU_CROUCH_HEIGHT :
                                         S3D_GPU_STAND_HEIGHT;
    float feet_y = sim.py[i] - height * 0.5f;
    float dy = p->goal_y - feet_y;
    float dz = p->goal_z - sim.pz[i];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

__device__ float s3d_hallway_progress(S3DGpuSim sim, int i) {
    const CourseParams* p = &sim.courses[i];
    float px = sim.px[i], pz = sim.pz[i];
    float best_distance = INFINITY;
    float best_progress = 0.0f;
    float accumulated = 0.0f;
    for (int j = 0; j + 1 < p->room_count; j++) {
        float ax = p->rooms[j].x, az = p->rooms[j].z;
        float bx = p->rooms[j + 1].x, bz = p->rooms[j + 1].z;
        float dx = bx - ax, dz = bz - az;
        float length_squared = dx * dx + dz * dz;
        float t = ((px - ax) * dx + (pz - az) * dz) / length_squared;
        t = s3d_clamp(t, 0.0f, 1.0f);
        float nearest_x = ax + t * dx, nearest_z = az + t * dz;
        float ox = px - nearest_x, oz = pz - nearest_z;
        float distance_squared = ox * ox + oz * oz;
        if (distance_squared < best_distance) {
            best_distance = distance_squared;
            best_progress = accumulated + t * sqrtf(length_squared);
        }
        accumulated += sqrtf(length_squared);
    }
    return best_progress;
}

__device__ float s3d_progress_dist(S3DGpuSim sim, int i) {
    const CourseParams* p = &sim.courses[i];
    if (p->room_count > 1)
        return p->route_length - s3d_hallway_progress(sim, i);
    float dx = p->goal_x - sim.px[i];
    float dz = p->goal_z - sim.pz[i];
    return sqrtf(dx * dx + dz * dz);
}

// -----------------------------------------------------------------------------
// Static-box casts. These use the exact separating-axis interval for an OBB
// translated against an axis-aligned box. It is the fixed-geometry equivalent
// of the Box3D convex shape cast used by the CPU controller.
// -----------------------------------------------------------------------------

__device__ bool s3d_sweep_axis(S3DVec axis, S3DVec center, S3DVec translation,
                               float hx, float hy, float hz, S3DVec axis_u,
                               S3DVec axis_w, const AABB* box, float* enter,
                               float* exit, int* enter_axis, float* enter_d,
                               int axis_index) {
    float axis_len2 = s3d_len2(axis);
    if (axis_len2 < 1.0e-10f) return true;
    axis = s3d_scale(axis, rsqrtf(axis_len2));
    float moving_radius = hx * s3d_abs(s3d_dot(axis, axis_u)) +
                          hy * s3d_abs(axis.y) +
                          hz * s3d_abs(s3d_dot(axis, axis_w));
    float static_radius = box->hx * s3d_abs(axis.x) +
                          box->hy * s3d_abs(axis.y) +
                          box->hz * s3d_abs(axis.z);
    float radius = moving_radius + static_radius;
    S3DVec delta = s3d_v(center.x - box->cx, center.y - box->cy,
                         center.z - box->cz);
    float d0 = s3d_dot(delta, axis);
    float dv = s3d_dot(translation, axis);
    if (s3d_abs(dv) < 1.0e-10f) return s3d_abs(d0) <= radius;

    float a = (-radius - d0) / dv;
    float b = (radius - d0) / dv;
    if (a > b) {
        float temp = a;
        a = b;
        b = temp;
    }
    if (a > *enter) {
        *enter = a;
        *enter_axis = axis_index;
        *enter_d = d0 + dv * a;
    }
    if (b < *exit) *exit = b;
    return *enter <= *exit + 1.0e-6f;
}

__device__ S3DGpuHit s3d_sweep_one_box(S3DVec center, S3DVec translation,
                                       float hx, float hy, float hz,
                                       S3DVec axis_u, S3DVec axis_w,
                                       const AABB* box) {
    S3DGpuHit result = {};
    result.fraction = 1.0f;
    S3DVec world_axes[3] = {s3d_v(1, 0, 0), s3d_v(0, 1, 0), s3d_v(0, 0, 1)};
    S3DVec obb_axes[3] = {axis_u, s3d_v(0, 1, 0), axis_w};
    S3DVec axes[15];
    int axis_count = 0;
    for (int a = 0; a < 3; a++) axes[axis_count++] = world_axes[a];
    for (int a = 0; a < 3; a++) axes[axis_count++] = obb_axes[a];
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            axes[axis_count++] = s3d_cross(obb_axes[a], world_axes[b]);

    float enter = 0.0f;
    float exit = 1.0f;
    int enter_axis = -1;
    float enter_d = 0.0f;
    bool initially_inside = true;
    for (int a = 0; a < axis_count; a++) {
        S3DVec axis = axes[a];
        float axis_len2 = s3d_len2(axis);
        if (axis_len2 < 1.0e-10f) continue;
        axis = s3d_scale(axis, rsqrtf(axis_len2));
        float moving_radius = hx * s3d_abs(s3d_dot(axis, axis_u)) +
                              hy * s3d_abs(axis.y) +
                              hz * s3d_abs(s3d_dot(axis, axis_w));
        float static_radius = box->hx * s3d_abs(axis.x) +
                              box->hy * s3d_abs(axis.y) +
                              box->hz * s3d_abs(axis.z);
        float radius = moving_radius + static_radius;
        S3DVec offset = s3d_v(center.x - box->cx, center.y - box->cy,
                              center.z - box->cz);
        float d0 = s3d_dot(offset, axis);
        if (s3d_abs(d0) >= radius - 1.0e-6f) initially_inside = false;
        if (!s3d_sweep_axis(axis, center, translation, hx, hy, hz, axis_u,
                            axis_w, box, &enter, &exit, &enter_axis,
                            &enter_d, a)) {
            return result;
        }
    }
    if (exit < 0.0f || enter > 1.0f) return result;
    if (initially_inside) {
        result.hit = 1;
        result.started_solid = 1;
        result.fraction = 0.0f;
        // Use the first world axis with the least remaining penetration.
        float best = INFINITY;
        S3DVec best_axis = s3d_v(0, 1, 0);
        float best_d = 0.0f;
        for (int a = 0; a < 3; a++) {
            S3DVec axis = world_axes[a];
            float moving_radius = hx * s3d_abs(s3d_dot(axis, axis_u)) +
                                  hy * s3d_abs(axis.y) +
                                  hz * s3d_abs(s3d_dot(axis, axis_w));
            float static_radius = box->hx * s3d_abs(axis.x) +
                                  box->hy * s3d_abs(axis.y) +
                                  box->hz * s3d_abs(axis.z);
            float d = s3d_dot(s3d_v(center.x - box->cx, center.y - box->cy,
                                    center.z - box->cz), axis);
            float penetration = moving_radius + static_radius - s3d_abs(d);
            if (penetration < best) {
                best = penetration;
                best_axis = axis;
                best_d = d;
            }
        }
        if (best_d < 0.0f) best_axis = s3d_scale(best_axis, -1.0f);
        result.nx = best_axis.x;
        result.ny = best_axis.y;
        result.nz = best_axis.z;
        result.px = center.x;
        result.py = center.y;
        result.pz = center.z;
        return result;
    }
    if (enter < 0.0f) enter = 0.0f;
    if (enter > 1.0f || enter > exit + 1.0e-6f) return result;
    result.hit = 1;
    result.fraction = enter;
    S3DVec normal = s3d_v(0, 1, 0);
    if (enter_axis >= 0) {
        S3DVec axis = axes[enter_axis];
        axis = s3d_scale(axis, rsqrtf(s3d_len2(axis)));
        if (enter_d < 0.0f) axis = s3d_scale(axis, -1.0f);
        normal = axis;
    }
    result.nx = normal.x;
    result.ny = normal.y;
    result.nz = normal.z;
    S3DVec at = s3d_add(center, s3d_scale(translation, enter));
    result.px = at.x;
    result.py = at.y;
    result.pz = at.z;
    // Box3D reports a zero-fraction cast as startedSolid, and its character
    // traces retry those casts with a smaller hull before attempting a step or
    // stance change.
    if (enter <= 1.0e-6f) {
        result.hit = 0;
        result.started_solid = 1;
    }
    return result;
}

__device__ S3DGpuHit s3d_query_box(S3DGpuSim sim, int index, S3DVec center,
                                   S3DVec translation, float hx, float hy,
                                   float hz, S3DVec axis_u, S3DVec axis_w) {
    S3DGpuHit best = {};
    best.fraction = 1.0f;
    const AABB* boxes = sim.boxes + (size_t)index * MAX_BOXES;
    int count = sim.num_boxes[index];
    for (int b = 0; b < count; b++) {
        S3DGpuHit hit = s3d_sweep_one_box(center, translation, hx, hy, hz,
                                          axis_u, axis_w, &boxes[b]);
        if (hit.started_solid) {
            best = hit;
            best.started_solid = 1;
            return best;
        }
        if (hit.hit && hit.fraction < best.fraction) best = hit;
    }
    return best;
}

__device__ S3DGpuHit s3d_query_ray(S3DGpuSim sim, int index, S3DVec origin,
                                   S3DVec translation) {
    S3DGpuHit best = {};
    best.fraction = 1.0f;
    const AABB* boxes = sim.boxes + (size_t)index * MAX_BOXES;
    int count = sim.num_boxes[index];
    for (int b = 0; b < count; b++) {
        const AABB* box = &boxes[b];
        float lower[3] = {box->cx - box->hx, box->cy - box->hy,
                          box->cz - box->hz};
        float upper[3] = {box->cx + box->hx, box->cy + box->hy,
                          box->cz + box->hz};
        float o[3] = {origin.x, origin.y, origin.z};
        float d[3] = {translation.x, translation.y, translation.z};
        float enter = 0.0f, exit = 1.0f;
        int normal_axis = -1;
        float normal_sign = 0.0f;
        bool inside = true;
        bool valid = true;
        for (int a = 0; a < 3; a++) {
            if (o[a] <= lower[a] || o[a] >= upper[a]) inside = false;
            if (s3d_abs(d[a]) < 1.0e-10f) {
                if (o[a] < lower[a] || o[a] > upper[a]) valid = false;
                continue;
            }
            float t0 = (lower[a] - o[a]) / d[a];
            float t1 = (upper[a] - o[a]) / d[a];
            float sign = d[a] > 0.0f ? -1.0f : 1.0f;
            if (t0 > t1) {
                float temp = t0;
                t0 = t1;
                t1 = temp;
                sign = -sign;
            }
            if (t0 > enter) {
                enter = t0;
                normal_axis = a;
                normal_sign = sign;
            }
            if (t1 < exit) exit = t1;
            if (enter > exit) valid = false;
        }
        if (!valid || exit < 0.0f || enter > 1.0f)
            continue;
        if (inside) {
            best.hit = 1;
            best.started_solid = 1;
            best.fraction = 0.0f;
            return best;
        }
        if (enter < 0.0f) continue;
        if (enter < best.fraction) {
            best.hit = 1;
            best.fraction = enter;
            best.nx = normal_axis == 0 ? normal_sign : 0.0f;
            best.ny = normal_axis == 1 ? normal_sign : 0.0f;
            best.nz = normal_axis == 2 ? normal_sign : 0.0f;
            S3DVec point = s3d_add(origin, s3d_scale(translation, enter));
            best.px = point.x;
            best.py = point.y;
            best.pz = point.z;
        }
    }
    return best;
}

__device__ S3DGpuHit s3d_trace_body(S3DGpuSim sim, int index, S3DVec from,
                                    S3DVec to, float radius_scale,
                                    float height_scale) {
    float height = (sim.crouched[index] ? S3D_GPU_CROUCH_HEIGHT :
                                         S3D_GPU_STAND_HEIGHT) * height_scale;
    S3DVec center = s3d_add(from, s3d_v(0.0f, height * 0.5f, 0.0f));
    S3DVec translation = s3d_sub(to, from);
    S3DVec axis_u = s3d_v(1, 0, 0);
    S3DVec axis_w = s3d_v(0, 0, 1);
    S3DGpuHit hit = s3d_query_box(
        sim, index, center, translation,
        S3D_GPU_BODY_RADIUS * 0.5f * radius_scale, height * 0.5f,
        S3D_GPU_BODY_RADIUS * 0.5f * radius_scale, axis_u, axis_w);
    // Public character traces are feet-anchored. The broad-phase query works
    // in center space, so convert the reported point back before step-up and
    // reground consume it.
    if (hit.hit) {
        S3DVec endpoint = s3d_add(from, s3d_scale(translation, hit.fraction));
        hit.px = endpoint.x;
        hit.py = endpoint.y;
        hit.pz = endpoint.z;
    }
    return hit;
}

__device__ S3DGpuHit s3d_trace_shrunk(S3DGpuSim sim, int index, S3DVec from,
                                      S3DVec to, float height_scale) {
    float radius_scale = 1.0f;
    S3DGpuHit hit = s3d_trace_body(sim, index, from, to, radius_scale,
                                   height_scale);
    while (hit.started_solid && radius_scale > 0.6f) {
        radius_scale -= 0.1f;
        hit = s3d_trace_body(sim, index, from, to, radius_scale, height_scale);
    }
    return hit;
}

__device__ float s3d_ray_distance(S3DGpuSim sim, int index, S3DVec origin,
                                   S3DVec direction, float max_t) {
    S3DGpuHit hit = s3d_query_ray(sim, index, origin,
                                  s3d_scale(direction, max_t));
    return hit.hit || hit.started_solid ? hit.fraction * max_t : max_t;
}

// Occupancy probes are horizontal rays. Avoid the general query's temporary
// arrays and normal bookkeeping for this hot path.
__device__ __forceinline__ float s3d_horizontal_ray_distance(
        S3DGpuSim sim, int index, S3DVec origin, S3DVec direction, float max_t) {
    const AABB* boxes = sim.boxes + (size_t)index * MAX_BOXES;
    float dx = direction.x * max_t;
    float dz = direction.z * max_t;
    float best = 1.0f;
    for (int b = 0; b < sim.num_boxes[index]; b++) {
        const AABB* box = &boxes[b];
        float lower_x = box->cx - box->hx;
        float upper_x = box->cx + box->hx;
        float lower_z = box->cz - box->hz;
        float upper_z = box->cz + box->hz;
        float lower_y = box->cy - box->hy;
        float upper_y = box->cy + box->hy;
        if (origin.y < lower_y || origin.y > upper_y) continue;

        float enter = 0.0f;
        float exit = 1.0f;
        bool inside = origin.x > lower_x && origin.x < upper_x &&
                      origin.y > lower_y && origin.y < upper_y &&
                      origin.z > lower_z && origin.z < upper_z;
        bool valid = true;
        if (s3d_abs(dx) < 1.0e-10f) {
            if (origin.x < lower_x || origin.x > upper_x) valid = false;
        } else {
            float t0 = (lower_x - origin.x) / dx;
            float t1 = (upper_x - origin.x) / dx;
            if (t0 > t1) {
                float temp = t0;
                t0 = t1;
                t1 = temp;
            }
            if (t0 > enter) enter = t0;
            if (t1 < exit) exit = t1;
            if (enter > exit) valid = false;
        }
        if (s3d_abs(dz) < 1.0e-10f) {
            if (origin.z < lower_z || origin.z > upper_z) valid = false;
        } else {
            float t0 = (lower_z - origin.z) / dz;
            float t1 = (upper_z - origin.z) / dz;
            if (t0 > t1) {
                float temp = t0;
                t0 = t1;
                t1 = temp;
            }
            if (t0 > enter) enter = t0;
            if (t1 < exit) exit = t1;
            if (enter > exit) valid = false;
        }
        if (!valid || exit < 0.0f || enter > 1.0f)
            continue;
        if (inside) return 0.0f;
        if (enter < 0.0f) continue;
        if (enter < best) best = enter;
    }
    return best < 1.0f ? best * max_t : max_t;
}

// -----------------------------------------------------------------------------
// Character controller and restricted static-body response
// -----------------------------------------------------------------------------

__device__ S3DVec s3d_feet(S3DGpuSim sim, int i) {
    float h = sim.crouched[i] ? S3D_GPU_CROUCH_HEIGHT : S3D_GPU_STAND_HEIGHT;
    return s3d_v(sim.px[i], sim.py[i] - h * 0.5f, sim.pz[i]);
}

__device__ void s3d_set_ground(S3DGpuSim sim, int i, bool on_ground,
                              S3DVec normal) {
    sim.on_ground[i] = on_ground ? 1 : 0;
    sim.ground_nx[i] = normal.x;
    sim.ground_ny[i] = normal.y;
    sim.ground_nz[i] = normal.z;
}

__device__ void s3d_categorize_ground(S3DGpuSim sim, int i) {
    S3DVec feet = s3d_feet(sim, i);
    S3DVec from = s3d_add(feet, s3d_v(0.0f, 4.0f * S3D_GPU_SRC, 0.0f));
    S3DVec to = s3d_add(feet, s3d_v(0.0f, -2.0f * S3D_GPU_SRC, 0.0f));
    float radius_scale = 1.0f;
    S3DGpuHit hit = s3d_trace_body(sim, i, from, to, radius_scale, 0.5f);
    while (hit.started_solid ||
           (hit.hit && hit.ny < S3D_GPU_MAX_SLOPE_COS)) {
        radius_scale -= 0.1f;
        if (radius_scale < 0.7f) {
            s3d_set_ground(sim, i, false, s3d_v(0, 1, 0));
            return;
        }
        hit = s3d_trace_body(sim, i, from, to, radius_scale, 0.5f);
    }
    if (!hit.started_solid && hit.hit && hit.ny >= S3D_GPU_MAX_SLOPE_COS &&
        sim.jump_cooldown[i] <= 0.0f) {
        s3d_set_ground(sim, i, true, s3d_v(hit.nx, hit.ny, hit.nz));
    } else {
        s3d_set_ground(sim, i, false, s3d_v(0, 1, 0));
    }
}

__device__ void s3d_reground(S3DGpuSim sim, int i, float step_size) {
    if (!sim.on_ground[i]) return;
    S3DVec old_pos = s3d_v(sim.px[i], sim.py[i], sim.pz[i]);
    S3DVec feet = s3d_feet(sim, i);
    S3DVec from = s3d_add(feet, s3d_v(0.0f, 0.05f, 0.0f));
    S3DVec to = s3d_add(feet, s3d_v(0.0f, -step_size, 0.0f));
    float radius_scale = 1.0f;
    S3DGpuHit hit = s3d_trace_body(sim, i, from, to, radius_scale, 0.5f);
    while (hit.started_solid) {
        radius_scale -= 0.1f;
        if (radius_scale < 0.7f) return;
        hit = s3d_trace_body(sim, i, from, to, radius_scale, 0.5f);
    }
    if (hit.hit) {
        float h = sim.crouched[i] ? S3D_GPU_CROUCH_HEIGHT : S3D_GPU_STAND_HEIGHT;
        S3DVec landed = s3d_add(
            s3d_v(hit.px, hit.py, hit.pz), s3d_v(0.0f, h * 0.5f + 0.01f, 0.0f));
        float delta_y = landed.y - old_pos.y;
        sim.px[i] = landed.x;
        sim.py[i] = landed.y;
        sim.pz[i] = landed.z;
        if (delta_y > 0.01f) sim.vy[i] = 0.0f;
    }
}

__device__ void s3d_try_step_impl(S3DGpuSim sim, int i, float max_step_height) {
    if (!sim.on_ground[i]) return;
    S3DVec velocity = s3d_v(sim.vx[i], 0.0f, sim.vz[i]);
    float speed = s3d_len(velocity);
    if (speed < 0.01f) return;
    S3DVec move_dir = s3d_scale(velocity, 1.0f / speed);
    S3DVec feet = s3d_feet(sim, i);
    float forward_dist = speed * S3D_GPU_TIME_STEP + S3D_GPU_BODY_RADIUS;
    S3DVec forward_from = s3d_sub(feet, s3d_scale(move_dir, 0.095f * S3D_GPU_SRC));
    S3DVec forward_to = s3d_add(feet, s3d_scale(move_dir, forward_dist));
    S3DGpuHit tr_forward = s3d_trace_shrunk(sim, i, forward_from,
                                             forward_to, 1.0f);
    if (!tr_forward.hit) return;

    S3DVec hit_pos = s3d_v(forward_from.x +
                               (forward_to.x - forward_from.x) * tr_forward.fraction,
                           forward_from.y +
                               (forward_to.y - forward_from.y) * tr_forward.fraction,
                           forward_from.z +
                               (forward_to.z - forward_from.z) * tr_forward.fraction);
    S3DVec up_to = s3d_add(hit_pos, s3d_v(0.0f, max_step_height, 0.0f));
    S3DGpuHit tr_up = s3d_trace_shrunk(sim, i, hit_pos, up_to, 1.0f);
    if (tr_up.started_solid) return;
    S3DVec top_pos = tr_up.hit ?
        s3d_v(hit_pos.x, hit_pos.y + max_step_height * tr_up.fraction,
              hit_pos.z) : up_to;
    float up_distance = top_pos.y - hit_pos.y;
    if (up_distance < 0.005f) return;

    float across_dist = forward_dist * (1.0f - tr_forward.fraction) +
                        S3D_GPU_BODY_RADIUS * 0.5f;
    S3DGpuHit tr_across = s3d_trace_shrunk(
        sim, i, top_pos, s3d_add(top_pos, s3d_scale(move_dir, across_dist)), 1.0f);
    if (tr_across.started_solid) return;
    S3DVec across_to = s3d_add(top_pos, s3d_scale(move_dir, across_dist));
    S3DVec across_pos = tr_across.hit ?
        s3d_v(top_pos.x + (across_to.x - top_pos.x) * tr_across.fraction,
              top_pos.y + (across_to.y - top_pos.y) * tr_across.fraction,
              top_pos.z + (across_to.z - top_pos.z) * tr_across.fraction) : across_to;
    S3DVec down_to = s3d_add(across_pos, s3d_v(0.0f, -max_step_height, 0.0f));
    S3DGpuHit tr_down = s3d_trace_shrunk(sim, i, across_pos, down_to, 1.0f);
    if (!tr_down.hit || tr_down.ny < S3D_GPU_MAX_SLOPE_COS) return;
    S3DVec down_end = s3d_v(across_pos.x,
                            across_pos.y - max_step_height * tr_down.fraction,
                            across_pos.z);
    float step_height = down_end.y - feet.y;
    if (step_height < 0.01f) return;
    float h = sim.crouched[i] ? S3D_GPU_CROUCH_HEIGHT : S3D_GPU_STAND_HEIGHT;
    S3DVec step_pos = s3d_add(down_end, s3d_v(0.0f, h * 0.5f + 0.01f, 0.0f));
    sim.px[i] = step_pos.x;
    sim.py[i] = step_pos.y;
    sim.pz[i] = step_pos.z;
    sim.vx[i] *= 0.9f;
    sim.vy[i] = 0.0f;
    sim.vz[i] *= 0.9f;
    sim.step_x[i] = step_pos.x;
    sim.step_y[i] = step_pos.y;
    sim.step_z[i] = step_pos.z;
    sim.did_step[i] = 1;
}

__device__ void s3d_update_body(S3DGpuSim sim, int i, S3DVec wish) {
    float wish_len = s3d_len(wish);
    S3DVec velocity = s3d_v(sim.vx[i], sim.vy[i], sim.vz[i]);
    float velocity_len = s3d_len(velocity);
    bool wants_gravity = !sim.on_ground[i] ||
                         velocity_len > 1.0f * S3D_GPU_SRC;
    // The CPU controller has a zero ground velocity for this static course.
    float damping = sim.on_ground[i] && wish_len < 1.0f * S3D_GPU_SRC
                        ? 10.0f * 0.2f : 0.1f;
    float linear_damping = 1.0f / (1.0f + S3D_GPU_TIME_STEP * damping);
    sim.vx[i] = linear_damping * sim.vx[i];
    sim.vy[i] = (wants_gravity ? -S3D_GPU_GRAVITY * S3D_GPU_TIME_STEP : 0.0f) +
                linear_damping * sim.vy[i];
    sim.vz[i] = linear_damping * sim.vz[i];
}

__device__ S3DVec s3d_add_clamped(S3DVec current, S3DVec add,
                                  float max_add_length) {
    float length = s3d_len(add);
    if (length > max_add_length && length > 0.0f)
        add = s3d_scale(add, max_add_length / length);
    return s3d_add(current, add);
}

__device__ void s3d_add_velocity(S3DGpuSim sim, int i, S3DVec wish) {
    S3DVec horizontal = s3d_v(wish.x, 0.0f, wish.z);
    float wish_len = s3d_len(horizontal);
    if (wish_len < 0.001f) return;
    float ground_factor = 0.25f + 0.6f * 10.0f;
    S3DVec velocity = s3d_v(sim.vx[i], sim.vy[i], sim.vz[i]);
    float saved_y = velocity.y;
    float speed = s3d_len(velocity);
    float max_speed = fmaxf(wish_len, speed);
    if (sim.on_ground[i]) {
        velocity = s3d_add_clamped(velocity, s3d_scale(horizontal, ground_factor),
                                   wish_len * ground_factor);
    } else {
        velocity = s3d_add_clamped(velocity, s3d_scale(horizontal, 0.05f),
                                   wish_len);
    }
    float new_speed = s3d_len(velocity);
    if (new_speed > max_speed && new_speed > 0.0f)
        velocity = s3d_scale(velocity, max_speed / new_speed);
    if (sim.on_ground[i]) velocity.y = saved_y;
    sim.vx[i] = velocity.x;
    sim.vy[i] = velocity.y;
    sim.vz[i] = velocity.z;
}

__device__ void s3d_set_crouch(S3DGpuSim sim, int i, bool want_crouch) {
    sim.crouch_wish[i] = want_crouch ? 1 : 0;
    if (want_crouch == (sim.crouched[i] != 0)) return;
    if (want_crouch) {
        float delta = S3D_GPU_STAND_HEIGHT - S3D_GPU_CROUCH_HEIGHT;
        sim.crouched[i] = 1;
        sim.py[i] -= delta * 0.5f;
        return;
    }
    float current_height = S3D_GPU_CROUCH_HEIGHT;
    float delta = S3D_GPU_STAND_HEIGHT - current_height;
    S3DVec feet = s3d_feet(sim, i);
    S3DVec from = s3d_add(feet, s3d_v(0.0f, 0.02f, 0.0f));
    S3DVec to = s3d_add(from, s3d_v(0.0f, delta + 0.02f, 0.0f));
    S3DGpuHit hit = s3d_trace_shrunk(sim, i, from, to, 1.0f);
    if (hit.started_solid || hit.hit) return;
    sim.crouched[i] = 0;
    sim.py[i] += delta * 0.5f;
}

__device__ void s3d_move_body(S3DGpuSim sim, int i) {
    float height = sim.crouched[i] ? S3D_GPU_CROUCH_HEIGHT : S3D_GPU_STAND_HEIGHT;
    float half_x = S3D_GPU_CAPSULE_RADIUS;
    float half_y = height * 0.5f;
    float half_z = S3D_GPU_CAPSULE_RADIUS;
    S3DVec position = s3d_v(sim.px[i], sim.py[i], sim.pz[i]);
    S3DVec velocity = s3d_v(sim.vx[i], sim.vy[i], sim.vz[i]);

    // Resolve an initial overlap before sweeping. This is the equivalent of
    // the solver's non-penetration correction for the fixed static world.
    for (int pass = 0; pass < 4; pass++) {
        bool pushed = false;
        const AABB* boxes = sim.boxes + (size_t)i * MAX_BOXES;
        for (int b = 0; b < sim.num_boxes[i]; b++) {
            const AABB* box = &boxes[b];
            float ox = half_x + box->hx - s3d_abs(position.x - box->cx);
            float oy = half_y + box->hy - s3d_abs(position.y - box->cy);
            float oz = half_z + box->hz - s3d_abs(position.z - box->cz);
            if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) continue;
            pushed = true;
            if (ox < oy && ox < oz) {
                float n = position.x >= box->cx ? 1.0f : -1.0f;
                position.x += n * (ox + S3D_GPU_LINEAR_SLOP);
                if (velocity.x * n < 0.0f) velocity.x = 0.0f;
            } else if (oy < oz) {
                float n = position.y >= box->cy ? 1.0f : -1.0f;
                position.y += n * (oy + S3D_GPU_LINEAR_SLOP);
                if (velocity.y * n < 0.0f) velocity.y = 0.0f;
            } else {
                float n = position.z >= box->cz ? 1.0f : -1.0f;
                position.z += n * (oz + S3D_GPU_LINEAR_SLOP);
                if (velocity.z * n < 0.0f) velocity.z = 0.0f;
            }
        }
        if (!pushed) break;
    }

    S3DVec remaining = s3d_scale(velocity, S3D_GPU_TIME_STEP);
    S3DVec axis_u = s3d_v(1, 0, 0);
    S3DVec axis_w = s3d_v(0, 0, 1);
    for (int iteration = 0; iteration < 5; iteration++) {
        if (s3d_len2(remaining) < 1.0e-12f) break;
        S3DGpuHit hit = s3d_query_box(sim, i, position, remaining,
                                      half_x, half_y, half_z, axis_u, axis_w);
        if (hit.started_solid) break;
        if (!hit.hit) {
            position = s3d_add(position, remaining);
            remaining = s3d_v(0, 0, 0);
            break;
        }
        float fraction = s3d_clamp(hit.fraction - 0.0001f, 0.0f, 1.0f);
        position = s3d_add(position, s3d_scale(remaining, fraction));
        S3DVec normal = s3d_v(hit.nx, hit.ny, hit.nz);
        float velocity_normal = s3d_dot(velocity, normal);
        if (velocity_normal < 0.0f)
            velocity = s3d_sub(velocity, s3d_scale(normal, velocity_normal));
        remaining = s3d_scale(remaining, 1.0f - fraction);
        float remaining_normal = s3d_dot(remaining, normal);
        if (remaining_normal < 0.0f)
            remaining = s3d_sub(remaining, s3d_scale(normal, remaining_normal));
    }
    sim.px[i] = position.x;
    sim.py[i] = position.y;
    sim.pz[i] = position.z;
    sim.vx[i] = velocity.x;
    sim.vy[i] = velocity.y;
    sim.vz[i] = velocity.z;
}

__device__ void s3d_pre_step(S3DGpuSim sim, int i, S3DVec forward,
                             S3DVec right, float throttle_x,
                             float throttle_y) {
    if (sim.jump_cooldown[i] > 0.0f)
        sim.jump_cooldown[i] -= S3D_GPU_TIME_STEP;
    if (!sim.crouch_wish[i] && sim.crouched[i])
        s3d_set_crouch(sim, i, false);
    float max_speed = sim.crouched[i] ? S3D_GPU_CROUCH_SPEED : S3D_GPU_WALK_SPEED;
    S3DVec wish = s3d_add(s3d_scale(forward, max_speed * throttle_x),
                          s3d_scale(right, max_speed * throttle_y));
    float wish_len = s3d_len(wish);
    if (wish_len > max_speed) wish = s3d_scale(wish, max_speed / wish_len);
    s3d_update_body(sim, i, wish);
    s3d_add_velocity(sim, i, wish);
    s3d_try_step_impl(sim, i, 18.0f * S3D_GPU_SRC);
}

__device__ void s3d_post_step(S3DGpuSim sim, int i) {
    if (sim.did_step[i]) {
        sim.px[i] = sim.step_x[i];
        sim.py[i] = sim.step_y[i];
        sim.pz[i] = sim.step_z[i];
        sim.did_step[i] = 0;
    }
    s3d_reground(sim, i, 18.0f * S3D_GPU_SRC);
    s3d_categorize_ground(sim, i);
}

// -----------------------------------------------------------------------------
// Device sensors, rewards, reset, and stepping
// -----------------------------------------------------------------------------

__device__ int s3d_gpu_occupancy_index(int vertical, int lateral, int forward) {
    return (vertical * OCCUPANCY_LATERAL_BINS + lateral) *
           OCCUPANCY_FORWARD_BINS + forward;
}

__device__ void s3d_update_occupancy(S3DGpuSim sim, int i) {
    int interval = sim.cfg.occupancy_interval > 0 ? sim.cfg.occupancy_interval :
                                                     OCCUPANCY_UPDATE_INTERVAL;
    if (sim.occupancy_valid[i] && sim.tick[i] % interval != 0) return;
    float* occupancy = sim.occupancy + (size_t)i * OCCUPANCY_SIZE;
    for (int j = 0; j < OCCUPANCY_SIZE; j++) occupancy[j] = 0.0f;
    S3DVec feet = s3d_feet(sim, i);
    float cy = cosf(sim.yaw[i]), sy = sinf(sim.yaw[i]);
    S3DVec forward = s3d_v(cy, 0.0f, sy);
    S3DVec right = s3d_v(-sy, 0.0f, cy);
    float forward_step = OCCUPANCY_RANGE / OCCUPANCY_FORWARD_BINS;
    for (int v = 0; v < OCCUPANCY_VERTICAL_BINS; v++) {
        float height = OCCUPANCY_VERTICAL_MIN + v * OCCUPANCY_VERTICAL_STEP;
        for (int l = 0; l < OCCUPANCY_LATERAL_BINS; l++) {
            float lateral = OCCUPANCY_LATERAL_MIN +
                            (l + 0.5f) * OCCUPANCY_LATERAL_STEP;
            S3DVec origin = s3d_add(feet, s3d_add(s3d_scale(right, lateral),
                                                  s3d_v(0.0f, height, 0.0f)));
            float hit_distance = s3d_horizontal_ray_distance(
                sim, i, origin, forward, OCCUPANCY_RANGE);
            int hit_bin = OCCUPANCY_FORWARD_BINS;
            if (hit_distance < OCCUPANCY_RANGE) {
                hit_bin = (int)(hit_distance / forward_step);
                if (hit_bin < 0) hit_bin = 0;
                if (hit_bin >= OCCUPANCY_FORWARD_BINS)
                    hit_bin = OCCUPANCY_FORWARD_BINS - 1;
            }
            for (int f = 0; f < hit_bin; f++)
                occupancy[s3d_gpu_occupancy_index(v, l, f)] = 0.5f;
            if (hit_bin < OCCUPANCY_FORWARD_BINS)
                occupancy[s3d_gpu_occupancy_index(v, l, hit_bin)] = 1.0f;
        }
    }
    sim.occupancy_valid[i] = 1;
}

__device__ void s3d_update_depth(S3DGpuSim sim, int i) {
    int interval = sim.cfg.depth_interval > 0 ? sim.cfg.depth_interval :
                                                DEPTH_MAP_UPDATE_INTERVAL;
    if (sim.depth_valid[i] && sim.tick[i] % interval != 0) return;
    float* depth = sim.depth + (size_t)i * DEPTH_MAP_SIZE;
    S3DVec feet = s3d_feet(sim, i);
    float eye_y = feet.y + (sim.crouched[i] ? S3D_GPU_CROUCH_HEIGHT :
                                                   S3D_GPU_STAND_HEIGHT) - 0.2032f;
    static const float pitches[DEPTH_MAP_HEIGHT] = {-60.0f, -35.0f, -10.0f,
                                                     15.0f, 40.0f};
    for (int p = 0; p < DEPTH_MAP_HEIGHT; p++) {
        float pitch = pitches[p] * S3D_GPU_PI / 180.0f;
        float cp = cosf(pitch);
        for (int a = 0; a < DEPTH_MAP_WIDTH; a++) {
            float angle = sim.yaw[i] +
                          (float)a / DEPTH_MAP_WIDTH * 2.0f * S3D_GPU_PI;
            S3DVec direction = s3d_v(cosf(angle) * cp, sinf(pitch),
                                     sinf(angle) * cp);
            float distance = s3d_ray_distance(
                sim, i, s3d_v(feet.x, eye_y, feet.z), direction, RAY_RANGE);
            depth[p * DEPTH_MAP_WIDTH + a] = distance / RAY_RANGE;
        }
    }
    sim.depth_valid[i] = 1;
}

__device__ void s3d_compute_observations(S3DGpuSim sim, int i) {
    s3d_update_occupancy(sim, i);
    s3d_update_depth(sim, i);
    obs_t* obs = sim.observations + (size_t)i * OBS_SIZE;
    S3DVec feet = s3d_feet(sim, i);
    float cy = cosf(sim.yaw[i]), sy = sinf(sim.yaw[i]);
    obs[0] = (cy * sim.vx[i] + sy * sim.vz[i]) / MAX_SPEED;
    obs[1] = (-sy * sim.vx[i] + cy * sim.vz[i]) / MAX_SPEED;
    obs[2] = sy;
    obs[3] = cy;
    obs[4] = sim.on_ground[i] ? 1.0f : 0.0f;
    obs[5] = sim.crouched[i] ? 1.0f : 0.0f;
    obs[6] = fminf(s3d_ray_distance(sim, i,
                                    s3d_v(feet.x, feet.y + 0.05f, feet.z),
                                    s3d_v(0, -1, 0), GROUND_RAY),
                   GROUND_RAY) / GROUND_RAY;
    float route_length = sim.courses[i].route_length > 0.0f ?
        sim.courses[i].route_length : COURSE_LENGTH;
    obs[7] = fminf(s3d_goal_dist(sim, i) / route_length, 1.5f);
    float bearing = atan2f(sim.courses[i].goal_z - feet.z,
                           sim.courses[i].goal_x - feet.x) - sim.yaw[i];
    while (bearing > S3D_GPU_PI) bearing -= 2.0f * S3D_GPU_PI;
    while (bearing < -S3D_GPU_PI) bearing += 2.0f * S3D_GPU_PI;
    obs[8] = sinf(bearing);
    obs[9] = cosf(bearing);
    obs[10] = s3d_clamp(sim.vy[i] / 15.0f, -1.0f, 1.0f);
    for (int j = 0; j < DEPTH_MAP_SIZE; j++)
        obs[DEPTH_MAP_OFFSET + j] = sim.depth[(size_t)i * DEPTH_MAP_SIZE + j];
    for (int j = 0; j < OCCUPANCY_SIZE; j++)
        obs[OCCUPANCY_OFFSET + j] =
            sim.occupancy[(size_t)i * OCCUPANCY_SIZE + j];
}

__device__ bool s3d_forward_obstacle(S3DGpuSim sim, int i, int fwd,
                                     S3DVec forward) {
    if (fwd <= 0) return false;
    float speed = sim.vx[i] * forward.x + sim.vz[i] * forward.z;
    if (speed > 0.5f) return false;
    S3DVec feet = s3d_feet(sim, i);
    S3DVec target = s3d_add(feet, s3d_scale(forward, HEAD_HIT_RANGE));
    S3DGpuHit hit = s3d_trace_body(sim, i, feet, target, 1.0f, 1.0f);
    return hit.hit || hit.started_solid;
}

__device__ void s3d_reset_state(S3DGpuSim sim, int i, bool generate_course) {
    if (generate_course) s3d_generate_course(sim, i, true);
    sim.tick[i] = 0;
    sim.px[i] = 0.0f;
    sim.py[i] = 1.0f;
    sim.pz[i] = 0.0f;
    sim.vx[i] = 0.0f;
    sim.vy[i] = 0.0f;
    sim.vz[i] = 0.0f;
    sim.yaw[i] = 0.0f;
    sim.jump_cooldown[i] = 0.0f;
    sim.prev_progress_dist[i] = s3d_progress_dist(sim, i);
    sim.episode_return[i] = 0.0f;
    sim.progress_sum[i] = 0.0f;
    sim.ground_nx[i] = 0.0f;
    sim.ground_ny[i] = 1.0f;
    sim.ground_nz[i] = 0.0f;
    sim.step_x[i] = 0.0f;
    sim.step_y[i] = 1.0f;
    sim.step_z[i] = 0.0f;
    sim.crouch_commit_ticks[i] = 0;
    sim.on_ground[i] = 0;
    sim.crouched[i] = 0;
    sim.crouch_wish[i] = 0;
    sim.did_step[i] = 0;
    sim.depth_valid[i] = 0;
    sim.occupancy_valid[i] = 0;
    s3d_compute_observations(sim, i);
}

__device__ void s3d_end_episode(S3DGpuSim sim, int i, float perf_score) {
    float* log = (float*)&sim.envs[i].log;
    log[0] += perf_score;
    log[1] += sim.episode_return[i];
    log[2] += (float)sim.tick[i];
    log[3] += sim.progress_sum[i];
    log[4] += 1.0f;
    sim.terminals[i] = 1.0f;
    bool regenerate = sim.cfg.course_mode == COURSE_MODE_RANDOM_EVERY_RESET &&
                      sim.tick[i] > 0;
    s3d_reset_state(sim, i, regenerate);
}

__global__ static void s3d_init_kernel(S3DGpuSim sim) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    sim.rng[i] = (uint32_t)i;
    sim.tick[i] = 0;
    s3d_set_fixed_course(&sim.courses[i]);
    s3d_generate_course(sim, i, true);
    sim.crouched[i] = 0;
    sim.on_ground[i] = 0;
}

__global__ static void s3d_reset_kernel(S3DGpuSim sim, int total) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= total) return;
    bool regenerate = sim.cfg.course_mode == COURSE_MODE_RANDOM_EVERY_RESET &&
                      sim.tick[i] > 0;
    s3d_reset_state(sim, i, regenerate);
}

__global__ static void s3d_step_kernel(S3DGpuSim sim, int start, int count) {
    int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    int i = start + lane;
    sim.rewards[i] = 0.0f;
    sim.terminals[i] = 0.0f;
    sim.tick[i] += 1;
    if (sim.tick[i] > sim.cfg.max_ticks) {
        s3d_end_episode(sim, i, 0.0f);
        return;
    }

    const float* action = sim.actions + (size_t)i * NUM_ATNS;
    int turn = (int)fmaxf(0.0f, fminf(action[0], 4.0f));
    const float turn_values[5] = {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f};
    sim.yaw[i] += turn_values[turn] * S3D_GPU_PI / 180.0f;
    int fwd = (int)fmaxf(0.0f, fminf(action[1], 2.0f)) - 1;
    int strafe = (int)fmaxf(0.0f, fminf(action[2], 2.0f)) - 1;
    bool jump = action[3] > 0.5f;
    bool successful_jump = jump && sim.on_ground[i] &&
                           sim.jump_cooldown[i] <= 0.0f;

    bool crouched_before = sim.crouched[i] != 0;
    bool request_crouch = action[4] > 0.5f;
    if (sim.crouch_commit_ticks[i] > 0) {
        request_crouch = crouched_before;
        sim.crouch_commit_ticks[i]--;
    }
    s3d_set_crouch(sim, i, request_crouch);
    bool crouched_after = sim.crouched[i] != 0;
    bool successful_crouch = !crouched_before && crouched_after;
    if (successful_crouch) {
        sim.crouch_commit_ticks[i] = sim.cfg.crouch_enter_commit_ticks;
    } else if (crouched_before && !crouched_after) {
        sim.crouch_commit_ticks[i] = sim.cfg.crouch_exit_commit_ticks;
    }

    float cy = cosf(sim.yaw[i]), sy = sinf(sim.yaw[i]);
    S3DVec forward = s3d_v(cy, 0.0f, sy);
    S3DVec right = s3d_v(-sy, 0.0f, cy);
    if (jump && sim.on_ground[i] && sim.jump_cooldown[i] <= 0.0f) {
        sim.vy[i] = S3D_GPU_JUMP_SPEED;
        sim.on_ground[i] = 0;
        sim.jump_cooldown[i] = 0.2f;
    }
    s3d_pre_step(sim, i, forward, right, (float)fwd, (float)strafe);
    s3d_move_body(sim, i);
    s3d_post_step(sim, i);

    float distance = s3d_progress_dist(sim, i);
    float progress = sim.prev_progress_dist[i] - distance;
    sim.prev_progress_dist[i] = distance;
    sim.progress_sum[i] += progress;
    float reward = progress * sim.cfg.reward_progress - sim.cfg.time_cost;
    if (successful_jump) reward += sim.cfg.jump_penalty;
    if (successful_crouch) reward += sim.cfg.crouch_penalty;
    if (s3d_forward_obstacle(sim, i, fwd, forward))
        reward += sim.cfg.reward_head_hit;
    sim.rewards[i] += reward;
    sim.episode_return[i] += reward;

    S3DVec feet = s3d_feet(sim, i);
    const CourseParams* course = &sim.courses[i];
    if (s3d_abs(feet.x - course->goal_x) < 0.65f &&
        s3d_abs(feet.z - course->goal_z) < 0.65f &&
        feet.y > course->goal_y - 1.0f && feet.y < course->goal_y + 1.5f) {
        sim.rewards[i] += sim.cfg.reward_goal;
        sim.episode_return[i] += sim.cfg.reward_goal;
        s3d_end_episode(sim, i, 1.0f);
        return;
    }
    if (feet.y < KILL_Y) {
        sim.rewards[i] += sim.cfg.reward_fall;
        sim.episode_return[i] += sim.cfg.reward_fall;
        s3d_end_episode(sim, i, 0.0f);
        return;
    }
    s3d_compute_observations(sim, i);
}

// -----------------------------------------------------------------------------
// PufferLib GPU environment ABI
// -----------------------------------------------------------------------------

static S3DNative* s3d_find_native(Env* envs) {
    for (int i = 0; i < 8; i++)
        if (s3d_native[i].envs == envs) return &s3d_native[i];
    return nullptr;
}

static void s3d_unregister_native(Env* envs) {
    S3DNative* native = s3d_find_native(envs);
    if (native) *native = {};
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    S3DNative* native = nullptr;
    for (int i = 0; i < 8; i++) {
        if (s3d_native[i].envs == nullptr) {
            native = &s3d_native[i];
            break;
        }
    }
    if (!native) {
        std::fprintf(stderr, "too many shenaniguns3d GPU environment pools\n");
        std::exit(1);
    }
    *native = {};
    S3DGpuSim& sim = native->sim;
    sim.count = total_agents;
    sim.cfg = s3d_gpu_config(env_kwargs);
    s3d_gpu_alloc((void**)&sim.envs, (size_t)total_agents * sizeof(Env),
                  "allocate Env shells");
    s3d_gpu_check(cudaMemset(sim.envs, 0, (size_t)total_agents * sizeof(Env)),
                  "clear Env shells");
    s3d_gpu_alloc((void**)&sim.boxes,
                  (size_t)total_agents * MAX_BOXES * sizeof(AABB),
                  "allocate course boxes");
    s3d_gpu_alloc((void**)&sim.num_boxes, (size_t)total_agents * sizeof(int),
                  "allocate box counts");
    s3d_gpu_alloc((void**)&sim.courses,
                  (size_t)total_agents * sizeof(CourseParams),
                  "allocate course parameters");
    s3d_gpu_check(cudaMemset(sim.courses, 0,
                             (size_t)total_agents * sizeof(CourseParams)),
                  "clear course parameters");
    s3d_gpu_alloc((void**)&sim.rng, (size_t)total_agents * sizeof(uint32_t),
                  "allocate course RNG");
    s3d_gpu_alloc((void**)&sim.tick, (size_t)total_agents * sizeof(int),
                  "allocate ticks");

#define S3D_ALLOC_FIELD(field, type) \
    s3d_gpu_alloc((void**)&sim.field, (size_t)total_agents * sizeof(type), \
                  "allocate state field")
    S3D_ALLOC_FIELD(px, float); S3D_ALLOC_FIELD(py, float);
    S3D_ALLOC_FIELD(pz, float); S3D_ALLOC_FIELD(vx, float);
    S3D_ALLOC_FIELD(vy, float); S3D_ALLOC_FIELD(vz, float);
    S3D_ALLOC_FIELD(yaw, float); S3D_ALLOC_FIELD(jump_cooldown, float);
    S3D_ALLOC_FIELD(prev_progress_dist, float);
    S3D_ALLOC_FIELD(episode_return, float);
    S3D_ALLOC_FIELD(progress_sum, float);
    S3D_ALLOC_FIELD(ground_nx, float); S3D_ALLOC_FIELD(ground_ny, float);
    S3D_ALLOC_FIELD(ground_nz, float); S3D_ALLOC_FIELD(step_x, float);
    S3D_ALLOC_FIELD(step_y, float); S3D_ALLOC_FIELD(step_z, float);
    S3D_ALLOC_FIELD(crouch_commit_ticks, int);
    S3D_ALLOC_FIELD(on_ground, unsigned char);
    S3D_ALLOC_FIELD(crouched, unsigned char);
    S3D_ALLOC_FIELD(crouch_wish, unsigned char);
    S3D_ALLOC_FIELD(did_step, unsigned char);
    S3D_ALLOC_FIELD(depth_valid, unsigned char);
    S3D_ALLOC_FIELD(occupancy_valid, unsigned char);
    s3d_gpu_alloc((void**)&sim.depth,
                  (size_t)total_agents * DEPTH_MAP_SIZE * sizeof(float),
                  "allocate depth state");
    s3d_gpu_alloc((void**)&sim.occupancy,
                  (size_t)total_agents * OCCUPANCY_SIZE * sizeof(float),
                  "allocate occupancy state");
#undef S3D_ALLOC_FIELD

    native->envs = sim.envs;
    s3d_init_kernel<<<s3d_gpu_grid(total_agents), S3D_GPU_BLOCK_SIZE>>>(sim);
    s3d_gpu_check_launch("initialize device environments");
    return sim.envs;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
                           float* terminals, int total_agents) {
    S3DNative* native = s3d_find_native(envs);
    if (!native || total_agents != native->sim.count) std::abort();
    S3DGpuSim& sim = native->sim;
    sim.observations = observations;
    sim.rewards = rewards;
    sim.terminals = terminals;
    s3d_reset_kernel<<<s3d_gpu_grid(total_agents), S3D_GPU_BLOCK_SIZE>>>(
        sim, total_agents);
    s3d_gpu_check_launch("reset device environments");
    s3d_gpu_check(cudaMemset(rewards, 0, (size_t)total_agents * sizeof(float)),
                  "clear reset rewards");
    s3d_gpu_check(cudaMemset(terminals, 0,
                             (size_t)total_agents * sizeof(float)),
                  "clear reset terminals");
}

static void puf_envs_step(Env* envs, const float* actions, obs_t* observations,
                          float* rewards, float* terminals, int start, int count,
                          cudaStream_t stream) {
    S3DNative* native = s3d_find_native(envs);
    if (!native || start < 0 || count < 0 || start + count > native->sim.count)
        std::abort();
    if (start != 0 || count != native->sim.count) {
        std::fprintf(stderr,
                     "shenaniguns3d GPU requires full-batch stepping\n");
        std::exit(1);
    }
    S3DGpuSim& sim = native->sim;
    sim.actions = actions;
    sim.observations = observations;
    sim.rewards = rewards;
    sim.terminals = terminals;
    s3d_step_kernel<<<s3d_gpu_grid(count), S3D_GPU_BLOCK_SIZE, 0, stream>>>(
        sim, start, count);
    s3d_gpu_check_launch("step device environments");
}

static void puf_envs_close(Env* envs) {
    S3DNative* native = s3d_find_native(envs);
    if (!native) return;
    s3d_gpu_check(cudaDeviceSynchronize(), "finish device environments");
    S3DGpuSim& sim = native->sim;
    s3d_gpu_free(sim.boxes); s3d_gpu_free(sim.num_boxes);
    s3d_gpu_free(sim.courses); s3d_gpu_free(sim.rng); s3d_gpu_free(sim.tick);
    s3d_gpu_free(sim.px); s3d_gpu_free(sim.py); s3d_gpu_free(sim.pz);
    s3d_gpu_free(sim.vx); s3d_gpu_free(sim.vy); s3d_gpu_free(sim.vz);
    s3d_gpu_free(sim.yaw); s3d_gpu_free(sim.jump_cooldown);
    s3d_gpu_free(sim.prev_progress_dist); s3d_gpu_free(sim.episode_return);
    s3d_gpu_free(sim.progress_sum); s3d_gpu_free(sim.ground_nx);
    s3d_gpu_free(sim.ground_ny); s3d_gpu_free(sim.ground_nz);
    s3d_gpu_free(sim.step_x); s3d_gpu_free(sim.step_y); s3d_gpu_free(sim.step_z);
    s3d_gpu_free(sim.crouch_commit_ticks); s3d_gpu_free(sim.on_ground);
    s3d_gpu_free(sim.crouched); s3d_gpu_free(sim.crouch_wish);
    s3d_gpu_free(sim.did_step); s3d_gpu_free(sim.depth_valid);
    s3d_gpu_free(sim.occupancy_valid); s3d_gpu_free(sim.depth);
    s3d_gpu_free(sim.occupancy); s3d_gpu_free(sim.envs);
    s3d_unregister_native(envs);
}

#endif
