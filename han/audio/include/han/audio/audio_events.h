// han/audio/audio_events.h — описание звуковых событий (E7) и разбор таблицы. Не зависит от miniaudio.
// Событие — это то, что просит играть игра («hit_body_heavy»): какие файлы, на какой шине, громкость, высота, 3D или нет.
// Таблицу событий ведёт тот, кому принадлежат звуки (бой — B, мир и физика — A); формат общий.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/audio_api.h"

namespace han::audio {
struct SoundEvent {
    std::string id;
    contracts::Bus bus = contracts::Bus::Sfx;
    std::vector<std::filesystem::path> files;  // несколько файлов — случайный вариант при каждом проигрывании
    float volume = 1.f, volumeJitter = 0.f;    // громкость 0..2 ± случайный разброс
    float pitch = 1.f, pitchJitter = 0.f;      // высота (1 — как есть) ± разброс
    bool spatial = true;                       // 3D (в точке мира) или плоский
    float minDistance = 1.f, maxDistance = 60.f;
    bool loop = false;
};

// Таблица: колонки через табуляцию, строки с # — комментарии:
//   id  шина  файлы(через ;)  громкость  разброс_громкости  высота  разброс_высоты  3D(0/1)  мин_дистанция  макс_дистанция  цикл(0/1)
// Шины: Master Sfx Voice Music Ambient Ui. Пути файлов — относительно baseDir. false — ошибка с номером строки.
bool parseEventTable(std::string_view text, const std::filesystem::path& baseDir, std::vector<SoundEvent>& out, std::string& error);
bool loadEventTable(const std::filesystem::path& file, const std::filesystem::path& baseDir, std::vector<SoundEvent>& out, std::string& error);

bool busFromName(std::string_view name, contracts::Bus& out);
const char* busName(contracts::Bus bus);
}  // namespace han::audio
// Пример: loadEventTable("data/audio/events.tsv", "assets/audio", events, error);
