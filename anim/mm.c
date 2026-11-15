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
    if (r->error || n > r->size - r->pos) {
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
    const char *s = n <= 0x10000u ? take(r, (n + 3u) & ~3u) : NULL;
    if (!s) r->error = 1;
    memset(out, 0, MM_NAME_MAX);
    if (s) memcpy(out, s, n < MM_NAME_MAX - 1 ? n : MM_NAME_MAX - 1);
}

/* Pointers into the file buffer (4-byte aligned, as every section is). A
 * section of 1- or 2-byte values is zero padded to a multiple of 4 bytes. */
static void *take_padded(reader *r, size_t n) { return (void *)take(r, (n + 3u) & ~(size_t)3u); }
static float *take_floats(reader *r, size_t n) { return (float *)take(r, n * sizeof(float)); }

#define HEADER_SIZE 88
#define MAX_COUNT (1u << 28)

typedef struct {
    uint32_t version, J, N, C, T, F, B;
    int small, large;
} header_counts;

/* Reads the counts of a header and checks them against the file size, so the
 * derived tables can be sized before parsing. */
static int read_counts(const uint8_t *h, size_t file_size, header_counts *hc) {
    uint32_t v[12];
    if (file_size < HEADER_SIZE + 4 || memcmp(h, "MMDB", 4) != 0) return -5;
    memcpy(v, h + 4, sizeof(v));
    hc->version = v[0];
    hc->J = v[2];
    hc->N = v[3];
    hc->C = v[4];
    hc->T = v[5];
    hc->F = v[6];
    hc->B = v[7];
    hc->small = (int)v[9];
    hc->large = (int)v[10];
    if (hc->version != 1 && hc->version != 2) return -5;
    if (hc->J == 0 || hc->N == 0 || hc->F == 0 || hc->T > 32 || hc->J > MAX_COUNT || hc->N > MAX_COUNT ||
        hc->C > MAX_COUNT || hc->F > MAX_COUNT || hc->B > MAX_COUNT || v[8] > MM_MAX_TRAJECTORY ||
        hc->small <= 0 || hc->large <= 0 || hc->large % hc->small != 0)
        return -6;
    /* Every joint, clip, block and frame takes at least 4 bytes of the file. */
    if ((size_t)hc->J + hc->C + hc->B + hc->N > file_size / 4) return -6;
    return 0;
}

static size_t align16(size_t n) { return (n + 15u) & ~(size_t)15u; }

/* Bytes of the tables derived at load time (names, skeleton arrays, clips,
 * blocks, frame-to-clip map, per-box tag masks, joint storage), each 16-byte
 * aligned. */
static size_t derived_size(const header_counts *hc) {
    size_t J = hc->J, N = hc->N;
    size_t S = (N + (size_t)hc->small - 1) / (size_t)hc->small, L = (N + (size_t)hc->large - 1) / (size_t)hc->large;
    size_t n = 0;
    n += align16(J * MM_NAME_MAX) + align16(J * sizeof(int)) + align16(J * sizeof(mm_vec3)) +
         align16(J * sizeof(mm_quat)) + align16(J * 16 * sizeof(float));
    n += align16(((size_t)hc->T + 1) * MM_NAME_MAX) + align16(((size_t)hc->C + 1) * sizeof(mm_clip)) +
         align16(((size_t)hc->B + 1) * sizeof(mm_block));
    n += align16(N * sizeof(int));
    n += 2 * align16(S * sizeof(uint32_t)) + 2 * align16(L * sizeof(uint32_t)) + align16(S) + align16(L);
    n += align16(J * sizeof(int)) + align16(J) + align16(J * sizeof(mm_quat));
    return n;
}

static void *carve(uint8_t **cursor, size_t n) {
    void *p = *cursor;
    memset(p, 0, align16(n));
    *cursor += align16(n);
    return p;
}

static mm_quat decode_quat(const uint16_t *v) {
    int i = (v[0] >> 15) | ((v[1] >> 15) << 1);
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = ((float)(v[k] & 0x7fffu) * (2.0f / 32767.0f) - 1.0f) * 0.70710678118654752f;
    float big = sqrtf(fmaxf(1.0f - (c[0] * c[0] + c[1] * c[1] + c[2] * c[2]), 0.0f));
    float q[4];
    int k = 0;
    for (int a = 0; a < 4; a++) q[a] = a == i ? big : c[k++];
    mm_quat out = {q[0], q[1], q[2], q[3]};
    return out;
}

