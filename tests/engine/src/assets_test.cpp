#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "han/core/asset_watcher.h"
#include "han/render/mesh_io.h"
#include "han/tools/gltf_import.h"
#include "han/tools/mesh_tools.h"

namespace fs = std::filesystem;
using namespace han;

namespace {

fs::path tempDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

bool sameMesh(const render::MeshData& a, const render::MeshData& b) {
    return a.indices == b.indices && a.vertices.size() == b.vertices.size() &&
           std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(render::Vertex)) == 0;
}

}  // namespace

TEST_CASE("mesh_io: запись и чтение возвращают тот же меш") {
    const render::MeshData box = render::makeBox({1, 2, 3}, {0.1f, 0.2f, 0.3f});
    const auto bytes = render::serializeMesh(box);
    CHECK(bytes.size() == render::kMeshHeaderSize + box.vertices.size() * sizeof(render::Vertex) + box.indices.size() * 4);

    render::MeshData back;
    std::string err;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));
    CHECK(sameMesh(box, back));
}

TEST_CASE("mesh_io: файл на диске") {
    const fs::path dir = tempDir("han_meshio_test");
    const render::MeshData plane = render::makePlane(5, {1, 1, 1});
    std::string err;
    REQUIRE(render::saveMeshFile(dir / "p.hmesh", plane, err));
    render::MeshData back;
    REQUIRE(render::loadMeshFile(dir / "p.hmesh", back, err));
    CHECK(sameMesh(plane, back));
    CHECK_FALSE(render::loadMeshFile(dir / "нет.hmesh", back, err));
    fs::remove_all(dir);
}

TEST_CASE("mesh_io: повреждённые данные отклоняются") {
    const auto good = render::serializeMesh(render::makeBox({1, 1, 1}, {1, 1, 1}));
    render::MeshData out;
    std::string err;

    CHECK_FALSE(render::deserializeMesh(good.data(), 10, out, err));  // короче заголовка

    auto badMagic = good;
    badMagic[0] = 'X';
    CHECK_FALSE(render::deserializeMesh(badMagic.data(), badMagic.size(), out, err));

    auto badVersion = good;
    badVersion[4] = 99;
    CHECK_FALSE(render::deserializeMesh(badVersion.data(), badVersion.size(), out, err));

    CHECK_FALSE(render::deserializeMesh(good.data(), good.size() - 1, out, err));  // обрезан

    auto badIndex = good;
    const std::size_t lastIndex = badIndex.size() - 4;
    badIndex[lastIndex] = 0xFF;  // индекс 255 при 24 вершинах
    CHECK_FALSE(render::deserializeMesh(badIndex.data(), badIndex.size(), out, err));

    auto hugeCount = good;
    hugeCount[11] = 0x7F;  // число вершин порядка 2^31
    CHECK_FALSE(render::deserializeMesh(hugeCount.data(), hugeCount.size(), out, err));
}

TEST_CASE("mesh_io: границы меша") {
    const render::MeshBounds b = render::computeBounds(render::makeBox({1, 2, 3}, {1, 1, 1}));
    CHECK(b.min.x == doctest::Approx(-1));
    CHECK(b.max.y == doctest::Approx(2));
    CHECK(b.min.z == doctest::Approx(-3));
}

TEST_CASE("mesh_tools: сварка склеивает одинаковые вершины и сохраняет геометрию") {
    render::MeshData m;
    const render::Vertex a{{0, 0, 0}, {0, 0, 1}, {1, 1, 1}}, b{{1, 0, 0}, {0, 0, 1}, {1, 1, 1}}, c{{0, 1, 0}, {0, 0, 1}, {1, 1, 1}},
        d{{1, 1, 0}, {0, 0, 1}, {1, 1, 1}};
    m.vertices = {a, b, c, b, d, c};  // два треугольника с общим ребром, вершины повторены
    m.indices = {0, 1, 2, 3, 4, 5};
    tools::weldVertices(m);
    CHECK(m.vertices.size() == 4);
    CHECK(m.indices.size() == 6);
    for (std::size_t i = 0; i < m.indices.size(); ++i) {
        CHECK(m.indices[i] < m.vertices.size());
    }
    // Третий и шестой индексы указывали на одну и ту же вершину c.
    CHECK(m.indices[2] == m.indices[5]);
}

