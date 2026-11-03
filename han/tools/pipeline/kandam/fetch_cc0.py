"""Скачивает ТОЛЬКО нужные району CC0-сканы ambientCG из GitHub-репозитория PatrickJnr/cc0-texture-pack
(частичный клон без блобов + checkout выбранных файлов, ~150 МБ вместо 9 ГБ).
Лицензия ассетов: CC0 1.0 (ambientCG.com). Запуск: python fetch_cc0.py [папка]"""
import fnmatch
import subprocess
import sys
from pathlib import Path

REPO = "https://github.com/PatrickJnr/cc0-texture-pack"
SCANS = ["asphalt010", "pavingstones029", "pavingstones030", "concrete008", "concrete015", "tactilepaving001",
         "metal001", "metal009", "bricks017", "tiles038", "tiles097", "plaster004",
         "facade012", "facade013", "facade014", "facade015", "facade016", "manholecover003",
         "tiles105", "terrazzo002", "wood021", "wood037", "plaster002",
         "leafset001", "leafset003", "leafset004", "leafset005", "leafset006", "leafset011", "leafset016"]


def main():
    root = Path(__file__).resolve().parents[4]
    dst = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "assets_bin" / "cc0-texture-pack"
    if not (dst / ".git").exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(["git", "clone", "--depth", "1", "--filter=blob:none", "--no-checkout", REPO, str(dst)])
    names = subprocess.check_output(["git", "-C", str(dst), "ls-tree", "-r", "HEAD", "--name-only", "textures"], text=True).split()
    want = [n for n in names if any(fnmatch.fnmatch(Path(n).name, f"{s}_*") for s in SCANS)]
    print(f"файлов к загрузке: {len(want)}")
    for i in range(0, len(want), 40):
        subprocess.check_call(["git", "-C", str(dst), "checkout", "HEAD", "--"] + want[i:i + 40])
    print("готово:", dst)


if __name__ == "__main__":
    main()
