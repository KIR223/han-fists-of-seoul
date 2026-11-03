#pragma once
// Формат готового меша .hmesh (E3): то, что пишет han_cooker и читает игра. Без зависимостей, порядок байт — little-endian.
//
//   смещение  размер  поле
//   0         4       магия "HMSH"
//   4         4       версия формата (сейчас 6; версии 3/4/5 читаются с прежними материалами)
//   8         4       число вершин
//   12        4       число индексов (кратно 3)
//   16        4       размер вершины в байтах (sizeof(Vertex) = 44: позиция, нормаль, цвет, uv — по float)
//   20        12      ограничивающий бокс: минимум (x, y, z)
//   32        12      ограничивающий бокс: максимум (x, y, z)
//   44        ...     вершины, затем индексы uint32, затем:
//             4       число частей (подмешей)
//             60 * N  часть: первый индекс, число индексов (кратно 3), номер текстуры (int32, -1 — нет),
//                     металличность и шероховатость (float, 0..1), номера карт нормалей и шероховатости (int32),
//                     alphaCutoff (float), alphaMode (uint32), doubleSided (uint32, 0/1),
//                     opacity (float), emissiveTexture (int32), emissive RGB (3 float)
//             4       число текстур
//             ...     текстура: uint16 длина имени, имя в UTF-8 (файл .ktx2 рядом с мешем)
//
// Версии 1 и 2 больше не читаются: файлов такого вида нигде не осталось (сварите меш заново).
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/mesh.h"

namespace han::render {

constexpr std::uint32_t kMeshFormatVersion = 6;
constexpr std::size_t kMeshHeaderSize = 44;

struct MeshBounds {
    core::Vec3 min, max;
};

MeshBounds computeBounds(const MeshData& mesh);

std::vector<std::uint8_t> serializeMesh(const MeshData& mesh);
// Проверяет заголовок, размеры (с защитой от переполнения), что индексы указывают на существующие вершины,
// что части лежат внутри индексов и ссылаются на существующие текстуры.
bool deserializeMesh(const std::uint8_t* data, std::size_t size, MeshData& out, std::string& error);

bool saveMeshFile(const std::filesystem::path& path, const MeshData& mesh, std::string& error);
bool loadMeshFile(const std::filesystem::path& path, MeshData& out, std::string& error);

}  // namespace han::render
// Пример: MeshData m; std::string err; if (!loadMeshFile("data/props/lamp.hmesh", m, err)) log(err); renderer->uploadMesh(m);
