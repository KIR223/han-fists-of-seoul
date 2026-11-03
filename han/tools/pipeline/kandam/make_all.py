"""Полная сборка района «Кандам»: CC0-сканы → текстуры → GLB.
  python make_all.py            # всё
  python make_all.py --no-fetch # без скачивания (пак уже в assets_bin/cc0-texture-pack)
Результат: assets_bin/kandam/out/kandam_district.glb (+ textures/, stats.json, buildings.json)."""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
WORK = Path(os.environ.get("KANDAM_WORK", ROOT / "assets_bin" / "kandam"))
sys.path.insert(0, str(HERE))

if "--no-fetch" not in sys.argv:
    subprocess.check_call([sys.executable, str(HERE / "fetch_cc0.py")])
import tex2   # noqa: E402
import scans  # noqa: E402
(WORK / "t2").mkdir(parents=True, exist_ok=True)
tex2.build_all(WORK / "t2")
scans.build_all(WORK / "t3")
subprocess.check_call([sys.executable, str(HERE / "build.py"), str(WORK / "out")], env=dict(os.environ, KANDAM_WORK=str(WORK)))
print("GLB:", WORK / "out" / "kandam_district.glb")
