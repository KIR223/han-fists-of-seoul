#pragma once
// Данные меша на стороне CPU (E2, E3) и процедурные формы для тестовых сцен.
#include <cstdint>
#include <string>
#include <vector>

#include "han/core/math.h"

namespace han::render {

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];  // множитель цвета материала (baseColorFactor); умножается на цвет из текстуры
    float uv[2];     // координаты текстуры, начало в левом верхнем углу изображения (как в glTF)
};

// Часть меша с одним материалом: подряд идущие индексы и номер текстуры в MeshData::textures (-1 — без текстуры).
struct Submesh {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t texture = -1;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // треугольники против часовой стрелки, если смотреть снаружи
    // Если пусто — весь меш одна часть без текстуры. Иначе части покрывают индексы подряд без пропусков.
    std::vector<Submesh> submeshes;
    // Имена файлов текстур (.ktx2) относительно файла меша; рядом с мешом по соглашению "<имя>.tex<N>.ktx2".
    std::vector<std::string> textures;
};

// Части меша для рисования: submeshes или одна часть на весь меш.
std::vector<Submesh> effectiveSubmeshes(const MeshData& mesh);

// Параллелепипед с центром в нуле; halfExtents — половины размеров по осям.
MeshData makeBox(core::Vec3 halfExtents, core::Vec3 color);
// Горизонтальная плоскость y = 0 со стороной 2·halfSize, нормаль вверх. UV повторяются uvRepeat раз по каждой стороне.
MeshData makePlane(float halfSize, core::Vec3 color, float uvRepeat = 1.0f);

}  // namespace han::render
// Пример: MeshData cube = makeBox({0.5f, 0.5f, 0.5f}, {0.8f, 0.3f, 0.2f}); auto id = renderer->uploadMesh(cube);