/* Parses a file held at the start of block (size bytes, followed by the
 * derived area). Every pointer of db points into block. */
static int parse(mm_db *db, uint8_t *block, size_t size, const header_counts *hc) {
    reader r = {block, size, 0, 0};
    uint8_t *cursor = block + align16(size);
    take(&r, 8);
    db->version = (int)hc->version;
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
    if (db->hips < 0 || db->hips >= J) return -6;
    for (int i = 0; i < MM_ROLE_COUNT; i++)
        if (db->roles[i] < 0 || db->roles[i] >= J) return -6;
    db->small_box_count = (N + db->small_box_size - 1) / db->small_box_size;
    db->large_box_count = (N + db->large_box_size - 1) / db->large_box_size;
    size_t S = (size_t)db->small_box_count, L = (size_t)db->large_box_count;

    db->joint_names = carve(&cursor, (size_t)J * MM_NAME_MAX);
    db->parents = carve(&cursor, (size_t)J * sizeof(int));
    db->rest_pos = carve(&cursor, (size_t)J * sizeof(mm_vec3));
    db->rest_rot = carve(&cursor, (size_t)J * sizeof(mm_quat));
    db->inverse_bind = carve(&cursor, (size_t)J * 16 * sizeof(float));
    db->tag_names = carve(&cursor, ((size_t)db->tag_count + 1) * MM_NAME_MAX);
    db->clips = carve(&cursor, ((size_t)db->clip_count + 1) * sizeof(mm_clip));
    db->blocks = carve(&cursor, ((size_t)db->block_count + 1) * sizeof(mm_block));
    db->frame_clip = carve(&cursor, (size_t)N * sizeof(int));
    db->small_tags_any = carve(&cursor, S * sizeof(uint32_t));
    db->small_tags_all = carve(&cursor, S * sizeof(uint32_t));
    db->large_tags_any = carve(&cursor, L * sizeof(uint32_t));
    db->large_tags_all = carve(&cursor, L * sizeof(uint32_t));
    db->small_searchable = carve(&cursor, S);
    db->large_searchable = carve(&cursor, L);
    db->joint_track = carve(&cursor, (size_t)J * sizeof(int));
    db->joint_dropped = carve(&cursor, (size_t)J);
    db->joint_constant = carve(&cursor, (size_t)J * sizeof(mm_quat));

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
        if (db->parents[j] >= j || db->parents[j] < -1) r.error = 1;
    }
    for (int t = 0; t < db->tag_count; t++) take_string(&r, db->tag_names[t]);
    int expected_start = 0;
    for (int c = 0; c < db->clip_count && !r.error; c++) {
        take_string(&r, db->clips[c].name);
        db->clips[c].start = (int)take_u32(&r);
        db->clips[c].length = (int)take_u32(&r);
        db->clips[c].tags = take_u32(&r);
        if (db->clips[c].start != expected_start || db->clips[c].length <= 0 ||
            db->clips[c].length > N - db->clips[c].start) {
            r.error = 1;
            break;
        }
        expected_start += db->clips[c].length;
        for (int i = 0; i < db->clips[c].length; i++) db->frame_clip[db->clips[c].start + i] = c;
    }
    if (expected_start != N) r.error = 1;
    for (int k = 0; k < db->trajectory_count; k++) db->trajectory_frames[k] = take_i32(&r);
    for (int b = 0; b < db->block_count; b++) {
        take_string(&r, db->blocks[b].name);
        db->blocks[b].type = (int)take_u32(&r);
        db->blocks[b].joint = take_i32(&r);
        db->blocks[b].offset = (int)take_u32(&r);
        db->blocks[b].size = (int)take_u32(&r);
        db->blocks[b].weight = take_f32(&r);
        if (db->blocks[b].offset < 0 || db->blocks[b].size < 0 || db->blocks[b].offset > F - db->blocks[b].size ||
            db->blocks[b].joint >= J)
            r.error = 1;
    }
    db->feature_mean = take_floats(&r, (size_t)F);
    db->feature_scale = take_floats(&r, (size_t)F);
    if (db->version == 1) {
        db->rotations = (mm_quat *)take_floats(&r, (size_t)N * J * 4);
        db->track_count = J;
        for (int j = 0; j < J; j++) db->joint_track[j] = j;
    } else {
        db->feature_step = take_floats(&r, (size_t)F);
        db->track_count = (int)take_u32(&r);
        if (db->track_count < 0 || db->track_count > J) r.error = 1;
        for (int j = 0; j < J && !r.error; j++) {
            db->joint_track[j] = take_i32(&r);
            db->joint_dropped[j] = (uint8_t)(take_u32(&r) & 1u);
            db->joint_constant[j].x = take_f32(&r);
            db->joint_constant[j].y = take_f32(&r);
            db->joint_constant[j].z = take_f32(&r);
            db->joint_constant[j].w = take_f32(&r);
            if (db->joint_track[j] < -1 || db->joint_track[j] >= db->track_count) r.error = 1;
        }
        if (!r.error) db->rotations_q = take_padded(&r, (size_t)N * (size_t)db->track_count * 3 * sizeof(uint16_t));
    }
    db->hips_pos = (mm_vec3 *)take_floats(&r, (size_t)N * 3);
    db->root_pos = (mm_vec3 *)take_floats(&r, (size_t)N * 3);
    db->root_yaw = take_floats(&r, (size_t)N);
    db->flags = take_padded(&r, (size_t)N);
    if (db->version == 1) {
        db->features = take_floats(&r, (size_t)N * F);
        db->small_min = take_floats(&r, S * F);
        db->small_max = take_floats(&r, S * F);
        db->large_min = take_floats(&r, L * F);
        db->large_max = take_floats(&r, L * F);
    } else {
        db->frame_tags = (uint32_t *)take(&r, (size_t)N * sizeof(uint32_t));
        db->features_q = take_padded(&r, (size_t)N * F * sizeof(int16_t));
        db->small_min_q = take_padded(&r, S * F * sizeof(int16_t));
        db->small_max_q = take_padded(&r, S * F * sizeof(int16_t));
        db->large_min_q = take_padded(&r, L * F * sizeof(int16_t));
        db->large_max_q = take_padded(&r, L * F * sizeof(int16_t));
    }
    const void *tail = take(&r, 4);
    if (r.error || memcmp(tail, "MMDB", 4) != 0 || r.pos != r.size) return -8;

    /* Per-box unions and intersections of the tags of searchable frames (clip
     * tags OR frame tags), for skipping whole boxes under a tag filter. */
    for (size_t b = 0; b < S; b++) db->small_tags_all[b] = 0xffffffffu;
    for (size_t b = 0; b < L; b++) db->large_tags_all[b] = 0xffffffffu;
    for (int i = 0; i < N; i++) {
        if (db->flags[i] & MM_FLAG_NOT_SEARCHABLE) continue;
        uint32_t tags = mm_frame_tags(db, i);
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

static int load_block(mm_db *db, uint8_t *block, size_t size, size_t total, const header_counts *hc) {
    db->file_data = block;
    db->memory_bytes = total;
    int err = parse(db, block, size, hc);
    if (err) mm_db_free(db);
    return err;
}

int mm_db_load(mm_db *db, const char *path) {
    memset(db, 0, sizeof(*db));
    uint32_t probe = 1;
    if (*(uint8_t *)&probe != 1) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -2;
    uint8_t h[HEADER_SIZE];
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size < HEADER_SIZE + 4 || fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, HEADER_SIZE, f) != HEADER_SIZE) {
        fclose(f);
        return -3;
    }
    header_counts hc;
    int err = read_counts(h, (size_t)size, &hc);
    if (err) {
        fclose(f);
        return err;
    }
    /* One allocation: the file, then the derived tables. One read fills it. */
    size_t total = align16((size_t)size) + derived_size(&hc);
    uint8_t *block = malloc(total);
    if (!block || fseek(f, 0, SEEK_SET) != 0 || fread(block, 1, (size_t)size, f) != (size_t)size) {
        free(block);
        fclose(f);
        return -4;
    }
    fclose(f);
    return load_block(db, block, (size_t)size, total, &hc);
}

