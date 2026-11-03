"""blender -b -P han/anim/tools/check_boxing.py -- Boxing.blend

Контактные проверки реальной геометрии, а не только имён клипов:
падение→подъём без скачка, пол, конечные координаты, опорная стопа в shuffle.
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
c=Matrix.Rotation(-math.pi/2,4,'X');world=c@rig.matrix_world
def pose(name,t):
    rig.animation_data.action=bpy.data.actions[name+'_Armature'];start,end=rig.animation_data.action.frame_range
    f=start+(end-start)*t;bpy.context.scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update()
    obj=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());data=obj.to_mesh()
    try:return [(c@obj.matrix_world@v.co).copy() for v in data.vertices]
    finally:obj.to_mesh_clear()
report={'transitions':{},'ground':{},'gait':{}}
for side in ('Back','Front','Left','Right'):
    a=pose('Boxing_Fall'+side,1);b=pose('Boxing_GetUp'+side,0)
    error=max((x-y).length for x,y in zip(a,b))
    assert error<.003,(side,'fall/getup discontinuity',error)
    report['transitions'][side]=error
    for prefix in ('Boxing_Fall','Boxing_GetUp'):
        low=1e9
        for i in range(17):
            points=pose(prefix+side,i/16)
            assert all(math.isfinite(v) and abs(v)<5 for p in points for v in p),(prefix,side,'invalid geometry')
            low=min(low,min(p.y for p in points))
        assert low>-.006,(prefix,side,'floor penetration',low)
        report['ground'][prefix+side]=low
root=Path(__file__).resolve().parents[3]
cfg=json.loads((root/'data/combat/boxing_animation.json').read_text())
cycle=cfg['detail']['gait']['stride']/cfg['detail']['gait']['plant_ratio']
for name,spec in cfg['detail']['clips'].items():
    if spec['kind']!='gait':continue
    points=[];width=10
    for t in (0,.05,.1,.15,.2,.3,.4,.5):
        pose(name,t)
        left=(world@rig.pose.bones['foot_l'].matrix).translation
        right=(world@rig.pose.bones['foot_r'].matrix).translation
        width=min(width,left.x-right.x)
        if t<=.5 and spec.get('phase',0)==0:points.append(left+Vector(spec['direction'])*cycle*t)
    assert width>.11,(name,'feet cross',width)
    drift=max(((p-points[0]).length for p in points),default=0)
    assert drift<.025,(name,'planted foot slides',drift)
    report['gait'][name]={'min_width':width,'plant_drift':drift}
path.with_suffix('.contact-check.json').write_text(json.dumps(report,indent=2)+'\n')
print('BOXING CONTACT OK: fall/getup continuous, floor contacts, 8-way planted feet')
