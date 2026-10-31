#pragma once
// Импорт glTF/GLB (E3) на fastgltf. Доступен, если собрано с HAN_WITH_GLTF.
#ifdef HAN_WITH_GLTF

#include <filesystem>
#include <string>

#include "han/render/mesh.h"

namespace han::tools {

// Читает .gltf (с внешними буферами) или .glb, применяет мировые преобразования узлов сцены и объединяет все треугольные
// примитивы в один меш. Цвет вершин — baseColorFactor материала (без материала — серый). Нормали берутся из файла,
// при отсутствии считаются гладкими. Единицы — метры, Y вверх (как в glTF), порядок обхода против часовой стрелки.
// Ограничение v1: UV и текстуры не читаются (появятся с текстурами в рендерере); нормали преобразуются без обратной
// транспонированной матрицы, поэтому неравномерный масштаб узлов даёт неточное освещение.
bool importGltf(const std::filesystem::path& path, render::MeshData& out, std::string& error);

}  // namespace han::tools
// Пример: MeshData m; std::string err; if (!importGltf("lamp.glb", m, err)) log(err);

#endif  // HAN_WITH_GLTF
