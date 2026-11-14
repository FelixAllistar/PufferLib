/*
 * mm.h: motion-matching runtime (C99, libm only).
 *
 * The runtime loads an MMDB database written by tools/mm_build.py, predicts a
 * trajectory from the game's capsule state, searches the database for the best
 * matching frame, plays it back with inertialized transitions and foot locking,
 * and outputs local rotations and global transforms in the target skeleton's
 * joint order. Animation is presentation only: the rendered root is the game's
 * capsule, and nothing here feeds back into the simulation.
 *
 * Units are those of the database's target skeleton (meters for glTF rigs),
 * time is in seconds, Y is up and yaw 0 faces +Z.
 *
 * Memory is allocated only by mm_db_load and mm_controller_init. Every other
 * function works in caller-provided or preallocated storage.
 */
#ifndef MM_H
#define MM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MM_NAME_MAX 64
#define MM_MAX_TRAJECTORY 8

typedef struct { float x, y, z; } mm_vec3;
typedef struct { float x, y, z, w; } mm_quat;

/* Per-frame flag bits stored in the database. */
enum {
    MM_FLAG_CONTACT_LEFT = 1,
    MM_FLAG_CONTACT_RIGHT = 2,
    MM_FLAG_TRAJECTORY_EXTRAPOLATED = 4,
    MM_FLAG_NOT_SEARCHABLE = 8
};

/* Feature block types. Pose blocks are copied from the playing frame; the
 * trajectory blocks of a query come from mm_trajectory_features. */
enum {
    MM_BLOCK_POSITION = 0,
    MM_BLOCK_VELOCITY = 1,
    MM_BLOCK_TRAJECTORY_POSITION = 2,
    MM_BLOCK_TRAJECTORY_DIRECTION = 3
};

/* Joints the runtime needs by function, in the order stored in the file. */
enum {
    MM_ROLE_LEFT_UPLEG, MM_ROLE_LEFT_LEG, MM_ROLE_LEFT_FOOT, MM_ROLE_LEFT_TOE,
    MM_ROLE_RIGHT_UPLEG, MM_ROLE_RIGHT_LEG, MM_ROLE_RIGHT_FOOT, MM_ROLE_RIGHT_TOE,
    MM_ROLE_COUNT
};

typedef struct {
    char name[MM_NAME_MAX];
    int start, length;
    uint32_t tags;
} mm_clip;

typedef struct {
    char name[MM_NAME_MAX];
    int type, joint, offset, size;
    float weight;
} mm_block;

typedef struct {
    float fps;
    int joint_count, frame_count, clip_count, tag_count;
    int feature_count, block_count, trajectory_count;
    int small_box_size, large_box_size, small_box_count, large_box_count;
    int hips;
    int roles[MM_ROLE_COUNT];
    int trajectory_frames[MM_MAX_TRAJECTORY];

    char (*joint_names)[MM_NAME_MAX];
    int *parents;
    mm_vec3 *rest_pos;
    mm_quat *rest_rot;
    float *inverse_bind;      /* joint_count x 16, column major. */
    char (*tag_names)[MM_NAME_MAX];
    mm_clip *clips;
    mm_block *blocks;
    float *feature_mean, *feature_scale;

    mm_quat *rotations;       /* frame_count x joint_count local rotations. */
    mm_vec3 *hips_pos;        /* Hips position relative to the root. */
    mm_vec3 *root_pos;        /* Simulation root in clip space. */
    float *root_yaw;
    uint8_t *flags;
    float *features;          /* frame_count x feature_count, normalized. */
    float *small_min, *small_max, *large_min, *large_max;

    /* Derived at load time. */
    int *frame_clip;
    uint32_t *small_tags_any, *small_tags_all, *large_tags_any, *large_tags_all;
    uint8_t *small_searchable, *large_searchable;

    void *file_data;
} mm_db;

/* Loads a database. Returns 0 on success and a negative value on failure, in
 * which case the database is left zeroed. */
