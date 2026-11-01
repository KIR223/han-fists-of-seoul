"""Конвертация FBX в GLB для han_cooker (E3, часть 76 дизайна). Запуск:

    blender --background --python fbx_to_glb.py -- <вход.fbx> <выход.glb>

Что делает: очищает сцену, импортирует FBX, применяет масштаб и поворот объектов (чтобы в GLB лежали метры и ось Y вверх),
экспортирует GLB. Дальнейшая «корёизация», упрощение, LOD и коллизии — отдельные скрипты (этап E20).
"""
import sys

import bpy


def parse_args():
    if "--" not in sys.argv:
        raise SystemExit("использование: blender --background --python fbx_to_glb.py -- вход.fbx выход.glb")
    args = sys.argv[sys.argv.index("--") + 1:]
    if len(args) != 2:
        raise SystemExit("нужны два аргумента: вход.fbx и выход.glb")
    return args


def main():
    source, target = parse_args()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=source)

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("в файле нет мешей")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    bpy.ops.export_scene.gltf(filepath=target, export_format="GLB", export_apply=True, export_yup=True)
    print("HAN: сохранено", target, "мешей:", len(meshes))


main()
