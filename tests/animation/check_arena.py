#!/usr/bin/env python3
"""Реальный Vulkan smoke + регрессия чёрного кадра от constant-UV манекена.
python tests/animation/check_arena.py --exe build/windows-release/bin/Release/han_arena_demo.exe
Нужны Boxing.hchar, устройство Vulkan и дисплей; не заменять успешным пропуском.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/engine/screenshots'))
from png_tools import read_png


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--require-validation',action='store_true')
    args=p.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        image=Path(tmp)/'arena.png'
        cmd=[str(args.exe.resolve()),'--auto','--foes','4','--frames','120','--no-ui','--no-capture',
             '--deterministic','--size','640','360','--screenshot',str(image)]
        result=subprocess.run(cmd,cwd=ROOT,text=True,encoding='utf-8',errors='replace',capture_output=True,timeout=180)
        log=result.stdout+result.stderr
        if result.returncode or 'A1: 65' not in log or not image.is_file():
            raise RuntimeError('арена не прошла A1 smoke:\n'+log)
        if args.require_validation and 'слой валидации: включён' not in log:
            raise RuntimeError('слой Vulkan validation отсутствует')
        if 'ошибок валидации: 0, предупреждений: 0' not in log:
            raise RuntimeError('Vulkan сообщил ошибки/предупреждения:\n'+log)
        png=read_png(image)
        samples=[max(row[i:i+3]) for row in png.rows for i in range(0,len(row),png.channels)]
        if max(samples)<80 or sum(samples)/len(samples)<20:
            raise RuntimeError('чёрный кадр: возможен NaN в material/bloom')
        print('A1 Vulkan OK: персонажи загружены, кадр не чёрный, validation 0/0')


if __name__=='__main__':main()
