// contracts/save_format.h — поставщик: B (game/save), потребители: все.
// Зачем: каждый модуль сам пишет и читает свои данные в сохранение; версии и миграции — в одном месте.
#pragma once
#include <cstdint>
#include <string_view>

namespace han::contracts {
constexpr std::uint32_t kSaveFormatVersion = 1;

struct ISaveWriter {
    virtual void writeInt(std::string_view key, std::int64_t v) = 0;
    virtual void writeFloat(std::string_view key, double v) = 0;
    virtual void writeString(std::string_view key, std::string_view v) = 0;
    virtual ~ISaveWriter() = default;
};
struct ISaveReader {
    virtual bool readInt(std::string_view key, std::int64_t& out) const = 0;
    virtual bool readFloat(std::string_view key, double& out) const = 0;
    virtual std::uint32_t version() const = 0;  // версия записи, по ней модуль мигрирует данные
    virtual ~ISaveReader() = default;
};
struct ISaveable {
    virtual std::string_view saveKey() const = 0;  // уникальный раздел сохранения модуля
    virtual void save(ISaveWriter&) const = 0;
    virtual void load(const ISaveReader&) = 0;
    virtual ~ISaveable() = default;
};
}  // namespace han::contracts
// Пример: void Wallet::save(ISaveWriter& w) const { w.writeInt("cash", cash); }