int mm_db_load_memory(mm_db *db, const void *data, size_t size) {
    memset(db, 0, sizeof(*db));
    uint32_t probe = 1;
    if (*(uint8_t *)&probe != 1) return -1;
    if (!data || size < HEADER_SIZE + 4) return -3;
    header_counts hc;
    int err = read_counts(data, size, &hc);
    if (err) return err;
    size_t total = align16(size) + derived_size(&hc);
    uint8_t *block = malloc(total);
    if (!block) return -4;
    memcpy(block, data, size);
    return load_block(db, block, size, total, &hc);
}

void mm_db_free(mm_db *db) {
    free(db->file_data);
    memset(db, 0, sizeof(*db));
}

mm_quat mm_db_rotation(const mm_db *db, int frame, int joint) {
    if (db->rotations) return db->rotations[(size_t)frame * db->joint_count + joint];
    int t = db->joint_track[joint];
    if (t < 0) return db->joint_constant[joint];
    return decode_quat(db->rotations_q + ((size_t)frame * db->track_count + t) * 3);
}

void mm_db_frame_features(const mm_db *db, int frame, float *out) {
    int F = db->feature_count;
    if (db->features) {
        memcpy(out, db->features + (size_t)frame * F, (size_t)F * sizeof(float));
        return;
    }
    const int16_t *k = db->features_q + (size_t)frame * F;
    for (int d = 0; d < F; d++) out[d] = (float)k[d] * db->feature_step[d];
}

