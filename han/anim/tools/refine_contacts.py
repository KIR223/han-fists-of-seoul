"""Уточняет только вертикальную root-кривую контакта с полом на 240 Гц.

Остальные суставы/позы не меняются. Коррекция считается из неизменённого клипа,
затем записывается целиком: предыдущий исправленный ключ не влияет на следующий.
"""
import json
import math
import sys
from pathlib import Path
import bpy
from mathutils import Matrix,Vector

path=Path(sys.argv[sys.argv.index('--')+1]).resolve()
bpy.ops.wm.open_mainfile(filepath=str(path))
rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
for track in rig.animation_data.nla_tracks:track.mute=True
mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers))
root=next(b for b in rig.data.bones if b.parent is None)
c=Matrix.Rotation(-math.pi/2,4,'X');world=c@rig.matrix_world
to_basis=root.matrix_local.to_3x3().inverted()@world.to_3x3().inverted()
cfg=json.loads((Path(__file__).resolve().parents[3]/'data/combat/boxing_animation.json').read_text())
margin=cfg['detail']['floor_clearance'];rate=cfg['detail']['contact_rate']
for action in list(bpy.data.actions):
    if not action.name.startswith(('Boxing_Fall','Boxing_GetUp')):continue
    rig.animation_data.action=action;start,end=action.frame_range
    samples=max(2,round((end-start)*rate/bpy.context.scene.render.fps))
    values=[]
    for i in range(samples+1):
        frame=start+(end-start)*i/samples
        bpy.context.scene.frame_set(int(frame),subframe=frame%1);bpy.context.view_layer.update()
        obj=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());data=obj.to_mesh()
        try:
            mat=c@obj.matrix_world;low=min((mat@v.co).y for v in data.vertices)
        finally:obj.to_mesh_clear()
        location=rig.pose.bones[root.name].location.copy()+to_basis@Vector((0,margin-low,0))
        values.append((frame,location))
    data_path=f'pose.bones["{root.name}"].location'
    for curve in list(action.fcurves):
        if curve.data_path==data_path:action.fcurves.remove(curve)
    for axis in range(3):
        curve=action.fcurves.new(data_path,index=axis,action_group=root.name)
        curve.keyframe_points.add(len(values))
        for key,(frame,location) in zip(curve.keyframe_points,values):key.co=(frame,location[axis]);key.interpolation='LINEAR'
        curve.update()
    print('CONTACT REFINED',action.name,len(values),flush=True)
bpy.ops.wm.save_as_mainfile(filepath=str(path))
