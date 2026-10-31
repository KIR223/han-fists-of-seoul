"""Конвейер переделки статичной модели в Blender (часть 76.4, шаги 1–3, 7, 9). Запуск (его делает han_fetch.py process):

    blender --background --python han_pipeline.py -- <params.json>

params.json: input, output_dir, name, report, pipeline {scale, pivot, budget_triangles, max_materials, max_texture_size, lods,
collision}, brand_blocklist (путь к списку слов). Результат в output_dir: <name>.glb (LOD0), <name>.lod1.glb …, <name>.col.glb
и report.json. Модели с костями (персонажи) не поддерживаются: для них нужен отдельный конвейер с ретаргетом.

Шаги «новые текстуры», «корёизация» и «чистка брендов» требуют решения человека: скрипт лишь ищет подозрительные имена
и выносит их в отчёт (brand_hits); логотипы внутри картинок надо смотреть глазами.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

MAX_COLLISION_VERTS = 32


def fail(message):
    print("HAN-ОШИБКА:", message)
    raise SystemExit(1)


def read_params():
    if "--" not in sys.argv:
        fail("использование: blender --background --python han_pipeline.py -- params.json")
    with open(sys.argv[sys.argv.index("--") + 1], encoding="utf-8") as f:
        return json.load(f)


def mesh_objects():
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def import_model(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ext = os.path.splitext(path)[1].lower()
    if ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)
    elif ext == ".obj":
        bpy.ops.wm.obj_import(filepath=path)
    else:
        fail(f"формат не поддерживается: {ext}")


def triangle_count(obj):
    mesh = obj.data
    mesh.calc_loop_triangles()
    return len(mesh.loop_triangles)


def evaluated_copy(obj):
    """Меш с применёнными модификаторами без использования операторов (не зависит от контекста)."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    return bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), depsgraph=depsgraph)