int mm_db_load(mm_db *db, const char *path);
void mm_db_free(mm_db *db);
int mm_db_find_joint(const mm_db *db, const char *name);
/* Returns the tag's bit, or 0 when the database has no such tag. */
uint32_t mm_db_tag_bit(const mm_db *db, const char *name);

/* Quaternion and vector helpers used by the runtime and the demo. */
mm_vec3 mm_vec3_make(float x, float y, float z);
mm_quat mm_quat_mul(mm_quat a, mm_quat b);
mm_quat mm_quat_inv(mm_quat q);
mm_vec3 mm_quat_rotate(mm_quat q, mm_vec3 v);
mm_quat mm_quat_from_yaw(float yaw);
mm_quat mm_quat_nlerp_shortest(mm_quat a, mm_quat b, float t);
/* Writes a column-major 4x4 matrix (the memory layout of raylib's Matrix). */
void mm_transform_matrix(mm_vec3 position, mm_quat rotation, float out[16]);

/* Forward kinematics. Joints with parent -1 are placed relative to the root;
 * the hips take hips_pos, every other joint its rest translation. */
void mm_forward_kinematics(const mm_db *db, const mm_quat *local_rot, mm_vec3 hips_pos,
                           mm_vec3 root_pos, mm_quat root_rot,
                           mm_vec3 *world_pos, mm_quat *world_rot);

/* Samples the pose between frame and the next frame of the same clip. */
void mm_sample_pose(const mm_db *db, int frame, float alpha, mm_quat *local_rot, mm_vec3 *hips_pos);

/* State of the game's capsule, plus what the game wants it to do. */
typedef struct {
    mm_vec3 position;
    mm_vec3 velocity;
    mm_vec3 acceleration;
    float yaw;
    float yaw_rate;
} mm_spring_state;

/* Predicts the root at the database's trajectory frames with critically damped
 * springs: the velocity approaches desired_velocity with velocity_halflife and
 * the yaw approaches desired_yaw with rotation_halflife (both in seconds). */
void mm_predict_trajectory(const mm_db *db, const mm_spring_state *state, mm_vec3 desired_velocity,
                           float desired_yaw, float velocity_halflife, float rotation_halflife,
                           mm_vec3 *out_pos, float *out_yaw);

/* Writes the normalized trajectory blocks of a query from predicted points. */
void mm_trajectory_features(const mm_db *db, mm_vec3 root_pos, float root_yaw,
                            const mm_vec3 *pos, const float *yaw, float *query);

typedef struct {
    uint32_t required;   /* Every one of these tags must be present. */
    uint32_t forbidden;  /* None of these tags may be present. */
} mm_filter;

int mm_frame_allowed(const mm_db *db, int frame, mm_filter filter);
float mm_frame_cost(const mm_db *db, const float *query, int frame);

/* Nearest neighbour over allowed frames with the two-level box acceleration.
 * Only candidates strictly cheaper than best_cost are returned; pass INFINITY
 * for an unconstrained search. Returns -1 when nothing qualifies. The result
 * equals mm_search_brute_force exactly. */
int mm_search(const mm_db *db, const float *query, mm_filter filter, float best_cost, float *out_cost);
int mm_search_brute_force(const mm_db *db, const float *query, mm_filter filter, float best_cost,
                          float *out_cost);

/* Two-bone IK. a, b, c are the world positions of the root, middle and end
 * joints, a_world and b_world the world rotations of the first two, and
 * a_local the local rotation of the first. pole is used as the bend direction
 * when the chain is straight. The end joint is moved toward target, at most
 * max_extension (0..1) of the chain length from a, and the new local rotations
 * of the first two joints are written to out_a_local and out_b_local. */
void mm_two_bone_ik(mm_vec3 a, mm_vec3 b, mm_vec3 c, mm_vec3 target, mm_vec3 pole,
                    mm_quat a_world, mm_quat b_world, mm_quat a_local,
                    float max_extension, mm_quat *out_a_local, mm_quat *out_b_local);

