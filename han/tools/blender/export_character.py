"""GLB → HCH1 для A1. Blender импортирует glTF, запекает constraints/NLA в TRS.

blender -b -P han/tools/blender/export_character.py -- input.glb output.hchar
HCH1 little-endian: bones(rest TRS + inverse bind), weighted vertices, indices,
clips (30 Hz TRS). Runtime строит ozz skeleton/animations; skin остаётся на GPU.
"""
import math
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


def export(source, output):
    if source.suffix.lower() == '.blend':
        bpy.ops.wm.open_mainfile(filepath=str(source))
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=str(source))
    rig = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
    if len(meshes) != 1:
        raise ValueError("ожидается один skinned mesh")
    mesh = meshes[0]
    bones = []

    def visit(bone):
        bones.append(bone)
        for child in bone.children:
            visit(child)
    for bone in rig.data.bones:
        if bone.parent is None:
            visit(bone)
    if len(bones) > 128:
        raise ValueError("слишком много костей")
    bone_index = {b.name: i for i, b in enumerate(bones)}
    c = Matrix.Rotation(-math.pi / 2, 4, "X")  # Blender Z-up → HAN Y-up
    ci = c.inverted()

    def trs(m):
        t, q, s = m.decompose()
        q.normalize()
        return (*t, q.x, q.y, q.z, q.w, *s)

    def floats(values):
        if not all(math.isfinite(v) for v in values):
            raise ValueError("NaN в персонаже")
        return struct.pack("<" + "f" * len(values), *values)

    def string(value):
        b = value.encode("utf-8")
        return struct.pack("<I", len(b)) + b

    vertices, indices = [], []
    mesh.data.calc_loop_triangles()
    normal_matrix = (c @ mesh.matrix_world).to_3x3().inverted().transposed()
    for tri in mesh.data.loop_triangles:
        mat = mesh.data.materials[tri.material_index]
        color = (0.72, 0.72, 0.72)
        if mat and mat.use_nodes:
            pbr = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
            if pbr: color = tuple(pbr.inputs['Base Color'].default_value[:3])
        for loop_id in tri.loops:
            loop = mesh.data.loops[loop_id]
            v = mesh.data.vertices[loop.vertex_index]
            p = c @ mesh.matrix_world @ v.co
            n = (normal_matrix @ loop.normal).normalized()
            uv = mesh.data.uv_layers.active.data[loop_id].uv if mesh.data.uv_layers.active else Vector((0, 0))
            weights = sorted([(g.weight, bone_index[mesh.vertex_groups[g.group].name]) for g in v.groups
                              if mesh.vertex_groups[g.group].name in bone_index and g.weight > 0], reverse=True)[:4]
            total = sum(w for w, _ in weights)
            if total <= 0: raise ValueError("вершина без весов")
            weights = [(w / total, j) for w, j in weights]
            weights += [(0, 0)] * (4 - len(weights))
            vertices.append(floats((*p, *n, *color, uv.x, 1 - uv.y)) + struct.pack('<4I', *(j for w, j in weights)) + floats([w for w, j in weights]))
            indices.append(len(indices))

    actions = sorted(bpy.data.actions, key=lambda a: a.name)
    for track in rig.animation_data.nla_tracks: track.mute = True
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary=output.with_suffix(output.suffix+'.part')
    with temporary.open('wb') as f:
        f.write(struct.pack('<4s4I', b'HCH1', len(bones), len(vertices), len(indices), len(actions)))
        for bone in bones:
            parent = bone_index[bone.parent.name] if bone.parent else -1
            local = bone.parent.matrix_local.inverted() @ bone.matrix_local if bone.parent else rig.matrix_world @ bone.matrix_local
            inverse = (c @ rig.matrix_world @ bone.matrix_local @ ci).inverted()
            f.write(string(bone.name) + struct.pack('<i', parent) + floats(trs(c @ local @ ci)))
            f.write(floats([inverse[row][col] for col in range(4) for row in range(4)]))
        f.write(b''.join(vertices))
        f.write(struct.pack('<' + 'I' * len(indices), *indices))
        for action in actions:
            rig.animation_data.action = action
            name = action.name.removesuffix('_Armature')
            start, end = action.frame_range
            duration = max((end - start) / bpy.context.scene.render.fps, 1 / 30)
            rate=240 if name.startswith(('Boxing_Fall','Boxing_GetUp')) else bpy.context.scene.render.fps
            samples = max(2, math.ceil(duration * rate) + 1)
            f.write(string(name) + struct.pack('<If', samples, duration))
            for sample in range(samples):
                frame = start + (end - start) * sample / (samples - 1)
                bpy.context.scene.frame_set(int(frame), subframe=frame % 1)
                bpy.context.view_layer.update()
                for bone in bones:
                    pose = rig.pose.bones[bone.name]
                    local = pose.parent.matrix.inverted() @ pose.matrix if pose.parent else rig.matrix_world @ pose.matrix
                    f.write(floats(trs(c @ local @ ci)))
    temporary.replace(output)
    print(f'HCH1: {len(bones)} bones, {len(indices)//3} triangles, {len(actions)} clips → {output}')


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:]
    export(Path(args[0]).resolve(), Path(args[1]).resolve())
