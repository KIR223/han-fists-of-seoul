#include <doctest/doctest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include "han/core/json.h"
#include "han/render/sign_atlas.h"
#include "han/render/street_models.h"
#include "han/world/cell_io.h"
#include "han/world/city_builder.h"
#include "han/world/city_day_cycle.h"
#include "han/world/city_props.h"

namespace {
std::string repoStreet(const char* file) { return (std::filesystem::path(HAN_REPO_DIR) / file).string(); }
han::world::CityStyle cityStyle() {
    han::world::CityStyle style;
    std::string error;
    REQUIRE_MESSAGE(han::world::loadCityStyle(repoStreet("data/city/streetscape.json"), style, error), error);
    return style;
}
}

TEST_CASE("streetscape: оформление города читается из данных, ошибки не меняют предыдущие настройки") {
    auto style = cityStyle();
    CHECK(style.enabled);
    CHECK(style.signBays >= 23);  // каждый вымышленный бренд доступен в атласе
    CHECK(style.personHeight > 1.6f);
    han::core::Json root;
    std::string error;
    REQUIRE(han::core::loadJsonFile(repoStreet("data/city/streetscape.json"), root, error));
    const auto file = std::filesystem::temp_directory_path() / "han_style_invalid.json";
    for (const auto key : {"version", "shop_bay_width", "sign_bays", "crosswalk_spacing", "origin_lat"}) {
        auto invalid = root;
        invalid.set(key, han::core::Json::number(key == std::string("origin_lat") ? 100 : -1));
        std::ofstream(file) << invalid.dump();
        const auto before = style;
        CHECK_FALSE(han::world::loadCityStyle(file.string(), style, error));
        CHECK(style.shopBayWidth == before.shopBayWidth);
        CHECK(style.originLat == before.originLat);
    }
    std::filesystem::remove(file);
}

TEST_CASE("streetscape: каталог импортированных пропсов ограничивает пути, масштаб и бюджет") {
    han::world::CityPropCatalog catalog;
    std::string error;
    REQUIRE_MESSAGE(han::world::loadCityProps(repoStreet("data/city/props.json"), catalog, error), error);
    REQUIRE(catalog.props.size() == 3);
    CHECK(catalog.maxDraws == 64);
    const auto path = std::filesystem::temp_directory_path() / "han_city_props_bad.json";
    han::core::Json data;
    REQUIRE(han::core::loadJsonFile(repoStreet("data/city/props.json"), data, error));
    auto props = han::core::Json::array();
    auto invalid = data["props"].at(0);
    invalid.set("mesh", han::core::Json::string("../outside.hmesh"));
    props.push(invalid); data.set("props", props);
    std::ofstream(path) << data.dump();
    CHECK_FALSE(han::world::loadCityProps(path.string(), catalog, error));
    CHECK(catalog.props.size() == 3);
    props = han::core::Json::array();
    invalid.set("mesh", han::core::Json::string("inside.hmesh")); invalid.set("scale", han::core::Json::number(-1.0));
    props.push(invalid); data.set("props", props);
    std::ofstream(path) << data.dump();
    CHECK_FALSE(han::world::loadCityProps(path.string(), catalog, error));
    CHECK(catalog.maxDraws == 64);
    std::filesystem::remove(path);
}

