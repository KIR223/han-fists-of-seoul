#include "han/ui/localization.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace han::ui {

std::vector<std::string> parseCsvLine(const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);  // BOM Excel
    if (line.empty() || line[0] == '#') return {};
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

namespace {
bool validKey(const std::string& k) {
    if (k.size() < 3 || k.size() > 120) return false;
    return std::all_of(k.begin(), k.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}
}  // namespace

bool Localization::loadTable(const std::string& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "нет файла " + file;
        return false;
    }
    std::string line;
    bool header = false;
    int lineNo = 0;
    std::string problems;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto cells = parseCsvLine(line);
        if (cells.empty()) continue;
        if (!header) {
            if (cells.size() < 4 || cells[0] != "key" || cells[1] != "ru" || cells[2] != "ko" || cells[3] != "en") {
                error = file + ": первая строка должна быть «key,ru,ko,en[,context]»";
                return false;
            }
            header = true;
            continue;
        }
        if (cells.size() < 4 || !validKey(cells[0])) {
            problems += file + ":" + std::to_string(lineNo) + ": неверная строка или ключ\n";
            continue;
        }
        auto& entry = texts_[cells[0]];
        entry["ru"] = cells[1];
        entry["ko"] = cells[2];
        entry["en"] = cells[3];
    }
    if (!header) {
        error = file + ": пустой файл";
        return false;
    }
    error += problems;
    return true;
}

bool Localization::loadLanguageFile(const std::string& lang, const std::string& file, std::string& error) {
    if (lang != "ru" && lang != "ko" && lang != "en") {
        error = "неизвестный язык «" + lang + "»";
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "нет файла " + file;
        return false;
    }
    std::string line;
    int lineNo = 0;
    bool header = false;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto cells = parseCsvLine(line);
        if (cells.empty()) continue;
        if (!header) {
            header = true;
            if (cells.size() >= 2 && cells[0] == "key") continue;  // заголовок «key,text»
        }
        if (cells.size() < 2 || !validKey(cells[0])) {
            error += file + ":" + std::to_string(lineNo) + ": неверная строка или ключ\n";
            continue;
        }
        texts_[cells[0]][lang] = cells[1];
    }
    return true;
}

bool Localization::loadLanguageDir(const std::string& lang, const std::string& dir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        error = "нет папки " + dir;
        return false;
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".csv") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    bool ok = true;
    for (const fs::path& f : files) ok = loadLanguageFile(lang, f.string(), error) && ok;
    return ok;
}

const std::string& Localization::get(const std::string& key) const {
    const auto it = texts_.find(key);
    if (it == texts_.end()) {
        static thread_local std::string missing;
        missing = key;
        return missing;
    }
    for (const char* lang : {lang_.c_str(), "en", "ru"}) {
        const auto t = it->second.find(lang);
        if (t != it->second.end() && !t->second.empty()) return t->second;
    }
    static thread_local std::string keyCopy;
    keyCopy = key;
    return keyCopy;
}

bool Localization::has(const std::string& key, const std::string& lang) const {
    const auto it = texts_.find(key);
    if (it == texts_.end()) return false;
    const auto t = it->second.find(lang);
    return t != it->second.end() && !t->second.empty();
}

std::string Localization::format(const std::string& key, const std::vector<std::string>& args) const {
    std::string text = get(key);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string token = "{" + std::to_string(i) + "}";
        for (std::size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + args[i].size())) text.replace(pos, token.size(), args[i]);
    }
    return text;
}

std::vector<std::string> Localization::incomplete() const {
    std::vector<std::string> out;
    for (const auto& [key, langs] : texts_)
        for (const std::string& lang : languages())
            if (!has(key, lang)) {
                out.push_back(key + " (" + lang + ")");
            }
    return out;
}

}  // namespace han::ui
