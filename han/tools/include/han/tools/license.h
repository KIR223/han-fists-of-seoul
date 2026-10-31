#pragma once
// Проверка лицензий ассетов (E20, часть 76 дизайна). Решение «можно / только владелец / нельзя» — по тексту лицензии.
#include <string_view>

namespace han::tools {

enum class LicenseVerdict {
    Allowed,     // CC0, CC-BY, MIT/BSD/Apache/zlib, OFL, 공공누리 тип 1
    NeedsOwner,  // CC-BY-SA, «Royalty Free» площадок — только с одобрения владельца
    Forbidden,   // NC, ND, Editorial, Personal use, без лицензии, неизвестное
};

LicenseVerdict classifyLicense(std::string_view text);
const char* verdictName(LicenseVerdict v);

}  // namespace han::tools
// Пример: if (classifyLicense("CC-BY 4.0") != LicenseVerdict::Allowed) reject();
