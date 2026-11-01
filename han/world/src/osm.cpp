#include "han/world/osm.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace han::world {
namespace {
struct Attr {
    std::string_view name;
    std::string value;
};

std::string decodeEntities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        const std::size_t semi = s.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out.push_back('&');
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const unsigned long cp = std::strtoul(std::string(ent.substr(hex ? 2 : 1)).c_str(), nullptr, hex ? 16 : 10);
            // Кодовая точка → UTF-8.
            if (cp < 0x80) out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        } else {
            out.append(s.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

// Разбирает атрибуты внутри тега: `name="v" name2='v2'`. false — повреждённый тег.
bool parseAttrs(std::string_view body, std::vector<Attr>& out) {
    std::size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r' || body[i] == '/')) ++i;
        if (i >= body.size()) break;
        const std::size_t nameStart = i;
        while (i < body.size() && body[i] != '=' && body[i] != ' ' && body[i] != '/') ++i;
        if (i >= body.size() || body[i] != '=') return false;
        const std::string_view name = body.substr(nameStart, i - nameStart);
        ++i;
        if (i >= body.size() || (body[i] != '"' && body[i] != '\'')) return false;
        const char quote = body[i++];
        const std::size_t end = body.find(quote, i);
        if (end == std::string_view::npos) return false;
        out.push_back({name, decodeEntities(body.substr(i, end - i))});
        i = end + 1;
    }
    return true;
}

const std::string* find(const std::vector<Attr>& attrs, std::string_view name) {
    for (const Attr& a : attrs)
        if (a.name == name) return &a.value;
    return nullptr;
}
}  // namespace

std::string_view OsmWay::tag(std::string_view key) const {
    for (const auto& [k, v] : tags)
        if (k == key) return v;
    return {};
}

bool parseOsmXml(std::string_view text, OsmData& out, std::string& error) {
    OsmData data;
    OsmWay* currentWay = nullptr;
    bool inRelation = false;
    std::size_t pos = 0;
    const auto fail = [&](const std::string& why) {
        error = "OSM: " + why + " (позиция " + std::to_string(pos) + ")";
        return false;
    };
    bool sawRoot = false;
    while (true) {
        pos = text.find('<', pos);
        if (pos == std::string_view::npos) break;
        if (text.compare(pos, 4, "<!--") == 0) {
            const std::size_t end = text.find("-->", pos + 4);
            if (end == std::string_view::npos) return fail("не закрыт комментарий");
            pos = end + 3;
            continue;
        }
        const std::size_t close = text.find('>', pos);
        if (close == std::string_view::npos) return fail("не закрыт тег");
        std::string_view tag = text.substr(pos + 1, close - pos - 1);
        const std::size_t tagPos = pos;
        pos = close + 1;
        if (tag.empty()) return fail("пустой тег");
        if (tag[0] == '?' || tag[0] == '!') continue;
        if (tag[0] == '/') {
            const std::string_view name = tag.substr(1);
            if (name == "way") currentWay = nullptr;
            else if (name == "relation") inRelation = false;
            continue;
        }
        std::size_t nameEnd = 0;
        while (nameEnd < tag.size() && tag[nameEnd] != ' ' && tag[nameEnd] != '/' && tag[nameEnd] != '\n' && tag[nameEnd] != '\t' && tag[nameEnd] != '\r') ++nameEnd;
        const std::string_view name = tag.substr(0, nameEnd);
        const bool selfClosing = !tag.empty() && tag.back() == '/';
        std::vector<Attr> attrs;
        if (!parseAttrs(tag.substr(nameEnd), attrs)) {
            pos = tagPos;
            return fail("повреждены атрибуты тега <" + std::string(name) + ">");
        }
        if (name == "osm") {
            sawRoot = true;
        } else if (name == "node") {
            const std::string *id = find(attrs, "id"), *lat = find(attrs, "lat"), *lon = find(attrs, "lon");
            if (id && lat && lon) data.nodes[std::strtoll(id->c_str(), nullptr, 10)] = {std::strtod(lat->c_str(), nullptr), std::strtod(lon->c_str(), nullptr)};
            // Узел с тегами внутри (<node ...><tag/></node>) — теги узлов не нужны, их тег <tag> попадёт в ветку ниже и будет проигнорирован.
        } else if (name == "way") {
            const std::string* id = find(attrs, "id");
            if (!id) {
                pos = tagPos;
                return fail("у линии нет id");
            }
            data.ways.push_back({});
            data.ways.back().id = std::strtoll(id->c_str(), nullptr, 10);
            currentWay = selfClosing ? nullptr : &data.ways.back();
        } else if (name == "nd" && currentWay) {
            if (const std::string* ref = find(attrs, "ref")) currentWay->nodes.push_back(std::strtoll(ref->c_str(), nullptr, 10));
        } else if (name == "tag" && currentWay) {
            const std::string *k = find(attrs, "k"), *v = find(attrs, "v");
            if (k && v) currentWay->tags.emplace_back(*k, *v);
        } else if (name == "relation") {
            ++data.relationsSkipped;
            inRelation = !selfClosing;
        }
        (void)inRelation;
    }
    if (!sawRoot) {
        pos = 0;
        return fail("это не файл OSM (нет тега <osm>)");
    }
    // Линии, ссылающиеся на отсутствующие узлы (обрезка по рамке): выбрасываем такие узлы, остальное сохраняем.
    for (OsmWay& way : data.ways) {
        const std::size_t before = way.nodes.size();
        way.nodes.erase(std::remove_if(way.nodes.begin(), way.nodes.end(), [&](std::int64_t id) { return data.nodes.find(id) == data.nodes.end(); }), way.nodes.end());
        if (way.nodes.size() != before) ++data.waysWithMissingNodes;
    }
    out = std::move(data);
    return true;
}

bool loadOsmFile(const std::string& path, OsmData& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "не удалось открыть " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseOsmXml(buffer.str(), out, error);
}

GeoProjection::GeoProjection(double originLat, double originLon) : lat0_(originLat), lon0_(originLon) {
    const double phi = originLat * 3.14159265358979323846 / 180.0;
    // Длина градуса по формулам WGS84 (ряды): достаточно для района в несколько километров.
    metersPerDegLat_ = 111132.92 - 559.82 * std::cos(2 * phi) + 1.175 * std::cos(4 * phi);
    metersPerDegLon_ = 111412.84 * std::cos(phi) - 93.5 * std::cos(3 * phi);
}

void GeoProjection::toLocal(double lat, double lon, float& x, float& z) const {
    x = static_cast<float>((lon - lon0_) * metersPerDegLon_);
    z = static_cast<float>(-(lat - lat0_) * metersPerDegLat_);
}

void GeoProjection::toGeo(float x, float z, double& lat, double& lon) const {
    lon = lon0_ + static_cast<double>(x) / metersPerDegLon_;
    lat = lat0_ - static_cast<double>(z) / metersPerDegLat_;
}
}  // namespace han::world
