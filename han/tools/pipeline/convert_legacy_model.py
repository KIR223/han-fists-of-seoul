"""Старые FBX/3DS/DAE → OBJ/MTL с UV. Дополнительные декодеры нужны только на этапе подготовки."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys


def image_lookup(roots):
    result={}
    for root in roots:
        for path in Path(root).rglob('*'):
            if path.is_file() and path.suffix.lower() in {'.png','.jpg','.jpeg','.tga','.bmp','.tif','.tiff'}:
                result.setdefault(path.name.casefold(),path)
    return result


def convert(source,destination,roots):
    # Изолированные необязательные зависимости подготовки; в runtime игры не добавляются.
    tools=Path(__file__).resolve().parents[3]/'build/asset-tools'
    sys.path.insert(0,str(tools))
    source,destination=Path(source),Path(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    images=image_lookup(roots); missing=[]
    material_lines=[]

    def material(index,color,filename):
        material_lines.extend([f'newmtl M{index}', 'Kd '+' '.join(str(c) for c in color[:3]), 'Ns 20'])
        if filename:
            basename=Path(filename.replace('\\','/')).name
            original=images.get(basename.casefold())
            if original:
                local='texture_'+hashlib.sha256(original.read_bytes()).hexdigest()[:12]+original.suffix.lower()
                shutil.copy2(original,destination.parent/local); material_lines.append('map_Kd '+local)
            else: missing.append(filename)

    vertices=uv_offset=1
    temporary=destination.with_suffix(destination.suffix+'.part')
    with temporary.open('w',encoding='utf-8') as output:
        output.write('mtllib '+destination.with_suffix('.mtl').name+'\n')
        with source.open('rb') as stream: header=stream.read(32)
        if source.suffix.lower()=='.fbx' and header.startswith(b'Kaydara FBX Binary'):
            import ufbx
            scene=ufbx.load_file(str(source))
            # FBX 6.x содержит пустые слоты Material: текущая Python-обёртка
            # аварийно обращается к NULL. Геометрия/UV читаются независимо;
            # вместо недостоверного назначения исходных карт используем нейтральный материал.
            legacy_materials=int.from_bytes(header[23:27],'little')<7000
            if legacy_materials:
                material(0,(0.7,0.7,0.7),'')
            for mesh in scene.meshes:
                # Копируем числовые значения сразу: временные view-объекты ufbx
                # не должны переживать итератор нативного массива.
                positions=[(p.x,p.y,p.z) for p in mesh.vertices]
                faces=[(f.index_begin,f.num_indices) for f in mesh.faces]
                vertex_indices=list(mesh.vertex_indices)
                face_materials=list(mesh.face_material)
                uv=mesh.vertex_uv
                uv_values=[(p.x,p.y) for p in uv.values] if uv.exists else []
                uv_indices=list(uv.indices) if uv.exists else []
                for instance in mesh.instances:
                    if legacy_materials:
                        material_ids=[0]
                    else:
                        slots=instance.materials
                        material_ids=[slots[index].typed_id for index in range(len(slots))]
                    output.write('o '+(mesh.name or 'Mesh').replace(' ','_')+'\n')
                    matrix=instance.geometry_to_world
                    for position in positions:
                        point=[matrix.c0[i]*position[0]+matrix.c1[i]*position[1]+matrix.c2[i]*position[2]+matrix.c3[i] for i in range(3)]
                        output.write('v '+' '.join(str(v) for v in point)+'\n')
                    for value in uv_values: output.write(f'vt {value[0]} {value[1]}\n')
                    previous_material=None
                    for number,(face_start,face_count) in enumerate(faces):
                        if face_count<3: continue
                        if material_ids:
                            local=face_materials[number] if face_materials and not legacy_materials else 0
                            if material_ids[local]!=previous_material:
                                output.write(f'usemtl M{material_ids[local]}\n'); previous_material=material_ids[local]
                        tokens=[]
                        for j in range(face_count):
                            corner=face_start+j; token=str(vertices+vertex_indices[corner])
                            if uv_values: token+='/'+str(uv_offset+uv_indices[corner])
                            tokens.append(token)
                        output.write('f '+' '.join(tokens)+'\n')
                    vertices+=len(positions)
                    uv_offset+=len(uv_values)
            for item in ([] if legacy_materials else scene.materials):
                filename=''
                for entry in item.textures:
                    if 'diffuse' in entry.material_prop.casefold() or 'base' in entry.material_prop.casefold():
                        texture=entry.texture
                        filename=texture.relative_filename or texture.filename
                        if filename: break
                material(item.typed_id,(1,1,1),filename)
        else:
            import assimp_py
            scene=assimp_py.import_file(str(source),assimp_py.Process_Triangulate|assimp_py.Process_PreTransformVertices)
            for index,item in enumerate(scene.materials):
                files=(item.get('TEXTURES') or {}).get(assimp_py.TextureType_DIFFUSE,[])
                material(index,item.get('COLOR_DIFFUSE',[1,1,1]),files[0] if files else item.get('TEXTURE_BASE',''))
            for mesh in scene.meshes:
                output.write('o '+(mesh.name or 'Mesh').replace(' ','_')+f'\nusemtl M{mesh.material_index}\n')
                points,indices=mesh.vertices.tolist(),mesh.indices.tolist()
                for i in range(0,len(points),3): output.write(f'v {points[i]} {points[i+1]} {points[i+2]}\n')
                uv=mesh.texcoords[0].tolist() if mesh.texcoords else []
                if uv:
                    stride=len(uv)//mesh.num_vertices
                    for i in range(mesh.num_vertices): output.write(f'vt {uv[i*stride]} {uv[i*stride+1]}\n')
                for i in range(0,len(indices),3):
                    output.write('f '+' '.join(str(vertices+j)+('/'+str(uv_offset+j) if uv else '') for j in indices[i:i+3])+'\n')
                vertices+=mesh.num_vertices
                if uv: uv_offset+=mesh.num_vertices
    if vertices<=1: raise ValueError('Декодер не вернул вершины')
    destination.with_suffix('.mtl').write_text('\n'.join(material_lines)+'\n',encoding='utf-8')
    destination.with_suffix('.conversion.json').write_text(json.dumps({'source':str(source),'vertices':vertices-1,'uvs':uv_offset-1,'missing_images':missing,
        'material_status':'neutral_replacement_required' if locals().get('legacy_materials') else 'source_materials'},ensure_ascii=False,indent=2),encoding='utf-8')
    temporary.replace(destination)
    print('Преобразовано:',source.name,'вершин',vertices-1,'UV',uv_offset-1,'ненайденных карт',len(missing),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path); parser.add_argument('destination',type=Path)
    parser.add_argument('--textures-root',type=Path,required=True); parser.add_argument('--dependencies',type=Path)
    args=parser.parse_args(); roots=[args.textures_root]
    if args.dependencies:
        packages=json.loads(args.dependencies.read_text(encoding='utf-8'))['packages']
        current=next((p for p in packages if args.source.resolve().is_relative_to(Path(p['content']).resolve())),None)
        if current:
            parent=Path(current['original_relative']).parent
            roots += [Path(p['content']) for p in packages if Path(p['original_relative']).parent==parent and p['category']=='materials']
    convert(args.source,args.destination,roots)
