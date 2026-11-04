"""Замена PBR в готовой сцене GLB без изменения геометрии, коллизии и свечения вывесок."""
import argparse
from io import BytesIO
import json
from pathlib import Path

from PIL import Image, ImageChops
from prepare_district import read_glb, write_glb


def image_from_texture(document, binary, texture):
    image = document['images'][document['textures'][texture]['source']]
    view = document['bufferViews'][image['bufferView']]
    start = view.get('byteOffset', 0)
    return Image.open(BytesIO(binary[start:start+view['byteLength']])).convert('RGB')


def refresh(source, destination, mappings, library):
    document, binary = read_glb(Path(source))
    lookup = {Path(item['source']).stem: item for item in library if item['state']=='prepared'}
    changed = []

    def texture(pixels):
        encoded = BytesIO(); pixels.save(encoded, format='PNG')
        binary.extend(b'\0' * (-len(binary) % 4))
        view = len(document['bufferViews'])
        document['bufferViews'].append({'buffer':0, 'byteOffset':len(binary), 'byteLength':len(encoded.getvalue())})
        binary.extend(encoded.getvalue())
        index = len(document.setdefault('images',[]))
        document['images'].append({'bufferView':view,'mimeType':'image/png'})
        result = len(document.setdefault('textures',[]))
        document['textures'].append({'source':index})
        return result

    for material in document.get('materials',[]):
        name = material.get('name','')
        if name not in mappings:
            continue
        spec = mappings[name]
        prepared = lookup[spec['source']]
        folder = Path(prepared['output'])
        pbr = material.setdefault('pbrMetallicRoughness',{})
        replacement = {role:Image.open(folder/(role+'.png')).convert('RGB') for role in ('BC','N','ORM')}
        if spec.get('preserve_facade_details'):
            original_orm = image_from_texture(document,binary,pbr['metallicRoughnessTexture']['index'])
            mask = ImageChops.multiply(original_orm.getchannel('G').point(lambda x:255 if x>=210 else 0),
                                       original_orm.getchannel('B').point(lambda x:255 if x<60 else 0))
            roles = {'BC':pbr['baseColorTexture']['index'], 'N':material['normalTexture']['index'], 'ORM':pbr['metallicRoughnessTexture']['index']}
            for role,index in roles.items():
                original = image_from_texture(document,binary,index)
                replacement[role] = Image.composite(replacement[role].resize(original.size),original,mask.resize(original.size))
        pbr.update(baseColorTexture={'index':texture(replacement['BC'])}, baseColorFactor=[1,1,1,1],
                   metallicRoughnessTexture={'index':texture(replacement['ORM'])}, metallicFactor=1,roughnessFactor=1)
        material['normalTexture']={'index':texture(replacement['N'])}
        material['occlusionTexture']={'index':pbr['metallicRoughnessTexture']['index']}
        changed.append(name)
    write_glb(Path(destination),document,binary)
    return changed


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path); parser.add_argument('destination',type=Path)
    parser.add_argument('--profile',type=Path,default=Path('data/city/material_refresh.json'))
    parser.add_argument('--section',required=True)
    args=parser.parse_args()
    profile=json.loads(args.profile.read_text(encoding='utf-8'))
    library=json.loads(Path('assets_bin/prepared/materials/index.json').read_text(encoding='utf-8'))
    print('Обновлены материалы:',refresh(args.source,args.destination,profile[args.section],library))
