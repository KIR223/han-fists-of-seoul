"""Проверка, что районы лежат в общей проекции и связаны настоящим дорожным графом OSM."""
import argparse
import heapq
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

from connect_district import geographic_projection


def road_graph(path, project):
    nodes, roads = {}, []
    allowed = {'motorway','trunk','primary','secondary','tertiary','residential','unclassified','service','living_street',
               'motorway_link','trunk_link','primary_link','secondary_link','tertiary_link'}
    for _, item in ET.iterparse(path, events=('end',)):
        if item.tag == 'node':
            nodes[int(item.attrib['id'])] = project(float(item.attrib['lat']),float(item.attrib['lon']))
            item.clear()
        elif item.tag == 'way':
            tags = {tag.attrib['k']:tag.attrib['v'] for tag in item.findall('tag')}
            if tags.get('highway') in allowed and tags.get('motor_vehicle')!='no' and tags.get('access') not in {'no','private'}:
                roads.append(([int(node.attrib['ref']) for node in item.findall('nd')],tags.get('oneway','')))
            item.clear()
    graph = {}
    for refs,oneway in roads:
        if oneway == '-1': refs = list(reversed(refs))
        for first,second in zip(refs,refs[1:]):
            if first not in nodes or second not in nodes: continue
            weight = math.dist(nodes[first],nodes[second])
            graph.setdefault(first,[]).append((second,weight)); graph.setdefault(second,[])
            if oneway not in {'yes','1','true','-1'}: graph[second].append((first,weight))
    return nodes,graph


def distances(graph, start):
    result = {start:0.0}; queue = [(0.0,start)]
    while queue:
        cost,current = heapq.heappop(queue)
        if cost != result[current]: continue
        for target,length in graph.get(current,[]):
            candidate = cost+length
            if candidate < result.get(target,math.inf):
                result[target]=candidate; heapq.heappush(queue,(candidate,target))
    return result


def audit(nodes, graph, districts, snap_limit=500):
    anchors = {}
    for district in districts:
        points = district['bounds']
        center = (sum(p[0] for p in points)/len(points),sum(p[1] for p in points)/len(points))
        node = min(graph,key=lambda key:math.dist(center,nodes[key]))
        distance = math.dist(center,nodes[node])
        if distance>snap_limit: raise ValueError('Район слишком далеко от дорожной сети: '+district['id'])
        anchors[district['id']]={'center':center,'node':node,'snap_meters':distance}
    routes=[]
    for name,anchor in anchors.items():
        costs=distances(graph,anchor['node'])
        for target,other in anchors.items():
            if name==target: continue
            cost=costs.get(other['node'])
            routes.append({'from':name,'to':target,'connected':cost is not None,'meters':cost})
    return {'anchors':anchors,'routes':routes,'connected':all(route['connected'] for route in routes)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--osm',type=Path,default=Path('assets_src/osm/seoul_connected.osm'))
    parser.add_argument('--out',type=Path,default=Path('assets_bin/city/district_routes.json'))
    args=parser.parse_args()
    style=json.loads(Path('data/city/streetscape.json').read_text(encoding='utf-8'))
    project,_=geographic_projection(style['origin_lat'],style['origin_lon'])
    nodes,graph=road_graph(args.osm,project)
    districts=[json.loads(p.read_text(encoding='utf-8')) for p in sorted(Path('data/world/districts').glob('*.json'))]
    result=audit(nodes,graph,districts)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Районы:',len(districts),'направленные маршруты:',len(result['routes']),'все связаны:',result['connected'])
    return 0 if result['connected'] else 1


if __name__=='__main__': raise SystemExit(main())
