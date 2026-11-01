"""Генератор тестовой модели blender_textured_cube.glb (куб с шахматной текстурой 64x64) для тестов импорта glTF.

    blender --background --python make_textured_fixture.py -- <выход.glb>

Текстура: клетки 8x8 пикселей красный/белый, левый верхний угол 8x8 — синий (по нему тест проверяет ориентацию изображения).
Используются только чистые значения каналов 0 и 1, поэтому цветовое пространство не искажает результат.
"""
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)

bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 1))
obj = bpy.context.active_object

size = 64
img = bpy.data.images.new("checker", size, size, alpha=False)
pixels = []
for y in range(size):  # в Blender строка 0 — нижняя
    for x in range(size):
        if x < 8 and y >= size - 8:
            pixels.extend((0.0, 0.0, 1.0, 1.0))  # синий: левый верхний угол изображения
        elif ((x // 8) + (y // 8)) % 2 == 0:
            pixels.extend((1.0, 1.0, 1.0, 1.0))
        else:
            pixels.extend((1.0, 0.0, 0.0, 1.0))
img.pixels = pixels
img.pack()

mat = bpy.data.materials.new("checker_material")
mat.use_nodes = True
bsdf = mat.node_tree.nodes["Principled BSDF"]
tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = img
mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
obj.data.materials.append(mat)

bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True)
print("HAN: сохранено", out)
