#pragma once
// Импорт glTF/GLB (E3) на fastgltf. Доступен, если собрано с HAN_WITH_GLTF.
#ifdef HAN_WITH_GLTF

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/mesh.h"

namespace han::tools {

// Изображение материала RGBA8: альбедо sRGB, карты данных линейные; мипы делает han_cooker.
struct ImportedTexture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool srgb = true;
    bool alphaWeighted = false;
};

struct ImportResult {
    render::MeshData mesh;                    // mesh.textures[i] — пока пусто: имена файлов назначает han_cooker
    std::vector<ImportedTexture> textures;    // слоты Submesh::texture/normalTexture/roughnessTexture
    std::vector<std::string> warnings;        // не фатальные проблемы (не открылась текстура, лишние атрибуты и т. п.)
};

// Читает .gltf (с внешними буферами и картинками) или .glb, применяет мировые преобразования узлов сцены и объединяет
// все треугольные примитивы в один меш с подмешами по материалам (одинаковые изображения используются один раз).
// Цвет вершин — baseColorFactor материала (без материала — серый), UV — из TEXCOORD_n базовой текстуры, нормали из файла
// (если нет — гладкие). Единицы — метры, Y вверх (как в glTF), порядок обхода против часовой стрелки.
// Читаются baseColorTexture, normalTexture и канал G metallicRoughnessTexture (шероховатость с множителем материала).
// Карты данных сохраняются линейными. KHR_texture_transform, карта металличности и эмиссия пропускаются;
// нормали преобразуются без обратной транспонированной матрицы,
// поэтому неравномерный масштаб узлов даёт неточное освещение.
bool importGltf(const std::filesystem::path& path, ImportResult& out, std::string& error);

}  // namespace han::tools
// Пример: ImportResult r; std::string err; if (!importGltf("lamp.glb", r, err)) log(err);

#endif  // HAN_WITH_GLTF
