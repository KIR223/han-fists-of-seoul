#include "han/core/asset_watcher.h"

#include <algorithm>
#include <system_error>

namespace han::core {
namespace {

struct Stat {
    bool exists = false;
    std::filesystem::file_time_type time{};
    std::uintmax_t size = 0;
};

Stat stat(const std::filesystem::path& path) {
    Stat s;
    std::error_code ec;
    s.exists = std::filesystem::is_regular_file(path, ec) && !ec;
    if (!s.exists) return s;
    s.time = std::filesystem::last_write_time(path, ec);
    if (ec) s.exists = false;
    s.size = std::filesystem::file_size(path, ec);
    if (ec) s.exists = false;
    return s;
}

}  // namespace

void AssetWatcher::watch(const std::filesystem::path& path) {
    for (const Entry& e : entries_)
        if (e.path == path) return;
    Entry e;
    e.path = path;
    const Stat s = stat(path);
    e.exists = s.exists;
    e.time = s.time;
    e.size = s.size;
    entries_.push_back(std::move(e));
}

void AssetWatcher::unwatch(const std::filesystem::path& path) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.path == path; }), entries_.end());
}

std::vector<std::filesystem::path> AssetWatcher::poll() {
    std::vector<std::filesystem::path> changed;
    for (Entry& e : entries_) {
        const Stat s = stat(e.path);
        if (!s.exists) {
            e.exists = false;
            e.pending = false;
            continue;
        }
        const bool differs = !e.exists || s.time != e.time || s.size != e.size;
        if (differs) {
            // Файл изменился (возможно, ещё пишется): запоминаем и ждём, пока он перестанет меняться.
            e.exists = true;
            e.time = s.time;
            e.size = s.size;
            e.pending = true;
        } else if (e.pending) {
            e.pending = false;
            changed.push_back(e.path);
        }
    }
    return changed;
}

}  // namespace han::core
