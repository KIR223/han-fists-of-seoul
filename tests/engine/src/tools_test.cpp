#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "han/tools/cook.h"
#include "han/tools/license.h"

using namespace han::tools;

// Правила лицензий проверяются по тому же файлу, что и Python-версия конвейера (han/tools/pipeline/license_rules.py).
TEST_CASE("лицензии: общие тест-векторы с Python-конвейером") {
    std::ifstream file(std::string(HAN_REPO_DIR) + "/han/tools/pipeline/tests/license_cases.tsv");
    REQUIRE(file.is_open());
    int checked = 0;
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto tab = line.find('\t');
        REQUIRE(tab != std::string::npos);
        const std::string expected = line.substr(0, tab), text = line.substr(tab + 1);
        CHECK_MESSAGE(std::string(verdictName(classifyLicense(text))) == expected, text);
        ++checked;
    }
    CHECK(checked >= 25);
}

TEST_CASE("лицензии: пустая строка запрещена") { CHECK(classifyLicense("") == LicenseVerdict::Forbidden); }

TEST_CASE("cooker: вид исходного файла по расширению") {
    CHECK(classifySource("a/b/lamp.GLB") == SourceKind::Gltf);
    CHECK(classifySource("x.fbx") == SourceKind::Fbx);
    CHECK(classifySource("t.png") == SourceKind::Image);
    CHECK(classifySource("s.ogg") == SourceKind::Audio);
    CHECK(classifySource("noext") == SourceKind::Unknown);
    CHECK(classifySource("a.zip") == SourceKind::Unknown);
}