uint32_t mm_frame_tags(const mm_db *db, int frame) {
    uint32_t tags = db->clips[db->frame_clip[frame]].tags;
    if (db->frame_tags) tags |= db->frame_tags[frame];
    return tags;
}

size_t mm_controller_memory(const mm_db *db) {
    size_t J = (size_t)db->joint_count;
    return J * (4 * sizeof(mm_quat) + 2 * sizeof(mm_vec3)) + (size_t)db->feature_count * sizeof(float);
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
    if (db->rotations) {
        const mm_quat *a = db->rotations + (size_t)frame * J;
        const mm_quat *b = db->rotations + (size_t)next * J;
        for (int j = 0; j < J; j++) local_rot[j] = alpha > 0.0f ? mm_quat_nlerp_shortest(a[j], b[j], alpha) : a[j];
    } else {
        for (int j = 0; j < J; j++) {
            mm_quat a = mm_db_rotation(db, frame, j);
            local_rot[j] = alpha > 0.0f && db->joint_track[j] >= 0
                               ? mm_quat_nlerp_shortest(a, mm_db_rotation(db, next, j), alpha)
                               : a;
        }
    }
    mm_vec3 h0 = db->hips_pos[frame], h1 = db->hips_pos[next];
    *hips_pos = v_add(h0, v_scale(v_sub(h1, h0), alpha));
}

/* Angular velocity of a joint's local rotation at a frame (per second). */
static mm_vec3 joint_angular_velocity(const mm_db *db, int frame, int joint) {
    int a = frame, b = frame + 1;
    if (db->joint_track[joint] < 0) return mm_vec3_make(0.0f, 0.0f, 0.0f);
    if (b > clip_last_frame(db, frame)) {
        b = frame;
        a = frame - 1;
        if (a < db->clips[db->frame_clip[frame]].start) return mm_vec3_make(0.0f, 0.0f, 0.0f);
    }
    mm_quat d = mm_quat_mul(mm_db_rotation(db, b, joint), mm_quat_inv(mm_db_rotation(db, a, joint)));
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
    uint32_t tags = mm_frame_tags(db, frame);
    return (tags & filter.required) == filter.required && (tags & filter.forbidden) == 0;
}

