#pragma once
// Наблюдатель за файлами для горячей перезагрузки (E3): опрос времени изменения и размера, без потоков и API ОС.
#include <filesystem>
#include <vector>

namespace han::core {

class AssetWatcher {
public:
    // Начинает следить за файлом. Если его ещё нет — первое появление тоже считается изменением.
    void watch(const std::filesystem::path& path);
    void unwatch(const std::filesystem::path& path);

    // Возвращает файлы, изменение которых завершилось: изменение замечено на одном опросе и не продолжилось на следующем
    // (так не подхватывается файл, который ещё пишется). Вызывать раз в кадр или реже (раз в 0,25 с достаточно).
    std::vector<std::filesystem::path> poll();

private:
    struct Entry {
        std::filesystem::path path;
        bool exists = false;
        std::filesystem::file_time_type time{};
        std::uintmax_t size = 0;
        bool pending = false;  // изменение замечено, ждём стабильного размера
    };
    std::vector<Entry> entries_;
};

}  // namespace han::core
// Пример: watcher.watch("data/props/lamp.hmesh"); ... for (auto& p : watcher.poll()) reload(p);
