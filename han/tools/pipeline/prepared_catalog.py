"""Единый каталог результатов подготовки с отдельными техническим и художественным статусами."""
import argparse
import json
import math
from pathlib import Path


def combine(indices):
    selected = {}
    for index in indices:
        for item in index:
            previous=selected.get(item['id'])
            if item.get('state')=='prepared' or previous is None or previous.get('state')!='prepared':
                selected[item['id']]=item
    return list(selected.values())


def inspect_result(record, root):
    result=dict(record); issues=[]
    if record.get('state')!='prepared': issues.append('conversion_incomplete')
    report=record.get('report',{})
    folder=Path(record.get('output',''))
    for name in report.get('files',{}).values():
        path=(root/folder/name).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file(): issues.append('missing_output:'+name)
    bounds=report.get('size_meters',{})
    if bounds and any(not math.isfinite(v) or v<0 for v in bounds.values()): issues.append('invalid_dimensions')
    if bounds and max(bounds.values())>2000: issues.append('verify_physical_scale_or_preview_geometry')
    if report.get('conversion',{}).get('material_status')=='neutral_replacement_required': issues.append('replace_legacy_materials')
    if report.get('materials',0)>4: issues.append('material_draw_budget_review')
    if report.get('brand_hits'): issues.append('brand_review')
    if report.get('type')=='skinned_source': issues.append('retarget_and_runtime_review')
    if bounds and not report.get('collision_vertices'): issues.append('compound_collision_review')
    result['checks']={'issues':sorted(set(issues)), 'technical_outputs_present':not any(x.startswith(('missing_output','conversion_incomplete','invalid_dimensions')) for x in issues),
                      'art_accepted':False,'runtime_ready':False}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--indices',nargs='+',type=Path,default=[Path('assets_bin/prepared')/folder/'index.json' for folder in ('models','models_legacy','models_corrected','variants')])
    parser.add_argument('--out',type=Path,default=Path('assets_bin/prepared/catalog.json'))
    args=parser.parse_args(); root=Path.cwd()
    items=[inspect_result(record,root) for record in combine([json.loads(p.read_text(encoding='utf-8')) for p in args.indices])]
    result={'version':1,'items':items,'summary':{'total':len(items),'converted':sum(i['state']=='prepared' for i in items),
        'outputs_present':sum(i['checks']['technical_outputs_present'] for i in items),'art_accepted':0},
        'note':'Техническая подготовка не равна приёмке: масштаб, бренды, материалы, коллизии и runtime проверяются по каждому назначению.'}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result['summary'],ensure_ascii=False))
    return 0 if result['summary']['outputs_present']==len(items) else 1


if __name__=='__main__': raise SystemExit(main())
