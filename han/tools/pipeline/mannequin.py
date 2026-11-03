"""Подготовка конкретного CC0-манекена UAL1 без потери skin/анимаций.

Это не универсальный импортёр glTF. Бинарный chunk, веса, ключи и inverse bind
сохраняются побайтно. Нормализация применяется общим родителем mesh и skeleton.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

REQUIRED_CLIPS = ("Idle_Loop", "Walk_Loop", "Sprint_Loop", "Jump_Start", "Jump_Loop", "Jump_Land", "Punch_Jab", "Punch_Cross")


def read_glb(path: Path):
    raw = path.read_bytes()
    if len(raw) < 20 or struct.unpack_from("<4sII", raw) != (b"glTF", 2, len(raw)):
        raise ValueError("неверный заголовок GLB 2.0")
    chunks = []
    offset = 12
    while offset < len(raw):
        if offset + 8 > len(raw):
            raise ValueError("обрезанный заголовок chunk")
        size, kind = struct.unpack_from("<II", raw, offset)
        offset += 8
        if size % 4 or offset + size > len(raw):
            raise ValueError("неверный размер chunk")
        chunks.append((kind, raw[offset:offset + size]))
        offset += size
    if not chunks or chunks[0][0] != 0x4E4F534A:
        raise ValueError("GLB без JSON")
    return json.loads(chunks[0][1]), chunks[1:]


def write_glb(path, doc, chunks):
    raw = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode()
    raw += b" " * (-len(raw) % 4)
    chunks = [(0x4E4F534A, raw), *chunks]
    body = b"".join(struct.pack("<II", len(data), kind) + data for kind, data in chunks)
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, len(body) + 12) + body)


def prepare(source: Path, target: Path, height: float = 1.8):
    if not math.isfinite(height) or height <= 0:
        raise ValueError("рост должен быть положительным")
    doc, chunks = read_glb(source)
    skins = doc.get("skins", [])
    if len(skins) != 1:
        raise ValueError("ожидался один humanoid-скелет UAL1")
    bones = [doc["nodes"][i]["name"] for i in skins[0]["joints"]]
    for name in ("pelvis", "Head", "hand_l", "hand_r", "thumb_01_l", "index_03_r", "foot_l", "foot_r"):
        if name not in bones:
            raise ValueError("не найдена кость " + name)
    clips = {a["name"]: a for a in doc.get("animations", [])}
    missing = set(REQUIRED_CLIPS) - clips.keys()
    if missing:
        raise ValueError("нет обязательных клипов: " + ", ".join(sorted(missing)))
    primitives = [p for mesh in doc["meshes"] for p in mesh["primitives"]]
    if len(doc["meshes"]) != 1 or doc["meshes"][0].get("name") != "Mannequin":
        raise ValueError("ожидался меш Mannequin из бесплатного UAL1")
    for p in primitives:
        if not {"POSITION", "NORMAL", "JOINTS_0", "WEIGHTS_0"} <= p["attributes"].keys():
            raise ValueError("меш потерял веса, joints или нормали")
    # Ожидаемый UAL1: вершины уже в метрах, scene roots с единичными TRS.
    roots = doc["scenes"][doc.get("scene", 0)]["nodes"]
    for i in roots:
        n = doc["nodes"][i]
        if "matrix" in n or n.get("scale", [1, 1, 1]) != [1, 1, 1] or n.get("translation", [0, 0, 0]) != [0, 0, 0] or n.get("rotation", [0, 0, 0, 1]) != [0, 0, 0, 1]:
            raise ValueError("неожиданное преобразование UAL1; проверьте масштаб в Blender")
    accessors = [doc["accessors"][p["attributes"]["POSITION"]] for p in primitives]
    bottom = min(a["min"][1] for a in accessors)
    top = max(a["max"][1] for a in accessors)
    if not 1.5 < top - bottom < 2.2:
        raise ValueError("неожиданный исходный рост UAL1")
    scale = height / (top - bottom)
    parent = len(doc["nodes"])
    doc["nodes"].append({"name": "HAN_Mannequin_180cm", "children": list(roots),
                         "scale": [scale] * 3, "translation": [0, -bottom * scale, 0]})
    doc["scenes"][doc.get("scene", 0)]["nodes"] = [parent]
    for i, material in enumerate(doc.get("materials", [])):
        color = 0.72 if i == 0 else 0.56
        material["pbrMetallicRoughness"] = {"baseColorFactor": [color, color, color, 1], "metallicFactor": 0, "roughnessFactor": 0.75}
        material["name"] = "HAN_White" if i == 0 else "HAN_LightGreyJoints"
    report = {"source": source.name, "height_m": height, "scale": scale, "bones": len(bones),
              "bone_names": bones, "triangles": sum(doc["accessors"][p["indices"]]["count"] // 3 for p in primitives),
              "clips": list(clips), "runtime_ready": False,
              "runtime_note": "GLB подготовлен; GPU-скиннинг и проигрыватель HAN — этап A1"}
    target.parent.mkdir(parents=True, exist_ok=True)
    write_glb(target, doc, chunks)
    target.with_suffix(".report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report
