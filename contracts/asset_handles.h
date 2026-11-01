// contracts/asset_handles.h — поставщик: A, потребители: все.
// Зачем: ссылка на ассет по имени из манифеста; горячая перезагрузка не ломает ссылки.
#pragma once
#include <cstdint>
#include <string_view>

namespace han::contracts {
enum class AssetKind : std::uint8_t { Mesh, Texture, Material, Animation, Sound, Font, Data };

struct AssetHandle {
    std::uint32_t id = 0;          // 0 — пустой хэндл
    std::uint32_t generation = 0;  // растёт при перезагрузке
    explicit operator bool() const { return id != 0; }
};

struct IAssets {
    virtual AssetHandle load(AssetKind kind, std::string_view manifestName) = 0;
    virtual bool isReady(AssetHandle h) const = 0;
    virtual void release(AssetHandle h) = 0;
    virtual ~IAssets() = default;
};
}  // namespace han::contracts
// Пример: auto mesh = assets.load(AssetKind::Mesh, "props/street_lamp");
