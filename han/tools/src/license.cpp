#include "han/tools/license.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace han::tools {
namespace {

// Нижний регистр без пробелов, дефисов и подчёркиваний: "CC BY-NC 4.0" -> "ccbync4.0".
std::string normalize(std::string_view text) {
    std::string out;
    for (const unsigned char c : text) {
        if (c == ' ' || c == '-' || c == '_' || c == '\t') continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool has(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

}  // namespace

LicenseVerdict classifyLicense(std::string_view text) {
    const std::string s = normalize(text);
    if (s.empty()) return LicenseVerdict::Forbidden;

    // Запреты проверяются первыми: «CC-BY-NC-SA» — это NC, то есть нельзя.
    for (const char* bad : {"bync", "bynd", "ccnc", "ccnd", "editorial", "personaluse", "noncommercial", "gamerip", "nolicense"})
        if (has(s, bad)) return LicenseVerdict::Forbidden;
    // 공공누리: тип 1 можно, типы 2-4 нельзя.
    if (has(s, "kogl") || has(s, "공공누리")) {
        return (has(s, "type1") || has(s, "1유형") || s.find("kogl1") != std::string::npos) ? LicenseVerdict::Allowed
                                                                                          : LicenseVerdict::Forbidden;
    }

    if (has(s, "ccbysa") || has(s, "royaltyfree")) return LicenseVerdict::NeedsOwner;

    if (has(s, "cc0") || has(s, "publicdomain") || s == "pd") return LicenseVerdict::Allowed;
    if (has(s, "ccby")) return LicenseVerdict::Allowed;
    for (const char* ok : {"bsd", "apache", "zlib", "ofl", "openfontlicense"})
        if (has(s, ok)) return LicenseVerdict::Allowed;
    if (s.rfind("mit", 0) == 0) return LicenseVerdict::Allowed;

    return LicenseVerdict::Forbidden;  // нет лицензии или неизвестная = нельзя
}

const char* verdictName(LicenseVerdict v) {
    switch (v) {
        case LicenseVerdict::Allowed: return "allowed";
        case LicenseVerdict::NeedsOwner: return "needs-owner";
        case LicenseVerdict::Forbidden: return "forbidden";
    }
    return "forbidden";
}

}  // namespace han::tools
