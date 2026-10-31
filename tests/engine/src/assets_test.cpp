#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "han/core/asset_watcher.h"
#include "han/render/ktx2_io.h"
#include "han/render/mesh_io.h"
#include "han/render/texture.h"
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
    if (a.indices != b.indices || a.vertices.size() != b.vertices.size() || a.textures != b.textures ||
        a.submeshes.size() != b.submeshes.size())
        return false;
    for (std::size_t i = 0; i < a.submeshes.size(); ++i) {
        if (a.submeshes[i].firstIndex != b.submeshes[i].firstIndex || a.submeshes[i].indexCount != b.submeshes[i].indexCount ||
            a.submeshes[i].texture != b.submeshes[i].texture)
            return false;
    }
    return std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(render::Vertex)) == 0;
}

// Смещение первого индекса в сериализованном меше: заголовок + вершины.
std::size_t indexOffset(const render::MeshData& m) { return render::kMeshHeaderSize + m.vertices.size() * sizeof(render::Vertex); }

}  // namespace

TEST_CASE("mesh_io: запись и чтение возвращают тот же меш") {
    const render::MeshData box = render::makeBox({1, 2, 3}, {0.1f, 0.2f, 0.3f});
    const auto bytes = render::serializeMesh(box);
    // Заголовок, вершины, индексы, затем два нуля: частей нет, текстур нет.
    CHECK(bytes.size() == render::kMeshHeaderSize + box.vertices.size() * sizeof(render::Vertex) + box.indices.size() * 4 + 8);

    render::MeshData back;
    std::string err;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));
    CHECK(sameMesh(box, back));
}

TEST_CASE("mesh_io: UV сохраняются") {
    const render::MeshData plane = render::makePlane(5, {1, 1, 1}, 4.0f);
    float maxU = 0;
    for (const auto& v : plane.vertices) maxU = std::max({maxU, v.uv[0], v.uv[1]});
    CHECK(maxU == doctest::Approx(4.0f));
    const auto bytes = render::serializeMesh(plane);
    render::MeshData back;
    std::string err;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));
    CHECK(sameMesh(plane, back));
}

TEST_CASE("mesh_io: части меша и имена текстур") {
    render::MeshData m = render::makeBox({1, 1, 1}, {1, 1, 1});
    m.textures = {"lamp.tex0.ktx2", "корея.tex1.ktx2"};
    m.submeshes = {{0, 12, 0}, {12, 12, -1}, {24, 12, 1}};
    const auto bytes = render::serializeMesh(m);
    render::MeshData back;
    std::string err;
    REQUIRE_MESSAGE(render::deserializeMesh(bytes.data(), bytes.size(), back, err), err);
    CHECK(sameMesh(m, back));
    CHECK(render::effectiveSubmeshes(back).size() == 3);
    CHECK(render::effectiveSubmeshes(render::makeBox({1, 1, 1}, {1, 1, 1})).size() == 1);
}

TEST_CASE("mesh_io: некорректные части меша отклоняются") {
    render::MeshData m = render::makeBox({1, 1, 1}, {1, 1, 1});
    m.textures = {"a.ktx2"};
    render::MeshData back;
    std::string err;

    m.submeshes = {{0, 12, 0}, {12, 30, 0}};  // вторая часть заходит за последний индекс (12 + 30 > 36)
    auto bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));

    m.submeshes = {{0, 36, 5}};  // текстуры 5 нет
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));

    m.submeshes = {{0, 12, 0}, {18, 18, 0}};  // между частями дыра (индексы 12..17 никому не принадлежат)
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));

    m.submeshes = {{0, 24, 0}};  // не покрыты последние индексы
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));

    m.submeshes = {{0, 35, 0}};  // не кратно трём
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));
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
    const render::MeshData box = render::makeBox({1, 1, 1}, {1, 1, 1});
    const auto good = render::serializeMesh(box);
    render::MeshData out;
    std::string err;

    CHECK_FALSE(render::deserializeMesh(good.data(), 10, out, err));  // короче заголовка

    auto badMagic = good;
    badMagic[0] = 'X';
    CHECK_FALSE(render::deserializeMesh(badMagic.data(), badMagic.size(), out, err));

    auto oldVersion = good;
    oldVersion[4] = 1;  // формат v1 больше не читается
    CHECK_FALSE(render::deserializeMesh(oldVersion.data(), oldVersion.size(), out, err));
    CHECK(err.find("сварите") != std::string::npos);

    CHECK_FALSE(render::deserializeMesh(good.data(), good.size() - 1, out, err));  // обрезан

    auto extra = good;
    extra.push_back(0);
    CHECK_FALSE(render::deserializeMesh(extra.data(), extra.size(), out, err));  // лишние данные

    auto badIndex = good;
    badIndex[indexOffset(box) + 4 * 35] = 0xFF;  // последний индекс 255 при 24 вершинах
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

