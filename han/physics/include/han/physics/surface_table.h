// han/physics/surface_table.h — таблица свойств поверхностей (P1): трение, упругость, ключ звука удара.
// Числа лежат в данных (data/materials/surfaces.tsv), код только читает их. Не зависит от Jolt.
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include "contracts/physics_query.h"

namespace han::physics {
struct SurfaceProperties {
    float friction = 0.5f;
    float restitution = 0.1f;
    std::string impactSound = "phys.impact.generic";  // ключ для звуковой системы (E7)
};

class SurfaceTable {
public:
    static constexpr int kCount = 11;  // по числу значений contracts::SurfaceMaterial

    // Разбирает текст таблицы (колонки через табуляцию, строки с # — комментарии). false — ошибка с номером строки.
    // Таблица заменяется целиком только при успехе; материалы, которых нет в тексте, остаются по умолчанию.
    bool parse(std::string_view text, std::string& error);
    bool load(const std::filesystem::path& file, std::string& error);

    const SurfaceProperties& get(contracts::SurfaceMaterial material) const;
    static bool materialFromName(std::string_view name, contracts::SurfaceMaterial& out);
    static const char* name(contracts::SurfaceMaterial material);

private:
    std::array<SurfaceProperties, kCount> props_{};
};
}  // namespace han::physics
// Пример: SurfaceTable t; std::string err; t.load("data/materials/surfaces.tsv", err); float mu = t.get(SurfaceMaterial::Wood).friction;
