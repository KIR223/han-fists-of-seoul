"""Авторский проход бокса на CC0 rig, без изменения CombatSim/баланса.

blender -b -P han/anim/tools/author_boxing.py -- input.glb output.blend
Jab/cross берутся из CC0 UAL; дуги hook/uppercut/overhand создаются отдельно
двухзвенным IK рук, с переносом корпуса и возвратом в защитную стойку.
Числа художественных поз — data/combat/boxing_animation.json.
"""
import json
import math
import sys
from pathlib import Path
import bpy
from mathutils import Matrix, Quaternion, Vector


def author(source, output, config_path, secondary=None):
    cfg = json.loads(config_path.read_text(encoding='utf-8'))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = cfg['fps']
    bpy.ops.import_scene.gltf(filepath=str(source))
    rig = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    for track in rig.animation_data.nla_tracks: track.mute = True
    c = Matrix.Rotation(-math.pi/2, 4, 'X')
    to_engine = c @ rig.matrix_world
    to_rig = to_engine.inverted()
    source_actions = {a.name.removesuffix('_Armature'): a for a in bpy.data.actions}
    if secondary:
        before=set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=str(secondary))
        added=set(bpy.data.objects)-before
        other=next(o for o in added if o.type=='ARMATURE')
        if set(other.data.bones.keys())!=set(rig.data.bones.keys()):
            raise ValueError('UAL2: несовместимые имена костей')
        for bone in rig.data.bones:
            difference=bone.matrix_local-other.data.bones[bone.name].matrix_local
            if max(abs(x) for row in difference for x in row)>.001:
                raise ValueError('UAL2: несовместимая bind pose '+bone.name)
        for track in other.animation_data.nla_tracks:
            for strip in track.strips:
                name=track.name
                if name in cfg.get('secondary_clips',[]):
                    strip.action.use_fake_user=True
                    strip.action.name=name+'_Armature'
                    source_actions[name]=strip.action
        for obj in added:bpy.data.objects.remove(obj,do_unlink=True)
        for action in list(bpy.data.actions):
            if not action.users:bpy.data.actions.remove(action)
    rig.animation_data.action = source_actions['Punch_Jab']
    bpy.context.scene.frame_set(0); bpy.context.view_layer.update()
    guard = {b.name: (b.location.copy(), b.rotation_quaternion.copy(), b.scale.copy()) for b in rig.pose.bones}
    bpy.context.scene.render.fps = cfg['fps']
    created = []

    def reset():
        for b in rig.pose.bones:
            b.rotation_mode = 'QUATERNION'
            b.location, b.rotation_quaternion, b.scale = guard[b.name]
        bpy.context.view_layer.update()

    def rotate(name, axis, angle):
        b = rig.pose.bones[name]
        local_axis = (to_rig.to_3x3() @ Vector(axis)).normalized()
        q = Quaternion(local_axis, angle)
        b.matrix = Matrix.LocRotScale(b.matrix.translation, q @ b.matrix.to_quaternion(), b.matrix.to_scale())
        bpy.context.view_layer.update()

    def move(name, delta):
        b=rig.pose.bones[name]; m=b.matrix.copy();m.translation += to_rig.to_3x3() @ Vector(delta);b.matrix=m
        bpy.context.view_layer.update()

    def chain(upper, lower, target, pole):
        start = to_engine @ upper.matrix.translation
        l1 = (to_engine.to_3x3() @ (upper.tail-upper.head)).length
        l2 = (to_engine.to_3x3() @ (lower.tail-lower.head)).length
        delta = Vector(target)-start
        distance = max(abs(l1-l2)+.001,min(delta.length,(l1+l2)*cfg['reach_limit']))
        direction = delta.normalized()
        axis = Vector(pole)-direction*Vector(pole).dot(direction)
        axis.normalize()
        along = (l1*l1-l2*l2+distance*distance)/(2*max(distance,.001))
        elbow = start+direction*along+axis*math.sqrt(max(0,l1*l1-along*along))
        wrist = start+direction*distance
        for bone, goal in [(upper,elbow),(lower,wrist)]:
            origin = bone.matrix.translation.copy()
            desired = (to_rig @ goal-origin).normalized()
            current = (bone.matrix.to_quaternion() @ Vector((0,1,0))).normalized()
            q=current.rotation_difference(desired) @ bone.matrix.to_quaternion()
            bone.matrix=Matrix.LocRotScale(origin,q,bone.matrix.to_scale())
            bpy.context.view_layer.update()

    def arm(side,target,pole):
        chain(rig.pose.bones['upperarm_'+side],rig.pose.bones['lowerarm_'+side],target,pole)

    def key(frame):
        for b in rig.pose.bones:
            b.keyframe_insert('location',frame=frame)
            b.keyframe_insert('rotation_quaternion',frame=frame)
            b.keyframe_insert('scale',frame=frame)

    def envelope(t): return math.sin(math.pi*max(0,min(1,t)))

    # Источники прямых ударов: пересэмплировать так, чтобы контакт был в 40% клипа.
    # Боевой binding затем отдельно совмещает этот marker с startup/active.
    for name, spec in cfg['clips'].items():
        action=bpy.data.actions.new(name+'_Armature'); action.use_fake_user=True
        frames=cfg['frames']; source_clip=spec.get('source')
        for frame in range(frames+1):
            t=frame/frames
            if source_clip:
                src=source_actions[source_clip]; a,z=src.frame_range
                contact=spec.get('source_contact',cfg['contact'])
                source_t=t if spec.get('loop') else (contact*(t/cfg['contact']) if t<=cfg['contact'] else contact+(1-contact)*(t-cfg['contact'])/(1-cfg['contact']))
                rig.animation_data.action=src
                f=a+(z-a)*source_t;bpy.context.scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update()
            else:
                rig.animation_data.action=None;reset()
            feet={side:((to_engine@rig.pose.bones['foot_'+side].matrix).translation.copy(),rig.pose.bones['foot_'+side].matrix.to_quaternion()) for side in ('l','r')}
            e=envelope(t)
            yaw=math.radians(spec.get('turn',0))*e
            if yaw:
                rotate('spine_01',(0,1,0),yaw*.35)
                rotate('spine_02',(0,1,0),yaw*.65)
            dip=spec.get('dip',0)*e
            if dip: move('pelvis',(0,-dip,0))
            if dip:
                # Опускаем таз через колени, а не погружаем ступни в пол.
                for side in ('l','r'):
                    chain(rig.pose.bones['thigh_'+side],rig.pose.bones['calf_'+side],feet[side][0],(0,0,1))
                    foot=rig.pose.bones['foot_'+side]
                    foot.matrix=Matrix.LocRotScale(foot.matrix.translation,feet[side][1],foot.matrix.to_scale())
                    bpy.context.view_layer.update()
            lean=math.radians(spec.get('lean',0))*e
            if lean: rotate('spine_01',(1,0,0),lean)
            sway=spec.get('sway',0)*e
            if sway: rotate('spine_02',(0,0,1),sway)
            if not source_clip:
                for side in ('l','r'):
                    target=Vector(spec.get('guard',cfg['guard'])[side])
                    if side==spec.get('side'):
                        # Cubic Bezier: chamber → contact → return, дуга задаётся явными точками.
                        points=[Vector(p) for p in spec['path']]
                        if t<=cfg['contact']:
                            u=t/cfg['contact']; p0,p1,p2=target,points[0],points[1]
                            target=(1-u)**2*p0+2*(1-u)*u*p1+u*u*p2
                        else:
                            u=(t-cfg['contact'])/(1-cfg['contact']);p0,p1,p2=points[1],points[2],target
                            target=(1-u)**2*p0+2*(1-u)*u*p1+u*u*p2
                    arm(side,target, cfg['elbow_poles'][side])
            else:
                inactive=['r'] if source_clip=='Punch_Jab' else ['l'] if source_clip=='Punch_Cross' else ['l','r']
                for side in inactive:arm(side,cfg['guard'][side],cfg['elbow_poles'][side])
            rig.animation_data.action=action;key(frame)
        for curve in action.fcurves:
            for k in curve.keyframe_points:k.interpolation='LINEAR'
        created.append(name)
    if cfg.get('detail'):
        from boxing_detail import add_details
        add_details(rig,source_actions,guard,cfg,to_engine,
                    {'reset':reset,'rotate':rotate,'move':move,'chain':chain,'arm':arm,'key':key})
    rig.animation_data.action=next(a for a in bpy.data.actions if a.name=='Boxing_Guard_Armature')
    bpy.context.scene.frame_set(0)
    output.parent.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    print('BOXING AUTHOR:',', '.join(created))


if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:]
    root=Path(__file__).resolve().parents[3]
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    author(Path(args[0]).resolve(),Path(args[1]).resolve(),root/'data/combat/boxing_animation.json',Path(args[2]).resolve() if len(args)>2 else None)
