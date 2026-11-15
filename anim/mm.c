/*
 * mm.c: motion-matching runtime. See mm.h and README.md.
 *
 * The spring formulas (critically damped spring-damper, its decay form and
 * the character velocity spring), the inertialization offsets and the
 * two-level bounding-box search are adapted from Daniel Holden's
 * Motion-Matching sample (github.com/orangeduck/Motion-Matching), which is
 * distributed under the following license:
 *
 *   MIT License
 *
 *   Copyright (c) 2021 Daniel Holden
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "mm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MM_PI 3.14159265358979323846f
#define MM_LN2 0.69314718056f
#define MM_EPS 1e-8f

/* ------------------------------------------------------------------ math */

mm_vec3 mm_vec3_make(float x, float y, float z) {
    mm_vec3 v = {x, y, z};
    return v;
}

static mm_vec3 v_add(mm_vec3 a, mm_vec3 b) { return mm_vec3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static mm_vec3 v_sub(mm_vec3 a, mm_vec3 b) { return mm_vec3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static mm_vec3 v_scale(mm_vec3 a, float s) { return mm_vec3_make(a.x * s, a.y * s, a.z * s); }
static float v_dot(mm_vec3 a, mm_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float v_length(mm_vec3 a) { return sqrtf(v_dot(a, a)); }
static mm_vec3 v_cross(mm_vec3 a, mm_vec3 b) {
    return mm_vec3_make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static mm_vec3 v_normalize(mm_vec3 a) {
    float l = v_length(a);
    return l > MM_EPS ? v_scale(a, 1.0f / l) : mm_vec3_make(0.0f, 0.0f, 0.0f);
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static float wrap_angle(float a) {
    while (a > MM_PI) a -= 2.0f * MM_PI;
    while (a < -MM_PI) a += 2.0f * MM_PI;
    return a;
}

mm_quat mm_quat_mul(mm_quat a, mm_quat b) {
    mm_quat q;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return q;
}

mm_quat mm_quat_inv(mm_quat q) {
    mm_quat r = {-q.x, -q.y, -q.z, q.w};
    return r;
}

mm_vec3 mm_quat_rotate(mm_quat q, mm_vec3 v) {
    mm_vec3 u = {q.x, q.y, q.z};
    mm_vec3 t = v_scale(v_cross(u, v), 2.0f);
    return v_add(v_add(v, v_scale(t, q.w)), v_cross(u, t));
}

mm_quat mm_quat_from_yaw(float yaw) {
    mm_quat q = {0.0f, sinf(0.5f * yaw), 0.0f, cosf(0.5f * yaw)};
    return q;
}

static mm_quat q_normalize(mm_quat q) {
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    mm_quat r = {q.x / l, q.y / l, q.z / l, q.w / l};
    return r;
}

static mm_quat q_abs(mm_quat q) {
    if (q.w < 0.0f) {
        mm_quat r = {-q.x, -q.y, -q.z, -q.w};
        return r;
    }
    return q;
}

mm_quat mm_quat_nlerp_shortest(mm_quat a, mm_quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s = d < 0.0f ? -1.0f : 1.0f;
    mm_quat r = {a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t,
                 a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t};
    return q_normalize(r);
}

static mm_quat q_from_angle_axis(float angle, mm_vec3 axis) {
    float s = sinf(0.5f * angle);
    mm_quat q = {axis.x * s, axis.y * s, axis.z * s, cosf(0.5f * angle)};
    return q;
}

/* Rotation vector (axis times angle) of a quaternion. */
static mm_vec3 q_to_scaled_angle_axis(mm_quat q) {
    q = q_abs(q);
    mm_vec3 u = {q.x, q.y, q.z};
    float len = v_length(u);
    if (len < 1e-7f) return v_scale(u, 2.0f);
    float angle = 2.0f * atan2f(len, q.w);
    return v_scale(u, angle / len);
}

static mm_quat q_from_scaled_angle_axis(mm_vec3 v) {
    float angle = v_length(v);
    if (angle < 1e-7f) {
        mm_quat q = {0.5f * v.x, 0.5f * v.y, 0.5f * v.z, 1.0f};
        return q_normalize(q);
    }
    return q_from_angle_axis(angle, v_scale(v, 1.0f / angle));
}

/* Shortest rotation taking direction a to direction b (both normalized). */
static mm_quat q_between(mm_vec3 a, mm_vec3 b) {
    mm_vec3 c = v_cross(a, b);
    float d = v_dot(a, b);
    if (d < -0.999999f) {
        mm_vec3 axis = v_cross(mm_vec3_make(1.0f, 0.0f, 0.0f), a);
        if (v_length(axis) < 1e-4f) axis = v_cross(mm_vec3_make(0.0f, 1.0f, 0.0f), a);
        return q_from_angle_axis(MM_PI, v_normalize(axis));
    }
    mm_quat q = {c.x, c.y, c.z, 1.0f + d};
    return q_normalize(q);
}

void mm_transform_matrix(mm_vec3 p, mm_quat q, float out[16]) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    out[0] = 1.0f - 2.0f * (y * y + z * z);
    out[1] = 2.0f * (x * y + w * z);
    out[2] = 2.0f * (x * z - w * y);
    out[3] = 0.0f;
    out[4] = 2.0f * (x * y - w * z);
    out[5] = 1.0f - 2.0f * (x * x + z * z);
    out[6] = 2.0f * (y * z + w * x);
    out[7] = 0.0f;
    out[8] = 2.0f * (x * z + w * y);
    out[9] = 2.0f * (y * z - w * x);
    out[10] = 1.0f - 2.0f * (x * x + y * y);
    out[11] = 0.0f;
    out[12] = p.x;
    out[13] = p.y;
    out[14] = p.z;
    out[15] = 1.0f;
}

/* ---------------------------------------------------------------- springs */

static float halflife_to_damping(float halflife) { return (4.0f * MM_LN2) / (halflife + 1e-5f); }

/* Critically damped decay of x toward zero with velocity v. */
static void decay_spring(float *x, float *v, float halflife, float dt) {
    float y = halflife_to_damping(halflife) / 2.0f;
    float j1 = *v + *x * y;
    float eydt = expf(-y * dt);
    *x = eydt * (*x + j1 * dt);
    *v = eydt * (*v - j1 * y * dt);
}

static void decay_spring_vec3(mm_vec3 *x, mm_vec3 *v, float halflife, float dt) {
    decay_spring(&x->x, &v->x, halflife, dt);
    decay_spring(&x->y, &v->y, halflife, dt);
    decay_spring(&x->z, &v->z, halflife, dt);
}

/* ------------------------------------------------------------------ loading */

typedef struct {
    const uint8_t *data;
    size_t size, pos;
    int error;
} reader;

static const void *take(reader *r, size_t n) {
    if (r->error || r->pos + n > r->size) {
        r->error = 1;
        return NULL;
    }
    const void *p = r->data + r->pos;
    r->pos += n;
    return p;
}

static uint32_t take_u32(reader *r) {
    uint32_t v = 0;
    const void *p = take(r, 4);
    if (p) memcpy(&v, p, 4);
    return v;
}

static int32_t take_i32(reader *r) { return (int32_t)take_u32(r); }

static float take_f32(reader *r) {
    float v = 0.0f;
    const void *p = take(r, 4);
    if (p) memcpy(&v, p, 4);
    return v;
}

static void take_string(reader *r, char out[MM_NAME_MAX]) {
    uint32_t n = take_u32(r);
    const char *s = take(r, (n + 3u) & ~3u);
    memset(out, 0, MM_NAME_MAX);
    if (s) memcpy(out, s, n < MM_NAME_MAX - 1 ? n : MM_NAME_MAX - 1);
}

/* Returns a pointer into the file buffer for n floats (the buffer is 4-byte aligned). */
static float *take_floats(reader *r, size_t n) { return (float *)take(r, n * sizeof(float)); }

int mm_db_load(mm_db *db, const char *path) {
    memset(db, 0, sizeof(*db));
    uint32_t probe = 1;
    if (*(uint8_t *)&probe != 1) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -2;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 16) {
        fclose(f);
        return -3;
    }
    void *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return -4;
    }
    fclose(f);
    reader r = {data, (size_t)size, 0, 0};
    db->file_data = data;
    if (memcmp(take(&r, 4), "MMDB", 4) != 0 || take_u32(&r) != 1) {
        mm_db_free(db);
        return -5;
    }
    db->fps = take_f32(&r);
    db->joint_count = (int)take_u32(&r);
    db->frame_count = (int)take_u32(&r);
    db->clip_count = (int)take_u32(&r);
    db->tag_count = (int)take_u32(&r);
    db->feature_count = (int)take_u32(&r);
    db->block_count = (int)take_u32(&r);
    db->trajectory_count = (int)take_u32(&r);
    db->small_box_size = (int)take_u32(&r);
    db->large_box_size = (int)take_u32(&r);
    take_u32(&r);
    db->hips = take_i32(&r);
    for (int i = 0; i < MM_ROLE_COUNT; i++) db->roles[i] = take_i32(&r);
    int J = db->joint_count, N = db->frame_count, F = db->feature_count;
    if (J <= 0 || N <= 0 || F <= 0 || db->trajectory_count > MM_MAX_TRAJECTORY || db->tag_count > 32 ||
        db->small_box_size <= 0 || db->large_box_size % db->small_box_size != 0) {
        mm_db_free(db);
        return -6;
    }
    db->small_box_count = (N + db->small_box_size - 1) / db->small_box_size;
    db->large_box_count = (N + db->large_box_size - 1) / db->large_box_size;

    db->joint_names = calloc((size_t)J, MM_NAME_MAX);
    db->parents = calloc((size_t)J, sizeof(int));
    db->rest_pos = calloc((size_t)J, sizeof(mm_vec3));
    db->rest_rot = calloc((size_t)J, sizeof(mm_quat));
    db->inverse_bind = calloc((size_t)J * 16, sizeof(float));
    db->tag_names = calloc((size_t)db->tag_count + 1, MM_NAME_MAX);
    db->clips = calloc((size_t)db->clip_count + 1, sizeof(mm_clip));
    db->blocks = calloc((size_t)db->block_count + 1, sizeof(mm_block));
    db->frame_clip = calloc((size_t)N, sizeof(int));
    if (!db->joint_names || !db->parents || !db->rest_pos || !db->rest_rot || !db->inverse_bind ||
        !db->tag_names || !db->clips || !db->blocks || !db->frame_clip) {
        mm_db_free(db);
        return -7;
    }
    for (int j = 0; j < J; j++) {
        take_string(&r, db->joint_names[j]);
        db->parents[j] = take_i32(&r);
        db->rest_pos[j].x = take_f32(&r);
        db->rest_pos[j].y = take_f32(&r);
        db->rest_pos[j].z = take_f32(&r);
        db->rest_rot[j].x = take_f32(&r);
        db->rest_rot[j].y = take_f32(&r);
        db->rest_rot[j].z = take_f32(&r);
        db->rest_rot[j].w = take_f32(&r);
        for (int k = 0; k < 16; k++) db->inverse_bind[j * 16 + k] = take_f32(&r);
        if (db->parents[j] >= j) r.error = 1;
    }
    for (int t = 0; t < db->tag_count; t++) take_string(&r, db->tag_names[t]);
    for (int c = 0; c < db->clip_count; c++) {
        take_string(&r, db->clips[c].name);
        db->clips[c].start = (int)take_u32(&r);
        db->clips[c].length = (int)take_u32(&r);
        db->clips[c].tags = take_u32(&r);
        if (db->clips[c].start < 0 || db->clips[c].length <= 0 || db->clips[c].start + db->clips[c].length > N) {
            r.error = 1;
            break;
        }
        for (int i = 0; i < db->clips[c].length; i++) db->frame_clip[db->clips[c].start + i] = c;
    }
    for (int k = 0; k < db->trajectory_count; k++) db->trajectory_frames[k] = take_i32(&r);
    for (int b = 0; b < db->block_count; b++) {
        take_string(&r, db->blocks[b].name);
        db->blocks[b].type = (int)take_u32(&r);
        db->blocks[b].joint = take_i32(&r);
        db->blocks[b].offset = (int)take_u32(&r);
        db->blocks[b].size = (int)take_u32(&r);
        db->blocks[b].weight = take_f32(&r);
        if (db->blocks[b].offset + db->blocks[b].size > F) r.error = 1;
    }
    db->feature_mean = take_floats(&r, (size_t)F);
    db->feature_scale = take_floats(&r, (size_t)F);
    db->rotations = (mm_quat *)take_floats(&r, (size_t)N * J * 4);
    db->hips_pos = (mm_vec3 *)take_floats(&r, (size_t)N * 3);
    db->root_pos = (mm_vec3 *)take_floats(&r, (size_t)N * 3);
    db->root_yaw = take_floats(&r, (size_t)N);
    db->flags = (uint8_t *)take(&r, ((size_t)N + 3u) & ~(size_t)3u);
    db->features = take_floats(&r, (size_t)N * F);
    db->small_min = take_floats(&r, (size_t)db->small_box_count * F);
    db->small_max = take_floats(&r, (size_t)db->small_box_count * F);
    db->large_min = take_floats(&r, (size_t)db->large_box_count * F);
    db->large_max = take_floats(&r, (size_t)db->large_box_count * F);
    const void *tail = take(&r, 4);
    if (r.error || memcmp(tail, "MMDB", 4) != 0 || r.pos != r.size) {
        mm_db_free(db);
        return -8;
    }

    /* Per-box tag unions and intersections and searchability, for skipping
     * whole boxes under a tag filter. */
    db->small_tags_any = calloc((size_t)db->small_box_count, sizeof(uint32_t));
    db->small_tags_all = calloc((size_t)db->small_box_count, sizeof(uint32_t));
    db->large_tags_any = calloc((size_t)db->large_box_count, sizeof(uint32_t));
    db->large_tags_all = calloc((size_t)db->large_box_count, sizeof(uint32_t));
    db->small_searchable = calloc((size_t)db->small_box_count, 1);
    db->large_searchable = calloc((size_t)db->large_box_count, 1);
    if (!db->small_tags_any || !db->small_tags_all || !db->large_tags_any || !db->large_tags_all ||
        !db->small_searchable || !db->large_searchable) {
        mm_db_free(db);
        return -7;
    }
    for (int b = 0; b < db->small_box_count; b++) db->small_tags_all[b] = 0xffffffffu;
    for (int b = 0; b < db->large_box_count; b++) db->large_tags_all[b] = 0xffffffffu;
    for (int i = 0; i < N; i++) {
        if (db->flags[i] & MM_FLAG_NOT_SEARCHABLE) continue;
        uint32_t tags = db->clips[db->frame_clip[i]].tags;
        int s = i / db->small_box_size, l = i / db->large_box_size;
        db->small_tags_any[s] |= tags;
        db->small_tags_all[s] &= tags;
        db->large_tags_any[l] |= tags;
        db->large_tags_all[l] &= tags;
        db->small_searchable[s] = 1;
        db->large_searchable[l] = 1;
    }
    return 0;
}

void mm_db_free(mm_db *db) {
    free(db->joint_names);
    free(db->parents);
    free(db->rest_pos);
    free(db->rest_rot);
    free(db->inverse_bind);
    free(db->tag_names);
    free(db->clips);
    free(db->blocks);
    free(db->frame_clip);
    free(db->small_tags_any);
    free(db->small_tags_all);
    free(db->large_tags_any);
    free(db->large_tags_all);
    free(db->small_searchable);
    free(db->large_searchable);
    free(db->file_data);
    memset(db, 0, sizeof(*db));
}

int mm_db_find_joint(const mm_db *db, const char *name) {
    for (int j = 0; j < db->joint_count; j++)
        if (strcmp(db->joint_names[j], name) == 0) return j;
    return -1;
}

uint32_t mm_db_tag_bit(const mm_db *db, const char *name) {
    for (int t = 0; t < db->tag_count; t++)
        if (strcmp(db->tag_names[t], name) == 0) return 1u << t;
    return 0;
}

/* ------------------------------------------------------------------- poses */

void mm_forward_kinematics(const mm_db *db, const mm_quat *local_rot, mm_vec3 hips_pos, mm_vec3 root_pos,
                           mm_quat root_rot, mm_vec3 *world_pos, mm_quat *world_rot) {
    for (int j = 0; j < db->joint_count; j++) {
        int p = db->parents[j];
        mm_vec3 t = j == db->hips ? hips_pos : db->rest_pos[j];
        if (p < 0) {
            world_pos[j] = v_add(mm_quat_rotate(root_rot, t), root_pos);
            world_rot[j] = mm_quat_mul(root_rot, local_rot[j]);
        } else {
            world_pos[j] = v_add(mm_quat_rotate(world_rot[p], t), world_pos[p]);
            world_rot[j] = mm_quat_mul(world_rot[p], local_rot[j]);
        }
    }
}

static int clip_last_frame(const mm_db *db, int frame) {
    const mm_clip *c = &db->clips[db->frame_clip[frame]];
    return c->start + c->length - 1;
}

void mm_sample_pose(const mm_db *db, int frame, float alpha, mm_quat *local_rot, mm_vec3 *hips_pos) {
    int J = db->joint_count;
    int next = frame < clip_last_frame(db, frame) ? frame + 1 : frame;
    const mm_quat *a = db->rotations + (size_t)frame * J;
    const mm_quat *b = db->rotations + (size_t)next * J;
    for (int j = 0; j < J; j++) local_rot[j] = alpha > 0.0f ? mm_quat_nlerp_shortest(a[j], b[j], alpha) : a[j];
    mm_vec3 h0 = db->hips_pos[frame], h1 = db->hips_pos[next];
    *hips_pos = v_add(h0, v_scale(v_sub(h1, h0), alpha));
}

/* Angular velocity of a joint's local rotation at a frame (per second). */
static mm_vec3 joint_angular_velocity(const mm_db *db, int frame, int joint) {
    int J = db->joint_count;
    int a = frame, b = frame + 1;
    if (b > clip_last_frame(db, frame)) {
        b = frame;
        a = frame - 1;
        if (a < db->clips[db->frame_clip[frame]].start) return mm_vec3_make(0.0f, 0.0f, 0.0f);
    }
    mm_quat d = mm_quat_mul(db->rotations[(size_t)b * J + joint], mm_quat_inv(db->rotations[(size_t)a * J + joint]));
    return v_scale(q_to_scaled_angle_axis(d), db->fps);
}

static mm_vec3 hips_velocity(const mm_db *db, int frame) {
    int a = frame, b = frame + 1;
    if (b > clip_last_frame(db, frame)) {
        b = frame;
        a = frame - 1;
        if (a < db->clips[db->frame_clip[frame]].start) return mm_vec3_make(0.0f, 0.0f, 0.0f);
    }
    return v_scale(v_sub(db->hips_pos[b], db->hips_pos[a]), db->fps);
}

/* -------------------------------------------------------------- trajectory */

void mm_predict_trajectory(const mm_db *db, const mm_spring_state *s, mm_vec3 desired_velocity, float desired_yaw,
                           float velocity_halflife, float rotation_halflife, mm_vec3 *out_pos, float *out_yaw) {
    float yv = halflife_to_damping(velocity_halflife) / 2.0f;
    float yr = halflife_to_damping(rotation_halflife) / 2.0f;
    mm_vec3 j0 = v_sub(s->velocity, desired_velocity);
    mm_vec3 j1 = v_add(s->acceleration, v_scale(j0, yv));
    float r0 = wrap_angle(s->yaw - desired_yaw);
    float r1 = s->yaw_rate + r0 * yr;
    for (int k = 0; k < db->trajectory_count; k++) {
        float t = (float)db->trajectory_frames[k] / db->fps;
        float e = expf(-yv * t);
        /* Integral of the velocity spring: x(t) - x(0). */
        mm_vec3 a = v_scale(j1, 1.0f / (yv * yv));
        mm_vec3 b = v_scale(v_add(j0, v_scale(j1, t)), 1.0f / yv);
        mm_vec3 dx = v_add(v_add(v_scale(v_sub(v_scale(a, -1.0f), b), e), a), v_scale(j0, 1.0f / yv));
        dx = v_add(dx, v_scale(desired_velocity, t));
        out_pos[k] = v_add(s->position, dx);
        out_pos[k].y = s->position.y;
        float er = expf(-yr * t);
        out_yaw[k] = wrap_angle(desired_yaw + er * (r0 + r1 * t));
    }
}

void mm_trajectory_features(const mm_db *db, mm_vec3 root_pos, float root_yaw, const mm_vec3 *pos, const float *yaw,
                            float *query) {
    mm_quat inv = mm_quat_from_yaw(-root_yaw);
    for (int b = 0; b < db->block_count; b++) {
        const mm_block *blk = &db->blocks[b];
        if (blk->type != MM_BLOCK_TRAJECTORY_POSITION && blk->type != MM_BLOCK_TRAJECTORY_DIRECTION) continue;
        for (int k = 0; k < db->trajectory_count && 2 * k + 1 < blk->size; k++) {
            float fx, fz;
            if (blk->type == MM_BLOCK_TRAJECTORY_POSITION) {
                mm_vec3 rel = mm_quat_rotate(inv, v_sub(pos[k], root_pos));
                fx = rel.x;
                fz = rel.z;
            } else {
                float d = yaw[k] - root_yaw;
                fx = sinf(d);
                fz = cosf(d);
            }
            int i = blk->offset + 2 * k;
            query[i] = (fx - db->feature_mean[i]) / db->feature_scale[i];
            query[i + 1] = (fz - db->feature_mean[i + 1]) / db->feature_scale[i + 1];
        }
    }
}

/* ------------------------------------------------------------------ search */

int mm_frame_allowed(const mm_db *db, int frame, mm_filter filter) {
    if (db->flags[frame] & MM_FLAG_NOT_SEARCHABLE) return 0;
    uint32_t tags = db->clips[db->frame_clip[frame]].tags;
    return (tags & filter.required) == filter.required && (tags & filter.forbidden) == 0;
}

float mm_frame_cost(const mm_db *db, const float *query, int frame) {
    const float *f = db->features + (size_t)frame * db->feature_count;
    float cost = 0.0f;
    for (int d = 0; d < db->feature_count; d++) {
        float diff = query[d] - f[d];
        cost += diff * diff;
    }
    return cost;
}

static float positive_part(float x) { return x > 0.0f ? x : 0.0f; }

/* Squared distance from the query to a box, in four interleaved partial sums
 * so the compiler can keep the loop branch free. Every term is at most the
 * matching term of any frame inside the box; only the order of the additions
 * differs from mm_frame_cost, which BOX_SLACK covers. */
static float box_bound(const float *query, const float *lo, const float *hi, int F) {
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    int d = 0;
    for (; d + 4 <= F; d += 4) {
        float e0 = positive_part(lo[d] - query[d]) + positive_part(query[d] - hi[d]);
        float e1 = positive_part(lo[d + 1] - query[d + 1]) + positive_part(query[d + 1] - hi[d + 1]);
        float e2 = positive_part(lo[d + 2] - query[d + 2]) + positive_part(query[d + 2] - hi[d + 2]);
        float e3 = positive_part(lo[d + 3] - query[d + 3]) + positive_part(query[d + 3] - hi[d + 3]);
        a0 += e0 * e0;
        a1 += e1 * e1;
        a2 += e2 * e2;
        a3 += e3 * e3;
    }
    for (; d < F; d++) {
        float e = positive_part(lo[d] - query[d]) + positive_part(query[d] - hi[d]);
        a0 += e * e;
    }
    return (a0 + a1) + (a2 + a3);
}

/* Relative rounding slack between box_bound and mm_frame_cost. Both sums have
 * at most a few dozen non-negative terms, so their relative rounding errors
 * are below 64 float epsilons; a box is skipped only when its bound exceeds
 * the best cost by more than that, so no frame cheaper than the best is ever
 * skipped and the search returns exactly what the brute-force scan returns. */
#define BOX_SLACK 1.00001f

/* The cost of a frame in the same four interleaved partial sums, used to
 * reject frames cheaply; survivors get the exact sequential cost. */
static float frame_bound(const float *query, const float *f, int F) {
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    int d = 0;
    for (; d + 4 <= F; d += 4) {
        float e0 = query[d] - f[d], e1 = query[d + 1] - f[d + 1];
        float e2 = query[d + 2] - f[d + 2], e3 = query[d + 3] - f[d + 3];
        a0 += e0 * e0;
        a1 += e1 * e1;
        a2 += e2 * e2;
        a3 += e3 * e3;
    }
    for (; d < F; d++) {
        float e = query[d] - f[d];
        a0 += e * e;
    }
    return (a0 + a1) + (a2 + a3);
}

static int box_tags_pass(uint32_t any, uint32_t all, mm_filter filter) {
    return (any & filter.required) == filter.required && (all & filter.forbidden) == 0;
}

int mm_search(const mm_db *db, const float *query, mm_filter filter, float best_cost, float *out_cost) {
    int F = db->feature_count, N = db->frame_count;
    int best = -1;
    int per_large = db->large_box_size / db->small_box_size;
    for (int l = 0; l < db->large_box_count; l++) {
        if (!db->large_searchable[l] || !box_tags_pass(db->large_tags_any[l], db->large_tags_all[l], filter)) continue;
        if (box_bound(query, db->large_min + (size_t)l * F, db->large_max + (size_t)l * F, F) > best_cost * BOX_SLACK)
            continue;
        int s_end = (l + 1) * per_large < db->small_box_count ? (l + 1) * per_large : db->small_box_count;
        for (int s = l * per_large; s < s_end; s++) {
            if (!db->small_searchable[s] || !box_tags_pass(db->small_tags_any[s], db->small_tags_all[s], filter))
                continue;
            if (box_bound(query, db->small_min + (size_t)s * F, db->small_max + (size_t)s * F, F) >
                best_cost * BOX_SLACK)
                continue;
            int i_end = (s + 1) * db->small_box_size < N ? (s + 1) * db->small_box_size : N;
            for (int i = s * db->small_box_size; i < i_end; i++) {
                if (!mm_frame_allowed(db, i, filter)) continue;
                const float *f = db->features + (size_t)i * F;
                if (frame_bound(query, f, F) > best_cost * BOX_SLACK) continue;
                float cost = 0.0f;
                for (int d = 0; d < F; d++) {
                    float diff = query[d] - f[d];
                    cost += diff * diff;
                    if (cost >= best_cost) break;
                }
                if (cost < best_cost) {
                    best_cost = cost;
                    best = i;
                }
            }
        }
    }
    if (out_cost) *out_cost = best_cost;
    return best;
}

int mm_search_brute_force(const mm_db *db, const float *query, mm_filter filter, float best_cost, float *out_cost) {
    int best = -1;
    for (int i = 0; i < db->frame_count; i++) {
        if (!mm_frame_allowed(db, i, filter)) continue;
        float cost = mm_frame_cost(db, query, i);
        if (cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    if (out_cost) *out_cost = best_cost;
    return best;
}

/* ---------------------------------------------------------------------- IK */

void mm_two_bone_ik(mm_vec3 a, mm_vec3 b, mm_vec3 c, mm_vec3 target, mm_vec3 pole, mm_quat a_world,
                    mm_quat b_world, mm_quat a_local, float max_extension, mm_quat *out_a_local,
                    mm_quat *out_b_local) {
    float lab = v_length(v_sub(b, a));
    float lcb = v_length(v_sub(b, c));
    float lat = clampf(v_length(v_sub(target, a)), 1e-4f, (lab + lcb) * max_extension);
    mm_vec3 ac = v_normalize(v_sub(c, a));
    mm_vec3 ab = v_normalize(v_sub(b, a));
    mm_vec3 bc = v_normalize(v_sub(c, b));
    float ac_ab_0 = acosf(clampf(v_dot(ac, ab), -1.0f, 1.0f));
    float ba_bc_0 = acosf(clampf(v_dot(v_scale(ab, -1.0f), bc), -1.0f, 1.0f));
    float ac_ab_1 = acosf(clampf((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
    float ba_bc_1 = acosf(clampf((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));
    /* Bend axis: the normal of the chain plane, or of the pole plane when the
     * chain is straight. */
    mm_vec3 axis = v_cross(ac, ab);
    if (v_length(axis) < 1e-4f) axis = v_cross(ac, pole);
    axis = v_normalize(axis);
    /* World-space bend at a and at b, then a swing at a onto the target. */
    mm_quat r0 = q_from_angle_axis(ac_ab_1 - ac_ab_0, axis);
    mm_quat r1 = q_from_angle_axis(ba_bc_1 - ba_bc_0, axis);
    mm_vec3 b1 = v_add(a, mm_quat_rotate(r0, v_sub(b, a)));
    mm_vec3 c1 = v_add(b1, mm_quat_rotate(mm_quat_mul(r0, r1), v_sub(c, b)));
    mm_quat r2 = q_between(v_normalize(v_sub(c1, a)), v_normalize(v_sub(target, a)));
    /* Express the world changes as local ones: a_world' = r2 r0 a_world and
     * b_world' = r2 r0 r1 b_world. */
    mm_quat a_change = mm_quat_mul(mm_quat_inv(a_world), mm_quat_mul(mm_quat_mul(r2, r0), a_world));
    *out_a_local = q_normalize(mm_quat_mul(a_local, a_change));
    mm_quat parent_a = mm_quat_mul(a_world, mm_quat_inv(a_local));
    mm_quat new_a_world = mm_quat_mul(parent_a, *out_a_local);
    mm_quat new_b_world = mm_quat_mul(mm_quat_mul(mm_quat_mul(r2, r0), r1), b_world);
    *out_b_local = q_normalize(mm_quat_mul(mm_quat_inv(new_a_world), new_b_world));
}

/* -------------------------------------------------------------- controller */

void mm_config_default(mm_config *c) {
    c->velocity_halflife = 0.27f;
    c->rotation_halflife = 0.27f;
    c->search_interval = 0.1f;
    c->transition_margin = 0.05f;
    c->force_search_velocity = 0.5f;
    c->force_search_yaw = 0.5f;
    c->inertialization_halflife = 0.1f;
    c->ignore_nearby_frames = 3;
    c->foot_lock = 1;
    c->foot_unlock_distance = 0.1f;
    c->foot_unlock_halflife = 0.1f;
    c->foot_max_extension = 0.999f;
    c->root_adjust_halflife = 0.2f;
    c->root_adjust_max = 0.0f;
}

int mm_controller_init(mm_controller *c, const mm_db *db, const mm_config *config, int start_frame,
                       const mm_input *input) {
    memset(c, 0, sizeof(*c));
    int J = db->joint_count;
    c->db = db;
    c->config = *config;
    c->offset_rot = calloc((size_t)J, sizeof(mm_quat));
    c->offset_ang_vel = calloc((size_t)J, sizeof(mm_vec3));
    c->anim_rot = calloc((size_t)J, sizeof(mm_quat));
    c->local_rot = calloc((size_t)J, sizeof(mm_quat));
    c->world_pos = calloc((size_t)J, sizeof(mm_vec3));
    c->world_rot = calloc((size_t)J, sizeof(mm_quat));
    c->query = calloc((size_t)db->feature_count, sizeof(float));
    if (!c->offset_rot || !c->offset_ang_vel || !c->anim_rot || !c->local_rot || !c->world_pos || !c->world_rot ||
        !c->query) {
        mm_controller_free(c);
        return -1;
    }
    for (int j = 0; j < J; j++) c->offset_rot[j].w = 1.0f;
    if (start_frame < 0 || start_frame >= db->frame_count) start_frame = 0;
    c->frame = start_frame;
    c->clip = db->frame_clip[start_frame];
    c->root_pos = input->position;
    c->root_yaw = input->yaw;
    c->prev_filter = input->filter;
    mm_sample_pose(db, c->frame, 0.0f, c->anim_rot, &c->anim_hips);
    memcpy(c->local_rot, c->anim_rot, (size_t)J * sizeof(mm_quat));
    c->hips_pos = c->anim_hips;
    mm_forward_kinematics(db, c->local_rot, c->hips_pos, c->root_pos, mm_quat_from_yaw(c->root_yaw), c->world_pos,
                          c->world_rot);
    return 0;
}

void mm_controller_free(mm_controller *c) {
    free(c->offset_rot);
    free(c->offset_ang_vel);
    free(c->anim_rot);
    free(c->local_rot);
    free(c->world_pos);
    free(c->world_rot);
    free(c->query);
    memset(c, 0, sizeof(*c));
}

void mm_controller_query(mm_controller *c, const mm_input *input) {
    const mm_db *db = c->db;
    const float *f = db->features + (size_t)c->frame * db->feature_count;
    memcpy(c->query, f, (size_t)db->feature_count * sizeof(float));
    mm_trajectory_features(db, input->position, input->yaw, c->trajectory_pos, c->trajectory_yaw, c->query);
}

/* Starts an inertialized transition from the playing pose to frame dst. */
static void transition(mm_controller *c, int dst) {
    const mm_db *db = c->db;
    int J = db->joint_count;
    const mm_quat *dst_rot = db->rotations + (size_t)dst * J;
    for (int j = 0; j < J; j++) {
        mm_vec3 src_av = joint_angular_velocity(db, c->frame, j);
        mm_vec3 dst_av = joint_angular_velocity(db, dst, j);
        /* The displayed rotation is offset * animation; keep it unchanged. */
        c->offset_rot[j] = q_abs(mm_quat_mul(mm_quat_mul(c->offset_rot[j], c->anim_rot[j]), mm_quat_inv(dst_rot[j])));
        c->offset_ang_vel[j] = v_sub(v_add(c->offset_ang_vel[j], src_av), dst_av);
    }
    c->offset_hips = v_sub(v_add(c->offset_hips, c->anim_hips), db->hips_pos[dst]);
    c->offset_hips_vel = v_sub(v_add(c->offset_hips_vel, hips_velocity(db, c->frame)), hips_velocity(db, dst));
    c->frame = dst;
    c->frame_alpha = 0.0f;
    c->clip = db->frame_clip[dst];
    c->transitions++;
}

static void search(mm_controller *c, const mm_input *input) {
    const mm_db *db = c->db;
    mm_controller_query(c, input);
    float limit = INFINITY;
    int current_ok = mm_frame_allowed(db, c->frame, input->filter) && c->frame < clip_last_frame(db, c->frame);
    if (current_ok) limit = mm_frame_cost(db, c->query, c->frame) - c->config.transition_margin;
    float cost = INFINITY;
    int best = mm_search(db, c->query, input->filter, limit, &cost);
    c->searches++;
    if (best < 0) return;
    int same_clip = db->frame_clip[best] == c->clip;
    int delta = best - c->frame;
    if (current_ok && same_clip && delta >= -c->config.ignore_nearby_frames && delta <= c->config.ignore_nearby_frames)
        return;
    c->last_cost = cost;
    transition(c, best);
}

static void foot_lock_update(mm_controller *c, float dt) {
    const mm_db *db = c->db;
    const mm_config *cfg = &c->config;
    mm_quat root_rot = mm_quat_from_yaw(c->root_yaw);
    mm_vec3 pole = mm_quat_rotate(root_rot, mm_vec3_make(0.0f, 0.0f, 1.0f));
    int changed = 0;
    for (int side = 0; side < 2; side++) {
        mm_foot_lock *fl = &c->feet[side];
        int upleg = db->roles[side ? MM_ROLE_RIGHT_UPLEG : MM_ROLE_LEFT_UPLEG];
        int leg = db->roles[side ? MM_ROLE_RIGHT_LEG : MM_ROLE_LEFT_LEG];
        int foot = db->roles[side ? MM_ROLE_RIGHT_FOOT : MM_ROLE_LEFT_FOOT];
        int contact = (db->flags[c->frame] & (side ? MM_FLAG_CONTACT_RIGHT : MM_FLAG_CONTACT_LEFT)) != 0;
        mm_vec3 p = c->world_pos[foot];
        if (contact && !fl->locked) {
            fl->locked = 1;
            fl->lock_pos = v_add(p, fl->offset);
        }
        if (fl->locked && (!contact || v_length(v_sub(fl->lock_pos, p)) > cfg->foot_unlock_distance)) fl->locked = 0;
        if (fl->locked) {
            mm_vec3 offset = v_sub(fl->lock_pos, p);
            fl->offset_vel = dt > 0.0f ? v_scale(v_sub(offset, fl->offset), 1.0f / dt) : fl->offset_vel;
            fl->offset = offset;
        } else {
            decay_spring_vec3(&fl->offset, &fl->offset_vel, cfg->foot_unlock_halflife, dt);
        }
        if (v_length(fl->offset) < 1e-6f && !fl->locked) continue;
        mm_vec3 target = v_add(p, fl->offset);
        mm_quat foot_world = c->world_rot[foot];
        mm_quat upleg_parent = mm_quat_mul(c->world_rot[upleg], mm_quat_inv(c->local_rot[upleg]));
        mm_quat a_local, b_local;
        mm_two_bone_ik(c->world_pos[upleg], c->world_pos[leg], p, target, pole, c->world_rot[upleg],
                       c->world_rot[leg], c->local_rot[upleg], cfg->foot_max_extension, &a_local,
                       &b_local);
        c->local_rot[upleg] = a_local;
        c->local_rot[leg] = b_local;
        /* Keep the foot's world orientation. */
        mm_quat leg_world = mm_quat_mul(mm_quat_mul(upleg_parent, a_local), b_local);
        c->local_rot[foot] = q_normalize(mm_quat_mul(mm_quat_inv(leg_world), foot_world));
        changed = 1;
    }
    if (changed)
        mm_forward_kinematics(db, c->local_rot, c->hips_pos, c->root_pos, root_rot, c->world_pos, c->world_rot);
}

void mm_controller_update(mm_controller *c, const mm_input *input, float dt) {
    const mm_db *db = c->db;
    const mm_config *cfg = &c->config;
    int J = db->joint_count;

    /* Capsule state for the trajectory springs. */
    mm_spring_state s;
    s.position = input->position;
    s.velocity = input->velocity;
    s.yaw = input->yaw;
    if (c->has_history && dt > 0.0f) {
        s.acceleration = v_scale(v_sub(input->velocity, c->prev_velocity), 1.0f / dt);
        s.yaw_rate = wrap_angle(input->yaw - c->prev_yaw) / dt;
    } else {
        s.acceleration = mm_vec3_make(0.0f, 0.0f, 0.0f);
        s.yaw_rate = 0.0f;
    }
    mm_predict_trajectory(db, &s, input->desired_velocity, input->desired_yaw, cfg->velocity_halflife,
                          cfg->rotation_halflife, c->trajectory_pos, c->trajectory_yaw);

    /* Advance playback one database frame per 1/fps seconds. */
    int last = clip_last_frame(db, c->frame);
    c->frame_alpha += dt * db->fps;
    while (c->frame_alpha >= 1.0f) {
        if (c->frame < last) {
            c->frame++;
            c->frame_alpha -= 1.0f;
        } else {
            c->frame_alpha = 0.0f;
        }
    }
    if (c->frame >= last) c->frame_alpha = 0.0f;
    mm_sample_pose(db, c->frame, c->frame_alpha, c->anim_rot, &c->anim_hips);

    /* Search on a timer, at the clip end, or when the input changes sharply. */
    int force = c->frame >= last - 1;
    if (c->has_history) {
        if (v_length(v_sub(input->desired_velocity, c->prev_desired_velocity)) > cfg->force_search_velocity) force = 1;
        if (fabsf(wrap_angle(input->desired_yaw - c->prev_desired_yaw)) > cfg->force_search_yaw) force = 1;
    }
    if ((input->filter.required != c->prev_filter.required || input->filter.forbidden != c->prev_filter.forbidden) &&
        !mm_frame_allowed(db, c->frame, input->filter))
        force = 1;
    c->search_timer -= dt;
    int transitioned = 0;
    if (force || c->search_timer <= 0.0f) {
        int before = c->transitions;
        search(c, input);
        c->search_timer = cfg->search_interval;
        transitioned = c->transitions != before;
        if (transitioned) mm_sample_pose(db, c->frame, c->frame_alpha, c->anim_rot, &c->anim_hips);
    }

    /* Inertialization: decay the offsets and apply them to the animation. On
     * the update of a transition the displayed pose is already the source pose
     * advanced by dt, so the offsets start decaying on the next update. */
    for (int j = 0; j < J; j++) {
        if (!transitioned) {
            mm_vec3 x = q_to_scaled_angle_axis(c->offset_rot[j]);
            decay_spring_vec3(&x, &c->offset_ang_vel[j], cfg->inertialization_halflife, dt);
            c->offset_rot[j] = q_from_scaled_angle_axis(x);
        }
        c->local_rot[j] = q_normalize(mm_quat_mul(c->offset_rot[j], c->anim_rot[j]));
    }
    if (!transitioned) decay_spring_vec3(&c->offset_hips, &c->offset_hips_vel, cfg->inertialization_halflife, dt);
    c->hips_pos = v_add(c->anim_hips, c->offset_hips);

    /* The rendered root is the capsule, optionally pulled toward the motion of
     * the animation root by a bounded offset. */
    if (cfg->root_adjust_max > 0.0f) {
        int next = c->frame < clip_last_frame(db, c->frame) ? c->frame + 1 : c->frame;
        int prev = next == c->frame && c->frame > db->clips[c->clip].start ? c->frame - 1 : c->frame;
        mm_vec3 d = v_sub(db->root_pos[next], db->root_pos[prev]);
        mm_vec3 local = mm_quat_rotate(mm_quat_from_yaw(-db->root_yaw[prev]), d);
        mm_vec3 anim_vel = v_scale(mm_quat_rotate(mm_quat_from_yaw(input->yaw), local), db->fps);
        c->root_offset = v_add(c->root_offset, v_scale(v_sub(anim_vel, input->velocity), dt));
        c->root_offset.y = 0.0f;
        c->root_offset = v_scale(c->root_offset, expf(-MM_LN2 * dt / (cfg->root_adjust_halflife + 1e-5f)));
        float len = v_length(c->root_offset);
        if (len > cfg->root_adjust_max) c->root_offset = v_scale(c->root_offset, cfg->root_adjust_max / len);
    } else {
        c->root_offset = mm_vec3_make(0.0f, 0.0f, 0.0f);
    }
    c->root_pos = v_add(input->position, c->root_offset);
    c->root_yaw = input->yaw;
    mm_forward_kinematics(db, c->local_rot, c->hips_pos, c->root_pos, mm_quat_from_yaw(c->root_yaw), c->world_pos,
                          c->world_rot);
    if (cfg->foot_lock) foot_lock_update(c, dt);

    c->has_history = 1;
    c->prev_velocity = input->velocity;
    c->prev_yaw = input->yaw;
    c->prev_desired_velocity = input->desired_velocity;
    c->prev_desired_yaw = input->desired_yaw;
    c->prev_filter = input->filter;
}
