#include "han/physics/surface_table.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace han::physics {
namespace {
using contracts::SurfaceMaterial;

constexpr const char* kNames[SurfaceTable::kCount] = {"Unknown", "Asphalt", "Concrete", "Wood", "Metal", "Glass",
                                                      "Grass",   "Dirt",    "Water",    "Tile", "Carpet"};

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            parts.push_back(line.substr(start));
            return parts;
        }
        parts.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

bool parseFloat(std::string_view text, float& out) {
    const std::string s(text);
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return !s.empty() && end == s.c_str() + s.size();
}
}  // namespace

const char* SurfaceTable::name(SurfaceMaterial material) {
    const auto index = static_cast<int>(material);
    return index >= 0 && index < kCount ? kNames[index] : "Unknown";
}

bool SurfaceTable::materialFromName(std::string_view name, SurfaceMaterial& out) {
    for (int i = 0; i < kCount; ++i)
        if (name == kNames[i]) {
            out = static_cast<SurfaceMaterial>(i);
            return true;
        }
    return false;
}

const SurfaceProperties& SurfaceTable::get(SurfaceMaterial material) const {
    const auto index = static_cast<int>(material);
    return props_[index >= 0 && index < kCount ? index : 0];
}

bool SurfaceTable::parse(std::string_view text, std::string& error) {
    std::array<SurfaceProperties, kCount> next = props_;
    std::array<bool, kCount> seen{};
    std::size_t pos = 0;
    int lineNumber = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNumber;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto fail = [&](const std::string& why) {
            error = "строка " + std::to_string(lineNumber) + ": " + why;
            return false;
        };
        const auto cols = splitTabs(line);
        if (cols.size() != 4) return fail("нужно 4 колонки через табуляцию (материал, трение, упругость, ключ звука)");
        SurfaceMaterial material;
        if (!materialFromName(cols[0], material)) return fail("неизвестный материал «" + std::string(cols[0]) + "»");
        const int index = static_cast<int>(material);
        if (seen[index]) return fail("материал «" + std::string(cols[0]) + "» указан дважды");
        seen[index] = true;
        float friction = 0, restitution = 0;
        if (!parseFloat(cols[1], friction) || friction < 0.f || friction > 2.f) return fail("трение должно быть числом 0..2");
        if (!parseFloat(cols[2], restitution) || restitution < 0.f || restitution > 1.f) return fail("упругость должна быть числом 0..1");
        if (cols[3].empty()) return fail("пустой ключ звука");
        next[index] = {friction, restitution, std::string(cols[3])};
    }
    props_ = next;
    return true;
}

bool SurfaceTable::load(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "не удалось открыть " + file.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), error);
}
}  // namespace han::physics
