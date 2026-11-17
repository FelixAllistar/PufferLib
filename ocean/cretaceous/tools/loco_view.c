// Viewer for the locomotion env (loco/loco_env.h, docs/locomotion/README.md):
// the ragdolls as their collision capsules and boxes on the ground, the
// clip's pose for the followed animal as a wire ghost, and the reward terms.
// Run from the repository root.
//   loco_view [--dino PATH] [--clip walk] [--agents 4] [--seed 1] [--policy CHECKPOINT.bin]
//             [--passive-tail] [--size W H] [--record OUT.mp4 --seconds S]
// Without --policy the animals run the plain PD-tracking baseline (actions
// zero). --record renders S seconds without a window prompt and encodes them
// with ffmpeg (an X display is still needed: xvfb-run works).
// Keys: space pause, R reset, G ghost on/off, Tab follow the next animal,
// arrows orbit and zoom the camera.
#include "raylib.h"
#include "rlgl.h"
#include "loco/loco_env.h"
#include "loco/loco_policy.h"

static Vector3 V3(b3Vec3 v) { return (Vector3){v.x, v.y, v.z}; }

// One shape of a body at a world transform, solid or as a wire ghost.
static void draw_shape(const LocoShape* S, b3Vec3 p, b3Quat q, Color c, bool wire) {
    if (S->type == LS_CAPSULE) {
        Vector3 a = V3(b3Add(p, lq_rot(q, S->c1))), b = V3(b3Add(p, lq_rot(q, S->c2)));
        if (wire) DrawCapsuleWires(a, b, S->radius, 6, 3, c); else DrawCapsule(a, b, S->radius, 10, 5, c);
        return;
    }
    b3Quat r = lq_mul(q, S->box_q); b3Vec3 ctr = b3Add(p, lq_rot(q, S->box_c));
    float ang = 2.0f * acosf(fminf(fabsf(r.s), 1.0f)); b3Vec3 ax = r.s < 0 ? b3Neg(r.v) : r.v; float n = sqrtf(lv_len2(ax));
    rlPushMatrix();
    rlTranslatef(ctr.x, ctr.y, ctr.z);
    if (n > 1e-6f) rlRotatef(ang * RAD2DEG, ax.x / n, ax.y / n, ax.z / n);
    Vector3 o = {0, 0, 0};
    if (wire) DrawCubeWires(o, 2 * S->box_h.x, 2 * S->box_h.y, 2 * S->box_h.z, c);
    else { DrawCube(o, 2 * S->box_h.x, 2 * S->box_h.y, 2 * S->box_h.z, c); DrawCubeWires(o, 2 * S->box_h.x, 2 * S->box_h.y, 2 * S->box_h.z, Fade(BLACK, 0.4f)); }
    rlPopMatrix();
}
static Color body_color(const LocoBody* B, bool fallen) {
    Color c = loco_is_tail(B) ? (Color){214, 132, 64, 255} : loco_is_leg(B) ? (Color){112, 150, 92, 255} : (Color){176, 160, 128, 255};
    return fallen ? ColorBrightness(c, -0.35f) : c;
}

