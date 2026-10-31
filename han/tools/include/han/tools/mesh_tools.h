#pragma once
// Обработка мешей при варке (E3): сварка одинаковых вершин, нормали, оптимизация под кэш GPU.
#include "han/render/mesh.h"

namespace han::tools {

// Объединяет побитово одинаковые вершины и переписывает индексы. Порядок треугольников сохраняется.
void weldVertices(render::MeshData& mesh);

// Пересчитывает гладкие нормали (с весом по площади треугольника). Вершины, общие для нескольких граней, усредняются.
void computeSmoothNormals(render::MeshData& mesh);

// Сварка + (если собрано с meshoptimizer) упорядочивание индексов и вершин под кэш GPU. Внешний вид не меняется.
void optimizeMesh(render::MeshData& mesh);

}  // namespace han::tools
// Пример: computeSmoothNormals(mesh); optimizeMesh(mesh); saveMeshFile("lamp.hmesh", mesh, err);
