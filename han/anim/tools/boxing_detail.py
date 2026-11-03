"""Контактный проход бокса: варианты, опора ног, реакции, падения и подъёмы.

Все параметры/маркеры — boxing_animation.json. Это офлайн-авторинг на CC0 rig,
не motion capture и не замена физического active ragdoll.
"""
import math
import bpy
from mathutils import Matrix, Vector


def add_details(rig, sources, guard, cfg, world, tools):
    detail=cfg['detail'];inv=world.inverted();fps=cfg['fps']
    reset,rotate,move,chain,arm,key=[tools[n] for n in ('reset','rotate','move','chain','arm','key')]
    posebones=rig.pose.bones
    def smooth(t):
        t=max(0,min(1,t));return t*t*(3-2*t)
    def pulse(t,end=1):return math.sin(math.pi*max(0,min(1,t/end))) if t<end else 0
    def capture():return {b.name:(b.location.copy(),b.rotation_quaternion.copy(),b.scale.copy()) for b in posebones}
    def apply(pose):
        rig.animation_data.action=None
        for b in posebones:b.location,b.rotation_quaternion,b.scale=pose[b.name]
        bpy.context.view_layer.update()
    def mix(a,b,t):
        return {name:(a[name][0].lerp(b[name][0],t),a[name][1].slerp(b[name][1],t),a[name][2].lerp(b[name][2],t)) for name in a}
    def sample(name,t):
        action=sources.get(name) or bpy.data.actions.get(name+'_Armature')
        if action is None:raise ValueError('нет исходника '+name)
        rig.animation_data.action=action
        a,z=action.frame_range;f=a+(z-a)*max(0,min(1,t))
        bpy.context.scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update();return capture()
    def feet():
        return {side:((world@posebones['foot_'+side].matrix).translation.copy(),posebones['foot_'+side].matrix.to_quaternion()) for side in ('l','r')}
    def plant(anchors):
        for side,(target,q) in anchors.items():
            chain(posebones['thigh_'+side],posebones['calf_'+side],target,(0,0,1))
            b=posebones['foot_'+side];b.matrix=Matrix.LocRotScale(b.matrix.translation,q,b.matrix.to_scale());bpy.context.view_layer.update()
    def high_guard():
        for side in ('l','r'):arm(side,cfg['guard'][side],cfg['elbow_poles'][side])
    root=next(b.name for b in rig.data.bones if b.parent is None)
    mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers))
    # Только для клипов контакта с землёй. Проверяем поверхность mesh, не точку pelvis.
    def ground():
        deps=bpy.context.evaluated_depsgraph_get();obj=mesh.evaluated_get(deps);data=obj.to_mesh()
        try:
            mat=Matrix.Rotation(-math.pi/2,4,'X')@obj.matrix_world
            low=min((mat@v.co).y for v in data.vertices)
        finally:obj.to_mesh_clear()
        # Skin collision margin компенсирует нелинейную деформацию между TRS-ключами.
        move(root,(0,detail['floor_clearance']-low,0))
    def settle_xz():
        p=(world@posebones['pelvis'].matrix).translation
        move(root,(-p.x,0,-p.z+detail['pelvis_anchor_z']))
    # Снимок стойки с сохранением хватов пальцев; руки подняты к щекам.
    reset();high_guard();base=capture();basefeet=feet()
    lay=sample('LayToIdle',0)
    # Сохраняем источники authored-actions, прежде чем заменить некоторые из них.
    generated={}
    for name in detail['clips']:
        old=bpy.data.actions.get(name+'_Armature')
        if old:
            old.name=name+'_Previous'
            generated[name]=old
    sources={**sources,**generated}
    for name,spec in detail['clips'].items():
        kind=spec['kind'];duration=spec.get('seconds',1);count=max(2,round(duration*fps))
        action=bpy.data.actions.new(name+'_Armature');action.use_fake_user=True
        for frame in range(count+1):
            t=frame/count
            if kind=='variant':
                apply(sample(spec['base'],t));anchors=feet()
                e=pulse(t,spec.get('prelude_end',1))
                if spec.get('dip'):move('pelvis',(0,-spec['dip']*e,0));plant(anchors)
                if spec.get('lean'):rotate('spine_01',(1,0,0),math.radians(spec['lean'])*e)
                if spec.get('sway'):rotate('spine_02',(0,0,1),spec['sway']*e)
                if spec.get('hip_turn'):
                    rotate('pelvis',(0,1,0),math.radians(spec['hip_turn'])*pulse(t));plant(anchors)
            elif kind=='series':
                # Отдельные контакты каждой руки. Нет визуальных ударов без события.
                section=next((s for s in spec['sections'] if t<=s['end']),spec['sections'][-1])
                start,contact,end=section['start'],section['contact'],section['end']
                u=.4*(t-start)/(contact-start) if t<=contact else .4+.6*(t-contact)/(end-contact)
                apply(sample(section['base'],u));anchors=feet()
                if spec.get('roll'):
                    wave=math.sin(t*math.pi*len(spec['sections'])*2)
                    rotate('spine_02',(0,0,1),spec['roll']*wave)
                    move('pelvis',(0,-spec['dip']*abs(wave),0));plant(anchors)
            elif kind=='gait':
                apply(base);anchors={side:(p.copy(),q.copy()) for side,(p,q) in basefeet.items()}
                center=(anchors['l'][0].x+anchors['r'][0].x)*.5
                for side,sign in [('l',1),('r',-1)]:anchors[side][0].x=center+sign*detail['gait']['stance_width']*.5
                direction=Vector(spec['direction']);stance=detail['gait']['plant_ratio'];stride=detail['gait']['stride']
                for i,side in enumerate(('l','r')):
                    phase=(t+i*.5+spec.get('phase',0))%1
                    if phase<stance:
                        distance=stride*(.5-phase/stance);lift=0
                    else:
                        u=(phase-stance)/(1-stance);distance=stride*(-.5+smooth(u));lift=detail['gait']['lift']*math.sin(math.pi*u)
                    anchors[side]=(anchors[side][0]+direction*distance+Vector((0,lift,0)),anchors[side][1])
                move('pelvis',(0,-detail['gait']['knee_flex']+detail['gait']['bob']*(math.cos(t*4*math.pi)-1),0));plant(anchors)
                rotate('spine_02',(0,1,0),detail['gait']['torso_sway']*math.sin(t*2*math.pi));high_guard()
            elif kind in ('guard','clinch'):
                apply(base);anchors=feet();breath=detail['breathing']*math.sin(t*2*math.pi)
                rotate('spine_02',(1,0,0),breath);plant(anchors);high_guard()
                if kind=='clinch':
                    for side in ('l','r'):arm(side,spec['hands'][side],cfg['elbow_poles'][side])
            elif kind=='reaction':
                apply(base);anchors=feet();e=math.exp(-detail['reaction_decay']*t)*math.sin(min(1,t/detail['reaction_peak'])*math.pi*.5)
                rotate('spine_01',(1,0,0),math.radians(spec.get('lean',0))*e)
                rotate('spine_02',(0,1,0),math.radians(spec.get('turn',0))*e)
                rotate('neck_01',(0,1,0),math.radians(spec.get('head_turn',0))*e)
                rotate('neck_01',(1,0,0),math.radians(spec.get('head_pitch',0))*e)
                move('pelvis',(0,-spec.get('dip',0)*e,0));plant(anchors);high_guard()
            elif kind in ('fall','getup'):
                if kind=='fall':
                    # Начальная защитная поза → аниматорский knockback → та же
                    # опорная поза, из которой действительно начинается подъём.
                    impact=detail['fall']['impact'];settle=detail['fall']['settle']
                    u=min(1,max(0,(t-impact)/(settle-impact)))
                    # Основная потеря опоры занимает большую часть времени падения.
                    land=detail['fall']['land_time'];source_land=detail['fall']['source_land']
                    source_t=source_land*u/land if u<land else source_land+(1-source_land)*(u-land)/(1-land)
                    src=sample(detail['fall']['source'],source_t)
                    if t<impact:pose=mix(base,src,smooth(t/impact))
                    elif t>settle:pose=mix(src,lay,smooth((t-settle)/(1-settle)))
                    else:pose=src
                    apply(pose)
                    turn=spec.get('roll',0)*smooth(t/settle)
                else:
                    src=sample('LayToIdle',smooth(t))
                    pose=mix(src,base,smooth((t-detail['getup']['guard_start'])/(1-detail['getup']['guard_start'])))
                    apply(pose)
                    # С бокового положения сначала перекатиться на опору, затем
                    # подняться через руку/колено исходного клипа (не reverse fall).
                    turn=spec.get('roll',0)*(1-smooth(t/detail['getup']['roll_end']))
                if turn:rotate('pelvis',(0,0,1),math.radians(turn))
                settle_xz();ground()
            elif kind=='defense':
                apply(base);anchors=feet();e=pulse(t)
                move('pelvis',(0,-spec.get('dip',0)*e,0));plant(anchors)
                rotate('spine_01',(1,0,0),math.radians(spec.get('lean',0))*e)
                rotate('spine_02',(0,0,1),spec.get('sway',0)*e)
                if spec.get('bob'):rotate('spine_02',(0,0,1),spec['bob']*math.sin(t*2*math.pi))
                rotate('spine_02',(0,1,0),math.radians(spec.get('turn',0))*e);high_guard()
            else:raise ValueError('неизвестный detail kind '+kind)
            rig.animation_data.action=action;key(frame)
        for curve in action.fcurves:
            for k in curve.keyframe_points:k.interpolation='LINEAR'
    rig.animation_data.action=None
    for old in generated.values():bpy.data.actions.remove(old)
    print('BOXING DETAIL:',len(detail['clips']),'clips')
