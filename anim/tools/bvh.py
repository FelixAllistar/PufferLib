"""Minimal BVH reader and writer.

A parsed BVH is a dict with:
    names     list of joint names (End Sites are dropped)
    parents   list of parent indices, parents before children
    offsets   (J, 3) OFFSET values
    channels  list of channel name lists per joint
    frame_time seconds per frame
    positions (F, J, 3) local translations: the channel values for joints with
              position channels (they replace the OFFSET, the common convention),
              the OFFSET otherwise
    rotations (F, J, 4) local rotations as quaternions (x, y, z, w)
"""
import numpy as np

import mmmath as mm


def read(path):
    with open(path) as f:
        tokens = f.read().split()
    pos = 0
    names, parents, offsets, channels = [], [], [], []
    stack = []
    pending_end = 0

    def take():
        nonlocal pos
        pos += 1
        return tokens[pos - 1]

    if take() != "HIERARCHY":
        raise ValueError(f"{path}: missing HIERARCHY")
    while True:
        tok = take()
        if tok in ("ROOT", "JOINT"):
            names.append(take())
            parents.append(stack[-1] if stack else -1)
            channels.append([])
            offsets.append([0.0, 0.0, 0.0])
        elif tok == "End":
            take()
            pending_end += 1
            depth = 1
            if take() != "{":
                raise ValueError(f"{path}: malformed End Site")
            while depth:
                t = take()
                depth += {"{": 1, "}": -1}.get(t, 0)
            pending_end -= 1
        elif tok == "{":
            stack.append(len(names) - 1)
        elif tok == "}":
            stack.pop()
        elif tok == "OFFSET":
            offsets[-1] = [float(take()), float(take()), float(take())]
        elif tok == "CHANNELS":
            n = int(take())
            channels[-1] = [take() for _ in range(n)]
        elif tok == "MOTION":
            break
        else:
            raise ValueError(f"{path}: unexpected token {tok}")
    if take() != "Frames:":
        raise ValueError(f"{path}: missing Frames:")
    frames = int(take())
    if take() != "Frame" or take() != "Time:":
        raise ValueError(f"{path}: missing Frame Time:")
    frame_time = float(take())
    total = sum(len(c) for c in channels)
    data = np.array(tokens[pos: pos + frames * total], dtype=np.float64).reshape(frames, total)
    offsets = np.array(offsets, dtype=np.float64)
    J = len(names)
    positions = np.repeat(offsets[None], frames, axis=0)
    rotations = np.zeros((frames, J, 4))
    rotations[..., 3] = 1.0
    col = 0
    for j, chans in enumerate(channels):
        rot_cols, rot_order = [], ""
        for c in chans:
            axis = c[0].upper()
            if c.lower().endswith("position"):
                positions[:, j, "XYZ".index(axis)] = data[:, col]
            else:
                rot_cols.append(col)
                rot_order += axis
            col += 1
        if rot_cols:
            rotations[:, j] = mm.quat_from_euler_deg(data[:, rot_cols], rot_order)
    return {"names": names, "parents": parents, "offsets": offsets, "channels": channels,
            "frame_time": frame_time, "positions": positions, "rotations": rotations}


def write(path, names, parents, offsets, positions, rotations, frame_time, order="ZXY",
          position_joints=(0,)):
    """Write a BVH. Joints in position_joints get six channels, the rest three."""
    J = len(names)
    children = [[] for _ in range(J)]
    for j, p in enumerate(parents):
        if p >= 0:
            children[p].append(j)
    lines = ["HIERARCHY"]
    chan_rot = " ".join(f"{a}rotation" for a in order)

    def emit(j, depth):
        ind = "  " * depth
        lines.append(f"{ind}{'ROOT' if parents[j] < 0 else 'JOINT'} {names[j]}")
        lines.append(f"{ind}{{")
        o = offsets[j]
        lines.append(f"{ind}  OFFSET {o[0]:.6f} {o[1]:.6f} {o[2]:.6f}")
        if j in position_joints:
            lines.append(f"{ind}  CHANNELS 6 Xposition Yposition Zposition {chan_rot}")
        else:
            lines.append(f"{ind}  CHANNELS 3 {chan_rot}")
        for c in children[j]:
            emit(c, depth + 1)
        if not children[j]:
            lines.append(f"{ind}  End Site")
            lines.append(f"{ind}  {{")
            lines.append(f"{ind}    OFFSET 0.000000 0.000000 0.000000")
            lines.append(f"{ind}  }}")
        lines.append(f"{ind}}}")

    for j in range(J):
        if parents[j] < 0:
            emit(j, 0)
    F = rotations.shape[0]
    lines.append("MOTION")
    lines.append(f"Frames: {F}")
    lines.append(f"Frame Time: {frame_time:.8f}")
    # Channel order in the file follows the depth-first emission order.
    emitted = []

    def walk(j):
        emitted.append(j)
        for c in children[j]:
            walk(c)

    for j in range(J):
        if parents[j] < 0:
            walk(j)
    eul = mm.euler_deg_from_quat(rotations, order)
    for f in range(F):
        row = []
        for j in emitted:
            if j in position_joints:
                row += [f"{v:.6f}" for v in positions[f, j]]
            row += [f"{v:.6f}" for v in eul[f, j]]
        lines.append(" ".join(row))
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
