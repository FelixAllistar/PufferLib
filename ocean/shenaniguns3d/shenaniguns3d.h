// Shenanigans 3D — FPS navigation gym ON BOX3D PHYSICS.
//
// The sim IS the deployment stack: pd64/character.{h,c} (s&box-style
// controller) runs inside a real box3d world, exactly like the game will.
// No hand-rolled collision, no sim gap for locomotion, no reward hacks from
// kinematic edge cases.
//
// Course (identical layout to the original kinematic version):
//   spawn -> doorway (gap |z|<0.8) -> crouch tunnel (1.15m clearance)
//   -> floor hole x[22,26] (3m drop) -> lower corridor -> goal pedestal
//
// UNITS: meters, dt = 1/60 fixed, y-up. Character: s&box dims (r=16u,
// h=72u stand / 40u crouch), Source-style wish-velocity movement.
//
// OBSERVATIONS (float32[26]): unchanged contract
//   [0:2] ego vel fwd/right / MAX_SPEED   [2:4] sin/cos yaw
//   [5] onGround  [6] crouched  [7] ground dist / 5
//   [8] goal dist / COURSE_LENGTH  [9:11] sin/cos goal bearing-yaw
//   [11:23] 12 horizontal rays @ eye      [23:26] 3 pitched-down rays
//
// ACTIONS (NUM_ATNS = 5): turn{5} fwd{3} strafe{3} jump{2} crouch{2}
//
// REWARD: +1/m progress to goal, +10 goal, -0.0005/tick, -10 kill-plane.

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
#define H_RAYS 12

#define NUM_ATNS 5
#define ACT_SIZES {5, 3, 3, 2, 2}
#define OBS_SIZE 26

#define MAX_BOXES 32
#define COURSE_LENGTH 36.0f
#define KILL_Y -9.0f
#define GOAL_X 30.0f
#define GOAL_Z 0.0f
#define GOAL_Y (-3.0f)
#define GOAL_BONUS 10.0f
#define TIME_COST 0.0005f
#define FALL_PENALTY 10.0f
#define PROGRESS_SHAPING_DEFAULT 0.05f
#define MAX_TICKS_DEFAULT 3600

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

struct Env {
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

    float prevGoalDist;
    float epReturn;
    float progressSum;
    bool reachedGoal;
    bool lastGoal;

    // reward knobs (configurable via [env] ini, see puf_init)
    float reward_progress; // small telescoping potential-delta scale
    float time_cost;       // - per tick (0.0005 default)
    float reward_goal;     // + on goal
    float reward_fall;     // - on kill-plane (negative)

