// han_fetch (каркас E20): пока умеет проверять лицензию. Загрузка по манифесту появится на этапе E20.
#include <cstdio>
#include <cstring>

#include "han/tools/license.h"

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "check-license") == 0) {
        const auto verdict = han::tools::classifyLicense(argv[2]);
        std::printf("%s\n", han::tools::verdictName(verdict));
        return verdict == han::tools::LicenseVerdict::Allowed ? 0 : 1;
    }
    std::fprintf(stderr, "использование: han_fetch check-license \"<текст лицензии>\"\n");
    return 2;
}