TEST_CASE("texture: число мип-уровней и корректность") {
    CHECK(render::mipLevelCount(1, 1) == 1);
    CHECK(render::mipLevelCount(256, 256) == 9);
    CHECK(render::mipLevelCount(64, 16) == 7);  // до 1x1 по большей стороне
    CHECK(render::mipLevelCount(5, 3) == 3);

    render::TextureData bad;
    CHECK_FALSE(bad.valid());
    auto t = render::makeCheckerTexture(8, 2, 0xFFFFFFFF, 0xFF000000);
    CHECK(t.valid());
    t.mips[0].pop_back();
    CHECK_FALSE(t.valid());
}

TEST_CASE("texture: мипы строятся до 1x1, средний цвет считается в линейном свете") {
    // Белый и красный (ABGR): полный белый (255,255,255) и красный (255,0,0). Среднее в линейном свете: G = B = 0.5 → sRGB ≈ 188.
    auto t = render::makeCheckerTexture(8, 1, 0xFFFFFFFF, 0xFF0000FF);
    render::generateMips(t);
    REQUIRE(t.mips.size() == 4);
    CHECK(t.valid());
    CHECK(t.mips.back().size() == 4);
    CHECK(t.mips.back()[0] == 255);
    CHECK(std::abs(static_cast<int>(t.mips.back()[1]) - 188) <= 2);
    CHECK(std::abs(static_cast<int>(t.mips.back()[2]) - 188) <= 2);
    CHECK(t.mips.back()[3] == 255);

    // Линейная текстура усредняется напрямую: 255 и 0 → 128.
    auto lin = render::makeCheckerTexture(2, 1, 0xFFFFFFFF, 0xFF000000);
    lin.srgb = false;
    render::generateMips(lin);
    REQUIRE(lin.mips.size() == 2);
    CHECK(std::abs(static_cast<int>(lin.mips[1][0]) - 128) <= 1);
}

TEST_CASE("texture: неквадратные и нечётные размеры") {
    std::vector<std::uint8_t> px(5 * 3 * 4, 200);
    auto t = render::makeTexture(5, 3, px, true);
    render::generateMips(t);
    CHECK(t.mips.size() == 3);
    CHECK(t.valid());
    CHECK(t.mips[1].size() == 2 * 1 * 4);
    CHECK(t.mips[2].size() == 4);
}

TEST_CASE("ktx2: запись и чтение возвращают ту же текстуру") {
    auto t = render::makeCheckerTexture(32, 4, 0xFF3366CC, 0xFFEEDDAA);
    render::generateMips(t);
    const auto bytes = render::serializeKtx2(t);
    REQUIRE_FALSE(bytes.empty());
    CHECK(std::memcmp(bytes.data() + 1, "KTX 20", 6) == 0);

    render::TextureData back;
    std::string err;
    REQUIRE_MESSAGE(render::deserializeKtx2(bytes.data(), bytes.size(), back, err), err);
    CHECK(back.width == 32);
    CHECK(back.height == 32);
    CHECK(back.srgb);
    CHECK(back.mips == t.mips);

    t.srgb = false;
    const auto linearBytes = render::serializeKtx2(t);
    REQUIRE(render::deserializeKtx2(linearBytes.data(), linearBytes.size(), back, err));
    CHECK_FALSE(back.srgb);
}

