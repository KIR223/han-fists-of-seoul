#include <doctest/doctest.h>

#include <chrono>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "han/core/asset_watcher.h"
#include "han/core/json.h"
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
            a.submeshes[i].texture != b.submeshes[i].texture || a.submeshes[i].metallic != b.submeshes[i].metallic ||
            a.submeshes[i].roughness != b.submeshes[i].roughness || a.submeshes[i].normalTexture != b.submeshes[i].normalTexture ||
            a.submeshes[i].roughnessTexture != b.submeshes[i].roughnessTexture || a.submeshes[i].alphaMode != b.submeshes[i].alphaMode ||
            a.submeshes[i].alphaCutoff != b.submeshes[i].alphaCutoff || a.submeshes[i].doubleSided != b.submeshes[i].doubleSided ||
            a.submeshes[i].opacity != b.submeshes[i].opacity || a.submeshes[i].emissiveTexture != b.submeshes[i].emissiveTexture ||
            !std::equal(std::begin(a.submeshes[i].emissive), std::end(a.submeshes[i].emissive), std::begin(b.submeshes[i].emissive)))
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
    m.submeshes = {{0, 12, 0, 0.0f, 0.9f}, {12, 12, -1, 1.0f, 0.25f}, {24, 12, 1}};
    const auto bytes = render::serializeMesh(m);
    render::MeshData back;
    std::string err;
    REQUIRE_MESSAGE(render::deserializeMesh(bytes.data(), bytes.size(), back, err), err);
    CHECK(sameMesh(m, back));
    CHECK(render::effectiveSubmeshes(back).size() == 3);
    CHECK(render::effectiveSubmeshes(render::makeBox({1, 1, 1}, {1, 1, 1})).size() == 1);
}

TEST_CASE("mesh_tools: пространственные части сохраняют геометрию и параметры стекла") {
    auto mesh = render::makeBox({20, 1, 20}, {1, 1, 1});
    mesh.submeshes = {{0, 36, -1}};
    mesh.submeshes[0].alphaMode = render::AlphaMode::Blend;
    mesh.submeshes[0].opacity = .3f;
    mesh.submeshes[0].emissive[0] = 4;
    const auto original = mesh;
    REQUIRE(tools::partitionMesh(mesh, 10));
    CHECK(mesh.submeshes.size() > 1);
    auto before = original.indices, after = mesh.indices;
    std::sort(before.begin(), before.end()); std::sort(after.begin(), after.end());
    CHECK(before == after);
    CHECK(std::memcmp(mesh.vertices.data(), original.vertices.data(), mesh.vertices.size() * sizeof(render::Vertex)) == 0);
    for (const auto& part : mesh.submeshes) {
        CHECK(part.alphaMode == render::AlphaMode::Blend); CHECK(part.opacity == .3f); CHECK(part.emissive[0] == 4);
    }
    auto bytes = render::serializeMesh(mesh);
    render::MeshData back; std::string error;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), back, error));
    CHECK(sameMesh(mesh, back));
    CHECK_FALSE(tools::partitionMesh(mesh, 0));
    CHECK_FALSE(tools::partitionMesh(mesh, std::nanf("")));
    mesh.indices[0] = static_cast<std::uint32_t>(mesh.vertices.size());
    CHECK_FALSE(tools::partitionMesh(mesh, 10));
}

TEST_CASE("mesh_io: существующие модели версии 3 читаются без карт PBR") {
    auto mesh = render::makeBox({1, 1, 1}, {1, 1, 1});
    mesh.textures = {"old.ktx2"};
    mesh.submeshes = {{0, 36, 0, 0.2f, 0.8f}};
    auto bytes = render::serializeMesh(mesh);
    const std::size_t maps = render::kMeshHeaderSize + mesh.vertices.size() * sizeof(render::Vertex) + mesh.indices.size() * 4 + 4 + 20;
    bytes.erase(bytes.begin() + maps, bytes.begin() + maps + 40);
    bytes[4] = 3;
    render::MeshData restored;
    std::string error;
    REQUIRE_MESSAGE(render::deserializeMesh(bytes.data(), bytes.size(), restored, error), error);
    CHECK(sameMesh(mesh, restored));
    CHECK(restored.submeshes.front().normalTexture == -1);
    CHECK(restored.submeshes.front().roughnessTexture == -1);
    bytes = render::serializeMesh(mesh);
    bytes.erase(bytes.begin() + maps + 8, bytes.begin() + maps + 40);
    bytes[4] = 4;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), restored, error));
    CHECK(sameMesh(mesh, restored));
    bytes = render::serializeMesh(mesh);
    bytes.erase(bytes.begin() + maps + 20, bytes.begin() + maps + 40);
    bytes[4] = 5;
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), restored, error));
    CHECK(sameMesh(mesh, restored));
}

