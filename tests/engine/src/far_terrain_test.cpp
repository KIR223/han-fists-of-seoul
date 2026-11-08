// Этап 08f: раскраска дальней земли по высоте и крутизне.
#include <doctest/doctest.h>

#include <filesystem>

#include "han/world/terrain.h"

using namespace han::world;

TEST_CASE("far_terrain: палитра читается, равнина, лесной склон и скала красятся по-разному, плохой файл отклоняется") {
    FarTerrainPalette palette;
    std::string error;
    REQUIRE_MESSAGE(loadFarTerrainPalette((std::filesystem::path(HAN_REPO_DIR) / "data/city/far_terrain.json").string(), palette, error), error);

    // Плоская равнина на отметке 10 м и пологий холм до 150 м.
    std::vector<float> h(41 * 41);
    for (int z = 0; z < 41; ++z)
        for (int x = 0; x < 41; ++x) {
            const float dx = static_cast<float>(x - 20), dz = static_cast<float>(z - 20);
            h[static_cast<std::size_t>(z * 41 + x)] = 10.f + std::max(0.f, 140.f - 7.f * std::sqrt(dx * dx + dz * dz));
        }
    const Heightmap map = Heightmap::fromGrid(0.f, 0.f, 100.f, 41, 41, h);
    GeoMesh mesh = buildFarTerrain(map, 100.f, 6.f);
    colorFarTerrain(mesh, palette, 6.f);
    const auto colorAt = [&](float x, float z) {
        const int i = static_cast<int>(std::lround(x / 100.f)), j = static_cast<int>(std::lround(z / 100.f));
        return mesh.vertices[static_cast<std::size_t>(j * 41 + i)].color;
    };
    const float* plain = colorAt(100.f, 100.f);    // угол карты — равнина 10 м
    const float* top = colorAt(2000.f, 2000.f);    // вершина холма 150 м
    for (int k = 0; k < 3; ++k) CHECK(plain[k] == doctest::Approx(palette.lowland[k]).epsilon(0.02));
    CHECK(top[1] > top[0]);                         // зелёный канал сильнее красного: лес (скала серая)
    CHECK(top[1] != doctest::Approx(plain[1]).epsilon(0.02));

    const auto path = std::filesystem::temp_directory_path() / "han_far_bad.json";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    std::fputs("{\"version\":1,\"lowland\":[1,1,1],\"forest\":[0.3,0.5,0.3],\"rock\":[0.5,0.5,0.5],\"forest_min_height\":90,\"forest_full_height\":45,\"rock_normal_y\":0.7,\"rock_blend\":0.1}", f);
    std::fclose(f);
    FarTerrainPalette untouched;
    untouched.forestMinHeight = 7.f;
    CHECK_FALSE(loadFarTerrainPalette(path.string(), untouched, error));  // границы леса не по возрастанию
    CHECK(untouched.forestMinHeight == 7.f);
    std::filesystem::remove(path);
}
