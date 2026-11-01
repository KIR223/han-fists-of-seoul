"""Тестовая модель для конвейера: высокополигональная «бочка» в сантиметрах, без UV, 3 материала, скрытый объект, «бренды» в именах."""
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)

bpy.ops.mesh.primitive_uv_sphere_add(segments=128, ring_count=64, radius=50, location=(0, 0, 50))
body = bpy.context.active_object
body.name = "Coca_Barrel_Body"
for layer in list(body.data.uv_layers):
    body.data.uv_layers.remove(layer)
for i, color in enumerate(((0.8, 0.1, 0.1, 1), (0.1, 0.6, 0.1, 1), (0.1, 0.1, 0.8, 1))):
    mat = bpy.data.materials.new(f"Material_{i}")
    mat.diffuse_color = color
    body.data.materials.append(mat)
for poly in body.data.polygons:
    poly.material_index = poly.index % 3

bpy.ops.mesh.primitive_cube_add(size=40, location=(120, 0, 20))
cube = bpy.context.active_object
cube.name = "Samsung_Crate"

bpy.ops.mesh.primitive_cube_add(size=10, location=(0, 0, 200))
hidden = bpy.context.active_object
hidden.name = "Hidden_Junk"
hidden.hide_viewport = True

bpy.ops.object.empty_add(location=(0, 0, 0))
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, use_visible=False)
print("HAN: тестовая модель:", out)