TEST_CASE("streetscape: объёмные вывески, оборудование фасадов и свет сохраняются в запечённой ячейке") {
    using namespace han::world;
    OsmData osm;
    std::string error;
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0005"/>
      <node id="3" lat="37.5004" lon="127.0005"/><node id="4" lat="37.5004" lon="127.0000"/>
      <way id="8"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="4"/><nd ref="1"/><tag k="building" v="yes"/><tag k="height" v="16"/></way></osm>)", osm, error));
    CityBuildSettings settings;
    settings.style = cityStyle();
    settings.shopTileMeters = settings.style.shopBayWidth * static_cast<float>(settings.style.signBays);
    settings.streetProps = false;
    const auto chunks = buildCity(osm, GeoProjection(37.5, 127), settings);
    int blades = 0, lights = 0, metal = 0;
    for (const auto& [cell, chunk] : chunks) {
        for (const auto& range : chunk.buildings.ranges) {
            blades += range.material == GeoMaterial::BladeSign;
            metal += range.material == GeoMaterial::Metal;
        }
        lights += static_cast<int>(chunk.lamps.size());
        for (const auto index : chunk.buildings.indices) CHECK(index < chunk.buildings.vertices.size());
        for (const auto& vertex : chunk.buildings.vertices) {
            for (const float value : vertex.position) CHECK(std::isfinite(value));
            const float length = std::sqrt(vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2]);
            CHECK(length == doctest::Approx(1.f));
        }
        const auto file = (std::filesystem::temp_directory_path() / "han_styled_cell.hcell").string();
        REQUIRE_MESSAGE(saveCell(file, cell, settings.cellSize, chunk, error), error);
        CityChunk loaded;
        CellCoord loadedCell;
        float size = 0.f;
        REQUIRE_MESSAGE(loadCell(file, loadedCell, size, loaded, error), error);
        CHECK(loaded.lamps.size() == chunk.lamps.size());
        REQUIRE(loaded.buildings.ranges.size() == chunk.buildings.ranges.size());
        for (std::size_t i = 0; i < loaded.buildings.ranges.size(); ++i) CHECK(loaded.buildings.ranges[i].material == chunk.buildings.ranges[i].material);
        std::filesystem::remove(file);
    }
    CHECK(blades > 0);
    CHECK(lights > 0);
    CHECK(metal > 0);
}

TEST_CASE("streetscape: пешеходная площадь заполняется мощением, переходы добавляются на городском перекрёстке") {
    using namespace han::world;
    OsmData osm;
    std::string error;
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0010"/>
      <node id="3" lat="37.5010" lon="127.0010"/><node id="4" lat="37.5010" lon="127.0000"/>
      <way id="1"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="4"/><nd ref="1"/><tag k="highway" v="pedestrian"/><tag k="area" v="yes"/></way></osm>)", osm, error));
    CityBuildSettings settings;
    settings.style = cityStyle();
    settings.streetProps = false;
    std::size_t paving = 0;
    for (const auto& [cell, chunk] : buildCity(osm, GeoProjection(37.5, 127), settings)) {
        CHECK(chunk.areaCount == 1);
        for (const auto& range : chunk.ground.ranges) if (range.material == GeoMaterial::Plaza) paving += range.indexCount;
    }
    CHECK(paving == 6);
    settings.streetProps = true;
    std::size_t furnitureColliders = 0;
    for (const auto& [cell, chunk] : buildCity(osm, GeoProjection(37.5, 127), settings)) furnitureColliders += chunk.walls.size();
    CHECK(furnitureColliders > 0);
    settings.streetProps = false;
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0010"/>
      <node id="3" lat="37.5000" lon="127.0020"/><node id="4" lat="37.5010" lon="127.0010"/>
      <way id="1"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="highway" v="secondary"/></way>
      <way id="2"><nd ref="2"/><nd ref="4"/><tag k="highway" v="secondary"/></way></osm>)", osm, error));
    const auto countMarks = [&](const CityBuildSettings& s) {
        std::size_t count = 0;
        for (const auto& [cell, chunk] : buildCity(osm, GeoProjection(37.5, 127), s))
            for (const auto& range : chunk.ground.ranges) if (range.material == GeoMaterial::Marking) count += range.indexCount;
        return count;
    };
    auto plain = settings;
    plain.style.enabled = false;
    CHECK(countMarks(settings) > countMarks(plain));
}

TEST_CASE("streetscape: вертикальные вывески имеют рамку и независимые секции свечения") {
    using namespace han::render;
    SignBrand red, blue;
    red.background = 0xff0000ff;
    blue.background = 0xffff0000;
    const auto atlas = buildBladeSignAtlas({red, blue}, "", 4);
    REQUIRE(atlas.valid());
    CHECK(atlas.width == 512);
    CHECK(atlas.height == 512);
    const auto pixel = [&](int x, int y) { return &atlas.mips[0][(static_cast<std::size_t>(y) * atlas.width + x) * 4]; };
    CHECK(pixel(64, 250)[0] == 255);
    CHECK(pixel(192, 250)[2] == 255);
    CHECK(pixel(320, 250)[0] == 255);
    CHECK(pixel(64, 250)[3] < 255);
    CHECK(pixel(1, 250)[3] == 255);
    CHECK_FALSE(buildBladeSignAtlas({}, "", 4).valid());
    CHECK_FALSE(buildBladeSignAtlas({red}, "", 33).valid());
}

