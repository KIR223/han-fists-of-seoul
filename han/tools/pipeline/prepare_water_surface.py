"""Географические треугольники воды → GLB в общей системе координат HAN, с обрезкой по карте."""
import argparse
from array import array
import json
from pathlib import Path
import struct
from connect_district import geographic_projection
from prepare_district import write_glb


def clip_triangle(triangle,bounds):
    minx,miny,maxx,maxy=bounds
    polygon=list(triangle)
    for axis,limit,sign in [(0,minx,1),(0,maxx,-1),(1,miny,1),(1,maxy,-1)]:
        if not polygon: break
        clipped=[]; previous=polygon[-1]; before=(previous[axis]-limit)*sign
        for current in polygon:
            after=(current[axis]-limit)*sign
            if (before>=0)!=(after>=0):
                t=before/(before-after)
                clipped.append(tuple(previous[i]+t*(current[i]-previous[i]) for i in range(2)))
            if after>=0: clipped.append(tuple(current))
            previous=current; before=after
        polygon=clipped
    return polygon


def prepare(areas,profile,origin):
    project,_=geographic_projection(*origin)
    south,west,north,east=areas['bbox']
    positions=[]; indices=[]
    for area in areas['polygons']:
        for triangle in area['triangles']:
            polygon=clip_triangle(triangle,(west,south,east,north))
            if len(polygon)<3: continue
            points=[project(lat,lon) for lon,lat in polygon]
            for i in range(1,len(points)-1):
                tri=[points[0],points[i],points[i+1]]
                cross=(tri[1][0]-tri[0][0])*(tri[2][1]-tri[0][1])-(tri[1][1]-tri[0][1])*(tri[2][0]-tri[0][0])
                if abs(cross)<1e-5: continue
                # XZ-обход по часовой даёт нормаль +Y.
                if cross>0: tri[1],tri[2]=tri[2],tri[1]
                for x,z in tri:
                    indices.append(len(positions)); positions.append((x,profile['level'],z))
    if not indices: raise ValueError('Нет геометрии воды внутри карты')
    binary=bytearray(); views=[]; accessors=[]
    def attribute(values,kind,channels,component,code):
        payload=array(code,[number for row in values for number in (row if channels>1 else [row])]).tobytes()
        binary.extend(b'\0'*(-len(binary)%4)); view=len(views)
        views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(payload)})
        binary.extend(payload)
        accessor={'bufferView':view,'componentType':component,'count':len(values),'type':kind}
        if channels>1:
            accessor['min']=[min(row[i] for row in values) for i in range(channels)]
            accessor['max']=[max(row[i] for row in values) for i in range(channels)]
        accessors.append(accessor); return len(accessors)-1
    position=attribute(positions,'VEC3',3,5126,'f')
    normal=attribute([(0.,1.,0.)]*len(positions),'VEC3',3,5126,'f')
    index=attribute(indices,'SCALAR',1,5125,'I')
    document={'asset':{'version':'2.0','generator':'HAN OSM water preparation'},'buffers':[{}],'bufferViews':views,'accessors':accessors,
              'materials':[{'name':'HanRiverWater','pbrMetallicRoughness':{'baseColorFactor':[*profile['color'],1],
                    'metallicFactor':0,'roughnessFactor':profile['roughness']},'alphaMode':'OPAQUE'}],
              'meshes':[{'primitives':[{'attributes':{'POSITION':position,'NORMAL':normal},'indices':index,'material':0}]}],
              'nodes':[{'mesh':0}],'scenes':[{'nodes':[0]}],'scene':0}
    write_glb(Path(profile['output']),document,binary)
    mask={'version':1,'cell_size':profile.get('mask_cell_size',128),'triangles':[
        [[p[0],p[2]] for p in positions[i:i+3]] for i in range(0,len(positions),3)]}
    Path(profile.get('mask',str(Path(profile['output']).with_suffix('.mask.json')))).write_text(json.dumps(mask,separators=(',',':')),encoding='utf-8')
    return {'triangles':len(indices)//3,'polygons':len(areas['polygons']),'level':profile['level'],
            'license':'ODbL-1.0','islands_preserved':True,'physical_water_and_bridges_verified':False}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path,default=Path('data/city/water.json'))
    args=parser.parse_args(); profile=json.loads(args.profile.read_text(encoding='utf-8'))
    city=json.loads(Path('data/city/streetscape.json').read_text(encoding='utf-8'))
    result=prepare(json.loads(Path(profile['source']).read_text(encoding='utf-8')),profile,(city['origin_lat'],city['origin_lon']))
    Path(profile['output']).with_suffix('.report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(result)
