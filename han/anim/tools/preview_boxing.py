"""Blender-превью бокса. Это проверка авторских поз, не кадр Vulkan-арены.
blender -b -t 4 -P han/anim/tools/preview_boxing.py -- Boxing.blend output_dir
"""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector

args=sys.argv[sys.argv.index('--')+1:]
bpy.ops.wm.open_mainfile(filepath=str(Path(args[0]).resolve()))
out=Path(args[1]).resolve();out.mkdir(parents=True,exist_ok=True)
rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
for track in rig.animation_data.nla_tracks:track.mute=True
bpy.ops.mesh.primitive_plane_add(size=200)
floor=bpy.context.object;mat=bpy.data.materials.new('Floor');mat.diffuse_color=(.08,.09,.11,1);floor.data.materials.append(mat)
bpy.ops.object.camera_add(location=(3,-5,2.7));camera=bpy.context.object
camera.rotation_euler=(Vector((0,0,1))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=2.4
scene=bpy.context.scene;scene.camera=camera;scene.render.engine='CYCLES';scene.cycles.samples=20;scene.cycles.use_denoising=False
scene.world=bpy.data.worlds.new('World');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.16,.18,.22,1)
for loc,power in [((3,4,5),700),((-3,2,3),400),((0,-3,4),800)]:
    bpy.ops.object.light_add(type='AREA',location=loc);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=4
    light.rotation_euler=(Vector((0,0,1))-light.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=400;scene.render.resolution_y=500;scene.render.resolution_percentage=100
for name,frame in [('Boxing_Guard',0),('Boxing_Jab',12),('Boxing_Cross',12),('Boxing_LeftHook',12),('Boxing_Uppercut',12),('Boxing_Block',15)]:
    rig.animation_data.action=bpy.data.actions[name+'_Armature']
    scene.frame_set(frame);bpy.context.view_layer.update()
    scene.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)
