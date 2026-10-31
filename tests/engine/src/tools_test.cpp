#include <doctest/doctest.h>

#include "han/tools/cook.h"
#include "han/tools/license.h"

using namespace han::tools;

TEST_CASE("лицензии: разрешённые") {
    for (const char* l : {"CC0", "CC0 1.0", "Public Domain", "CC-BY 4.0", "CC BY 3.0", "MIT", "BSD-3-Clause", "Apache 2.0",
                          "zlib", "SIL OFL 1.1", "KOGL type1", "CC BY 4.0 Licence", "CC0 (see description)"})
        CHECK_MESSAGE(classifyLicense(l) == LicenseVerdict::Allowed, l);
}

TEST_CASE("лицензии: только с одобрения владельца") {
    CHECK(classifyLicense("CC-BY-SA 4.0") == LicenseVerdict::NeedsOwner);
    CHECK(classifyLicense("Royalty Free") == LicenseVerdict::NeedsOwner);
}

TEST_CASE("лицензии: запрещённые") {
    for (const char* l : {"CC-BY-NC 4.0", "CC BY-ND", "CC-BY-NC-SA", "Editorial", "Personal use", "", "free download",
                          "KOGL type 3"})
        CHECK_MESSAGE(classifyLicense(l) == LicenseVerdict::Forbidden, l);
}

TEST_CASE("cooker: вид исходного файла по расширению") {
    CHECK(classifySource("a/b/lamp.GLB") == SourceKind::Gltf);
    CHECK(classifySource("x.fbx") == SourceKind::Fbx);
    CHECK(classifySource("t.png") == SourceKind::Image);
    CHECK(classifySource("s.ogg") == SourceKind::Audio);
    CHECK(classifySource("noext") == SourceKind::Unknown);
    CHECK(classifySource("a.zip") == SourceKind::Unknown);
}