TEST_CASE("streetscape: масштаб фото-детали меняется согласованно без изменения размера текстуры") {
    using namespace han::render;
    std::vector<std::uint8_t> pixels(4 * 4 * 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            pixels[(y * 4 + x) * 4] = static_cast<std::uint8_t>(x * 40);
            pixels[(y * 4 + x) * 4 + 1] = static_cast<std::uint8_t>(y * 40);
            pixels[(y * 4 + x) * 4 + 3] = 255;
        }
    auto texture = makeTexture(4, 4, pixels, false);
    REQUIRE(retileTexture(texture, 2));
    CHECK(texture.width == 4);
    CHECK(texture.height == 4);
    CHECK_FALSE(texture.srgb);
    CHECK(texture.valid());
    CHECK(texture.mips.size() == 3);
    CHECK(texture.mips[0][4] == 80);
    CHECK(texture.mips[0][8] == 0);
    CHECK(texture.mips[0][(4 + 1) * 4 + 1] == 80);
    const auto before = texture.mips;
    CHECK_FALSE(retileTexture(texture, 0));
    CHECK(texture.mips == before);
}

TEST_CASE("streetscape: стекло не наследует рельеф и шероховатость стены, включая карты другого размера") {
    using namespace han::render;
    auto albedo = makeTexture(2, 1, {100, 90, 80, 0, 100, 90, 80, 255}, true);
    auto normal = makeTexture(4, 1, {20, 40, 220, 255, 20, 40, 220, 255, 20, 40, 220, 255, 20, 40, 220, 255}, false);
    auto rough = normal;
    REQUIRE(applyGlassSurface(normal, albedo, true, .08f));
    CHECK(normal.mips[0][0] == 128); CHECK(normal.mips[0][2] == 255);
    CHECK(normal.mips[0][4] == 128); CHECK(normal.mips[0][8] == 20);
    REQUIRE(applyGlassSurface(rough, albedo, false, .08f));
    CHECK(rough.mips[0][0] == 20); CHECK(rough.mips[0][1] == 20);
    CHECK(rough.mips[0][9] == 40); CHECK(rough.mips[0][11] == 255);
    CHECK(normal.valid()); CHECK(rough.valid()); CHECK(rough.mips.size() == 3);
    const auto before = rough.mips;
    CHECK_FALSE(applyGlassSurface(rough, {}, false, .08f));
    CHECK_FALSE(applyGlassSurface(rough, albedo, false, 2.f));
    CHECK(rough.mips == before);
    CHECK_FALSE(applyGlassSurface(albedo, albedo, true, .08f));
}

TEST_CASE("streetscape: машины и взрослые прохожие имеют корректные меши и опору на землю") {
    using namespace han::render;
    const auto validate = [](const MeshData& mesh) {
        REQUIRE_FALSE(mesh.vertices.empty());
        REQUIRE_FALSE(mesh.indices.empty());
        std::size_t covered = 0;
        for (const auto& part : mesh.submeshes) {
            CHECK(part.firstIndex == covered);
            covered += part.indexCount;
        }
        CHECK(covered == mesh.indices.size());
        CHECK(mesh.submeshes.size() <= 8);
        float minimum = 100.f, maximum = 0.f;
        for (const auto index : mesh.indices) CHECK(index < mesh.vertices.size());
        for (const auto& v : mesh.vertices) {
            minimum = std::min(minimum, v.position[1]); maximum = std::max(maximum, v.position[1]);
            for (const float f : v.position) CHECK(std::isfinite(f));
            const float n = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
            CHECK(n == doctest::Approx(1.f));
        }
        CHECK(minimum == doctest::Approx(0.f).epsilon(0.001));
        CHECK(maximum > 1.f);
    };
    validate(makeStreetCar({1.78f, 1.45f, 4.35f}, {0.4f, 0.4f, 0.4f}));
    validate(makeCityMetroEntrance({2.8f, 2.6f, 4.8f}));
    for (const float stride : {-0.9f, 0.f, 0.9f}) validate(makeAdultCityPerson(1.74f, {0.2f, 0.3f, 0.4f}, stride));
    CHECK(makeStreetCar({0, 1, 4}, {1, 1, 1}).vertices.empty());
    CHECK(makeAdultCityPerson(std::numeric_limits<float>::infinity(), {1, 1, 1}).vertices.empty());
}