    Log log;
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

static void build_course(Shenanigans3D* env) {
    env->numBoxes = 0;

    // A: floor x[-4,6] top 0
    add_box(env, 1.0f, -1.0f, 0.0f, 5.0f, 1.0f, 2.0f);

    // doorway wall at x=6 (gap |z|<0.8, opening 2.2 high)
    add_box(env, 6.0f, 2.1f, 1.525f, 0.2f, 2.1f, 0.725f);
    add_box(env, 6.0f, 2.1f, -1.525f, 0.2f, 2.1f, 0.725f);
    add_box(env, 6.0f, 3.2f, 0.0f, 0.2f, 1.1f, 2.0f);

    // B: floor x[6,22] top 0
    add_box(env, 14.0f, -1.0f, 0.0f, 8.0f, 1.0f, 2.0f);

    // crouch tunnel ceiling x[12,16], bottom at 1.15
    add_box(env, 14.0f, 2.575f, 0.0f, 2.0f, 1.425f, 2.0f);

    // HOLE x[22,26]: no floor; landing pad + lower corridor slab x[22,34] top -3
    add_box(env, 28.0f, -4.0f, 0.0f, 6.0f, 1.0f, 2.0f);

    // goal pedestal
    add_box(env, GOAL_X, GOAL_Y + 0.25f, GOAL_Z, 0.35f, 0.25f, 0.35f);
}

// ---------------------------------------------------------------------------
// Senses (real engine rays)
// ---------------------------------------------------------------------------

static float cast_ray_dist(Shenanigans3D* env, float ox, float oy, float oz,
                           float dx, float dy, float dz, float maxT) {
    b3Vec3 trans = { dx * maxT, dy * maxT, dz * maxT };
    b3RayResult r = b3World_CastRayClosest(env->world, (b3Pos){ ox, oy, oz }, trans,
                                           b3DefaultQueryFilter());
    return r.hit ? r.fraction * maxT : maxT;
}

static float feet_x(Shenanigans3D* env) { return (float)pd_char_feet_position(&env->ch).x; }
static float feet_y(Shenanigans3D* env) { return (float)pd_char_feet_position(&env->ch).y; }
static float feet_z(Shenanigans3D* env) { return (float)pd_char_feet_position(&env->ch).z; }

static float goal_dist(Shenanigans3D* env) {
    float dx = GOAL_X - feet_x(env);
    float dz = GOAL_Z - feet_z(env);
    float dy = GOAL_Y - feet_y(env);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void compute_observations(Shenanigans3D* env) {
    PDCharacter* c = &env->ch;
    obs_t* obs = (obs_t*)env->agents[0].observations;

    float px = feet_x(env), py = feet_y(env), pz = feet_z(env);
    float eyeY = py + c->totalHeight - 0.2032f;
    float cy = cosf(env->yaw), sy = sinf(env->yaw);

    b3Vec3 v = b3Body_GetLinearVelocity(c->body);
    obs[0] = (cy * v.x + sy * v.z) / MAX_SPEED;
    obs[1] = (-sy * v.x + cy * v.z) / MAX_SPEED;
    obs[2] = sy;
    obs[3] = cy;
    obs[4] = c->onGround ? 1.0f : 0.0f;
    obs[5] = pd_char_is_crouched(c) ? 1.0f : 0.0f;
    obs[6] = fminf(cast_ray_dist(env, px, py + 0.05f, pz, 0, -1, 0, GROUND_RAY),
                   GROUND_RAY) / GROUND_RAY;
    obs[7] = fminf(goal_dist(env) / COURSE_LENGTH, 1.5f);
    float bearing = atan2f(GOAL_Z - pz, GOAL_X - px) - env->yaw;
    while (bearing > (float)M_PI) bearing -= 2.0f * (float)M_PI;
    while (bearing < -(float)M_PI) bearing += 2.0f * (float)M_PI;
    obs[8] = sinf(bearing);
    obs[9] = cosf(bearing);

    float* hr = obs + 11;
    for (int r = 0; r < H_RAYS; r++) {
        float ang = env->yaw + (float)r / (float)H_RAYS * 2.0f * (float)M_PI;
        hr[r] = fminf(cast_ray_dist(env, px, eyeY, pz, cosf(ang), 0, sinf(ang), RAY_RANGE),
                      RAY_RANGE) / RAY_RANGE;
    }

    static const float pitches[3] = { -30.0f, -50.0f, -70.0f };
    float* pr = obs + 11 + H_RAYS;
    for (int r = 0; r < 3; r++) {
        float pit = pitches[r] * (float)M_PI / 180.0f;
        float cp = cosf(pit);
        pr[r] = fminf(cast_ray_dist(env, px, eyeY, pz, cy * cp, sinf(pit), sy * cp,
                                    RAY_RANGE),
                      RAY_RANGE) / RAY_RANGE;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void init(Shenanigans3D* env) {
    env->tag = 0;
    env->boundary_reached = 0;
    env->reward_progress = PROGRESS_SHAPING_DEFAULT;
    env->time_cost = TIME_COST;
    env->reward_goal = GOAL_BONUS;
    env->reward_fall = -FALL_PENALTY;

    b3WorldDef worldDef = b3DefaultWorldDef();
    env->world = b3CreateWorld(&worldDef);
    build_course(env);
    pd_char_init(&env->ch, env->world, (b3Pos){ 0.0f, 1.0f, 0.0f });
}

void allocate_env(Shenanigans3D* env) {
    init(env);
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->max_ticks = MAX_TICKS_DEFAULT;

    init((Shenanigans3D*)env);

    // reward shaping — all tunable from config without recompile
    DictItem* it;
    it = dict_find(kwargs, "reward_progress");
    ((Shenanigans3D*)env)->reward_progress =
        it ? (float)it->value : PROGRESS_SHAPING_DEFAULT;
    it = dict_find(kwargs, "time_cost");
    ((Shenanigans3D*)env)->time_cost = it ? (float)it->value : TIME_COST;
    it = dict_find(kwargs, "reward_goal");
    ((Shenanigans3D*)env)->reward_goal = it ? (float)it->value : GOAL_BONUS;
    it = dict_find(kwargs, "reward_fall");
    ((Shenanigans3D*)env)->reward_fall = it ? (float)it->value : -FALL_PENALTY;
    it = dict_find(kwargs, "max_ticks");
    if (it) env->max_ticks = (int)it->value;
}

void puf_reset(Shenanigans3D* env) {
    env->tick = 0;
    env->epReturn = 0.0f;
    env->progressSum = 0.0f;
    env->reachedGoal = false;

    // teleport character back to spawn, zero velocity, force standing
    PDCharacter* c = &env->ch;
    b3Body_SetTransform(c->body, (b3Pos){ 0.0f, 1.0f, 0.0f }, b3Quat_identity);
    b3Body_SetLinearVelocity(c->body, (b3Vec3){ 0, 0, 0 });
    c->crouchWish = false;
    pd_char_set_crouch(c, false); // clearance is trivially clear at spawn
    env->yaw = 0.0f;

    env->prevGoalDist = goal_dist(env);
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
    if (env->client != NULL) {
        if (IsWindowReady()) CloseWindow();
        free(env->client);
        env->client = NULL;
    }
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
    env->tick++;
    if (env->tick > env->max_ticks) {
        end_episode(env, 0.0f);
        return;
    }
    *env->agents[0].rewards = 0.0f;

    // --- actions ---
    float* atn = env->agents[0].actions;
    int turn = (int)fmaxf(0.0f, fminf(atn[0], 4.0f));
    static const float turnVals[5] = { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f };
    env->yaw += turnVals[turn] * (float)M_PI / 180.0f;

    int fwd = (int)fmaxf(0.0f, fminf(atn[1], 2.0f)) - 1;
    int strafe = (int)fmaxf(0.0f, fminf(atn[2], 2.0f)) - 1;
    bool jump = atn[3] > 0.5f;

    pd_char_set_crouch(&env->ch, atn[4] > 0.5f);

    float cy = cosf(env->yaw), sy = sinf(env->yaw);
    b3Vec3 forward = { cy, 0.0f, sy };
    b3Vec3 right = { -sy, 0.0f, cy };
    b3Vec2 throttle = { (float)fwd, (float)strafe };

    pd_char_pre_step(&env->ch, 1.0f / DT, forward, right, throttle);
    if (jump) pd_char_jump(&env->ch);
    b3World_Step(env->world, 1.0f / DT, SIM_SUBSTEPS);
    pd_char_post_step(&env->ch, 1.0f / DT);

    // --- reward shaping (all weights from config) ---
    float d = goal_dist(env);
    // Signed potential delta: summing this over an episode telescopes to
    // initial goal distance minus final goal distance.
    float progress = env->prevGoalDist - d;
    env->prevGoalDist = d;
    env->progressSum += progress;
    float stepReward = progress * env->reward_progress - env->time_cost;
    *env->agents[0].rewards += stepReward;
    env->epReturn += stepReward;

    float py = feet_y(env);
    if (fabsf(feet_x(env) - GOAL_X) < 0.65f && fabsf(feet_z(env) - GOAL_Z) < 0.65f &&
        py > GOAL_Y - 1.0f && py < GOAL_Y + 1.5f) {
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

    DrawCube((Vector3){ GOAL_X, GOAL_Y + 1.0f, GOAL_Z }, 0.4f, 2.0f, 0.4f,
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

    for (int r = 0; r < H_RAYS; r++) {
        float ang = env->yaw + (float)r / (float)H_RAYS * 2.0f * (float)M_PI;
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
