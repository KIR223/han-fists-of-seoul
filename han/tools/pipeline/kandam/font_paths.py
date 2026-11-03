"""Поиск уже лицензированного шрифта Noto для генератора на Windows и Linux."""
import os
from pathlib import Path


def font_path(bold=True, *, root=None, system=None):
    override = os.environ.get('KANDAM_FONT')
    if override:
        selected = Path(override)
        if not selected.is_file():
            raise FileNotFoundError(f'KANDAM_FONT: файл не найден: {selected}')
        return selected, 1 if selected.suffix.lower() == '.ttc' else 0
    root = Path(root) if root is not None else Path(__file__).resolve().parents[4]
    local = root / 'assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf'
    if local.is_file():
        return local, 0
    system = Path(system) if system is not None else Path('/usr/share/fonts/opentype/noto')
    linux = system / ('NotoSansCJK-Bold.ttc' if bold else 'NotoSansCJK-Medium.ttc')
    if linux.is_file():
        return linux, 1  # KR в коллекции Noto CJK
    raise FileNotFoundError('Нужен шрифт Noto Sans KR из манифеста fn_noto_sans_kr или переменная KANDAM_FONT')
