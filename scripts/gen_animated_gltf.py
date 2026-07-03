#!/usr/bin/env python3
"""Generate a minimal rigged + animated glTF 2.0 asset (issue #287 follow-up).

The engine had no animated model to exercise the runtime skinned path, so this emits the
smallest self-contained one: a single-bone quad whose joint rotates about Z from 0 to 90
degrees over one second. The buffer is embedded as a base64 data URI so the .gltf is a
single committable file with no external .bin.

Run: python3 scripts/gen_animated_gltf.py > assets/models/animated_bone.gltf
"""
import base64
import json
import math
import struct
import sys

# --- geometry: a quad in the XY plane, every vertex skinned 100% to joint 0 ---------
positions = [(-0.5, 0.0, 0.0), (0.5, 0.0, 0.0), (0.5, 1.0, 0.0), (-0.5, 1.0, 0.0)]
normals = [(0.0, 0.0, 1.0)] * 4
joints = [(0, 0, 0, 0)] * 4              # VEC4 unsigned byte
weights = [(1.0, 0.0, 0.0, 0.0)] * 4     # VEC4 float
indices = [0, 1, 2, 0, 2, 3]             # unsigned short

# Inverse bind matrix for joint 0 = identity (its bind pose sits at the origin).
ibm = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]  # column-major

# Animation: rotate joint 0 about +Z, 0 -> 45 -> 90 degrees at t = 0, 0.5, 1.0 s.
times = [0.0, 0.5, 1.0]


def quat_z(deg):
    r = math.radians(deg) / 2.0
    return (0.0, 0.0, math.sin(r), math.cos(r))  # x, y, z, w


rotations = [quat_z(0.0), quat_z(45.0), quat_z(90.0)]

# --- pack a single binary buffer, 4-byte aligned per view ---------------------------
buf = bytearray()


def align4():
    while len(buf) % 4:
        buf.append(0)


def put(fmt, flat):
    align4()
    off = len(buf)
    buf.extend(struct.pack("<" + fmt, *flat))
    return off, len(buf) - off


pos_off, pos_len = put("%df" % (len(positions) * 3), [c for v in positions for c in v])
nrm_off, nrm_len = put("%df" % (len(normals) * 3), [c for v in normals for c in v])
jnt_off, jnt_len = put("%dB" % (len(joints) * 4), [c for v in joints for c in v])
wgt_off, wgt_len = put("%df" % (len(weights) * 4), [c for v in weights for c in v])
idx_off, idx_len = put("%dH" % len(indices), indices)
ibm_off, ibm_len = put("16f", ibm)
tin_off, tin_len = put("%df" % len(times), times)
tout_off, tout_len = put("%df" % (len(rotations) * 4), [c for q in rotations for c in q])

data_uri = "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode("ascii")

pmin = [min(v[i] for v in positions) for i in range(3)]
pmax = [max(v[i] for v in positions) for i in range(3)]

gltf = {
    "asset": {"version": "2.0", "generator": "IKore gen_animated_gltf.py (#287)"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "SkinnedQuad", "mesh": 0, "skin": 0},
        {"name": "Bone0", "translation": [0.0, 0.0, 0.0]},
    ],
    "meshes": [{
        "name": "QuadMesh",
        "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
            "indices": 4,
            "mode": 4,
        }],
    }],
    "skins": [{"joints": [1], "inverseBindMatrices": 5, "skeleton": 1}],
    "animations": [{
        "name": "Rotate",
        "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}],
        "samplers": [{"input": 6, "output": 7, "interpolation": "LINEAR"}],
    }],
    "buffers": [{"byteLength": len(buf), "uri": data_uri}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": pos_len, "target": 34962},
        {"buffer": 0, "byteOffset": nrm_off, "byteLength": nrm_len, "target": 34962},
        {"buffer": 0, "byteOffset": jnt_off, "byteLength": jnt_len, "target": 34962},
        {"buffer": 0, "byteOffset": wgt_off, "byteLength": wgt_len, "target": 34962},
        {"buffer": 0, "byteOffset": idx_off, "byteLength": idx_len, "target": 34963},
        {"buffer": 0, "byteOffset": ibm_off, "byteLength": ibm_len},
        {"buffer": 0, "byteOffset": tin_off, "byteLength": tin_len},
        {"buffer": 0, "byteOffset": tout_off, "byteLength": tout_len},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": pmin, "max": pmax},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5121, "count": 4, "type": "VEC4"},
        {"bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC4"},
        {"bufferView": 4, "componentType": 5123, "count": 6, "type": "SCALAR"},
        {"bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4"},
        {"bufferView": 6, "componentType": 5126, "count": 3, "type": "SCALAR",
         "min": [times[0]], "max": [times[-1]]},
        {"bufferView": 7, "componentType": 5126, "count": 3, "type": "VEC4"},
    ],
}

sys.stdout.write(json.dumps(gltf, indent=2))
sys.stdout.write("\n")
