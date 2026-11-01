#!/usr/bin/env python3
"""Компилирует шейдеры из shaders/*.vert|frag в SPIR-V и кладёт заголовки в han/render/generated/.

Нужен glslc из Vulkan SDK (VULKAN_SDK). Результат хранится в Git, поэтому CI шейдерный компилятор не требует.
Запуск: python shaders/compile_shaders.py
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "han" / "render" / "generated"


def find_glslc():
    sdk = os.environ.get("VULKAN_SDK")
    candidates = [shutil.which("glslc")]
    if sdk:
        candidates.append(str(pathlib.Path(sdk) / "Bin" / "glslc.exe"))
    for c in candidates:
        if c and pathlib.Path(c).exists():
            return c
    sys.exit("glslc не найден: установите Vulkan SDK (переменная VULKAN_SDK)")


def main():
    glslc = find_glslc()
    OUT.mkdir(parents=True, exist_ok=True)
    for src in sorted((ROOT / "shaders").glob("*.[vf][er][rag]*")):
        if src.suffix not in (".vert", ".frag"):
            continue
        spv = OUT / (src.name + ".spv")
        subprocess.run([glslc, "--target-env=vulkan1.3", "-O", "-I", str(ROOT / "shaders"), str(src), "-o", str(spv)], check=True)
        data = spv.read_bytes()
        words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
        name = src.name.replace(".", "_")
        lines = ", ".join(f"0x{w:08x}" for w in words)
        header = (f"// Сгенерировано shaders/compile_shaders.py из {src.name}. Не править вручную.\n"
                  f"#pragma once\n#include <cstdint>\n\nnamespace han::render::shaders {{\n"
                  f"inline constexpr std::uint32_t k_{name}[] = {{{lines}}};\n}}  // namespace han::render::shaders\n")
        (OUT / f"{name}.h").write_text(header, encoding="utf-8", newline="\n")
        spv.unlink()
        print(f"{src.name}: {len(words)} слов")


if __name__ == "__main__":
    main()
