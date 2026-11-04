#pragma once
// Данные меша на стороне CPU (E2, E3) и процедурные формы для тестовых сцен.
#include <cstdint>
#include <string>
#include <vector>

#include "han/core/math.h"

namespace han::render {
enum class AlphaMode : std::uint32_t { EmissiveMask, Opaque, Mask, Blend };

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];  // множитель цвета материала (baseColorFactor); умножается на цвет из текстуры
    float uv[2];     // координаты текстуры, начало в левом верхнем углу изображения (как в glTF)
};

// Часть меша с одним материалом: подряд идущие индексы, номер текстуры в MeshData::textures (-1 — без текстуры) и
// параметры PBR (металличность и шероховатость, 0..1). Цвет — из вершин (множитель) и текстуры.
struct Submesh {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t texture = -1;
    float metallic = 0.0f;
    float roughness = 0.5f;
    // Карты материала (номера в MeshData::textures, -1 — нет): нормали (OpenGL, +Y вверх, линейные) и шероховатость (линейная, серый).
    std::int32_t normalTexture = -1, roughnessTexture = -1;
    // EmissiveMask — маска окон/вывесок города; Opaque — альфа не используется; Mask — вырезание листвы.
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::EmissiveMask;
    bool doubleSided = false;
    float opacity = 1.f;
    std::int32_t emissiveTexture = -1;
    float emissive[3] = {0.f, 0.f, 0.f};  // линейный glTF emissiveFactor * emissiveStrength
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // треугольники против часовой стрелки, если смотреть снаружи
    // Если пусто — весь меш одна часть без текстуры. Иначе части покрывают индексы подряд без пропусков.
    std::vector<Submesh> submeshes;
    // Имена файлов текстур (.ktx2) относительно файла меша; han_cooker называет их по содержимому ("tex_<WxH>_<хэш>.ktx2"),
    // поэтому LOD-ы и разные ассеты в одной папке делят общие файлы.
    std::vector<std::string> textures;
    struct Influence {
        std::uint32_t joints[4]{};
        float weights[4]{};
    };
    // Только skinned mesh; пусто для статических. Порядок совпадает с vertices.
    std::vector<Influence> skin;
};

// Части меша для рисования: submeshes или одна часть на весь меш.
std::vector<Submesh> effectiveSubmeshes(const MeshData& mesh);
struct ShadowBatches {
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> parts;
};
// Группирует непрозрачные части по ячейкам; маска листвы сохраняет исходный материал.
ShadowBatches buildShadowBatches(const MeshData& mesh, float cellSize);

// Параллелепипед с центром в нуле; halfExtents — половины размеров по осям.
MeshData makeBox(core::Vec3 halfExtents, core::Vec3 color);
// Горизонтальная плоскость y = 0 со стороной 2·halfSize, нормаль вверх. UV повторяются uvRepeat раз по каждой стороне.
MeshData makePlane(float halfSize, core::Vec3 color, float uvRepeat = 1.0f);
// Сфера с центром в нуле (UV-сфера: segments по долготе, rings по широте), нормали наружу; u по долготе, v сверху вниз.
MeshData makeSphere(float radius, int segments, int rings, core::Vec3 color);

}  // namespace han::render
// Пример: MeshData cube = makeBox({0.5f, 0.5f, 0.5f}, {0.8f, 0.3f, 0.2f}); auto id = renderer->uploadMesh(cube);
