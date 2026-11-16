#!/usr/bin/env python3
"""Read Assimp's scene and mesh/bone names without altering the FBX."""
import ctypes as C
import os
import sys
import numpy as np


class String(C.Structure):
    _fields_ = [("length", C.c_uint32), ("data", C.c_char * 1024)]


class Vec(C.Structure):
    _fields_ = [("x", C.c_float), ("y", C.c_float), ("z", C.c_float)]


class Bone(C.Structure):
    _fields_ = [("name", String), ("num_weights", C.c_uint)]


class Node(C.Structure):
    pass


Node._fields_ = [("name", String), ("transform", C.c_float * 16),
                 ("parent", C.POINTER(Node)), ("num_children", C.c_uint),
                 ("children", C.POINTER(C.POINTER(Node))), ("num_meshes", C.c_uint),
                 ("meshes", C.POINTER(C.c_uint)), ("metadata", C.c_void_p)]


class Mesh(C.Structure):
    _fields_ = [("primitive_types", C.c_uint), ("num_vertices", C.c_uint),
                ("num_faces", C.c_uint), ("vertices", C.POINTER(Vec)),
                ("normals", C.POINTER(Vec)), ("tangents", C.c_void_p),
                ("bitangents", C.c_void_p), ("colors", C.c_void_p * 8),
                ("texture_coords", C.c_void_p * 8), ("num_uv", C.c_uint * 8),
                ("faces", C.c_void_p), ("num_bones", C.c_uint),
                ("bones", C.POINTER(C.POINTER(Bone))), ("material_index", C.c_uint),
                ("name", String)]


class Scene(C.Structure):
    _fields_ = [("flags", C.c_uint), ("root", C.c_void_p), ("num_meshes", C.c_uint),
                ("meshes", C.POINTER(C.POINTER(Mesh)))]


assimp = C.CDLL("libassimp.so.5")
assimp.aiImportFile.argtypes = [C.c_char_p, C.c_uint]
assimp.aiImportFile.restype = C.POINTER(Scene)
assimp.aiReleaseImport.argtypes = [C.POINTER(Scene)]
assimp.aiGetErrorString.restype = C.c_char_p
scene = assimp.aiImportFile(os.fsencode(sys.argv[1]), 0x8 | 0x40)
if not scene:
    raise SystemExit(assimp.aiGetErrorString().decode(errors="replace"))
try:
    print("meshes:", scene.contents.num_meshes)
    for i in range(scene.contents.num_meshes):
        m = scene.contents.meshes[i].contents
        bones = [(m.bones[b].contents.name.data.decode(errors="replace"),
                  m.bones[b].contents.num_weights) for b in range(m.num_bones)]
        print(i, m.name.data.decode(errors="replace"), "vertices", m.num_vertices,
              "faces", m.num_faces, "bones", bones)
    bounds = {}
    def visit(ptr, parent):
        n = ptr.contents
        transform = parent @ np.array(n.transform).reshape(4, 4)
        for k in range(n.num_meshes):
            m = scene.contents.meshes[n.meshes[k]].contents
            labels = [m.bones[b].contents.name.data.decode() for b in range(m.num_bones)
                      if m.bones[b].contents.num_weights]
            assert len(labels) <= 1, labels
            group = labels[0] if labels else "Root"
            xyz = np.ctypeslib.as_array(C.cast(m.vertices, C.POINTER(C.c_float)),
                                       shape=(m.num_vertices * 3,)).reshape(-1, 3)
            pos = xyz @ transform[:3, :3].T + transform[:3, 3]
            lo, hi = pos.min(axis=0), pos.max(axis=0)
            if group in bounds:
                lo, hi = np.minimum(lo, bounds[group][0]), np.maximum(hi, bounds[group][1])
            bounds[group] = lo, hi
        for k in range(n.num_children):
            visit(n.children[k], transform)
    visit(C.cast(scene.contents.root, C.POINTER(Node)), np.eye(4))
    for label, (lo, hi) in sorted(bounds.items()):
        print("BOUNDS", label, "center", ((lo + hi) / 2).round(3), "size", (hi - lo).round(3))
    assert set(bounds) == {"Root", "FL", "FR", "RL", "RR"}, set(bounds)
finally:
    assimp.aiReleaseImport(scene)
