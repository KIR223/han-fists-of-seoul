#pragma once
// Данные меша на стороне CPU (E2) и процедурные формы для тестовых сцен.
#include <cstdint>
#include <vector>

#include "han/core/math.h"

namespace han::render {

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // треугольники против часовой стрелки, если смотреть снаружи
};

// Параллелепипед с центром в нуле; halfExtents — половины размеров по осям.
MeshData makeBox(core::Vec3 halfExtents, core::Vec3 color);
// Горизонтальная плоскость y = 0 со стороной 2·halfSize, нормаль вверх.
MeshData makePlane(float halfSize, core::Vec3 color);

}  // namespace han::render
// Пример: MeshData cube = makeBox({0.5f, 0.5f, 0.5f}, {0.8f, 0.3f, 0.2f}); auto id = renderer->uploadMesh(cube);
