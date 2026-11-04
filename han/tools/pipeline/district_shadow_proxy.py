"""Теневой LOD района: силуэты зданий и маски листвы, без мелких рам/проводов/рекламы."""
import argparse
import copy
import json
from pathlib import Path
from prepare_district import read_glb,write_glb


def build(source,destination,materials):
    document,binary=read_glb(source)
    result=copy.deepcopy(document)
    solid={'name':'HAN_ShadowSolid','pbrMetallicRoughness':{'baseColorFactor':[1,1,1,1]},'alphaMode':'OPAQUE'}
    result['materials']=[solid]
    counts={'before':0,'after':0,'masked':0}
    for original,mesh in zip(document['meshes'],result['meshes']):
        selected=[]
        for primitive in original['primitives']:
            count=document['accessors'][primitive['indices']]['count']//3
            counts['before']+=count
            material=document['materials'][primitive['material']]
            if material.get('name') not in materials: continue
            part=copy.deepcopy(primitive)
            if material.get('alphaMode')=='MASK':
                part['material']=len(result['materials']); result['materials'].append(copy.deepcopy(material)); counts['masked']+=count
            else:
                part['material']=0
            selected.append(part); counts['after']+=count
        mesh['primitives']=selected
    if counts['after']==0: raise ValueError('Пустая теневая геометрия')
    write_glb(destination,result,binary)
    destination.with_suffix('.report.json').write_text(json.dumps(counts,indent=2),encoding='utf-8')
    return counts


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path); parser.add_argument('destination',type=Path)
    parser.add_argument('--profile',type=Path,default=Path('data/city/shadow_proxy.json'))
    args=parser.parse_args()
    print(build(args.source,args.destination,json.loads(args.profile.read_text(encoding='utf-8'))['materials']))