typedef struct {
    float velocity_halflife;       /* Trajectory velocity spring, seconds. */
    float rotation_halflife;       /* Trajectory facing spring, seconds. */
    float search_interval;         /* Seconds between regular searches. */
    float transition_margin;       /* Required cost improvement over continuing. */
    float force_search_velocity;   /* Desired velocity change per update that forces a search. */
    float force_search_yaw;        /* Desired yaw change per update (radians) that forces a search. */
    float inertialization_halflife;/* Transition offset decay, seconds. */
    int ignore_nearby_frames;      /* Candidates this close in the playing clip are not transitions. */
    int foot_lock;                 /* Nonzero enables foot locking. */
    float foot_unlock_distance;    /* Unlock when the foot drifts this far from its lock. */
    float foot_unlock_halflife;    /* Decay of the lock offset after unlocking, seconds. */
    float foot_max_extension;      /* Fraction of the leg length IK may extend to. */
    float root_adjust_halflife;    /* Root adjustment decay toward the capsule, seconds. */
    float root_adjust_max;         /* Max distance of the rendered root from the capsule; 0 is off. */
} mm_config;

void mm_config_default(mm_config *config);

/* What the game provides every update. */
typedef struct {
    mm_vec3 position;          /* Capsule position on the ground. */
    mm_vec3 velocity;          /* Capsule velocity. */
    float yaw;                 /* Capsule facing. */
    mm_vec3 desired_velocity;  /* Velocity the game is steering toward. */
    float desired_yaw;         /* Facing the game is steering toward. */
    mm_filter filter;          /* Tags allowed for matching (style, stance, mirrored). */
} mm_input;

typedef struct {
    int locked;
    mm_vec3 lock_pos;
    mm_vec3 offset, offset_vel;
} mm_foot_lock;

typedef struct {
    const mm_db *db;
    mm_config config;

    int frame;              /* Playing database frame. */
    float frame_alpha;      /* Sub-frame position between frame and frame + 1. */
    float search_timer;
    int clip;               /* Clip of the playing frame. */
    float last_cost;        /* Cost of the last search result. */
    int transitions;        /* Number of transitions so far. */
    int searches;

    /* Capsule history for acceleration and yaw-rate estimates. */
    int has_history;
    mm_vec3 prev_velocity;
    float prev_yaw;
    mm_vec3 prev_desired_velocity;
    float prev_desired_yaw;
    mm_filter prev_filter;

    /* Inertialization offsets, per joint, and for the hips position. */
    mm_quat *offset_rot;
    mm_vec3 *offset_ang_vel;
    mm_vec3 offset_hips, offset_hips_vel;

    /* Scratch and output. */
    mm_quat *anim_rot;      /* Sampled animation pose before offsets. */
    mm_vec3 anim_hips;
    mm_quat *local_rot;     /* Output local rotations (after IK). */
    mm_vec3 hips_pos;       /* Output hips position relative to the root. */
    mm_vec3 *world_pos;     /* Output global transforms. */
    mm_quat *world_rot;
    float *query;

    mm_vec3 root_pos;       /* Rendered root. */
    float root_yaw;
    mm_vec3 root_offset;

    mm_vec3 trajectory_pos[MM_MAX_TRAJECTORY];
    float trajectory_yaw[MM_MAX_TRAJECTORY];

    mm_foot_lock feet[2];
} mm_controller;

/* Allocates the controller's buffers and starts playing start_frame. Returns
 * 0 on success. */
int mm_controller_init(mm_controller *c, const mm_db *db, const mm_config *config, int start_frame,
                       const mm_input *input);
void mm_controller_free(mm_controller *c);
/* Advances the controller by dt seconds. Allocation free. */
void mm_controller_update(mm_controller *c, const mm_input *input, float dt);
/* Fills a query from the playing frame's pose features and the trajectory. */
void mm_controller_query(mm_controller *c, const mm_input *input);

#ifdef __cplusplus
}
#endif

#endif
