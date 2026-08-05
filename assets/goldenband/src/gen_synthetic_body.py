#!/usr/bin/env python3
"""gen_synthetic_body.py -- S144-07: writes a synthetic .gskel + .gmesh test
asset, matching the exact 5-joint skeleton (Hips->Spine->{Head,L_Arm,R_Arm})
gband_rig.c (S144-06's box-rig) already animates, so the same tyler_idle.gband
/tyler_walk.gband clips drive this mesh too -- no new animation data needed.

Not a Blender export: this exists so the C skinning pipeline (gskel.c/
gmesh.c loaders + the consuming engine's forward-kinematics/skin blend) can
be built and verified end-to-end (real multi-bone blended weights, not just
rigid single-bone boxes) before any real Blender asset exists. See
GoblinFoxDragon/docs2/GOLDENBAND_INTEGRATION_NORTHSTAR.md Phase 2.

Usage: python3 gen_synthetic_body.py <out_dir>
Writes <out_dir>/synthetic_body.gskel and <out_dir>/synthetic_body.gmesh.
"""
import struct
import sys
import os

MAGIC_GSKEL = b"GSKL"
MAGIC_GMESH = b"GMSH"

# Same joint list/rest offsets as gband_rig.c's JOINT_REST_OFFSET, except
# Hips now gets a real rest_translation (bind-pose standing height) instead
# of (0,0,0) -- the box-rig treats Hips's translation as purely animated
# with no rest constant; this mesh-skinning system needs a real bind pose to
# compute inverse-bind matrices against, and 0.65 matches tyler_idle.bvh/
# tyler_walk.bvh's own baseline Hips.Y value, so bind and animated pose
# don't pop apart.
JOINTS = [
    # name,    parent, rest_translation (local, from parent)
    ("Hips",   -1, (0.0, 0.65, 0.0)),
    ("Spine",   0, (0.0, 0.35, 0.0)),
    ("Head",    1, (0.0, 0.30, 0.0)),
    ("L_Arm",   1, (-0.45, 0.0, 0.0)),
    ("R_Arm",   1, (0.45, 0.0, 0.0)),
]

IDENTITY_QUAT = (0.0, 0.0, 0.0, 1.0)


def bind_world_positions():
    world = {}
    for i, (name, parent, offset) in enumerate(JOINTS):
        px, py, pz = world[parent] if parent != -1 else (0.0, 0.0, 0.0)
        world[i] = (px + offset[0], py + offset[1], pz + offset[2])
    return world


def identity4():
    return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]  # column-major


def inverse_bind_translation_only(world_pos):
    # rest rotation is identity everywhere, so the inverse of a pure
    # translation matrix is just translate(-world_pos) -- no real matrix
    # inversion needed (same shortcut a Blender exporter with an identity
    # rest pose could also take, though a general exporter should use
    # mathutils' real inverse for rotated rest poses).
    m = identity4()
    m[12], m[13], m[14] = -world_pos[0], -world_pos[1], -world_pos[2]
    return m


def write_gskel(path):
    world = bind_world_positions()
    with open(path, "wb") as f:
        f.write(MAGIC_GSKEL)
        f.write(struct.pack("<I", 1))              # version
        f.write(struct.pack("<I", len(JOINTS)))     # joint_count
        for i, (name, parent, offset) in enumerate(JOINTS):
            name_bytes = name.encode("ascii")
            f.write(name_bytes + b"\x00" * (32 - len(name_bytes)))
            f.write(struct.pack("<i", parent))
            f.write(struct.pack("<3f", *offset))
            f.write(struct.pack("<4f", *IDENTITY_QUAT))
            f.write(struct.pack("<16f", *inverse_bind_translation_only(world[i])))


def weight_for_y(y):
    """Returns (bone_a_idx, bone_b_idx, weight_a) for a torso ring at height y,
    blending smoothly between Hips(0)/Spine(1)/Head(2) -- a genuine
    multi-bone weight gradient, not just per-ring hard assignment."""
    hips_y, spine_y, head_y = 0.65, 1.00, 1.30
    if y <= hips_y:
        return 0, 0, 1.0
    if y <= spine_y:
        t = (y - hips_y) / (spine_y - hips_y)
        return 0, 1, 1.0 - t
    if y <= head_y:
        t = (y - spine_y) / (head_y - spine_y)
        return 1, 2, 1.0 - t
    return 2, 2, 1.0