TEST_CASE("ktx2: файл на диске и повреждения") {
    const fs::path dir = tempDir("han_ktx2_test");
    auto t = render::makeCheckerTexture(16, 4, 0xFFFFFFFF, 0xFF000000);
    render::generateMips(t);
    std::string err;
    REQUIRE(render::saveKtx2File(dir / "t.ktx2", t, err));
    render::TextureData back;
    REQUIRE(render::loadKtx2File(dir / "t.ktx2", back, err));
    CHECK(back.mips == t.mips);
    CHECK_FALSE(render::loadKtx2File(dir / "нет.ktx2", back, err));

    const auto good = render::serializeKtx2(t);
    CHECK_FALSE(render::deserializeKtx2(good.data(), 40, back, err));  // короче заголовка

    auto badSig = good;
    badSig[1] = 'Z';
    CHECK_FALSE(render::deserializeKtx2(badSig.data(), badSig.size(), back, err));

    CHECK_FALSE(render::deserializeKtx2(good.data(), good.size() - 8, back, err));  // обрезан последний (самый большой) уровень

    auto compressed = good;
    compressed[12] = 0;  // vkFormat = 0 — сжатая (Basis) текстура
    compressed[13] = compressed[14] = compressed[15] = 0;
    CHECK_FALSE(render::deserializeKtx2(compressed.data(), compressed.size(), back, err));
    CHECK(err.find("сжат") != std::string::npos);

    auto hugeLevels = good;
    hugeLevels[40] = 99;  // уровней больше, чем помещается
    CHECK_FALSE(render::deserializeKtx2(hugeLevels.data(), hugeLevels.size(), back, err));

    render::TextureData invalid;
    CHECK(render::serializeKtx2(invalid).empty());
    fs::remove_all(dir);
}