TEST_CASE("mesh_io: стекло и HDR-свечение сохраняются, повреждённые параметры отклоняются") {
    auto mesh = render::makeBox({1, 1, 1}, {1, 1, 1});
    mesh.textures = {"emission.ktx2"};
    mesh.submeshes = {{0, 36, -1}};
    auto& part = mesh.submeshes.front();
    part.alphaMode = render::AlphaMode::Blend;
    part.opacity = .25f;
    part.emissiveTexture = 0;
    part.emissive[0] = 12.f; part.emissive[1] = 2.f;
    render::MeshData back;
    std::string error;
    auto bytes = render::serializeMesh(mesh);
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), back, error));
    CHECK(sameMesh(mesh, back));
    for (const float bad : {-1.f, 2.f, std::nanf("")}) {
        part.opacity = bad;
        bytes = render::serializeMesh(mesh);
        CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, error));
    }
    part.opacity = .25f;
    part.emissiveTexture = 1;
    bytes = render::serializeMesh(mesh);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, error));
    part.emissiveTexture = 0;
    part.emissive[0] = std::numeric_limits<float>::infinity();
    bytes = render::serializeMesh(mesh);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, error));
}

TEST_CASE("mesh_io: режим листвы сохраняется, неверная альфа отклоняется") {
    auto mesh = render::makeBox({1, 1, 1}, {1, 1, 1});
    mesh.textures = {"leaves.ktx2"};
    mesh.submeshes = {{0, 36, 0, 0.f, 0.8f, -1, -1, 0.3f, render::AlphaMode::Mask, true}};
    render::MeshData restored;
    std::string error;
    auto bytes = render::serializeMesh(mesh);
    REQUIRE(render::deserializeMesh(bytes.data(), bytes.size(), restored, error));
    CHECK(sameMesh(mesh, restored));
    mesh.submeshes.front().alphaCutoff = std::nanf("");
    bytes = render::serializeMesh(mesh);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), restored, error));
    mesh.submeshes.front().alphaCutoff = 0.5f;
    mesh.submeshes.front().alphaMode = static_cast<render::AlphaMode>(100);
    bytes = render::serializeMesh(mesh);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), restored, error));
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
    m.submeshes = {{0, 36, 0, 0.f, 0.5f, 3, -1}};
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));

    m.submeshes = {{0, 12, 0}, {18, 18, 0}};  // между частями дыра (индексы 12..17 никому не принадлежат)
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));

    m.submeshes = {{0, 36, 0, 1.5f, 0.5f}};  // металличность вне 0..1
    bytes = render::serializeMesh(m);
    CHECK_FALSE(render::deserializeMesh(bytes.data(), bytes.size(), back, err));
    m.submeshes = {{0, 36, 0, 0.0f, std::nanf("")}};  // NaN в шероховатости
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
    oldVersion[4] = 2;  // формат v2 (без параметров материала) больше не читается
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
std::vector<std::uint8_t> makeTriangleGlb(const std::string& nodeJson, int colors = 0) {
    std::vector<std::uint8_t> bin;
    for (const float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) putF(bin, f);
    for (const std::uint16_t i : {0, 1, 2}) {
        bin.push_back(static_cast<std::uint8_t>(i & 0xFF));
        bin.push_back(static_cast<std::uint8_t>(i >> 8));
    }
    bin.push_back(0);
    bin.push_back(0);  // выравнивание до 4 байт: 36 + 6 + 2 = 44
    const int channels = colors == 1 ? 3 : 4;
    if (colors) {
        for (int vertex = 0; vertex < 3; ++vertex)
            for (int channel = 0; channel < channels; ++channel) {
                if (colors == 3) bin.push_back(channel == vertex ? 255 : 128);
                else putF(bin, channel == vertex ? 1.f : 0.25f);
            }
    }

    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[" + nodeJson +
        "],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"max\":[1,1,0],\"min\":[0,0,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":44}]}";
    if (colors) {
        core::Json root;
        std::string error;
        core::parseJson(json, root, error);
        auto attributes = root["meshes"].at(0)["primitives"].at(0)["attributes"];
        attributes.set("COLOR_0", core::Json::number(2));
        auto primitive = root["meshes"].at(0)["primitives"].at(0);
        primitive.set("attributes", attributes);
        primitive.set("material", core::Json::number(0));
        auto primitives = core::Json::array(); primitives.push(primitive);
        auto mesh = root["meshes"].at(0); mesh.set("primitives", primitives);
        auto meshes = core::Json::array(); meshes.push(mesh); root.set("meshes", meshes);
        core::Json material;
        core::parseJson(R"({"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.75,1,1]}})", material, error);
        auto materials = core::Json::array(); materials.push(material); root.set("materials", materials);
        core::Json accessor;
        core::parseJson(R"({"bufferView":2,"count":3})", accessor, error);
        accessor.set("type", core::Json::string(channels == 3 ? "VEC3" : "VEC4"));
        accessor.set("componentType", core::Json::number(colors == 3 ? 5121 : 5126));
        if (colors == 3) accessor.set("normalized", core::Json::boolean(true));
        auto accessors = root["accessors"]; accessors.push(accessor); root.set("accessors", accessors);
        auto view = core::Json::object();
        view.set("buffer", core::Json::number(0)); view.set("byteOffset", core::Json::number(44));
        view.set("byteLength", core::Json::number(bin.size() - 44));
        auto views = root["bufferViews"]; views.push(view); root.set("bufferViews", views);
        auto buffer = core::Json::object(); buffer.set("byteLength", core::Json::number(bin.size()));
        auto buffers = core::Json::array(); buffers.push(buffer); root.set("buffers", buffers);
        json = root.dump();
    }
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

