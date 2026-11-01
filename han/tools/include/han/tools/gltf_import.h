#pragma once
// Импорт glTF/GLB (E3) на fastgltf. Доступен, если собрано с HAN_WITH_GLTF.
#ifdef HAN_WITH_GLTF

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/mesh.h"

namespace han::tools {

// Изображение материала, декодированное в RGBA8 (текстуры sRGB; сжатие и мипы делает han_cooker).
struct ImportedTexture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

struct ImportResult {
    render::MeshData mesh;                    // mesh.textures[i] — пока пусто: имена файлов назначает han_cooker
    std::vector<ImportedTexture> textures;    // textures[i] соответствует Submesh::texture == i
    std::vector<std::string> warnings;        // не фатальные проблемы (не открылась текстура, лишние атрибуты и т. п.)
};

// Читает .gltf (с внешними буферами и картинками) или .glb, применяет мировые преобразования узлов сцены и объединяет
// все треугольные примитивы в один меш с подмешами по материалам (одинаковые изображения используются один раз).
// Цвет вершин — baseColorFactor материала (без материала — серый), UV — из TEXCOORD_n базовой текстуры, нормали из файла
// (если нет — гладкие). Единицы — метры, Y вверх (как в glTF), порядок обхода против часовой стрелки.
// Ограничения v1: читается только базовый цвет (baseColorTexture + baseColorFactor); KHR_texture_transform и другие
// карты (нормали, шероховатость, эмиссия) пропускаются; нормали преобразуются без обратной транспонированной матрицы,
// поэтому неравномерный масштаб узлов даёт неточное освещение.
bool importGltf(const std::filesystem::path& path, ImportResult& out, std::string& error);

}  // namespace han::tools
// Пример: ImportResult r; std::string err; if (!importGltf("lamp.glb", r, err)) log(err);

#endif  // HAN_WITH_GLTF