TEST_CASE("mesh_tools: сварка склеивает одинаковые вершины и сохраняет геометрию") {
    render::MeshData m;
    const render::Vertex a{{0, 0, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0}}, b{{1, 0, 0}, {0, 0, 1}, {1, 1, 1}, {1, 0}},
        c{{0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 1}}, d{{1, 1, 0}, {0, 0, 1}, {1, 1, 1}, {1, 1}};
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

TEST_CASE("mesh_tools: вершины с разными UV не склеиваются") {
    render::MeshData m;
    render::Vertex a{{0, 0, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0}};
    render::Vertex b = a;
    b.uv[0] = 0.5f;
    m.vertices = {a, b};
    m.indices = {0, 1, 0};
    tools::weldVertices(m);
    CHECK(m.vertices.size() == 2);
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

TEST_CASE("mesh_tools: оптимизация не меняет число треугольников и сохраняет части меша") {
    render::MeshData box = render::makeBox({1, 1, 1}, {1, 1, 1});
    box.textures = {"a.ktx2", "b.ktx2"};
    box.submeshes = {{0, 18, 0}, {18, 18, 1}};
    tools::optimizeMesh(box);
    CHECK(box.indices.size() == 36);
    CHECK(box.vertices.size() == 24);
    for (const auto i : box.indices) CHECK(i < box.vertices.size());
    REQUIRE(box.submeshes.size() == 2);
    CHECK(box.submeshes[0].indexCount == 18);
    CHECK(box.submeshes[1].firstIndex == 18);
    // После оптимизации меш остаётся корректным для записи.
    const auto bytes = render::serializeMesh(box);
    render::MeshData back;
    std::string err;
    CHECK_MESSAGE(render::deserializeMesh(bytes.data(), bytes.size(), back, err), err);
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

// Минимальный GLB: один треугольник (0,0,0) (1,0,0) (0,1,0), индексы uint16; nodeJson описывает узел с мешем.
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

bool importFromBytes(const std::vector<std::uint8_t>& glb, tools::ImportResult& result, std::string& err) {
    const fs::path dir = tempDir("han_gltf_test");
    const fs::path file = dir / "t.glb";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    }
    const bool ok = tools::importGltf(file, result, err);
    fs::remove_all(dir);
    return ok;
}

}  // namespace

TEST_CASE("glTF: узел сдвигает вершины, нормали считаются, обход сохраняется") {
    tools::ImportResult r;
    std::string err;
    REQUIRE_MESSAGE(importFromBytes(makeTriangleGlb("{\"mesh\":0,\"translation\":[1,2,3]}"), r, err), err);
    const render::MeshData& mesh = r.mesh;
    REQUIRE(mesh.vertices.size() == 3);
    REQUIRE(mesh.indices.size() == 3);
    CHECK(mesh.vertices[0].position[0] == doctest::Approx(1));
    CHECK(mesh.vertices[0].position[1] == doctest::Approx(2));
    CHECK(mesh.vertices[0].position[2] == doctest::Approx(3));
    CHECK(mesh.vertices[1].position[0] == doctest::Approx(2));
    CHECK(mesh.vertices[0].normal[2] == doctest::Approx(1));  // против часовой стрелки в плоскости XY: нормаль +Z
    CHECK(mesh.indices[0] == 0);
    CHECK(mesh.indices[1] == 1);
    CHECK(mesh.submeshes.empty());  // без текстур весь меш одна часть
    CHECK(r.textures.empty());
}

TEST_CASE("glTF: зеркальный масштаб переворачивает обход, нормаль остаётся наружу") {
    tools::ImportResult r;
    std::string err;
    REQUIRE_MESSAGE(importFromBytes(makeTriangleGlb("{\"mesh\":0,\"scale\":[-1,1,1]}"), r, err), err);
    REQUIRE(r.mesh.indices.size() == 3);
    CHECK(r.mesh.vertices[1].position[0] == doctest::Approx(-1));
    CHECK(r.mesh.indices[1] == 2);  // индексы 1 и 2 поменяны местами
    CHECK(r.mesh.vertices[0].normal[2] == doctest::Approx(1));
}

TEST_CASE("glTF: реальный файл из Blender (куб и конус), весь путь до .hmesh") {
    tools::ImportResult r;
    std::string err;
    REQUIRE_MESSAGE(tools::importGltf(fs::path(HAN_TEST_DATA_DIR) / "blender_cube_cone.glb", r, err), err);
    render::MeshData& mesh = r.mesh;
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

#ifdef HAN_WITH_STB
TEST_CASE("glTF: текстура из Blender извлекается, UV и подмеши на месте") {
    tools::ImportResult r;
    std::string err;
    REQUIRE_MESSAGE(tools::importGltf(fs::path(HAN_TEST_DATA_DIR) / "blender_textured_cube.glb", r, err), err);
    for (const auto& w : r.warnings) MESSAGE(w);

    REQUIRE(r.textures.size() == 1);
    const tools::ImportedTexture& tex = r.textures[0];
    REQUIRE(tex.width == 64);
    REQUIRE(tex.height == 64);
    REQUIRE(tex.rgba.size() == 64u * 64u * 4u);
    // Левый верхний угол изображения синий (метка из make_textured_fixture.py): так проверяется ориентация картинки.
    CHECK(tex.rgba[0] == 0);
    CHECK(tex.rgba[1] == 0);
    CHECK(tex.rgba[2] == 255);
    CHECK(tex.rgba[3] == 255);
    // Следующая клетка по горизонтали (x = 8) — белая, ниже синей (y = 8) — тоже белая или красная, но не синяя.
    const std::size_t right = 8 * 4;
    CHECK(tex.rgba[right + 2] == 255);
    CHECK(tex.rgba[right + 0] == 255);  // белая клетка (255, 255, 255)

    REQUIRE(r.mesh.submeshes.size() == 1);
    CHECK(r.mesh.submeshes[0].texture == 0);
    CHECK(r.mesh.submeshes[0].firstIndex == 0);
    CHECK(r.mesh.submeshes[0].indexCount == r.mesh.indices.size());

    bool anyUv = false;
    for (const auto& v : r.mesh.vertices) {
        CHECK(v.uv[0] >= -0.001f);
        CHECK(v.uv[0] <= 1.001f);
        CHECK(v.uv[1] >= -0.001f);
        CHECK(v.uv[1] <= 1.001f);
        anyUv = anyUv || v.uv[0] != 0 || v.uv[1] != 0;
    }
    CHECK(anyUv);

    // Полный путь варки: текстура → мипы → KTX2 → чтение.
    render::TextureData texture = render::makeTexture(tex.width, tex.height, tex.rgba, true);
    render::generateMips(texture);
    CHECK(texture.mips.size() == 7);
    const auto bytes = render::serializeKtx2(texture);
    render::TextureData back;
    REQUIRE(render::deserializeKtx2(bytes.data(), bytes.size(), back, err));
    CHECK(back.mips == texture.mips);
}
#endif  // HAN_WITH_STB

TEST_CASE("glTF: ошибки отдаются, а не падают") {
    tools::ImportResult r;
    std::string err;
    CHECK_FALSE(tools::importGltf("нет-такого-файла.glb", r, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(importFromBytes({'g', 'l', 'T', 'F', 0, 0}, r, err));
}
#endif  // HAN_WITH_GLTF
