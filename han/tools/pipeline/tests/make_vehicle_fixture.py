"""Своя маленькая модель для проверки направления, материалов и габаритов транспортного конвейера."""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Matrix

root = Path(sys.argv[sys.argv.index('--') + 1])
(root / 'source').mkdir(parents=True, exist_ok=True)
(root / 'textures').mkdir(exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
for name, color in [('CompactBlue.png', (.1, .3, .6, 1)), ('Compact_Glossiness.png', (.75, .75, .75, 1)),
                    ('Compact_Metallic.png', (.6, .6, .6, 1)), ('wheel_C_Diffuse.png', (.1, .1, .1, 1)),
                    ('wheel_C_Glossiness.png', (.2, .2, .2, 1)), ('wheel_C_Metallic.png', (.1, .1, .1, 1))]:
    image = bpy.data.images.new(name, width=4, height=4)
    image.colorspace_settings.name = 'Non-Color'
    image.pixels = list(color) * 16
    image.filepath_raw = str(root / 'textures' / name); image.file_format = 'PNG'; image.save()
image = bpy.data.images.new('lights', width=4, height=4)
image.pixels = [1, .01, .01, 1] * 16
image.filepath_raw = str(root / 'textures/lights.jpg'); image.file_format = 'JPEG'; image.save()
materials = [bpy.data.materials.new(name) for name in ('Body', 'Glass', 'Optics', 'Wheel')]
rotation = Matrix.Rotation(math.radians(35), 4, 'Z')
for name, position, scale in [('Compact Body', (0, 0, 1), (.9, 2, .65)),
                              ('Wheel_C', (-1, -1.4, .35), (.15, .35, .35)),
                              ('Wheel_C001', (1, -1.4, .35), (.15, .35, .35)),
                              ('Wheel_C002', (-1, 1.4, .35), (.15, .35, .35)),
                              ('Wheel_C003', (1, 1.4, .35), (.15, .35, .35))]:
    bpy.ops.mesh.primitive_cube_add(size=2, location=position)
    obj = bpy.context.object; obj.name = name; obj.scale = scale
    if 'Body' in name:
        for mat in materials[:3]: obj.data.materials.append(mat)
        for index, polygon in enumerate(obj.data.polygons): polygon.material_index = index % 3
    else:
        obj.data.materials.append(materials[3])
    bpy.context.view_layer.update()
    obj.matrix_world = rotation @ obj.matrix_world
bpy.ops.export_scene.fbx(filepath=str(root / 'source' / 'fab.fbx'), use_selection=False, add_leaf_bones=False)