int main(int argc, char** argv) {
    LocoConfig c = loco_default_config(); c.num_agents = 4;
    unsigned int seed = 1; const char* policy = NULL; const char* record = NULL; float seconds = 10; int W = 1280, H = 720;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i]; bool more = i + 1 < argc;
        if (!strcmp(a, "--dino") && more) snprintf(c.dino, sizeof c.dino, "%s", argv[++i]);
        else if (!strcmp(a, "--clip") && more) snprintf(c.clip, sizeof c.clip, "%s", argv[++i]);
        else if (!strcmp(a, "--agents") && more) c.num_agents = atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && more) seed = (unsigned int)atoi(argv[++i]);
        else if (!strcmp(a, "--policy") && more) policy = argv[++i];
        else if (!strcmp(a, "--passive-tail")) c.gains.passive = true;
        else if (!strcmp(a, "--size") && i + 2 < argc) { W = atoi(argv[++i]); H = atoi(argv[++i]); }
        else if (!strcmp(a, "--record") && more) record = argv[++i];
        else if (!strcmp(a, "--seconds") && more) seconds = (float)atof(argv[++i]);
        else { fprintf(stderr, "loco_view: unknown argument %s (see the header of tools/loco_view.c)\n", a); return 2; }
    }
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    if (!loco_world_init(w, &c, seed, err, sizeof err)) { fprintf(stderr, "loco_view: %s\n", err); return 1; }
    LocoPolicy* pol = NULL;
    if (policy && !(pol = loco_policy_load(policy, c.num_agents, err, sizeof err))) { fprintf(stderr, "loco_view: %s\n", err); return 1; }
    int n = c.num_agents;
    float* obs = (float*)calloc((size_t)n * LOCO_OBS, sizeof(float)); float* act = (float*)calloc((size_t)n * LOCO_MAX_DOF, sizeof(float));
    const float* ap[LOCO_MAX_AGENTS]; float rew[LOCO_MAX_AGENTS], term[LOCO_MAX_AGENTS], last_r[LOCO_MAX_AGENTS] = {0};
    for (int i = 0; i < n; i++) ap[i] = act + (size_t)i * LOCO_MAX_DOF;
    loco_reset_all(w);

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(W, H, "Cretaceous locomotion");
    SetTargetFPS(c.policy_hz);
    FILE* enc = NULL;
    if (record) {
        char cmd[1024];
        snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -pix_fmt yuv420p -crf 20 '%s'", W, H, c.policy_hz, record);
        enc = popen(cmd, "w");
        if (!enc) { fprintf(stderr, "loco_view: cannot start ffmpeg\n"); return 1; }
    }
    Camera3D cam = {0}; cam.up = (Vector3){0, 1, 0}; cam.fovy = 45; cam.projection = CAMERA_PERSPECTIVE;
    float yaw = 0.6f, pitch = 0.22f, dist = 2.2f * w->rig.length;   // a three-quarter side view
    int follow = 0, frames = 0;
    bool paused = false, ghost = true;
    b3Vec3 look = lv(0, w->pelvis_h * 0.8f, 0);
    while (!WindowShouldClose() && (!record || frames < (int)(seconds * c.policy_hz))) {
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_R)) loco_reset_all(w);
        if (IsKeyPressed(KEY_G)) ghost = !ghost;
        if (IsKeyPressed(KEY_TAB)) follow = (follow + 1) % n;
        if (IsKeyDown(KEY_LEFT)) yaw -= 0.03f;
        if (IsKeyDown(KEY_RIGHT)) yaw += 0.03f;
        if (IsKeyDown(KEY_UP)) dist = fmaxf(dist * 0.98f, 1.0f);
        if (IsKeyDown(KEY_DOWN)) dist *= 1.02f;
        if (!paused) {
            if (pol) { for (int i = 0; i < n; i++) loco_observe(w, i, obs + (size_t)i * LOCO_OBS); loco_policy_forward(pol, obs, n, act); }
            loco_step(w, pol ? ap : NULL, rew, term);
            for (int i = 0; i < n; i++) { last_r[i] = rew[i]; if (term[i] && pol) loco_policy_reset(pol, i); }
        }
        // Camera: eased toward the followed animal's pelvis.
        loco_doll_get(&w->phys, follow, w->pos, w->rot, NULL, NULL);
        b3Vec3 pel = b3Add(w->pos[0], w->phys.doll[follow].origin);
        look = lv_lerp(look, lv(pel.x, w->pelvis_h * 0.8f, pel.z), 0.15f);
        cam.target = V3(look);
        cam.position = (Vector3){look.x + dist * cosf(pitch) * sinf(yaw), look.y + dist * sinf(pitch), look.z + dist * cosf(pitch) * cosf(yaw)};
        BeginDrawing();
        ClearBackground((Color){196, 214, 226, 255});
        BeginMode3D(cam);
        float gx = floorf(look.x / 2) * 2, gz = floorf(look.z / 2) * 2;
        DrawPlane((Vector3){gx, 0, gz}, (Vector2){80, 80}, (Color){150, 160, 120, 255});
        rlPushMatrix(); rlTranslatef(gx, 0.002f, gz); DrawGrid(40, 2.0f); rlPopMatrix();
        for (int i = 0; i < n; i++) {
            loco_doll_get(&w->phys, i, w->pos, w->rot, NULL, NULL);
            b3Vec3 o = w->phys.doll[i].origin;
            for (int b = 0; b < w->rig.nbody; b++)
                for (int k = 0; k < w->rig.body[b].nshape; k++)
                    draw_shape(&w->rig.body[b].shape[k], b3Add(o, w->pos[b]), w->rot[b], body_color(&w->rig.body[b], false), false);
            if (ghost && i == follow) {
                loco_ref_at(w, w->ag[i].t);
                for (int b = 0; b < w->rig.nbody; b++)
                    for (int k = 0; k < w->rig.body[b].nshape; k++)
                        draw_shape(&w->rig.body[b].shape[k], b3Add(o, w->rpos[b]), w->rrot[b], Fade(DARKBLUE, 0.6f), true);
            }
        }
        EndMode3D();
        const LocoAgent* A = &w->ag[follow];
        DrawRectangle(10, 10, 470, 118, Fade(WHITE, 0.75f));
        DrawText(TextFormat("%s, clip %s (%.2f m/s)  %s%s", w->rig.table, w->ref.clip, w->ref.speed, pol ? "policy" : "PD-tracking baseline", c.gains.passive ? ", passive tail" : ""), 18, 16, 18, BLACK);
        DrawText(TextFormat("animal %d of %d: up %.1f s, reward %.2f (pose %.2f vel %.2f feet/head/tail %.2f com %.2f)", follow + 1, n, A->elapsed, last_r[follow], A->r_pose, A->r_vel, A->r_ee, A->r_com), 18, 40, 14, DARKGRAY);
        DrawText(TextFormat("episodes ended %.0f (falls %.0f), mean time up %.2f s", w->stats.episodes, w->stats.falls, w->stats.episodes > 0 ? w->stats.length / w->stats.episodes : 0.0), 18, 60, 14, DARKGRAY);
        DrawText("ghost: the clip's pose now   space pause  R reset  G ghost  Tab next  arrows camera", 18, 82, 12, GRAY);
        DrawText(TextFormat("t %.1f s", frames / (float)c.policy_hz), 18, 102, 12, GRAY);
        EndDrawing();
        if (enc) { Image img = LoadImageFromScreen(); if (img.width == W && img.height == H) fwrite(img.data, 4, (size_t)W * H, enc); UnloadImage(img); }
        frames++;
    }
    if (enc) pclose(enc);
    CloseWindow();
    loco_policy_free(pol); free(obs); free(act); loco_world_close(w); free(w);
    return 0;
}
