"""Проверка геометрии пользовательских моделей без выполнения скриптов из ассетов.

blender --background --factory-startup --disable-autoexec --python inspect_models.py -- report.json out
Результаты и геометрические превью предназначены для аудита, не для игры.
"""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    suffix = path.suffix.lower()
    if suffix in {'.glb', '.gltf'}:
        bpy.ops.import_scene.gltf(filepath=str(path))
    elif suffix == '.fbx':
        bpy.ops.import_scene.fbx(filepath=str(path))
    elif suffix == '.obj':
        bpy.ops.wm.obj_import(filepath=str(path))
    elif suffix == '.ply':
        bpy.ops.wm.ply_import(filepath=str(path))
    elif suffix == '.stl':
        bpy.ops.wm.stl_import(filepath=str(path))
    elif suffix == '.blend':
        # Загружаем только объекты: исходные сцены, обработчики и текстовые скрипты не выполняются.
        with bpy.data.libraries.load(str(path), link=False) as (source, target):
            target.objects = source.objects
        for obj in target.objects:
            if obj is not None:
                bpy.context.scene.collection.objects.link(obj)
    else:
        raise ValueError('unsupported format: ' + suffix)
    bpy.context.view_layer.update()


def inspect(path, preview):
    load(path)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    points = [obj.matrix_world @ Vector(corner) for obj in meshes for corner in obj.bound_box]
    if not points:
        raise ValueError('no mesh geometry')
    low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
    high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
    objects = []
    for obj in meshes:
        obj.data.calc_loop_triangles()
        objects.append({'name': obj.name, 'vertices': len(obj.data.vertices),
                        'triangles': len(obj.data.loop_triangles),
                        'materials': [slot.material.name if slot.material else None for slot in obj.material_slots]})
        # Предпросмотр не должен запускать вычисления модификаторов из чужой сцены.
        for modifier in obj.modifiers:
            modifier.show_render = False
    textures = [{'name': image.name, 'path': image.filepath, 'packed': bool(image.packed_file),
                 'size': list(image.size)} for image in bpy.data.images]
    center = (low + high) / 2
    radius = max((high - low).length / 2, .01)
    camera = bpy.data.objects.new('AuditCamera', bpy.data.cameras.new('AuditCamera'))
    bpy.context.scene.collection.objects.link(camera)
    camera.location = center + Vector((1.25, -1.65, 1.15)).normalized() * radius * 3.5
    camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = radius * 2.3
    camera.data.clip_end = max(radius * 12, 100)
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.background_type = 'WORLD'
    scene.world = bpy.data.worlds.new('AuditWorld')
    scene.world.color = (.075, .075, .075)
    scene.render.resolution_x = 480
    scene.render.resolution_y = 360
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.filepath = str(preview)
    bpy.ops.render.render(write_still=True)
    return {'objects': objects, 'size_blender': list(high - low), 'textures': textures,
            'triangles': sum(obj['triangles'] for obj in objects), 'preview': str(preview)}


def main():
    args = sys.argv[sys.argv.index('--') + 1:]
    records = json.loads(Path(args[0]).read_text(encoding='utf-8'))
    out = Path(args[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    index = 0
    for pack in records:
        for model in pack.get('models', []):
            index += 1
            marker = out / f'{index:04d}.json'
            if marker.exists():
                continue
            result = {'pack': pack['relative'], 'file': model['file'], 'source': str(Path(pack['folder']) / model['file'])}
            try:
                result.update(inspect(Path(result['source']), out / f'{index:04d}.png'))
            except Exception as error:
                result['error'] = str(error)
            marker.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            print(f'HAN_AUDIT {index} {result["file"]} {result.get("error", "OK")}', flush=True)


if __name__ == '__main__':
    main()