TEST_CASE("streetscape: ориентация моделей совпадает с курсом трафика и прохожих") {
    const auto east = han::render::streetModelTransform({10, 2, 20}, 3.14159265f * 0.5f);
    // Нос модели направлен по -Z: преобразованный вектор должен смотреть на восток.
    CHECK(-east.m[8] == doctest::Approx(1.f));
    CHECK(-east.m[10] == doctest::Approx(0.f).epsilon(0.0001));
    CHECK(east.m[12] == 10.f);
    CHECK(east.m[13] == 2.f);
    CHECK(east.m[14] == 20.f);
}

TEST_CASE("streetscape: индекс запечённого города задаёт общую проекцию и размер вывесок") {
    using namespace han::world;
    const auto file = std::filesystem::temp_directory_path() / "han_bake_info.json";
    std::ofstream(file) << R"({"format":"HCL2","origin":[37.557,126.914],"cell_size":256,"shop_tile":153.6})";
    CityBakeInfo info;
    std::string error;
    REQUIRE(loadCityBakeInfo(file.string(), info, error));
    CHECK(info.originLat == doctest::Approx(37.557));
    CHECK(info.originLon == doctest::Approx(126.914));
    CHECK(info.cellSize == 256.f);
    CHECK(info.shopTile == doctest::Approx(153.6f));
    std::ofstream(file) << R"({"format":"HCL2","origin":[37.557,126.914],"cell_size":0,"shop_tile":153.6})";
    CHECK_FALSE(loadCityBakeInfo(file.string(), info, error));
    CHECK(info.cellSize == 256.f);
    std::ofstream(file) << R"({"format":"HCL9","origin":[37.557,126.914],"cell_size":256,"shop_tile":153.6})";
    CHECK_FALSE(loadCityBakeInfo(file.string(), info, error));
    std::filesystem::remove(file);
}

TEST_CASE("streetscape: суточный цикл плавно проходит полночь, вечером зажигаются окна, поздно ночью их меньше") {
    han::world::CityDayCycle cycle;
    std::string error;
    REQUIRE_MESSAGE(cycle.load(repoStreet("data/city/day_cycle.json"), error), error);
    CHECK(cycle.sample(10).elevation == doctest::Approx(35.f));
    CHECK(cycle.sample(34).elevation == cycle.sample(10).elevation);
    CHECK(cycle.sample(-1).azimuth == cycle.sample(23).azimuth);
    CHECK(cycle.sample(19.5f).litWindows > cycle.sample(3).litWindows);
    CHECK(std::fabs(cycle.sample(23.999f).azimuth - cycle.sample(24.001f).azimuth) < 0.1f);
    CHECK(cycle.sample(0).kelvin >= 1000.f);
    CHECK(cycle.secondsPerDay() > 0.f);
    han::core::Json root;
    REQUIRE(han::core::loadJsonFile(repoStreet("data/city/day_cycle.json"), root, error));
    auto duplicate = han::core::Json::array();
    duplicate.push(root["keys"].at(0)); duplicate.push(root["keys"].at(0));
    root.set("keys", duplicate);
    const auto file = std::filesystem::temp_directory_path() / "han_day_cycle_bad.json";
    std::ofstream(file) << root.dump();
    CHECK_FALSE(cycle.load(file.string(), error));
    CHECK(cycle.sample(10).elevation == doctest::Approx(35.f));
    std::filesystem::remove(file);
}