float mm_frame_cost(const mm_db *db, const float *query, int frame) {
    float cost = 0.0f;
    if (!db->features) {
        /* The stored value is k * step, exactly as mm_db_frame_features and
         * mm_search compute it. */
        const int16_t *k = db->features_q + (size_t)frame * db->feature_count;
        for (int d = 0; d < db->feature_count; d++) {
            float diff = query[d] - (float)k[d] * db->feature_step[d];
            cost += diff * diff;
        }
        return cost;
    }
    const float *f = db->features + (size_t)frame * db->feature_count;
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

/* The same bounds on quantized features: the stored value of feature d is
 * (float)k * step[d], an exact float, and the box limits are the minimum and
 * maximum k of the box, so the bound terms keep the property above. */
static float box_bound_q(const float *query, const int16_t *lo, const int16_t *hi, const float *step, int F) {
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    int d = 0;
    for (; d + 4 <= F; d += 4) {
        float e0 = positive_part((float)lo[d] * step[d] - query[d]) + positive_part(query[d] - (float)hi[d] * step[d]);
        float e1 = positive_part((float)lo[d + 1] * step[d + 1] - query[d + 1]) +
                   positive_part(query[d + 1] - (float)hi[d + 1] * step[d + 1]);
        float e2 = positive_part((float)lo[d + 2] * step[d + 2] - query[d + 2]) +
                   positive_part(query[d + 2] - (float)hi[d + 2] * step[d + 2]);
        float e3 = positive_part((float)lo[d + 3] * step[d + 3] - query[d + 3]) +
                   positive_part(query[d + 3] - (float)hi[d + 3] * step[d + 3]);
        a0 += e0 * e0;
        a1 += e1 * e1;
        a2 += e2 * e2;
        a3 += e3 * e3;
    }
    for (; d < F; d++) {
        float e = positive_part((float)lo[d] * step[d] - query[d]) + positive_part(query[d] - (float)hi[d] * step[d]);
        a0 += e * e;
    }
    return (a0 + a1) + (a2 + a3);
}

static float frame_bound_q(const float *query, const int16_t *k, const float *step, int F) {
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    int d = 0;
    for (; d + 4 <= F; d += 4) {
        float e0 = query[d] - (float)k[d] * step[d], e1 = query[d + 1] - (float)k[d + 1] * step[d + 1];
        float e2 = query[d + 2] - (float)k[d + 2] * step[d + 2], e3 = query[d + 3] - (float)k[d + 3] * step[d + 3];
        a0 += e0 * e0;
        a1 += e1 * e1;
        a2 += e2 * e2;
        a3 += e3 * e3;
    }
    for (; d < F; d++) {
        float e = query[d] - (float)k[d] * step[d];
        a0 += e * e;
    }
    return (a0 + a1) + (a2 + a3);
}

static int search_quantized(const mm_db *db, const float *query, mm_filter filter, float best_cost,
                            float *out_cost) {
    int F = db->feature_count, N = db->frame_count;
    const float *step = db->feature_step;
    int best = -1;
    int per_large = db->large_box_size / db->small_box_size;
    for (int l = 0; l < db->large_box_count; l++) {
        if (!db->large_searchable[l] || !box_tags_pass(db->large_tags_any[l], db->large_tags_all[l], filter)) continue;
        if (box_bound_q(query, db->large_min_q + (size_t)l * F, db->large_max_q + (size_t)l * F, step, F) >
            best_cost * BOX_SLACK)
            continue;
        int s_end = (l + 1) * per_large < db->small_box_count ? (l + 1) * per_large : db->small_box_count;
        for (int s = l * per_large; s < s_end; s++) {
            if (!db->small_searchable[s] || !box_tags_pass(db->small_tags_any[s], db->small_tags_all[s], filter))
                continue;
            if (box_bound_q(query, db->small_min_q + (size_t)s * F, db->small_max_q + (size_t)s * F, step, F) >
                best_cost * BOX_SLACK)
                continue;
            int i_end = (s + 1) * db->small_box_size < N ? (s + 1) * db->small_box_size : N;
            for (int i = s * db->small_box_size; i < i_end; i++) {
                if (!mm_frame_allowed(db, i, filter)) continue;
                const int16_t *k = db->features_q + (size_t)i * F;
                if (frame_bound_q(query, k, step, F) > best_cost * BOX_SLACK) continue;
                float cost = 0.0f;
                for (int d = 0; d < F; d++) {
                    float diff = query[d] - (float)k[d] * step[d];
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

int mm_search(const mm_db *db, const float *query, mm_filter filter, float best_cost, float *out_cost) {
    if (!db->features) return search_quantized(db, query, filter, best_cost, out_cost);
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
    mm_db_frame_features(db, c->frame, c->query);
    mm_trajectory_features(db, input->position, input->yaw, c->trajectory_pos, c->trajectory_yaw, c->query);
}

/* Starts an inertialized transition from the playing pose to frame dst. */
static void transition(mm_controller *c, int dst) {
    const mm_db *db = c->db;
    int J = db->joint_count;
    for (int j = 0; j < J; j++) {
        mm_quat dst_rot = mm_db_rotation(db, dst, j);
        mm_vec3 src_av = joint_angular_velocity(db, c->frame, j);
        mm_vec3 dst_av = joint_angular_velocity(db, dst, j);
        /* The displayed rotation is offset * animation; keep it unchanged. */
        c->offset_rot[j] = q_abs(mm_quat_mul(mm_quat_mul(c->offset_rot[j], c->anim_rot[j]), mm_quat_inv(dst_rot)));
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
