"""Подготовка лицензированного транспортного набора: метры, направление, восстановление PBR-карт."""
import json
import math
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix


def prepare_vehicle(input_path, profile_path, objects):
    profile = json.loads(Path(profile_path).read_text(encoding='utf-8'))
    if profile.get('version') != 1 or not objects:
        raise ValueError('invalid vehicle profile')
    body = next((obj for obj in objects if obj.name == profile['body_object']), None)
    if body is None:
        raise ValueError('vehicle body object is missing')
    angle = -body.matrix_world.to_euler().z + math.radians(profile['yaw_degrees'])
    rotation = Matrix.Rotation(angle, 4, 'Z')
    for obj in objects:
        obj.matrix_world = rotation @ obj.matrix_world
    root = Path(input_path).parent.parent
    files = {}
    for path in root.rglob('*'):
        if path.is_file():
            files.setdefault(path.name.lower(), []).append(path)

    def image(name, linear=False):
        found = files.get(name.lower(), [])
        if len(found) != 1:
            raise ValueError('missing/ambiguous vehicle texture: ' + name)
        pixels = bpy.data.images.load(str(found[0]), check_existing=False)
        if not pixels.size[0] or not pixels.size[1]:
            raise ValueError('unreadable vehicle texture: ' + name)
        if linear:
            pixels.colorspace_settings.name = 'Non-Color'
        return pixels

    def material(name, base_name, gloss_name=None, metal_name=None, opacity=1, roughness=.5, emission=0):
        result = bpy.data.materials.new('HAN_' + name)
        result.use_nodes = True
        nodes, links = result.node_tree.nodes, result.node_tree.links
        shader = nodes.get('Principled BSDF')
        base = nodes.new('ShaderNodeTexImage'); base.image = image(base_name)
        links.new(base.outputs['Color'], shader.inputs['Base Color'])
        shader.inputs['Roughness'].default_value = roughness
        shader.inputs['Alpha'].default_value = opacity
        if opacity < 1:
            result.surface_render_method = 'BLENDED'
        if emission:
            links.new(base.outputs['Color'], shader.inputs['Emission Color'])
            shader.inputs['Emission Strength'].default_value = emission
        if gloss_name and metal_name:
            gloss, metal = image(gloss_name, True), image(metal_name, True)
            if tuple(gloss.size) != tuple(metal.size):
                raise ValueError('glossiness and metallic maps differ in size')
            width, height = gloss.size
            g, m = np.empty(width * height * 4, np.float32), np.empty(width * height * 4, np.float32)
            gloss.pixels.foreach_get(g); metal.pixels.foreach_get(m)
            orm = np.ones((width * height, 4), dtype=np.float32)
            orm[:, 1] = 1 - np.clip(g.reshape(-1, 4)[:, 0], 0, 1)
            orm[:, 2] = np.clip(m.reshape(-1, 4)[:, 0], 0, 1)
            packed = bpy.data.images.new('HAN_' + name + '_ORM', width=width, height=height)
            packed.colorspace_settings.name = 'Non-Color'
            packed.pixels.foreach_set(orm.reshape(-1)); packed.pack()
            texture = nodes.new('ShaderNodeTexImage'); texture.image = packed
            channels = nodes.new('ShaderNodeSeparateColor')
            links.new(texture.outputs['Color'], channels.inputs['Color'])
            links.new(channels.outputs['Green'], shader.inputs['Roughness'])
            links.new(channels.outputs['Blue'], shader.inputs['Metallic'])
        return result

    prefix, wheel = profile['body_texture_prefix'], profile['wheel_texture_prefix']
    materials = {
        'body': material('Body', profile['albedo'], prefix + '_Glossiness.png', prefix + '_Metallic.png'),
        'glass': material('Glass', profile['albedo'], opacity=profile['glass_opacity'], roughness=profile['glass_roughness']),
        'optics': material('Optics', profile['optics_albedo'], roughness=profile['optics_roughness'], emission=profile['optics_emission']),
        'wheel': material('Wheel', wheel + '_Diffuse.png', wheel + '_Glossiness.png', wheel + '_Metallic.png'),
    }
    for obj in objects:
        for slot in obj.material_slots:
            old = slot.material.name.lower() if slot.material else ''
            role = next((kind for kind in ('body', 'glass', 'optics', 'wheel') if kind in old), None)
            if role is None:
                raise ValueError('unrecognised vehicle material: ' + old)
            slot.material = materials[role]
    bpy.context.view_layer.update()