def apply_decimate(obj, ratio):
    mod = obj.modifiers.new("HanDecimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = max(0.001, min(1.0, ratio))
    mod.use_collapse_triangulate = True
    new_mesh = evaluated_copy(obj)
    obj.modifiers.remove(mod)
    old = obj.data
    obj.data = new_mesh
    if old.users == 0:
        bpy.data.meshes.remove(old)


def decimate_to(obj, target):
    """Упрощает до target треугольников (с запасом итераций: Decimate не бьёт в число точно)."""
    for _ in range(6):
        tris = triangle_count(obj)
        if tris <= target:
            return
        apply_decimate(obj, target / tris * 0.97)


def normalize(scale, warnings):
    """Шаг 1: иерархия → плоский список мешей в метрах, без скрытых и лишних объектов, нормали наружу."""
    if any(o.type == "ARMATURE" for o in bpy.context.scene.objects):
        fail("в модели есть кости: этот конвейер для статичных моделей (пропсы, здания, машины без скелета)")

    for obj in mesh_objects():
        obj.data = obj.data.copy()  # каждому объекту свои данные: масштаб не применится дважды к общим мешам
        if obj.parent:
            world = obj.matrix_world.copy()
            obj.parent = None
            obj.matrix_world = world

    for obj in list(bpy.context.scene.objects):
        hidden = obj.hide_viewport or obj.hide_get() or obj.hide_render
        if obj.type != "MESH" or hidden:
            if obj.type == "MESH":
                warnings.append(f"удалён скрытый объект {obj.name}")
            bpy.data.objects.remove(obj, do_unlink=True)

    meshes = mesh_objects()
    if not meshes:
        fail("в модели нет видимых мешей")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    for obj in meshes:
        if scale != 1.0:
            obj.data.transform(Matrix.Scale(scale, 4))
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        loose = [v for v in bm.verts if not v.link_edges]
        if loose:
            bmesh.ops.delete(bm, geom=loose, context="VERTS")
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()


def join_all(name):
    meshes = mesh_objects()
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = name
    return obj


def set_pivot_base_center(obj):
    """Пивот в центре основания (часть 48.1): центр по X/Y, низ по Z (в glTF это X/Z и Y)."""
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    center_x = (min(c.x for c in corners) + max(c.x for c in corners)) / 2
    center_y = (min(c.y for c in corners) + max(c.y for c in corners)) / 2
    bottom = min(c.z for c in corners)
    obj.data.transform(Matrix.Translation(Vector((-center_x, -center_y, -bottom))))
    obj.data.update()


def ensure_uv(obj, warnings):
    if obj.data.uv_layers:
        return True
    warnings.append("у модели не было UV — построена автоматическая развёртка (Smart UV Project)")
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    return False


def shrink_textures(max_size, warnings):
    for image in bpy.data.images:
        if image.size[0] == 0 or image.size[1] == 0:
            continue
        w, h = image.size
        if max(w, h) > max_size:
            k = max_size / max(w, h)
            new_w, new_h = max(1, int(w * k)), max(1, int(h * k))
            image.scale(new_w, new_h)
            warnings.append(f"текстура {image.name} уменьшена {w}x{h} -> {new_w}x{new_h}")


def material_count(obj):
    return len({slot.material for slot in obj.material_slots if slot.material})


def export_object(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=True)


def copy_object(obj, name):
    dup = obj.copy()
    dup.data = obj.data.copy()
    dup.name = name
    bpy.context.scene.collection.objects.link(dup)
    return dup


def make_collision(obj, name):
    """Выпуклая оболочка не более 32 вершин (часть 48.1); без материалов, чтобы в файл не попали текстуры."""
    hull = copy_object(obj, name)
    hull.data.materials.clear()
    # Оболочка из тысяч точек получается с лишними гранями, и упрощать её бесполезно. Сначала упрощаем исходную чистую сетку.
    decimate_to(hull, 256)
    for _ in range(12):
        bm = bmesh.new()
        bm.from_mesh(hull.data)
        result = bmesh.ops.convex_hull(bm, input=bm.verts, use_existing_faces=False)
        bmesh.ops.delete(bm, geom=result["geom_interior"] + result["geom_unused"], context="VERTS")
        bm.to_mesh(hull.data)
        count = len(bm.verts)
        bm.free()
        hull.data.update()
        if count <= MAX_COLLISION_VERTS:
            return hull, count
        apply_decimate(hull, MAX_COLLISION_VERTS / count * 0.9)
    fail("не удалось упростить коллизию до 32 вершин")


def scan_brands(blocklist_path):
    """Имена объектов, материалов, картинок и узлов, похожие на реальные бренды (часть 76.4, шаг 6)."""
    words = []
    if blocklist_path and os.path.exists(blocklist_path):
        with open(blocklist_path, encoding="utf-8") as f:
            words = [line.strip().lower() for line in f if line.strip() and not line.startswith("#")]
    names = {o.name for o in bpy.data.objects} | {m.name for m in bpy.data.materials} | {i.name for i in bpy.data.images}
    names |= {os.path.basename(i.filepath) for i in bpy.data.images if i.filepath}
    for material in bpy.data.materials:
        if material.use_nodes and material.node_tree:
            names |= {n.label or n.name for n in material.node_tree.nodes}
    hits = []
    for name in sorted(names):
        low = name.lower()
        for word in words:
            if word in low:
                hits.append({"name": name, "word": word})
    return hits


def main():
    params = read_params()
    cfg = params["pipeline"]
    out_dir = params["output_dir"]
    name = params["name"]
    os.makedirs(out_dir, exist_ok=True)
    warnings = []

    import_model(params["input"])
    brand_hits = scan_brands(params.get("brand_blocklist"))
    normalize(float(cfg.get("scale", 1.0)), warnings)
    obj = join_all(name)
    if cfg.get("pivot", "base_center") == "base_center":
        set_pivot_base_center(obj)
    has_uv = ensure_uv(obj, warnings)
    shrink_textures(int(cfg.get("max_texture_size", 1024)), warnings)

    original_tris = triangle_count(obj)
    budget = int(cfg["budget_triangles"])
    decimate_to(obj, budget)
    triangles = {"lod0": triangle_count(obj)}
    if original_tris > budget:
        warnings.append(f"упрощено с {original_tris} до {triangles['lod0']} треугольников (бюджет {budget})")

    materials = material_count(obj)
    if materials > int(cfg.get("max_materials", 2)):
        warnings.append(f"материалов {materials} при допустимых {cfg.get('max_materials', 2)}: слить через запекание в атлас вручную")

    files = {"lod0": f"{name}.glb"}
    export_object(obj, os.path.join(out_dir, files["lod0"]))

    for index, ratio in enumerate(cfg.get("lods", []), start=1):
        lod = copy_object(obj, f"{name}_lod{index}")
        apply_decimate(lod, float(ratio))
        key = f"lod{index}"
        triangles[key] = triangle_count(lod)
        files[key] = f"{name}.lod{index}.glb"
        export_object(lod, os.path.join(out_dir, files[key]))

    collision_verts = None
    if cfg.get("collision", True):
        hull, collision_verts = make_collision(obj, f"{name}_col")
        files["collision"] = f"{name}.col.glb"
        export_object(hull, os.path.join(out_dir, files["collision"]))

    corners = [Vector(c) for c in obj.bound_box]
    size_blender = [max(c[i] for c in corners) - min(c[i] for c in corners) for i in range(3)]
    report = {
        "name": name,
        "triangles": triangles,
        "original_triangles": original_tris,
        "budget_triangles": budget,
        "materials": materials,
        "has_uv": has_uv,
        "size_meters": {"x": size_blender[0], "y": size_blender[2], "z": size_blender[1]},  # в осях glTF: Y вверх
        "collision_vertices": collision_verts,
        "brand_hits": brand_hits,
        "warnings": warnings,
        "files": files,
    }
    with open(params["report"], "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    print("HAN: готово", name, triangles)


main()