TEST_CASE("streetscape: экземпляры деревьев и коллизии стволов заменяют процедурные кроны") {
    han::world::OsmData osm;
    std::string error;
    REQUIRE(han::world::parseOsmXml(R"(<osm><node id="1" lat="37.5" lon="127"/><node id="2" lat="37.5" lon="127.003"/>
      <way id="10"><nd ref="1"/><nd ref="2"/><tag k="highway" v="secondary"/></way></osm>)", osm, error));
    han::world::CityBuildSettings settings;
    settings.style = cityStyle(); settings.instanceTrees = true;
    std::size_t trees = 0, trunks = 0;
    for (const auto& [cell, chunk] : han::world::buildCity(osm, han::world::GeoProjection(37.5, 127), settings)) {
        trees += chunk.trees.size();
        for (const auto& tree : chunk.trees) {
            CHECK(tree.height >= settings.style.treeHeight);
            CHECK(tree.height <= settings.style.treeHeight + settings.style.treeHeightVariation);
        }
        for (const auto& collider : chunk.walls) if (collider.half.x == settings.style.treeTrunkRadius && collider.half.z == settings.style.treeTrunkRadius) ++trunks;
    }
    CHECK(trees > 0); CHECK(trunks == trees);
}

TEST_CASE("streetscape: HCL3 сохраняет деревья, повреждение не меняет загруженную ячейку") {
    const auto file = std::filesystem::temp_directory_path() / "han_tree_cell.hcell";
    han::world::CityChunk source, restored;
    source.trees.push_back({{12, 3, 14}, 7.7f, 0.8f});
    std::string error;
    REQUIRE(han::world::saveCell(file.string(), {0, 0}, 256.f, source, error));
    han::world::CellCoord cell; float size = 0;
    REQUIRE(han::world::loadCell(file.string(), cell, size, restored, error));
    REQUIRE(restored.trees.size() == 1);
    CHECK_FALSE(restored.empty()); CHECK(restored.trees[0].position.y == 3.f);
    CHECK(restored.trees[0].height == 7.7f); CHECK(restored.trees[0].yaw == 0.8f);
    std::ifstream input(file, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()); input.close();
    REQUIRE(bytes.substr(0, 4) == "HCL3");
    bytes[16] = 2; // неизвестные флаги HCL3
    std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    CHECK_FALSE(han::world::loadCell(file.string(), cell, size, restored, error));
    CHECK(restored.trees[0].height == 7.7f);
    bytes[16] = 0;
    std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 1));
    CHECK_FALSE(han::world::loadCell(file.string(), cell, size, restored, error));
    source.trees[0].height = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(han::world::saveCell(file.string(), {0, 0}, 256.f, source, error));
    std::filesystem::remove(file);
}

TEST_CASE("streetscape: бюджет растительности и порядок LOD проверяются при загрузке") {
    han::world::CityVegetation vegetation;
    std::string error;
    REQUIRE(han::world::loadCityVegetation(repoStreet("data/city/vegetation.json"), vegetation, error));
    CHECK(vegetation.lodNear < vegetation.lodFar);
    CHECK(vegetation.lodFar < vegetation.drawRadius);
    CHECK(vegetation.shadowRadius <= vegetation.drawRadius);
    auto original = vegetation;
    han::core::Json data;
    REQUIRE(han::core::loadJsonFile(repoStreet("data/city/vegetation.json"), data, error));
    const auto file = std::filesystem::temp_directory_path() / "han_vegetation_bad.json";
    data.set("lod_near", han::core::Json::number(300));
    std::ofstream(file) << data.dump();
    CHECK_FALSE(han::world::loadCityVegetation(file.string(), vegetation, error));
    CHECK(vegetation.lodNear == original.lodNear);
    data.set("mesh", han::core::Json::string("../outside.hmesh"));
    std::ofstream(file) << data.dump();
    CHECK_FALSE(han::world::loadCityVegetation(file.string(), vegetation, error));
    CHECK(vegetation.mesh == original.mesh);
    std::filesystem::remove(file);
}

TEST_CASE("streetscape: транспорт использует четыре набора LOD, плохие пути и порядок отклоняются") {
    han::world::CityVehicleModels models;
    std::string error;
    REQUIRE(han::world::loadCityVehicleModels(repoStreet("data/city/traffic_models.json"), models, error));
    REQUIRE(models.meshes.size() == 4);
    CHECK(models.warmupSeconds == 60);
    CHECK(models.lodNear < models.lodMid); CHECK(models.lodMid < models.lodFar);
    han::core::Json data;
    REQUIRE(han::core::loadJsonFile(repoStreet("data/city/traffic_models.json"), data, error));
    const auto file = std::filesystem::temp_directory_path() / "han_bad_vehicle_models.json";
    for (double value : {-1., 121.}) {
        auto invalid = data; invalid.set("warmup_seconds", han::core::Json::number(value));
        std::ofstream(file) << invalid.dump();
        CHECK_FALSE(han::world::loadCityVehicleModels(file.string(), models, error));
        CHECK(models.warmupSeconds == 60);
    }
    auto bad = data; bad.set("lod_mid", han::core::Json::number(1));
    std::ofstream(file) << bad.dump();
    CHECK_FALSE(han::world::loadCityVehicleModels(file.string(), models, error));
    auto names = han::core::Json::array(); names.push(han::core::Json::string("../escape.hmesh"));
    data.set("meshes", names); std::ofstream(file) << data.dump();
    CHECK_FALSE(han::world::loadCityVehicleModels(file.string(), models, error));
    CHECK(models.meshes.size() == 4);
    std::filesystem::remove(file);
}
