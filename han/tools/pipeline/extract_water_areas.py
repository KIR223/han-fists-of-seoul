"""OSM-мультиполигоны воды с островами → географические треугольники для запекания города."""
import argparse
import json
from pathlib import Path

from han_osm_extract import overlaps


def triangulate_rings(rings):
    import mapbox_earcut
    import numpy as np
    points=[]; ends=[]
    for ring in rings:
        values=list(ring)
        if len(values)>1 and values[0]==values[-1]: values.pop()
        if len(values)<3: raise ValueError('Контур воды имеет меньше трёх вершин')
        points.extend(values); ends.append(len(points))
    vertices=np.asarray(points,dtype=np.float64)
    indices=mapbox_earcut.triangulate_float64(vertices,np.asarray(ends,dtype=np.uint32))
    return [[points[int(i)] for i in indices[offset:offset+3]] for offset in range(0,len(indices),3)]


def extract(source, destination, bbox):
    import osmium
    polygons=[]
    processor=osmium.FileProcessor(source).with_locations('flex_mem').with_areas()
    processor=processor.with_filter(osmium.filter.EntityFilter(osmium.osm.AREA))
    for area in processor:
        # Закрытые ways уже присутствуют в основной карте: добавляем только relations.
        if area.from_way() or area.tags.get('natural')!='water': continue
        for outer in area.outer_rings():
            # Результат строго lon/lat; не сохраняем названия/бренды.
            ring=[(node.lon,node.lat) for node in outer]
            if not overlaps([(lat,lon) for lon,lat in ring],bbox): continue
            holes=[[(node.lon,node.lat) for node in inner] for inner in area.inner_rings(outer)]
            triangles=triangulate_rings([ring,*holes])
            polygons.append({'relation_id':area.orig_id(),'water':area.tags.get('water',''),
                             'rings':[ring,*holes],'triangles':triangles})
            print('Вода:',area.orig_id(),'островов',len(holes),'треугольников',len(triangles),flush=True)
    result={'version':1,'bbox':bbox,'source':str(source),'license':'ODbL-1.0','attribution':'© OpenStreetMap contributors',
            'polygons':polygons,'limitations':'География из OSM; физические уровни воды и мостов требуют отдельной подготовки.'}
    destination.parent.mkdir(parents=True,exist_ok=True)
    temporary=destination.with_suffix('.tmp')
    temporary.write_text(json.dumps(result,ensure_ascii=False),encoding='utf-8'); temporary.replace(destination)
    print('Мультиполигоны воды:',len(polygons),flush=True)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path); parser.add_argument('destination',type=Path)
    parser.add_argument('--config',type=Path,default=Path('data/city/seoul_extent.json'))
    args=parser.parse_args()
    extract(args.source,args.destination,json.loads(args.config.read_text(encoding='utf-8'))['bbox'])
