"""Декор существующих помещений: размещать только на свободном полу, оставить проходы к дверям."""
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix,Vector
from mathutils.bvhtree import BVHTree

root=Path(__file__).resolve().parents[3]
config=json.loads((root/sys.argv[sys.argv.index('--')+1]).read_text(encoding='utf-8'))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(root/config['source']))
vertices=[]; triangles=[]
for obj in list(bpy.context.scene.objects):
    if obj.type!='MESH': continue
    first=len(vertices); obj.data.calc_loop_triangles()
    vertices.extend(obj.matrix_world@v.co for v in obj.data.vertices)
    triangles.extend(tuple(first+i for i in t.vertices) for t in obj.data.loop_triangles)
geometry=BVHTree.FromPolygons(vertices,triangles,all_triangles=True)
occupied=[]; additions=[]


def clear(x,z,floor,radius,height):
    if any(math.hypot(x-px,z-pz)<radius+pr for px,pz,pr in occupied): return False
    for dx,dz in [(0,0),(radius,0),(-radius,0),(0,radius),(0,-radius)]:
        position=Vector((x+dx,-z-dz,floor+0.12))
        hit,_,_,_=geometry.ray_cast(position,Vector((0,0,1)),height)
        if hit is not None: return False
        hit,normal,_,_=geometry.ray_cast(position,Vector((0,0,-1)),0.4)
        if hit is None or normal.z<0.8: return False
    return True


def add(source,x,z,floor,height,label):
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(root/source))
    meshes=[obj for obj in bpy.data.objects if obj not in before and obj.type=='MESH']
    points=[obj.matrix_world@Vector(v) for obj in meshes for v in obj.bound_box]
    low=Vector(tuple(min(p[i] for p in points) for i in range(3)))
    high=Vector(tuple(max(p[i] for p in points) for i in range(3)))
    scale=height/(high.z-low.z)
    transform=Matrix.Translation(Vector((x,-z,floor)))@Matrix.Scale(scale,4)@Matrix.Translation(Vector((-(low.x+high.x)/2,-(low.y+high.y)/2,-low.z)))
    for obj in meshes:
        world=obj.matrix_world.copy(); obj.parent=None
        obj.matrix_world=transform@world; obj.name=label+'_'+obj.name
    additions.append({'source':source,'label':label,'position':[x,floor,z],'height':height})


for index,room in enumerate(json.loads((root/config['metadata']).read_text(encoding='utf-8'))['interiors']):
    polygon=room['footprint']; margin=config['wall_clearance']
    left=min(p[0] for p in polygon)+margin; right=max(p[0] for p in polygon)-margin
    top=min(p[1] for p in polygon)+margin; bottom=max(p[1] for p in polygon)-margin
    floor=room['floor_y']; door=room['door']
    candidates=[(left+(right-left)*u,top+(bottom-top)*v) for u in config['grid_fractions'] for v in config['grid_fractions']]
    def place(source,radius,height,label):
        for x,z in candidates:
            if math.hypot(x-door[0],z-door[2])<config['door_clearance']+radius: continue
            if clear(x,z,floor,radius,height):
                add(source,x,z,floor,height,label); occupied.append((x,z,radius)); return True
        return False
    plants=config.get('plants',[config['plant']])
    place(plants[index%len(plants)],config['plant_radius'],config['plant_height'],f'room_{index}_plant')
    if room['type']=='cafe':
        place(config['table'],config['furniture_radius'],config['table_height'],f'room_{index}_table')
        place(config['stool'],config['furniture_radius'],config['stool_height'],f'room_{index}_stool')
    surface_spec=config.get('surface_items',{}).get(room['type'])
    if surface_spec:
        placed=0
        for u in config.get('surface_grid_fractions',[]):
            for v in config.get('surface_grid_fractions',[]):
                if placed>=surface_spec['count']: break
                x=left+(right-left)*u; z=top+(bottom-top)*v
                if any(math.hypot(x-px,z-pz)<pr+surface_spec['radius'] for px,pz,pr in occupied): continue
                hit,normal,_,_=geometry.ray_cast(Vector((x,-z,floor+config['surface_max_height'])),Vector((0,0,-1)),config['surface_max_height']-config['surface_min_height'])
                if hit is None or normal.z<0.95: continue
                add(surface_spec['source'],x,z,hit.z+config['surface_lift'],surface_spec['height'],f'room_{index}_display_{placed}')
                occupied.append((x,z,surface_spec['radius'])); placed+=1

output=root/config['output']; output.parent.mkdir(parents=True,exist_ok=True)
bpy.ops.export_scene.gltf(filepath=str(output),export_format='GLB',export_yup=True,export_apply=True)
(root/config['report']).write_text(json.dumps({'added':additions,'count':len(additions),'door_clearance':config['door_clearance'],'placement':'BVH: свободный пол, проверка пяти точек и расстояния до двери'},ensure_ascii=False,indent=2),encoding='utf-8')
print('Добавлено предметов:',len(additions),flush=True)
