/*
 * mm_demo: stick-figure motion-matching demo (raylib 5.5).
 *
 *   mm_demo DATABASE.mmdb                      interactive window
 *   mm_demo DATABASE.mmdb --headless N [CSV]   scripted path for N updates at 60 Hz,
 *                                              joint positions written to CSV
 *                                              (default mm_demo.csv), no window
 *   mm_demo DATABASE.mmdb --record N PNG       scripted path in the window, then a
 *                                              screenshot after N updates
 *
 * Controls: WASD moves relative to the camera, the mouse turns the camera,
 * shift runs, C toggles the crouch stance tag, holding the right mouse button
 * strafes (the body keeps facing the camera direction). The window shows the
 * capsule, the predicted trajectory and the matched clip.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "../mm.h"

#define DT (1.0f / 60.0f)

typedef struct {
    mm_vec3 pos, vel;
    float yaw;
} capsule;

static float wrap(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

/* The game side: the capsule approaches the desired velocity and facing. */
static void capsule_step(capsule *c, mm_vec3 desired_vel, float desired_yaw) {
    float k = 1.0f - expf(-DT * 6.0f);
    c->vel.x += (desired_vel.x - c->vel.x) * k;
    c->vel.z += (desired_vel.z - c->vel.z) * k;
    c->pos.x += c->vel.x * DT;
    c->pos.z += c->vel.z * DT;
    c->yaw = wrap(c->yaw + wrap(desired_yaw - c->yaw) * (1.0f - expf(-DT * 8.0f)));
}

/* The scripted path of --headless: walk, stop, a 90 degree turn, strafe, run, crouch. */
static void script(float t, mm_vec3 *vel, float *yaw, int *crouch) {
    *crouch = 0;
    if (t < 3.0f) {
        *vel = mm_vec3_make(0.0f, 0.0f, 1.4f);
        *yaw = 0.0f;
    } else if (t < 5.0f) {
        *vel = mm_vec3_make(0.0f, 0.0f, 0.0f);
        *yaw = 0.0f;
    } else if (t < 8.0f) {
        *vel = mm_vec3_make(1.4f, 0.0f, 0.0f);
        *yaw = PI / 2.0f;
    } else if (t < 11.0f) {
        *vel = mm_vec3_make(0.0f, 0.0f, 1.1f);
        *yaw = PI / 2.0f;
    } else if (t < 14.0f) {
        *vel = mm_vec3_make(3.5f, 0.0f, 0.0f);
        *yaw = PI / 2.0f;
    } else {
        *vel = mm_vec3_make(0.7f, 0.0f, 0.0f);
        *yaw = PI / 2.0f;
        *crouch = 1;
    }
}

static mm_filter stance_filter(const mm_db *db, int crouch) {
    mm_filter f = {0, 0};
    uint32_t c = mm_db_tag_bit(db, "crouch"), s = mm_db_tag_bit(db, "stand");
    if (crouch && c) f.required = c;
    if (!crouch && s) f.required = s;
    return f;
}

static int run_headless(const mm_db *db, int steps, const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        printf("cannot write %s\n", csv_path);
        return 1;
    }
    fprintf(csv, "frame,time,clip");
    for (int j = 0; j < db->joint_count; j++)
        fprintf(csv, ",%s_x,%s_y,%s_z", db->joint_names[j], db->joint_names[j], db->joint_names[j]);
    fprintf(csv, "\n");
    mm_config cfg;
    mm_config_default(&cfg);
    mm_controller c;
    mm_input in;
    memset(&in, 0, sizeof(in));
    in.filter = stance_filter(db, 0);
    if (mm_controller_init(&c, db, &cfg, 0, &in)) {
        fclose(csv);
        return 1;
    }
    capsule cap = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f};
    for (int s = 0; s < steps; s++) {
        float t = (float)s * DT;
        int crouch;
        script(t, &in.desired_velocity, &in.desired_yaw, &crouch);
        in.filter = stance_filter(db, crouch);
        capsule_step(&cap, in.desired_velocity, in.desired_yaw);
        in.position = cap.pos;
        in.velocity = cap.vel;
        in.yaw = cap.yaw;
        mm_controller_update(&c, &in, DT);
        fprintf(csv, "%d,%.4f,%s", s, t, db->clips[c.clip].name);
        for (int j = 0; j < db->joint_count; j++)
            fprintf(csv, ",%.5f,%.5f,%.5f", c.world_pos[j].x, c.world_pos[j].y, c.world_pos[j].z);
        fprintf(csv, "\n");
    }
    printf("headless: %d updates, %d transitions, wrote %s\n", steps, c.transitions, csv_path);
    mm_controller_free(&c);
    fclose(csv);
    return 0;
}

static Vector3 V(mm_vec3 v) { return (Vector3){v.x, v.y, v.z}; }