TEST_CASE("mesh_tools: нормали плоскости смотрят вверх") {
    render::MeshData plane = render::makePlane(3, {1, 1, 1});
    for (auto& v : plane.vertices) v.normal[0] = v.normal[1] = v.normal[2] = 0;
    tools::computeSmoothNormals(plane);
    for (const auto& v : plane.vertices) {
        CHECK(v.normal[0] == doctest::Approx(0).epsilon(1e-5));
        CHECK(v.normal[1] == doctest::Approx(1));
        CHECK(v.normal[2] == doctest::Approx(0).epsilon(1e-5));
    }
}

TEST_CASE("mesh_tools: оптимизация не меняет число треугольников") {
    render::MeshData box = render::makeBox({1, 1, 1}, {1, 1, 1});
    tools::optimizeMesh(box);
    CHECK(box.indices.size() == 36);
    CHECK(box.vertices.size() == 24);
    for (const auto i : box.indices) CHECK(i < box.vertices.size());
}

TEST_CASE("AssetWatcher: изменение объявляется после стабилизации файла") {
    const fs::path dir = tempDir("han_watcher_test");
    const fs::path file = dir / "a.hmesh";
    core::AssetWatcher w;
    w.watch(file);
    CHECK(w.poll().empty());  // файла ещё нет

    { std::ofstream(file, std::ios::binary) << "v1"; }
    CHECK(w.poll().empty());                 // замечено
    const auto first = w.poll();             // не менялось — объявляем
    REQUIRE(first.size() == 1);
    CHECK(first[0] == file);
    CHECK(w.poll().empty());

    { std::ofstream(file, std::ios::binary) << "version two"; }
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
    CHECK(w.poll().empty());
    CHECK(w.poll().size() == 1);
    CHECK(w.poll().empty());
    fs::remove_all(dir);
}

TEST_CASE("AssetWatcher: перестали следить — тишина") {
    const fs::path dir = tempDir("han_watcher_test2");
    const fs::path file = dir / "b.txt";
    { std::ofstream(file) << "x"; }
    core::AssetWatcher w;
    w.watch(file);
    w.unwatch(file);
    { std::ofstream(file) << "changed"; }
    CHECK(w.poll().empty());
    CHECK(w.poll().empty());
    fs::remove_all(dir);
}

#ifdef HAN_WITH_GLTF
namespace {

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void putF(std::vector<std::uint8_t>& v, float f) {
    std::uint32_t x;
    std::memcpy(&x, &f, 4);
    put32(v, x);
}

// Минимальный GLB: один треугольник (0,0,0) (1,0,0) (0,1,0), индексы uint16, узел сдвинут на translation и масштабирован на scaleX.
std::vector<std::uint8_t> makeTriangleGlb(const std::string& nodeJson) {
    std::vector<std::uint8_t> bin;
    for (const float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) putF(bin, f);
    for (const std::uint16_t i : {0, 1, 2}) {
        bin.push_back(static_cast<std::uint8_t>(i & 0xFF));
        bin.push_back(static_cast<std::uint8_t>(i >> 8));
    }
    bin.push_back(0);
    bin.push_back(0);  // выравнивание до 4 байт: 36 + 6 + 2 = 44

    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[" + nodeJson +
        "],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"max\":[1,1,0],\"min\":[0,0,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":44}]}";
    while (json.size() % 4 != 0) json.push_back(' ');

    std::vector<std::uint8_t> glb;
    put32(glb, 0x46546C67);  // "glTF"
    put32(glb, 2);
    put32(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    put32(glb, static_cast<std::uint32_t>(json.size()));
    put32(glb, 0x4E4F534A);  // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    put32(glb, static_cast<std::uint32_t>(bin.size()));
    put32(glb, 0x004E4942);  // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

bool importFromBytes(const std::vector<std::uint8_t>& glb, render::MeshData& mesh, std::string& err) {
    const fs::path dir = tempDir("han_gltf_test");
    const fs::path file = dir / "t.glb";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    }
    const bool ok = tools::importGltf(file, mesh, err);
    fs::remove_all(dir);
    return ok;
}

}  // namespace