TEST_CASE("glTF: цвета вершин RGB/RGBA и UNORM умножаются на цвет материала") {
    for (const int encoding : {1, 2, 3}) {
        tools::ImportResult result;
        std::string error;
        REQUIRE_MESSAGE(importFromBytes(makeTriangleGlb("{\"mesh\":0}", encoding), result, error), error);
        REQUIRE(result.mesh.vertices.size() == 3);
        const float secondary = encoding == 3 ? 128.f / 255.f : .25f;
        const float factor[] = {.5f, .75f, 1.f};
        for (int vertex = 0; vertex < 3; ++vertex)
            for (int channel = 0; channel < 3; ++channel)
                CHECK(result.mesh.vertices[vertex].color[channel] == doctest::Approx(factor[channel] * (vertex == channel ? 1.f : secondary)));
    }
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
TEST_CASE("texture: прозрачный фон листвы не создаёт светлые ореолы в мипах") {
    auto image = render::makeTexture(2, 2, {0, 255, 0, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0}, true);
    render::generateMips(image, true);
    REQUIRE(image.mips.size() == 2);
    CHECK(image.mips[1][0] == 0);
    CHECK(image.mips[1][1] == 255);
    CHECK(image.mips[1][2] == 0);
    CHECK(image.mips[1][3] == 64);
    auto empty = render::makeTexture(2, 2, std::vector<std::uint8_t>(16, 0), true);
    render::generateMips(empty, true);
    CHECK(empty.mips[1] == std::vector<std::uint8_t>(4, 0));
}

TEST_CASE("glTF: PBR-карты сохраняют линейный цвет, канал G и множитель шероховатости") {
    const auto file = fs::path(HAN_TEST_DATA_DIR) / "blender_textured_cube.glb";
    std::ifstream input(file, std::ios::binary);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    REQUIRE(bytes.size() > 20);
    std::uint32_t jsonLength = 0;
    std::memcpy(&jsonLength, bytes.data() + 12, 4);
    REQUIRE(20u + jsonLength < bytes.size());
    core::Json root;
    std::string error;
    REQUIRE(core::parseJson(std::string_view(reinterpret_cast<const char*>(bytes.data() + 20), jsonLength), root, error));
    auto materials = core::Json::array();
    auto material = root["materials"].at(0);
    const auto imageInfo = material["pbrMetallicRoughness"]["baseColorTexture"];
    material.set("normalTexture", imageInfo);
    material.set("emissiveTexture", imageInfo);
    core::Json emission;
    REQUIRE(core::parseJson("[0.2,0.4,0.6]", emission, error));
    material.set("emissiveFactor", emission);
    core::Json extensions;
    REQUIRE(core::parseJson(R"({"KHR_materials_emissive_strength":{"emissiveStrength":5}})", extensions, error));
    material.set("extensions", extensions);
    auto used = core::Json::array(); used.push(core::Json::string("KHR_materials_emissive_strength")); root.set("extensionsUsed", used);
    material.set("alphaMode", core::Json::string("MASK"));
    material.set("alphaCutoff", core::Json::number(0.35));
    material.set("doubleSided", core::Json::boolean(true));
    auto pbr = material["pbrMetallicRoughness"];
    pbr.set("metallicRoughnessTexture", imageInfo);
    pbr.set("roughnessFactor", core::Json::number(0.5));
    auto factor = core::Json::array();
    factor.push(core::Json::number(1)); factor.push(core::Json::number(1)); factor.push(core::Json::number(1)); factor.push(core::Json::number(0.5));
    pbr.set("baseColorFactor", factor);
    material.set("pbrMetallicRoughness", pbr);
    materials.push(material); root.set("materials", materials);
    auto json = root.dump();
    while (json.size() % 4) json.push_back(' ');
    std::vector<std::uint8_t> modified;
    put32(modified, 0x46546C67); put32(modified, 2);
    put32(modified, static_cast<std::uint32_t>(bytes.size() - jsonLength + json.size()));
    put32(modified, static_cast<std::uint32_t>(json.size())); put32(modified, 0x4E4F534A);
    modified.insert(modified.end(), json.begin(), json.end());
    modified.insert(modified.end(), bytes.begin() + 20 + jsonLength, bytes.end());
    tools::ImportResult result;
    REQUIRE_MESSAGE(importFromBytes(modified, result, error), error);
    REQUIRE(result.textures.size() == 4);
    REQUIRE_FALSE(result.mesh.submeshes.empty());
    const auto& part = result.mesh.submeshes.front();
    CHECK(part.alphaMode == render::AlphaMode::Mask);
    CHECK(part.alphaCutoff == doctest::Approx(0.35f));
    CHECK(part.doubleSided);
    REQUIRE(part.emissiveTexture >= 0);
    CHECK(part.emissive[0] == doctest::Approx(1)); CHECK(part.emissive[1] == doctest::Approx(2)); CHECK(part.emissive[2] == doctest::Approx(3));
    CHECK(result.textures[part.emissiveTexture].srgb);
    REQUIRE(part.texture >= 0); REQUIRE(part.normalTexture >= 0); REQUIRE(part.roughnessTexture >= 0);
    const auto& color = result.textures[part.texture];
    const auto& normal = result.textures[part.normalTexture];
    const auto& rough = result.textures[part.roughnessTexture];
    CHECK(color.srgb); CHECK_FALSE(normal.srgb); CHECK_FALSE(rough.srgb);
    CHECK(color.alphaWeighted); CHECK_FALSE(normal.alphaWeighted);
    for (std::size_t pixel = 0; pixel < color.rgba.size(); pixel += 4) {
        REQUIRE(color.rgba[pixel] == normal.rgba[pixel]);
        REQUIRE(color.rgba[pixel + 3] == 128);
        REQUIRE(normal.rgba[pixel + 3] == 255);
        const auto value = static_cast<std::uint8_t>(std::lround(color.rgba[pixel + 1] * 0.5f));
        REQUIRE(rough.rgba[pixel] == value);
        REQUIRE(rough.rgba[pixel + 1] == value);
        REQUIRE(rough.rgba[pixel + 2] == value);
    }
    result.mesh.textures = {"color.ktx2", "emissive.ktx2", "normal.ktx2", "rough.ktx2"};
    render::MeshData restored;
    const auto packed = render::serializeMesh(result.mesh);
    REQUIRE(render::deserializeMesh(packed.data(), packed.size(), restored, error));
    CHECK(restored.submeshes.front().normalTexture == part.normalTexture);
    CHECK(restored.submeshes.front().roughnessTexture == part.roughnessTexture);
    CHECK(restored.submeshes.front().alphaMode == render::AlphaMode::Mask);
    CHECK(restored.submeshes.front().doubleSided);
    CHECK(restored.submeshes.front().emissiveTexture == part.emissiveTexture);
    CHECK(restored.submeshes.front().emissive[2] == part.emissive[2]);
}

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
