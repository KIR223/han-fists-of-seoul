"""blender -b -P han/tools/blender/check_mannequin.py -- path/to/CH_Mannequin.glb

Проверяет реальный импорт и деформацию всех клипов в Blender; не заменяет тест HAN.
"""
import json
import math
import sys
from pathlib import Path

import bpy

source = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(source))
rigs = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
assert len(rigs) == 1, "ожидается один скелет"
rig = rigs[0]
assert len(rig.data.bones) == 65
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
assert len(meshes) == 1
mesh = meshes[0]
for track in rig.animation_data.nla_tracks:
    track.mute = True
rig.animation_data.action = None
rig.data.pose_position = "REST"
bpy.context.view_layer.update()
points = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
height = max(v.z for v in points) - min(v.z for v in points)
assert abs(height - 1.8) < 0.001, height
rig.data.pose_position = "POSE"
results = {}
depsgraph = bpy.context.evaluated_depsgraph_get()
for action in bpy.data.actions:
    rig.animation_data.action = action
    start, end = action.frame_range
    signatures = []
    for fraction in (0, 0.25, 0.5, 0.75, 1):
        frame = start + (end - start) * fraction
        bpy.context.scene.frame_set(int(frame), subframe=frame % 1)
        bpy.context.view_layer.update()
        evaluated = mesh.evaluated_get(depsgraph)
        data = evaluated.to_mesh()
        try:
            coords = [evaluated.matrix_world @ v.co for v in data.vertices]
            assert all(math.isfinite(c) and abs(c) < 10 for v in coords for c in v), action.name
            signatures.append(tuple(round(c, 5) for v in coords for c in v))
        finally:
            evaluated.to_mesh_clear()
    moving = len(set(signatures)) > 1
    if any(action.name.startswith(n + "_") for n in ("Walk_Loop", "Sprint_Loop", "Punch_Jab", "Punch_Cross")):
        assert moving, "клип не деформирует mesh: " + action.name
    results[action.name] = {"frames": [float(start), float(end)], "deforms_mesh": moving}
report = {"height_m": height, "bones": len(rig.data.bones), "sampled_clips": len(results), "clips": results}
source.with_suffix(".blender-check.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(f"MANNEQUIN OK: {height:.4f} m, {len(rig.data.bones)} bones, {len(results)} sampled clips")