TEST_CASE("glTF: узел сдвигает вершины, нормали считаются, обход сохраняется") {
    render::MeshData mesh;
    std::string err;
    REQUIRE_MESSAGE(importFromBytes(makeTriangleGlb("{\"mesh\":0,\"translation\":[1,2,3]}"), mesh, err), err);
    REQUIRE(mesh.vertices.size() == 3);
    REQUIRE(mesh.indices.size() == 3);
    CHECK(mesh.vertices[0].position[0] == doctest::Approx(1));
    CHECK(mesh.vertices[0].position[1] == doctest::Approx(2));
    CHECK(mesh.vertices[0].position[2] == doctest::Approx(3));
    CHECK(mesh.vertices[1].position[0] == doctest::Approx(2));
    CHECK(mesh.vertices[0].normal[2] == doctest::Approx(1));  // против часовой стрелки в плоскости XY: нормаль +Z
    CHECK(mesh.indices[0] == 0);
    CHECK(mesh.indices[1] == 1);
}

TEST_CASE("glTF: зеркальный масштаб переворачивает обход, нормаль остаётся наружу") {
    render::MeshData mesh;
    std::string err;
    REQUIRE_MESSAGE(importFromBytes(makeTriangleGlb("{\"mesh\":0,\"scale\":[-1,1,1]}"), mesh, err), err);
    REQUIRE(mesh.indices.size() == 3);
    CHECK(mesh.vertices[1].position[0] == doctest::Approx(-1));
    CHECK(mesh.indices[1] == 2);  // индексы 1 и 2 поменяны местами
    CHECK(mesh.vertices[0].normal[2] == doctest::Approx(1));
}

TEST_CASE("glTF: реальный файл из Blender (куб и конус), весь путь до .hmesh") {
    render::MeshData mesh;
    std::string err;
    REQUIRE_MESSAGE(tools::importGltf(fs::path(HAN_TEST_DATA_DIR) / "blender_cube_cone.glb", mesh, err), err);
    CHECK(mesh.vertices.size() >= 8 + 5);
    CHECK(mesh.indices.size() % 3 == 0);
    for (const auto i : mesh.indices) REQUIRE(i < mesh.vertices.size());

    // Куб 2x2x2 стоит на земле в начале координат, конус справа (x = 3): Blender Z-вверх → glTF Y-вверх.
    const render::MeshBounds b = render::computeBounds(mesh);
    CHECK(b.min.x == doctest::Approx(-1).epsilon(0.02));
    CHECK(b.max.x == doctest::Approx(3.7).epsilon(0.02));
    CHECK(b.min.y == doctest::Approx(0).epsilon(0.02));
    CHECK(b.max.y == doctest::Approx(2).epsilon(0.02));
    CHECK(b.min.z == doctest::Approx(-1).epsilon(0.02));
    CHECK(b.max.z == doctest::Approx(1).epsilon(0.02));

    for (const auto& v : mesh.vertices) {
        const float len = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
        REQUIRE(len == doctest::Approx(1).epsilon(0.01));
        for (const float c : v.color) REQUIRE((c >= 0 && c <= 1));
    }

    // Обход против часовой стрелки: геометрическая нормаль каждого треугольника не противоречит нормали вершин.
    int wrong = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b2 = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const core::Vec3 pa{a.position[0], a.position[1], a.position[2]}, pb{b2.position[0], b2.position[1], b2.position[2]},
            pc{c.position[0], c.position[1], c.position[2]};
        const core::Vec3 n = core::cross(pb - pa, pc - pa);
        if (core::dot(n, {a.normal[0], a.normal[1], a.normal[2]}) < 0) ++wrong;
    }
    CHECK(wrong == 0);

    // Варка и обратное чтение дают тот же меш.
    tools::optimizeMesh(mesh);
    const auto bytes = render::serializeMesh(mesh);
    render::MeshData back;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));
    CHECK(sameMesh(mesh, back));
}

TEST_CASE("glTF: ошибки отдаются, а не падают") {
    render::MeshData mesh;
    std::string err;
    CHECK_FALSE(tools::importGltf("нет-такого-файла.glb", mesh, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(importFromBytes({'g', 'l', 'T', 'F', 0, 0}, mesh, err));
}
#endif  // HAN_WITH_GLTF
