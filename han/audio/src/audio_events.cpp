#include "han/audio/audio_events.h"

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace han::audio {
namespace {
using contracts::Bus;

constexpr const char* kBusNames[] = {"Master", "Sfx", "Voice", "Music", "Ambient", "Ui"};

std::vector<std::string_view> split(std::string_view line, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = line.find(sep, start);
        if (at == std::string_view::npos) {
            parts.push_back(line.substr(start));
            return parts;
        }
        parts.push_back(line.substr(start, at - start));
        start = at + 1;
    }
}

bool number(std::string_view text, float& out) {
    const std::string s(text);
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return !s.empty() && end == s.c_str() + s.size();
}
}  // namespace

bool busFromName(std::string_view name, Bus& out) {
    for (int i = 0; i < 6; ++i)
        if (name == kBusNames[i]) {
            out = static_cast<Bus>(i);
            return true;
        }
    return false;
}

const char* busName(Bus bus) {
    const auto i = static_cast<int>(bus);
    return i >= 0 && i < 6 ? kBusNames[i] : "Master";
}

bool parseEventTable(std::string_view text, const std::filesystem::path& baseDir, std::vector<SoundEvent>& out, std::string& error) {
    std::vector<SoundEvent> parsed;
    std::set<std::string> ids;
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
        const auto c = split(line, '\t');
        if (c.size() != 11) return fail("нужно 11 колонок через табуляцию, найдено " + std::to_string(c.size()));
        SoundEvent e;
        e.id = std::string(c[0]);
        if (e.id.empty() || e.id.find(' ') != std::string::npos) return fail("пустой идентификатор или пробел в нём");
        if (!ids.insert(e.id).second) return fail("событие «" + e.id + "» указано дважды");
        if (!busFromName(c[1], e.bus)) return fail("неизвестная шина «" + std::string(c[1]) + "»");
        for (std::string_view f : split(c[2], ';'))
            if (!f.empty()) {
                const std::filesystem::path p{std::string(f)};
                if (p.is_absolute() || f.find("..") != std::string_view::npos) return fail("путь файла должен быть относительным и без «..»: " + std::string(f));
                e.files.push_back(baseDir / p);
            }
        if (e.files.empty()) return fail("не указано ни одного файла");
        if (!number(c[3], e.volume) || e.volume < 0.f || e.volume > 2.f) return fail("громкость должна быть числом 0..2");
        if (!number(c[4], e.volumeJitter) || e.volumeJitter < 0.f || e.volumeJitter > 1.f) return fail("разброс громкости должен быть числом 0..1");
        if (!number(c[5], e.pitch) || e.pitch < 0.25f || e.pitch > 4.f) return fail("высота должна быть числом 0.25..4");
        if (!number(c[6], e.pitchJitter) || e.pitchJitter < 0.f || e.pitchJitter > 1.f) return fail("разброс высоты должен быть числом 0..1");
        if (c[7] != "0" && c[7] != "1") return fail("признак 3D — 0 или 1");
        e.spatial = c[7] == "1";
        if (!number(c[8], e.minDistance) || e.minDistance <= 0.f) return fail("мин. дистанция должна быть больше 0");
        if (!number(c[9], e.maxDistance) || e.maxDistance <= e.minDistance) return fail("макс. дистанция должна быть больше минимальной");
        if (c[10] != "0" && c[10] != "1") return fail("признак цикла — 0 или 1");
        e.loop = c[10] == "1";
        parsed.push_back(std::move(e));
    }
    out.insert(out.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
    return true;
}

bool loadEventTable(const std::filesystem::path& file, const std::filesystem::path& baseDir, std::vector<SoundEvent>& out, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "не удалось открыть " + file.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseEventTable(buffer.str(), baseDir, out, error);
}
}  // namespace han::audio
