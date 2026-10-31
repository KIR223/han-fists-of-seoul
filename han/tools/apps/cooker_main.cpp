// han_cooker (каркас E3): определяет вид исходника. Конвертация появится на этапе E3.
#include <cstdio>

#include "han/tools/cook.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "использование: han_cooker <исходник> <выход>\n");
        return 2;
    }
    const auto kind = han::tools::classifySource(argv[1]);
    if (kind == han::tools::SourceKind::Unknown) {
        std::fprintf(stderr, "неизвестный формат: %s\n", argv[1]);
        return 1;
    }
    std::fprintf(stderr, "вид: %s; конвертация будет в этапе E3\n", han::tools::sourceKindName(kind));
    return 3;
}
