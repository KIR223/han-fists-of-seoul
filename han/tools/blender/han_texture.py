"""Линейная EXR-карта normal/roughness → PNG без экспозиции и тонмаппинга.

Запуск: blender -b --factory-startup --python-exit-code 1 --python han_texture.py -- input.exr output.png
"""
import sys
import bpy


def main():
    source, target = sys.argv[sys.argv.index("--") + 1:]
    image = bpy.data.images.load(source, check_existing=False)
    image.colorspace_settings.name = "Non-Color"
    # Blender читает изображение лениво: загрузить пиксели до смены пути на новый PNG.
    if not len(image.pixels):
        raise RuntimeError(f"изображение не прочитано: {source}")
    image.pixels[0] = image.pixels[0]
    # Image.save сохраняет данные напрямую, в отличие от save_render с преобразованием вида.
    image.file_format = "PNG"
    image.filepath_raw = target
    image.save()
    bpy.data.images.remove(image)


if __name__ == "__main__":
    main()
