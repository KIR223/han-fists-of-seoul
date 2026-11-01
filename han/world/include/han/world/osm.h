// han/world/osm.h — чтение данных OpenStreetMap (E10): XML .osm → узлы и линии с тегами, проекция в метры.
// Данные OSM — © участники OpenStreetMap, лицензия ODbL: файлы в Git не кладутся (скрипт загрузки — han/tools/pipeline/han_osm_fetch.py).
// Из данных берётся только геометрия и тип объекта; названия и бренды из тегов в игру не попадают (правило проекта: бренды вымышленные).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace han::world {
struct OsmNode {
    double lat = 0, lon = 0;
};

struct OsmWay {
    std::int64_t id = 0;
    std::vector<std::int64_t> nodes;
    std::vector<std::pair<std::string, std::string>> tags;
    // Значение тега или пустая строка.
    std::string_view tag(std::string_view key) const;
    bool closed() const { return nodes.size() >= 4 && nodes.front() == nodes.back(); }
};

struct OsmData {
    std::unordered_map<std::int64_t, OsmNode> nodes;
    std::vector<OsmWay> ways;
    std::size_t relationsSkipped = 0;  // отношения (мультиполигоны с дырами) пока не читаются
    std::size_t waysWithMissingNodes = 0;
};

// Разбирает текст .osm (XML). false — файл повреждён (error с позицией). Лишние теги и комментарии пропускаются.
bool parseOsmXml(std::string_view text, OsmData& out, std::string& error);
bool loadOsmFile(const std::string& path, OsmData& out, std::string& error);

// Локальная проекция около точки origin: x — на восток, z — на юг (взгляд камеры по умолчанию вдоль -Z, то есть на север), метры.
class GeoProjection {
public:
    GeoProjection(double originLat, double originLon);
    void toLocal(double lat, double lon, float& x, float& z) const;
    void toGeo(float x, float z, double& lat, double& lon) const;
    double originLat() const { return lat0_; }
    double originLon() const { return lon0_; }

private:
    double lat0_, lon0_, metersPerDegLat_, metersPerDegLon_;
};
}  // namespace han::world
// Пример: OsmData d; std::string err; loadOsmFile("assets_src/osm/mapo.osm", d, err); GeoProjection p(37.5572, 126.9230); p.toLocal(lat, lon, x, z);
