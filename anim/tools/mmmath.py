"""Vectorized quaternion and transform helpers shared by the builder tools.

Quaternions are stored as (..., 4) arrays in x, y, z, w order, matching glTF
and the runtime. All functions broadcast over leading dimensions.
"""
import numpy as np


def quat_normalize(q):
    q = np.asarray(q, dtype=np.float64)
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def quat_mul(a, b):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    ax, ay, az, aw = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    bx, by, bz, bw = b[..., 0], b[..., 1], b[..., 2], b[..., 3]
    return np.stack([
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ], axis=-1)


def quat_inv(q):
    q = np.asarray(q, dtype=np.float64)
    return q * np.array([-1.0, -1.0, -1.0, 1.0])


def quat_rotate(q, v):
    q = np.asarray(q, dtype=np.float64)
    v = np.asarray(v, dtype=np.float64)
    u = q[..., :3]
    w = q[..., 3:4]
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def quat_from_axis_angle(axis, angle):
    axis = np.asarray(axis, dtype=np.float64)
    angle = np.asarray(angle, dtype=np.float64)
    axis = axis / np.linalg.norm(axis, axis=-1, keepdims=True)
    half = 0.5 * angle[..., None]
    return np.concatenate([axis * np.sin(half), np.cos(half)], axis=-1)


def quat_from_yaw(yaw):
    yaw = np.asarray(yaw, dtype=np.float64)
    zero = np.zeros_like(yaw)
    return np.stack([zero, np.sin(0.5 * yaw), zero, np.cos(0.5 * yaw)], axis=-1)


def quat_abs(q):
    """Flip quaternions onto the hemisphere with non-negative w."""
    q = np.asarray(q, dtype=np.float64)
    return np.where(q[..., 3:4] < 0.0, -q, q)


def quat_unroll(q):
    """Make a (frames, ..., 4) quaternion sequence continuous in time."""
    q = np.array(q, dtype=np.float64)
    for f in range(1, q.shape[0]):
        d = np.sum(q[f] * q[f - 1], axis=-1, keepdims=True)
        q[f] = np.where(d < 0.0, -q[f], q[f])
    return q


def quat_to_matrix(q):
    q = quat_normalize(q)
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    m = np.empty(q.shape[:-1] + (3, 3))
    m[..., 0, 0] = 1 - 2 * (y * y + z * z)
    m[..., 0, 1] = 2 * (x * y - w * z)
    m[..., 0, 2] = 2 * (x * z + w * y)
    m[..., 1, 0] = 2 * (x * y + w * z)
    m[..., 1, 1] = 1 - 2 * (x * x + z * z)
    m[..., 1, 2] = 2 * (y * z - w * x)
    m[..., 2, 0] = 2 * (x * z - w * y)
    m[..., 2, 1] = 2 * (y * z + w * x)
    m[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def quat_from_matrix(m):
    """Convert a single 3x3 rotation matrix to a quaternion."""
    m = np.asarray(m, dtype=np.float64)
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = np.sqrt(tr + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    return quat_normalize(np.array(q))


def quat_from_euler_deg(angles, order):
    """Compose intrinsic rotations given in degrees, as BVH channel order does.

    angles is (..., 3) with one column per letter of order (for example "ZXY");
    the result is R = R_order[0] * R_order[1] * R_order[2].
    """
    angles = np.radians(np.asarray(angles, dtype=np.float64))
    axes = {"X": [1.0, 0, 0], "Y": [0, 1.0, 0], "Z": [0, 0, 1.0]}
    q = None
    for i, letter in enumerate(order):
        r = quat_from_axis_angle(np.broadcast_to(axes[letter], angles.shape[:-1] + (3,)), angles[..., i])
        q = r if q is None else quat_mul(q, r)
    return q


def euler_deg_from_quat(q, order):
    """Inverse of quat_from_euler_deg for the six Tait-Bryan orders."""
    m = quat_to_matrix(q)
    idx = {"X": 0, "Y": 1, "Z": 2}
    i, j, k = idx[order[0]], idx[order[1]], idx[order[2]]
    # Sign of the permutation decides the signs in the decomposition.
    sign = 1.0 if (i, j, k) in [(0, 1, 2), (1, 2, 0), (2, 0, 1)] else -1.0
    b = np.arcsin(np.clip(sign * m[..., i, k], -1.0, 1.0))
    a = np.arctan2(-sign * m[..., j, k], m[..., k, k])
    c = np.arctan2(-sign * m[..., i, j], m[..., i, i])
    return np.degrees(np.stack([a, b, c], axis=-1))


def forward_kinematics(parents, local_pos, local_rot, root_pos=None, root_rot=None):
    """Compute world positions and rotations.

    parents has one entry per joint with parents before children; local_pos and
    local_rot are (frames, joints, 3/4). Joints with parent -1 are placed in the
    space given by root_pos/root_rot (frames, 3/4) when provided.
    """
    frames, joints = local_rot.shape[:2]
    world_pos = np.zeros((frames, joints, 3))
    world_rot = np.zeros((frames, joints, 4))
    for j, p in enumerate(parents):
        if p < 0:
            if root_rot is None:
                world_pos[:, j] = local_pos[:, j]
                world_rot[:, j] = local_rot[:, j]
            else:
                world_pos[:, j] = quat_rotate(root_rot, local_pos[:, j]) + root_pos
                world_rot[:, j] = quat_mul(root_rot, local_rot[:, j])
        else:
            world_pos[:, j] = quat_rotate(world_rot[:, p], local_pos[:, j]) + world_pos[:, p]
            world_rot[:, j] = quat_mul(world_rot[:, p], local_rot[:, j])
    return world_pos, world_rot
