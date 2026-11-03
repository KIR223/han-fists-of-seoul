#!/usr/bin/env python3
"""Одна команда: проверить CC0 исходник → авторский проход → HCH1 с клипами бокса.
python han/anim/tools/build_boxing.py [--blender path/to/blender]
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'han/tools/pipeline'))
import han_fetch


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--blender')
    args=p.parse_args()
    blender=args.blender or han_fetch.find_blender()
    if not blender: p.error('Blender не найден; установите Blender 4.3 или задайте --blender')
    fetch=ROOT/'han/tools/pipeline/han_fetch.py'
    for command in ('fetch','process'):
        subprocess.run([sys.executable,str(fetch),command,'quaternius_ual','quaternius_ual2'],cwd=ROOT,check=True)
    folder=ROOT/'assets_bin/cooked/quaternius_ual'
    subprocess.run([blender,'--background','--python-exit-code','1','--python',str(ROOT/'han/anim/tools/author_boxing.py'),'--',
                    str(folder/'CH_Mannequin.glb'),str(folder/'Boxing.blend'),
                    str(ROOT/'assets_bin/cooked/quaternius_ual2/AN_UniversalLibrary2.glb')],cwd=ROOT,check=True)
    subprocess.run([blender,'--background','--python-exit-code','1','--python',str(ROOT/'han/anim/tools/refine_contacts.py'),'--',
                    str(folder/'Boxing.blend')],cwd=ROOT,check=True)
    subprocess.run([blender,'--background','--python-exit-code','1','--python',str(ROOT/'han/anim/tools/check_boxing.py'),'--',
                    str(folder/'Boxing.blend')],cwd=ROOT,check=True)
    subprocess.run([blender,'--background','--python-exit-code','1','--python',str(ROOT/'han/tools/blender/export_character.py'),'--',
                    str(folder/'Boxing.blend'),str(folder/'Boxing.hchar')],cwd=ROOT,check=True)
    print('Готово:',folder/'Boxing.hchar')
    print('Соберите и запустите han_arena_demo --foes 1 (или --auto --foes 4).')


if __name__=='__main__':main()
