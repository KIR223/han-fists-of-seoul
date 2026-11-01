// han/ai/archetype.h — архетипы прохожих (E11): возраст, деньги, расписание дня, реакции на события, реплики.
// Формат — contracts/schemas/npc_archetype.schema.json; данные — data/people/archetypes/*.json (зона A). Только взрослые: возраст ≥ 18.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "han/core/json.h"

namespace han::ai {
enum class Activity : std::uint8_t { Work, Commute, Cafe, Shop, Home, Wait, Walk, Eat, Exercise, Sleep };
enum class Reaction : std::uint8_t { Ignore, Flee, Watch, Film, CallPolice, Intervene };
// События мира, на которые реагирует толпа.
enum class WorldEventType : std::uint8_t { Fight, Gunshot, Siren, Explosion, Crash };

struct ScheduleEntry {
    float from = 0, to = 24;  // часы суток; from > to — через полночь (22 → 6)
    Activity activity = Activity::Walk;
    std::string placeCategory;  // пусто — по умолчанию для занятия (см. defaultPlaceCategory)
};

struct Archetype {
    std::string id, nameKey;
    std::uint32_t numericId = 0;
    int ageMin = 18, ageMax = 60;
    int moneyMin = 0, moneyMax = 0;
    float walkSpeed = 1.4f;   // м/с
    float courage = 0.5f;     // 0 — убегает, 1 — вмешивается
    float fightsBack = 0.1f;
    std::vector<ScheduleEntry> schedule;
    std::map<std::string, Reaction> reactions;  // имя события ("fight", "gunshot", …) → реакция
    std::vector<std::string> lineKeys;

    // Занятие в указанный час (первая подходящая запись; нет записи — прогулка).
    const ScheduleEntry* entryAt(float hour) const;
    Activity activityAt(float hour) const;
    // Реакция на событие: из таблицы или (если там нет) по храбрости.
    Reaction reactionTo(WorldEventType type) const;
};

const char* eventName(WorldEventType type);
const char* activityName(Activity a);
const char* reactionName(Reaction r);
bool activityFromName(const std::string& name, Activity& out);
bool reactionFromName(const std::string& name, Reaction& out);
// Категория места по умолчанию для занятия ("work", "cafe", "home"…); пустая — любое место поблизости.
const char* defaultPlaceCategory(Activity a);

// Разбор объекта JSON по схеме. false — ошибка в error (со ссылкой на поле).
bool parseArchetype(const core::Json& json, Archetype& out, std::string& error);

class ArchetypeLibrary {
public:
    bool add(Archetype a, std::string& error);  // false — повтор id или numericId
    // Читает все *.json из папки (по алфавиту). Ошибки в файлах складываются в error, остальные файлы грузятся.
    bool loadDirectory(const std::string& dir, std::string& error);
    const Archetype* byId(const std::string& id) const;
    const Archetype* byNumericId(std::uint32_t id) const;
    const std::vector<Archetype>& all() const { return list_; }
    // Указатели на все архетипы (для Crowd::populate). Действительны, пока в библиотеку не добавляют новые.
    std::vector<const Archetype*> pointers() const;
    std::size_t size() const { return list_.size(); }

private:
    std::vector<Archetype> list_;
};
}  // namespace han::ai
// Пример: ArchetypeLibrary lib; std::string err; lib.loadDirectory("data/people/archetypes", err); Activity a = lib.byId("office_worker")->activityAt(9.5f);