def make_torso():
    """5 rings x 4 corners, tapered, connected by quads -- a real
    continuous mesh with a smooth multi-bone weight gradient up the spine,
    not rigid per-joint boxes."""
    rings_y = [0.45, 0.65, 1.00, 1.30, 1.475]
    rings_r = [0.28, 0.28, 0.22, 0.18, 0.15]
    verts = []
    for y, r in zip(rings_y, rings_r):
        a, b, w = weight_for_y(y)
        for cx, cz in [(-r, -r), (r, -r), (r, r), (-r, r)]:
            pos = (cx, y, cz)
            # normal: outward from the vertical axis, flat Y component
            import math
            n = (cx, 0.0, cz)
            nlen = math.sqrt(n[0]*n[0] + n[2]*n[2]) or 1.0
            normal = (n[0]/nlen, 0.15, n[2]/nlen)
            nlen2 = math.sqrt(sum(c*c for c in normal))
            normal = tuple(c/nlen2 for c in normal)
            bone_idx = (a, b, 0, 0) if a != b else (a, 0, 0, 0)
            weights = (w, 1.0 - w, 0.0, 0.0) if a != b else (1.0, 0.0, 0.0, 0.0)
            verts.append({"pos": pos, "normal": normal, "uv": (0.0, 0.0),
                          "bone_idx": bone_idx, "weights": weights})
    tris = []
    ring_count = len(rings_y)
    for r in range(ring_count - 1):
        base0 = r * 4
        base1 = (r + 1) * 4
        for c in range(4):
            c2 = (c + 1) % 4
            v00, v01 = base0 + c, base0 + c2
            v10, v11 = base1 + c, base1 + c2
            tris += [(v00, v10, v11), (v00, v11, v01)]
    return verts, tris


def make_box(center, half_extents, bone_idx):
    """A rigid single-bone-weighted box (same shape convention as the
    box-rig's own arm boxes) -- returns (verts, tris) with local vertex
    indices starting at 0."""
    cx, cy, cz = center
    hx, hy, hz = half_extents
    corners = [
        (cx-hx, cy-hy, cz-hz), (cx+hx, cy-hy, cz-hz), (cx+hx, cy+hy, cz-hz), (cx-hx, cy+hy, cz-hz),
        (cx-hx, cy-hy, cz+hz), (cx+hx, cy-hy, cz+hz), (cx+hx, cy+hy, cz+hz), (cx-hx, cy+hy, cz+hz),
    ]
    verts = []
    for p in corners:
        import math
        n = (p[0]-cx, p[1]-cy, p[2]-cz)
        nlen = math.sqrt(sum(c*c for c in n)) or 1.0
        normal = tuple(c/nlen for c in n)
        verts.append({"pos": p, "normal": normal, "uv": (0.0, 0.0),
                      "bone_idx": (bone_idx, 0, 0, 0), "weights": (1.0, 0.0, 0.0, 0.0)})
    faces = [(0,1,2,3), (5,4,7,6), (4,0,3,7), (1,5,6,2), (3,2,6,7), (4,5,1,0)]
    tris = []
    for a,b,c,d in faces:
        tris += [(a,b,c), (a,c,d)]
    return verts, tris


def write_gmesh(path):
    verts, tris = make_torso()
    l_arm_v, l_arm_t = make_box((-0.45, 0.75, 0.0), (0.09, 0.25, 0.09), 3)  # L_Arm bone index 3
    r_arm_v, r_arm_t = make_box((0.45, 0.75, 0.0), (0.09, 0.25, 0.09), 4)   # R_Arm bone index 4

    offset = len(verts)
    verts += l_arm_v
    tris += [(a+offset, b+offset, c+offset) for a,b,c in l_arm_t]

    offset = len(verts)
    verts += r_arm_v
    tris += [(a+offset, b+offset, c+offset) for a,b,c in r_arm_t]

    with open(path, "wb") as f:
        f.write(MAGIC_GMESH)
        f.write(struct.pack("<I", 1))            # version
        f.write(struct.pack("<I", len(verts)))   # vertex_count
        f.write(struct.pack("<I", len(tris) * 3))# index_count
        for v in verts:
            f.write(struct.pack("<3f", *v["pos"]))
            f.write(struct.pack("<3f", *v["normal"]))
            f.write(struct.pack("<2f", *v["uv"]))
            f.write(bytes(v["bone_idx"]))
            f.write(struct.pack("<4f", *v["weights"]))
        for a, b, c in tris:
            f.write(struct.pack("<3I", a, b, c))

    return len(verts), len(tris)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out_dir, exist_ok=True)
    write_gskel(os.path.join(out_dir, "synthetic_body.gskel"))
    nv, nt = write_gmesh(os.path.join(out_dir, "synthetic_body.gmesh"))
    print(f"wrote synthetic_body.gskel (5 joints) + synthetic_body.gmesh ({nv} verts, {nt} tris)")