static int run_window(const mm_db *db, int record_steps, const char *record_path) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "motion matching demo");
    SetTargetFPS(60);
    DisableCursor();
    mm_config cfg;
    mm_config_default(&cfg);
    mm_controller c;
    mm_input in;
    memset(&in, 0, sizeof(in));
    int crouch = 0;
    in.filter = stance_filter(db, crouch);
    if (mm_controller_init(&c, db, &cfg, 0, &in)) {
        CloseWindow();
        return 1;
    }
    capsule cap = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f};
    float cam_yaw = 0.0f, cam_pitch = 0.35f;
    Camera3D camera = {0};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 50.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    for (int step = 0; !WindowShouldClose(); step++) {
        Vector2 md = record_steps > 0 ? (Vector2){0.0f, 0.0f} : GetMouseDelta();
        cam_yaw -= md.x * 0.004f;
        cam_pitch = fminf(fmaxf(cam_pitch + md.y * 0.003f, -0.2f), 1.2f);
        if (IsKeyPressed(KEY_C)) crouch = !crouch;
        int strafe = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        float speed = crouch ? 0.7f : (IsKeyDown(KEY_LEFT_SHIFT) ? 3.5f : 1.4f);
        mm_vec3 fwd = {sinf(cam_yaw), 0.0f, cosf(cam_yaw)};
        mm_vec3 right = {-cosf(cam_yaw), 0.0f, sinf(cam_yaw)};
        float f = (float)(IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
        float r = (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
        mm_vec3 move = {fwd.x * f + right.x * r, 0.0f, fwd.z * f + right.z * r};
        float len = sqrtf(move.x * move.x + move.z * move.z);
        if (len > 0.0f) move = mm_vec3_make(move.x / len * speed, 0.0f, move.z / len * speed);
        in.desired_velocity = move;
        if (strafe)
            in.desired_yaw = cam_yaw;
        else if (len > 0.0f)
            in.desired_yaw = atan2f(move.x, move.z);
        if (record_steps > 0) {
            script((float)step * DT, &in.desired_velocity, &in.desired_yaw, &crouch);
            cam_yaw = cap.yaw + 0.6f;
        }
        in.filter = stance_filter(db, crouch);
        capsule_step(&cap, in.desired_velocity, in.desired_yaw);
        in.position = cap.pos;
        in.velocity = cap.vel;
        in.yaw = cap.yaw;
        mm_controller_update(&c, &in, DT);

        Vector3 focus = {cap.pos.x, 1.0f, cap.pos.z};
        camera.target = focus;
        camera.position = (Vector3){focus.x - 4.5f * sinf(cam_yaw) * cosf(cam_pitch), focus.y + 4.5f * sinf(cam_pitch),
                                    focus.z - 4.5f * cosf(cam_yaw) * cosf(cam_pitch)};
        BeginDrawing();
        ClearBackground((Color){30, 32, 36, 255});
        BeginMode3D(camera);
        DrawGrid(40, 1.0f);
        for (int j = 0; j < db->joint_count; j++) {
            int p = db->parents[j];
            if (p >= 0) DrawLine3D(V(c.world_pos[p]), V(c.world_pos[j]), RAYWHITE);
            DrawSphere(V(c.world_pos[j]), 0.012f, ORANGE);
        }
        for (int side = 0; side < 2; side++)
            if (c.feet[side].locked) DrawSphere(V(c.feet[side].lock_pos), 0.04f, RED);
        DrawCylinderWires((Vector3){cap.pos.x, 0.0f, cap.pos.z}, 0.3f, 0.3f, crouch ? 1.2f : 1.8f, 12,
                          (Color){80, 160, 255, 120});
        Vector3 prev = {cap.pos.x, 0.02f, cap.pos.z};
        for (int k = 0; k < db->trajectory_count; k++) {
            Vector3 p = {c.trajectory_pos[k].x, 0.02f, c.trajectory_pos[k].z};
            Vector3 d = {p.x + 0.3f * sinf(c.trajectory_yaw[k]), 0.02f, p.z + 0.3f * cosf(c.trajectory_yaw[k])};
            DrawLine3D(prev, p, GREEN);
            DrawLine3D(p, d, LIME);
            DrawSphere(p, 0.04f, GREEN);
            prev = p;
        }
        EndMode3D();
        DrawText(TextFormat("clip %s  frame %d  cost %.3f  transitions %d", db->clips[c.clip].name,
                            c.frame - db->clips[c.clip].start, c.last_cost, c.transitions),
                 12, 12, 20, RAYWHITE);
        DrawText(TextFormat("%s%s  WASD move, shift run, C crouch, right mouse strafe", crouch ? "crouch" : "stand",
                            strafe ? " strafe" : ""),
                 12, 38, 20, LIGHTGRAY);
        EndDrawing();
        if (record_steps > 0 && step + 1 >= record_steps) {
            TakeScreenshot(record_path);
            break;
        }
    }
    mm_controller_free(&c);
    CloseWindow();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: mm_demo DATABASE.mmdb [--headless N [out.csv]]\n");
        return 1;
    }
    mm_db db;
    int err = mm_db_load(&db, argv[1]);
    if (err) {
        printf("cannot load %s (%d)\n", argv[1], err);
        return 1;
    }
    int result;
    if (argc >= 4 && strcmp(argv[2], "--headless") == 0)
        result = run_headless(&db, atoi(argv[3]), argc >= 5 ? argv[4] : "mm_demo.csv");
    else if (argc >= 5 && strcmp(argv[2], "--record") == 0)
        result = run_window(&db, atoi(argv[3]), argv[4]);
    else
        result = run_window(&db, 0, NULL);
    mm_db_free(&db);
    return result;
}
